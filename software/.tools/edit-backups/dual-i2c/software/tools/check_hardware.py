"""Check the C pin table against fresh KiCad XML exports and the saved PCB.

Run with KiCad's Python, which provides pcbnew. Export main.xml, sector.xml
and led.xml into the supplied directory before running this command.
"""
from pathlib import Path
from datetime import date
import argparse
import collections
import hashlib
import json
import re
import xml.etree.ElementTree as ET
import pcbnew


def netlist(path):
    root = ET.parse(path).getroot()
    parts = {c.attrib['ref']: c.findtext('value')
             for c in root.findall('./components/comp')}
    pins = collections.defaultdict(dict)
    nets = {}
    for n in root.findall('./nets/net'):
        name = n.attrib['name']
        nets[name] = [dict(p.attrib) for p in n.findall('node')]
        for pin in nets[name]:
            pins[pin['ref']][pin['pin']] = dict(pin, net=name)
    return parts, pins, nets


def require(condition, message):
    if not condition:
        raise ValueError(message)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--netlist-dir', type=Path, required=True)
    ap.add_argument('--write-contract', type=Path)
    args = ap.parse_args()
    workspace = Path(__file__).resolve().parents[2]
    board_dir = workspace / 'main_board'
    part, pins, nets = netlist(args.netlist_dir / 'main.xml')
    c = (workspace / 'software/firmware/common/board_pins.c').read_text()
    package_map = {
        10: ('A', 0), 11: ('A', 1), 12: ('A', 2), 13: ('A', 3),
        14: ('A', 4), 15: ('A', 5), 16: ('A', 6), 17: ('A', 7),
        18: ('B', 0), 19: ('B', 1), 21: ('B', 10), 22: ('B', 11),
        38: ('A', 15), 39: ('B', 3), 40: ('B', 4), 41: ('B', 5),
        42: ('B', 6), 43: ('B', 7), 45: ('B', 8), 46: ('B', 9),
    }
    assignments = {}
    board = pcbnew.LoadBoard(str(board_dir / 'main_board.kicad_pcb'))
    footprints = {f.GetReference(): f for f in board.GetFootprints()}
    pcb_pins = {r: {p.GetNumber(): p.GetNetname() for p in f.Pads()}
                for r, f in footprints.items() if r in ('MCU:1', 'MCU:2', 'USB:HUB1')}
    for table, ref, prefix in [('touch_irq', 'MCU:1', 'TOUCH_IRQ_S'),
                                ('led_data', 'MCU:2', 'LED_GPIO_S')]:
        require(part[ref] == 'CH32V203C8T6', f'{ref}: unexpected MCU')
        body = re.search(r'wacca_' + table + r'\[WACCA_SECTORS\]\s*=\s*\{(.*?)\};',
                         c, re.S).group(1)
        rows = re.findall(r'\{ WACCA_GPIO_([AB]), (\d+), (\d+) \}', body)
        require(len(rows) == 12, f'{table}: expected 12 rows')
        require(len(set(rows)) == 12, f'{table}: duplicate GPIO')
        assignments[table] = []
        for sector, (port, bit, physical_pin) in enumerate(rows, 1):
            bit, number = int(bit), int(physical_pin)
            require(package_map[number] == (port, bit), f'{ref}: wrong package pin')
            expected = f'/{prefix}{sector}'
            require(pins[ref][physical_pin]['net'] == expected,
                    f'{ref} sector {sector}: schematic differs')
            require(pcb_pins[ref][physical_pin] == expected,
                    f'{ref} sector {sector}: PCB differs')
            assignments[table].append({'sector': sector, 'gpio': f'P{port}{bit}',
                                       'package_pin': number, 'net': expected})
    for name, ref, package_pin, gpio, expected in [
        ('touch_scl', 'MCU:1', '21', ('B', 10), '/TOUCH_MCU_SCL'),
        ('touch_sda', 'MCU:1', '22', ('B', 11), '/TOUCH_MCU_SDA'),
        ('hub_reset', 'MCU:1', '46', ('B', 9), '/TOUCH_MCU_GPIO'),
        ('brightness', 'MCU:2', '13', ('A', 3), 'Net-(MCU:2-PA3)'),
    ]:
        body = re.search(r'wacca_' + name + r' = \{ WACCA_GPIO_([AB]), (\d+), (\d+) \}', c)
        require(body is not None, f'{name}: missing definition')
        port, bit, pin = body.groups()
        require((port, int(bit)) == gpio and pin == package_pin, f'{name}: wrong pin')
        require(pins[ref][pin]['net'] == expected and pcb_pins[ref][pin] == expected,
                f'{name}: saved board differs')
    for hub_pin, ref, mcu_pin in [('2', 'MCU:1', '33'), ('3', 'MCU:1', '32'),
                                  ('4', 'MCU:2', '33'), ('5', 'MCU:2', '32')]:
        expected = pins[ref][mcu_pin]['net']
        require(pins['USB:HUB1'][hub_pin]['net'] == expected and
                pcb_pins['USB:HUB1'][hub_pin] == expected, 'PORT_SWAP wiring changed')
    for ref in ('MCU:1_X1', 'MCU:2_X1'):
        require(part[ref] == 'ABM3-8.000MHZ-D2Y-T', f'{ref}: clock changed')
    require(part['USB:HUB_X1'] == 'ABM3-24.000MHZ-D2Y-T', 'Hub clock changed')
    geometry_header = (workspace / 'software/firmware/common/board_pins.h').read_text()
    geometry = {}
    for constant, expected in [('WACCA_SECTORS', 12),
                               ('WACCA_LED_MODULES_PER_SECTOR', 5),
                               ('WACCA_PHYSICAL_LEDS_PER_MODULE', 80)]:
        match = re.search(r'\b' + constant + r'\s*=\s*(\d+)\b', geometry_header)
        require(match is not None and int(match.group(1)) == expected,
                f'{constant}: differs from user-confirmed assembly geometry')
        geometry[constant] = expected
    led_parts, led_pins, _ = netlist(args.netlist_dir / 'led.xml')
    leds = {ref for ref, value in led_parts.items() if value == 'WS2812C-2020-V6'}
    require(len(leds) == geometry['WACCA_PHYSICAL_LEDS_PER_MODULE'],
            'Expected 80 LEDs on one module PCB, five modules per sector')
    by_input = collections.defaultdict(list)
    for ref in leds:
        by_input[led_pins[ref]['3']['net']].append(ref)
    current = by_input['/LED_DATA_S']
    chain = []
    while current:
        require(len(current) == 1, 'Parallel LED input: topology changed')
        ref = current[0]
        require(ref not in chain, 'LED chain cycle')
        chain.append(ref)
        current = by_input.get(led_pins[ref]['1']['net'], [])
    require(set(chain) == leds, 'Not all 80 module LEDs are reachable from DIN')
    require(led_pins[chain[-1]]['1']['net'] == led_pins['FPC2']['6']['net'],
            'Module chain does not reach the output connector')
    sector_parts, sector_pins, _ = netlist(args.netlist_dir / 'sector.xml')
    require(sector_parts['TDC1'] == 'MSP430FR2673TRHBR', 'Sector MCU changed')
    report = {
        'checked_on': date.today().isoformat(),
        'source_sha256': {name: hashlib.sha256((board_dir / name).read_bytes()).hexdigest()
                          for name in ('main_board.kicad_sch', 'main_board.kicad_pcb',
                                       'sector_board.kicad_sch', 'led_board.kicad_sch')},
        'main_mcu_pins': {ref: pins[ref] for ref in ('MCU:1', 'MCU:2', 'USB:HUB1')},
        'sector_mcu_pins': sector_pins['TDC1'],
        'sector_id_resistors': {ref: val for ref, val in part.items() if ref.startswith('s.ID_R')},
        'assignments': assignments,
        'led_module_serial_chain': chain,
        'led_geometry': {
            'physical_leds_per_module': len(chain),
            'modules_per_sector': geometry['WACCA_LED_MODULES_PER_SECTOR'],
            'sectors': geometry['WACCA_SECTORS'],
            'physical_leds_per_sector': len(chain) * geometry['WACCA_LED_MODULES_PER_SECTOR'],
            'physical_leds_total': len(chain) * geometry['WACCA_LED_MODULES_PER_SECTOR'] * geometry['WACCA_SECTORS'],
            'rgb_frame_bytes_total': len(chain) * geometry['WACCA_LED_MODULES_PER_SECTOR'] * geometry['WACCA_SECTORS'] * 3,
            'assembly_geometry_source': 'User correction, 2026-10-25; five module PCBs per sector',
            'netlist_scope': 'One 80-LED module; inter-module cabling is outside this netlist',
        },
        'hub_port_swap': {'register': '0xFA', 'value': '0x06', 'intentional': True},
    }
    if args.write_contract:
        args.write_contract.parent.mkdir(parents=True, exist_ok=True)
        args.write_contract.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('PASS: 24 sector GPIO assignments, I2C/reset/ADC pins, clocks, intentional')
    print('PORT_SWAP wiring, and the 80-LED MODULE chain match the saved hardware.')
    print('Assembly: 5 modules/sector x 12 sectors = 400 LEDs/sector, 4800 total (user-confirmed).')


if __name__ == '__main__':
    main()
