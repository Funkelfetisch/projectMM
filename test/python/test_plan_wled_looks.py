import io
import json
import sys
import tempfile
import unittest
import zipfile
from copy import deepcopy
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "moondeck" / "helio"))
from plan_wled_looks import plan_archive, plan_preset


def preset(fx=0, pal=1):
    return {"n": "Blue", "on": True, "bri": 255, "mainseg": 0, "ledmap": 0,
            "r": "PRIVATE", "seg": [{"id": 0, "start": 0, "stop": 31,
            "grp": 1, "spc": 0, "of": 0, "set": 0, "si": 0, "m12": 0,
            "rev": False, "mi": False, "frz": False, "o1": False, "o2": False,
            "o3": False, "on": True, "bri": 255, "col": [[0, 8, 255, 0], [0, 0, 0, 0]],
            "fx": fx, "pal": pal, "sx": 230, "ix": 128}, {"stop": 0}]}


def pack(entries):
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w") as archive:
        for name, data in entries:
            archive.writestr(name, data)
    return out.getvalue()


class PlanTests(unittest.TestCase):
    def test_supported_candidates_keep_values_and_warn(self):
        solid = plan_preset(preset(), 42, 0)
        self.assertEqual(solid["controls"]["green"], 8)
        self.assertEqual(solid["controls"]["segment stop"], 31)
        self.assertEqual(solid["status"], "review")
        self.assertTrue(solid["warnings"])
        blink = plan_preset(preset(1, 3), 42, 0)
        self.assertEqual(blink["controls"]["frame ms"], 23)
        self.assertEqual(blink["controls"]["speed"], 230)
        self.assertEqual(plan_preset(preset(9, 11), 42, 0)["scene"], "nebulite-rainbow")
        wash = plan_preset(preset(113, 1), 42, 0)
        self.assertEqual(wash["scene"], "nebulite-washing")
        self.assertEqual(wash["controls"]["frame ms"], 23)
        self.assertEqual(plan_preset(preset(113, 1), 42, 1)["status"], "unsupported")
        sine = plan_preset(preset(108, 1), 42, 0)
        self.assertEqual(sine["scene"], "nebulite-sine")
        self.assertEqual(sine["controls"]["frame ms"], 23)
        self.assertEqual(sine["controls"]["secondary white"], 0)
        self.assertEqual(plan_preset(preset(108, 1), 42, 1)["status"], "unsupported")
        palette = plan_preset(preset(65, 3), 42, 0)
        self.assertEqual(palette["scene"], "nebulite-palette")
        self.assertEqual(palette["controls"]["speed"], 230)
        self.assertEqual(plan_preset(preset(65, 3), 42, 2)["status"], "unsupported")
        firefly = plan_preset(preset(106, 35), 42, 0)
        self.assertEqual(firefly["scene"], "nebulite-firefly")
        self.assertEqual(firefly["controls"]["speed"], 230)
        self.assertEqual(plan_preset(preset(106, 35), 42, 1)["status"], "unsupported")
        snow = plan_preset(preset(74, 36), 42, 0)
        self.assertEqual(snow["scene"], "nebulite-snowflakes")
        self.assertEqual(snow["controls"]["frame ms"], 24)
        self.assertEqual(snow["controls"]["source brightness"], 255)
        flow = plan_preset(preset(179, 74), 42, 0)
        self.assertEqual(flow["scene"], "nebulite-flow-stripe")
        self.assertEqual(flow["controls"]["frame ms"], 23)
        self.assertEqual(flow["controls"]["speed"], 230)
        tiny = preset(179, 74); tiny["seg"][0]["stop"] = 1
        self.assertEqual(plan_preset(tiny, 42, 0)["status"], "unsupported")
        sound = plan_preset(preset(133, 74), 42, 0)
        self.assertEqual(sound["scene"], "nebulite-plasmoid")
        self.assertEqual(sound["controls"]["audio floor"], 0)
        self.assertTrue(sound["controls"]["simulate if no source"])
        self.assertTrue(any("Audio input levels" in warning for warning in sound["warnings"]))
        self.assertEqual(plan_preset(preset(133, 74), 42, 1)["status"], "unsupported")
        fire = plan_preset(preset(143, 1), 42, 0)
        self.assertEqual(fire["scene"], "nebulite-noisefire")
        self.assertEqual(fire["controls"]["audio floor"], 0)
        self.assertTrue(fire["controls"]["simulate if no source"])
        self.assertTrue(any("Audio input levels" in warning for warning in fire["warnings"]))
        source_fire = preset(143, 1); source_fire['seg'][0]['m12'] = 2
        fire = plan_preset(source_fire, 42, 0)
        self.assertEqual(fire['scene'], 'nebulite-noisefire')
        self.assertEqual(fire['status'], 'review')
        self.assertTrue(any('1D-to-2D arc' in warning for warning in fire['warnings']))
        self.assertEqual(plan_preset(source_fire, 42, 0, True)['status'], 'unsupported')
        source_fire['seg'][0]['m12'] = 3
        self.assertEqual(plan_preset(source_fire, 42, 0)['status'], 'unsupported')

    def test_rejects_unrepresented_or_invalid_state(self):
        for field, value in [("rev", True), ("grp", 2), ("fx", False), ("pal", True), ("m12", 1)]:
            source = preset()
            source["seg"][0][field] = value
            self.assertEqual(plan_preset(source, 42, 0)["status"], "unsupported", field)
        source = preset(1, 3)
        source["seg"][0]["col"][0][3] = 1
        self.assertEqual(plan_preset(source, 42, 0)["status"], "unsupported")
        source = preset()
        source["seg"].append(deepcopy(source["seg"][0]))
        self.assertEqual(plan_preset(source, 42, 0)["status"], "unsupported")

    def test_preserves_inventory_without_exposing_opaque_data(self):
        entries = {"0": {}, "1": preset(), "2": {"n": "Other", "seg": []}, "250": {"r": "PRIVATE"}, "9": 4}
        config = {"hw": {"led": {"fps": 42}}, "light": {"pal-mode": 0}, "password": "SECRET"}
        inner = pack([("presets.json", json.dumps(entries)), ("cfg.json", json.dumps(config))])
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "archive.zip"
            path.write_bytes(pack([("firmware.bin", bytes([0xe9]) + bytes(23)), ("files.zip", inner)]))
            result = plan_archive(path)
        self.assertEqual(len(result["looks"]), 5)
        self.assertEqual([e["status"] for e in result["looks"]], ["retained-only", "review", "unsupported", "retained-only", "retained-only"])
        self.assertEqual(result["looks"][1]["source_segments"][0]["col"][0], [0, 8, 255, 0])
        self.assertEqual(result["looks"][1]["source_segments"][0]["m12"], 0)
        self.assertEqual(result["looks"][1]["source_segments"][0]["si"], 0)
        self.assertEqual(result["source_frame_rate"], 42)
        self.assertNotIn("PRIVATE", json.dumps(result))
        self.assertNotIn("SECRET", json.dumps(result))

    def test_audio_controls_and_geometry_mapping_are_preserved(self):
        source = preset(140, 2)
        source['seg'][0].update({'c1': 8, 'c2': 48, 'c3': 16, 'm12': 2})
        result = plan_preset(source, 42, 0)
        self.assertEqual(result['reason'], 'sound simulation or 1D-to-2D mapping requires separate migration')
        from plan_wled_looks import source_summary
        summary = source_summary(source)[0]
        self.assertEqual([summary[k] for k in ('c1', 'c2', 'c3', 'm12', 'si')], [8, 48, 16, 2, 0])

    def test_archive_matrix_geometry_is_not_inferred_as_a_strip(self):
        source = preset(143, 1); source['seg'][0]['m12'] = 2
        image = b'\xe9' + bytes(31)
        def archive(matrix):
            led = {'fps': 42}
            if matrix: led['matrix'] = {}
            inner = pack([('presets.json', json.dumps({'1': source})),
                          ('cfg.json', json.dumps({'hw': {'led': led}, 'light': {'pal-mode': 0}}))])
            return pack([('firmware.bin', image), ('files.zip', inner)])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'source.zip'
            path.write_bytes(archive(False))
            look = plan_archive(path)['looks'][0]
            self.assertEqual(look['status'], 'review'); self.assertIs(look['source_is_matrix'], False)
            path.write_bytes(archive(True))
            look = plan_archive(path)['looks'][0]
            self.assertEqual(look['status'], 'unsupported'); self.assertIs(look['source_is_matrix'], True)
            self.assertIn('source geometry', look['reason'])
            inner = pack([('presets.json', json.dumps({'1': source})), ('cfg.json', '{}')])
            path.write_bytes(pack([('firmware.bin', image), ('files.zip', inner)]))
            look = plan_archive(path)['looks'][0]
            self.assertEqual(look['status'], 'unsupported'); self.assertIsNone(look['source_is_matrix'])


if __name__ == "__main__":
    unittest.main()
