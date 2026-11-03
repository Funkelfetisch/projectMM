"""The MoonBase serial write plan must match the built table and slot sizes."""
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'moondeck' / 'build'))
import build_esp32 as build


class FlashBoundsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='moonbase-bounds-')
        root = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)
        self.app = root / 'esp32-nebulite-esp32'; self.app.mkdir()
        recovery = root / 'moonbase-esp32'; recovery.mkdir()
        self.moonbase = recovery / build.MOONBASE_BIN; self.moonbase.write_bytes(b'base')
        self.image = self.app / build.APP_BIN; self.image.write_bytes(b'app')
        self.csv = root / 'table.csv'
        self.csv.write_text('otadata,data,ota,0xe000,0x2000,\nmoonbase,app,factory,0x10000,0xe0000,\napp,app,ota_0,0xf0000,0x270000,\n')
        self.table = self.app / 'partition-table.bin'
        rows = [(1, 0, 0xe000, 0x2000), (0, 0, 0x10000, 0xe0000), (0, 16, 0xf0000, 0x270000)]
        self.table.write_bytes(b''.join(struct.pack('<HBBII16sI', 0x50aa, *row, b'', 0) for row in rows) + b'\xff' * 32)
        self.metadata = {'partition-table': {'file': 'partition-table.bin'},
            'flash_files': {'0x8000': 'partition-table.bin', '0xe000': 'ota_data_initial.bin', '0x10000': build.APP_BIN}}
        self.write_metadata()
        self.csv_patch = patch.object(build, 'moonbase_table_csv', return_value=self.csv)
        self.csv_patch.start(); self.addCleanup(self.csv_patch.stop)

    def write_metadata(self):
        (self.app / 'flasher_args.json').write_text(json.dumps(self.metadata))

    def plan(self):
        return build.moonbase_flash_files('nebulite-esp32', self.app)

    def test_exact_plan_targets_separate_slots(self):
        rows = {(int(offset, 16), path.name) for offset, path in self.plan()}
        self.assertIn((0xf0000, build.APP_BIN), rows)
        self.assertIn((0x10000, build.MOONBASE_BIN), rows)
        self.assertIn((0xe000, 'ota_data_slot0.bin'), rows)

    def test_changed_or_malformed_built_table_rejected_before_slot_file(self):
        data = bytearray(self.table.read_bytes()); struct.pack_into('<I', data, 64 + 4, 0x100000)
        self.table.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'differs'): self.plan()
        self.assertFalse((self.app / 'ota_data_slot0.bin').exists())
        self.table.write_bytes(b'broken')
        with self.assertRaisesRegex(ValueError, 'invalid built'): self.plan()

    def test_oversized_or_missing_images_are_rejected(self):
        self.image.write_bytes(b'x' * (0x270000 + 1))
        with self.assertRaisesRegex(ValueError, 'app image does not fit'): self.plan()
        self.image.write_bytes(b'app'); self.moonbase.write_bytes(b'x' * (0xe0000 + 1))
        with self.assertRaisesRegex(ValueError, 'MoonBase image does not fit'): self.plan()
        self.moonbase.unlink()
        with self.assertRaises(FileNotFoundError): self.plan()

    def test_missing_or_duplicate_app_entry_rejected(self):
        del self.metadata['flash_files']['0x10000']; self.write_metadata()
        with self.assertRaisesRegex(ValueError, 'exactly one'): self.plan()
        self.metadata['flash_files']['0x10000'] = build.APP_BIN
        self.metadata['flash_files']['0x20000'] = build.APP_BIN; self.write_metadata()
        with self.assertRaisesRegex(ValueError, 'exactly one'): self.plan()

    def test_duplicate_or_overlapping_source_partitions_rejected(self):
        with self.csv.open('a') as out: out.write('again,app,factory,0x20000,0xe0000,\n')
        with self.assertRaisesRegex(ValueError, 'duplicate'): self.plan()
        self.csv.write_text('otadata,data,ota,0xe000,0x2000,\nmoonbase,app,factory,0x10000,0xe0000,\napp,app,ota_0,0xe0000,0x270000,\n')
        with self.assertRaisesRegex(ValueError, 'overlapping'): self.plan()

    def test_other_flash_parts_cannot_overlap_or_be_missing(self):
        extra = self.app / 'extra.bin'; extra.write_bytes(b'x' * 4096)
        self.metadata['flash_files']['0xf0000'] = 'extra.bin'; self.write_metadata()
        with self.assertRaisesRegex(ValueError, 'overlaps a reserved partition'): self.plan()
        self.assertFalse((self.app / 'ota_data_slot0.bin').exists())
        del self.metadata['flash_files']['0xf0000']
        self.metadata['flash_files']['0x200000'] = 'extra.bin'; self.write_metadata()
        with self.assertRaisesRegex(ValueError, 'overlaps a reserved partition'): self.plan()
        del self.metadata['flash_files']['0x200000']
        self.metadata['flash_files']['0x4000'] = 'missing.bin'; self.write_metadata()
        with self.assertRaisesRegex(FileNotFoundError, 'missing flash image'): self.plan()


if __name__ == '__main__': unittest.main()
