"""Check real Intel HEX addresses/checksums and USB package boundaries."""
from pathlib import Path
import struct
import sys
import unittest
import zlib
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from pack_firmware import layout_values, package


class PackageTests(unittest.TestCase):
	def setUp(self):
		self.layout = layout_values(Path(__file__).resolve().parents[1] / "firmware/common/image_layout.h")

	def test_factory_and_usb_agree(self):
		for role in (1, 2):
			boot = b"\x37\x29" * 1234
			app = bytes(range(256)) * 17 + b"\x00"
			update, factory = package(boot, app, role, self.layout)
			magic, version, actual_role, offset, size, crc, layout = struct.unpack("<8s6I", update[:32])
			self.assertEqual((magic, version, actual_role, offset, size, crc, layout),
				(b"WCFU\0\0\0\0", 1, role, self.layout["BOOT_SIZE"], len(app), zlib.crc32(app), self.layout["IMAGE_LAYOUT_ID"]))
			memory = {}
			upper = 0
			for line in factory.splitlines():
				record = bytes.fromhex(line[1:])
				self.assertEqual(sum(record) & 255, 0)
				self.assertEqual(len(record), record[0] + 5)
				address = int.from_bytes(record[1:3], "big")
				if record[3] == 4:
					upper = int.from_bytes(record[4:-1], "big") << 16
				elif record[3] == 0:
					for i, byte in enumerate(record[4:-1]):
						self.assertNotIn(upper + address + i, memory)
						memory[upper + address + i] = byte
			base = self.layout["FLASH_BASE"]
			self.assertEqual(bytes(memory[base + i] for i in range(len(boot))), boot)
			self.assertEqual(bytes(memory[base + offset + i] for i in range(size)), app)
			meta = base + self.layout["METADATA_OFFSET"]
			self.assertEqual(bytes(memory[meta + i] for i in range(32)), update[:32])
			self.assertEqual(bytes(memory[meta + 32 + i] for i in range(4)), struct.pack("<I", self.layout["IMAGE_COMMIT"]))
			self.assertEqual(len(memory), len(boot) + len(app) + 36)

	def test_partition_overflow(self):
		with self.assertRaises(ValueError):
			package(bytes(self.layout["BOOT_SIZE"] + 1), bytes(512), 1, self.layout)
		with self.assertRaises(ValueError):
			package(bytes(10), bytes(self.layout["METADATA_OFFSET"] - self.layout["BOOT_SIZE"] + 1), 1, self.layout)
		with self.assertRaises(ValueError):
			package(bytes(10), bytes(511), 1, self.layout)


if __name__ == "__main__":
	unittest.main()
