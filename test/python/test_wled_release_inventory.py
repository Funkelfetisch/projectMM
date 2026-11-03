import io
import base64
import json
import sys
import warnings
import zipfile
from pathlib import Path

import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "moondeck" / "helio"))
from inventory_wled_release import inventory, compare_candidate


def packed(entries):
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w") as archive:
        for name, data in entries:
            archive.writestr(name, data)
    return output.getvalue()


def check_boundaries(tmp_path):
    presets = {"5": {"n": "Blue", "bri": 42, "r": "encoded data", "seg": [
        {"start": 0, "stop": 31, "fx": 65, "sx": 17, "unknown": 3, "col": [[0, 8, 255, 0]]},
        {"stop": 0}, {"id": 2, "on": False, "stop": 91, "fx": 123}]}}
    config = {"hw": {"led": {"fps": 42}}, "light": {"pal-mode": 0, "gc": {"col": 1, "val": 2.8}},
              "wifi": {"password": "secret-must-not-appear"}}
    files = packed([("presets.json", json.dumps(presets)), ("cfg.json", json.dumps(config))])
    image = bytearray(24)
    image[0], image[12] = 0xe9, 9
    release = tmp_path / "release.zip"
    release.write_bytes(packed([("firmware.bin", image), ("files.zip", files)]))
    result = inventory(release, "#define FX_MODE_PALETTE 65\n", {"factoryScenes": [{"id": "pink", "source": {"effect": 65}}]})
    first, second = result["presets"][0]["segments"]
    assert (first["start"], first["stop"], first["sx"]) == (0, 31, 17)
    assert first["catalogue_id_candidates"] == ["pink"]
    assert first["unreported_fields"] == ["unknown"]
    assert result["presets"][0]["unreported_fields"] == ["r"]
    assert result["presets"][0]["opaque_r"] == {"valid_base64": False}
    assert len(result["presets"][0]["canonical_preset_sha256"]) == 64
    assert second["on"] is False and second["source_effect_name"] is None
    assert "candidates only" in result["review"]
    assert "secret-must-not-appear" not in json.dumps(result)
    assert result["render_settings"] == {"target_fps": 42, "palette_blend_mode": 0,
                                         "color_gamma_control": 1, "color_gamma_value": 2.8}


def test_opaque_r_is_fingerprinted_without_emitting_its_content(tmp_path):
    content = b"private raw-looking data"
    presets = {"1": {"n": "Look", "r": base64.b64encode(content).decode(), "seg": []}}
    image = bytes([0xe9]) + bytes(23)
    path = tmp_path / "release.zip"
    path.write_bytes(packed([("firmware.bin", image), ("files.zip", packed([("presets.json", json.dumps(presets))]))]))
    result = inventory(path, "", {})
    assert result["presets"][0]["opaque_r"]["decoded_bytes"] == len(content)
    assert result["presets"][0]["opaque_r"]["valid_base64"] is True
    assert content.decode() not in json.dumps(result)
    assert presets["1"]["r"] not in json.dumps(result)


def check_duplicates(tmp_path):
    image = bytes([0xe9]) + bytes(23)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", UserWarning)
        archive = packed([("firmware.bin", image), ("firmware.bin", image), ("files.zip", b"")])
    path = tmp_path / "ambiguous.zip"
    path.write_bytes(archive)
    try:
        inventory(path, "", {})
    except ValueError as error:
        assert "duplicated" in str(error)
    else:
        raise AssertionError("duplicate firmware member accepted")


class InventoryTests(unittest.TestCase):
    def test_boundaries(self):
        with tempfile.TemporaryDirectory() as directory:
            check_boundaries(Path(directory))

    def test_duplicates(self):
        with tempfile.TemporaryDirectory() as directory:
            check_duplicates(Path(directory))

    def test_candidate_fit_does_not_conflate_chip_or_capacity(self):
        with tempfile.TemporaryDirectory() as directory:
            image = bytearray(100)
            image[0], image[12] = 0xe9, 9
            path = Path(directory) / "candidate.bin"
            path.write_bytes(image)
            too_large = compare_candidate(path, 9, 99)
            self.assertTrue(too_large["source_chip_matches"])
            self.assertFalse(too_large["fits_assumed_slot"])
            different = compare_candidate(path, 0, 100)
            self.assertFalse(different["source_chip_matches"])
            self.assertTrue(different["fits_assumed_slot"])
            self.assertIn("does not establish", different["qualification"])
            with self.assertRaises(ValueError):
                compare_candidate(path, 9, 0)


if __name__ == "__main__":
    unittest.main()
