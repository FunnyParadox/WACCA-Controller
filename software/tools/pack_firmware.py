"""Create WCFU USB update packages and factory USB HEX images. No dependencies."""
import argparse
from pathlib import Path
import re
import struct
import zlib


def layout_values(path):
	text = Path(path).read_text(encoding="utf-8")
	values = {}
	for name in ("FLASH_BASE", "FLASH_SIZE", "FLASH_PAGE", "BOOT_SIZE", "METADATA_OFFSET", "IMAGE_LAYOUT_ID", "IMAGE_COMMIT"):
		match = re.search(r"^#define WACCA_" + name + r"\s+(0x[0-9A-Fa-f]+|[0-9]+)\s*$", text, re.M)
		if not match:
			raise ValueError("Missing numeric image-layout constant: " + name)
		values[name] = int(match[1], 0)
	return values


def record(address, kind, data):
	body = bytes((len(data), address >> 8, address & 255, kind)) + data
	return ":" + (body + bytes((-sum(body) & 255,))).hex().upper() + "\n"


def intel_hex(parts):
	text = []
	upper = None
	for address, data in parts:
		for offset in range(0, len(data), 16):
			location = address + offset
			if location >> 16 != upper:
				upper = location >> 16
				text.append(record(0, 4, struct.pack(">H", upper)))
			text.append(record(location & 65535, 0, data[offset:offset + 16]))
	return "".join(text) + record(0, 1, b"")


def package(boot, app, role, layout):
	capacity = layout["METADATA_OFFSET"] - layout["BOOT_SIZE"]
	if not boot or len(boot) > layout["BOOT_SIZE"]:
		raise ValueError("Bootloader does not fit its reserved region")
	if not 512 <= len(app) <= capacity:
		raise ValueError("Application does not fit its reserved region")
	header = struct.pack("<8s6I", b"WCFU", 1, role, layout["BOOT_SIZE"], len(app), zlib.crc32(app), layout["IMAGE_LAYOUT_ID"])
	metadata = header + struct.pack("<I", layout["IMAGE_COMMIT"])
	base = layout["FLASH_BASE"]
	factory = intel_hex(((base, boot), (base + layout["BOOT_SIZE"], app), (base + layout["METADATA_OFFSET"], metadata)))
	return header + app, factory


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--boot", required=True, type=Path)
	parser.add_argument("--app", required=True, type=Path)
	parser.add_argument("--role", required=True, type=int, choices=(1, 2))
	parser.add_argument("--output", required=True, type=Path)
	parser.add_argument("--layout", required=True, type=Path)
	args = parser.parse_args()
	try:
		update, factory = package(args.boot.read_bytes(), args.app.read_bytes(), args.role, layout_values(args.layout))
		args.output.with_suffix(".wcfu").write_bytes(update)
		args.output.with_suffix(".factory.hex").write_text(factory, encoding="ascii", newline="\n")
	except (OSError, ValueError) as error:
		parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
	main()
