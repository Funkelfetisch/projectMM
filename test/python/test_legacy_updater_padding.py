"""A classic NEBULITE image always ends on a short packet of the 2021 BLE updater, which finishes an update only there."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'moondeck' / 'build'))
import build_esp32 as build


class LegacyUpdaterPaddingTest(unittest.TestCase):
    def image(self, size: int) -> Path:
        directory = tempfile.TemporaryDirectory(prefix='legacy-pad-')
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / build.APP_BIN
        path.write_bytes(b'\xe9' + b'\x00' * (size - 1))
        return path

    def test_an_image_that_would_end_on_a_full_packet_gains_erased_bytes_past_its_end(self):
        path = self.image(4647 * build.LEGACY_UPDATER_PACKET)
        self.assertTrue(build.pad_for_legacy_updater(path))
        data = path.read_bytes()
        self.assertEqual(len(data), 4647 * build.LEGACY_UPDATER_PACKET + build.LEGACY_UPDATER_PAD)
        self.assertNotEqual(len(data) % build.LEGACY_UPDATER_PACKET, 0)
        self.assertEqual(data[-build.LEGACY_UPDATER_PAD:], b'\xff' * build.LEGACY_UPDATER_PAD)

    def test_an_image_already_ending_on_a_short_packet_is_left_as_built_and_padding_twice_changes_nothing(self):
        path = self.image(4647 * build.LEGACY_UPDATER_PACKET + 16)
        before = path.read_bytes()
        self.assertFalse(build.pad_for_legacy_updater(path))
        self.assertEqual(path.read_bytes(), before)

    def test_only_the_classic_nebulite_build_goes_through_the_2021_updater(self):
        self.assertEqual([name for name, spec in build.FIRMWARES.items() if spec.get('legacyUpdater')], ['nebulite-esp32'])


if __name__ == '__main__':
    unittest.main()
