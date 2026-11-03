"""The browser and firmware use the same current product scene data."""
import importlib.util
import json
from pathlib import Path


def test_generated_product_scene_tables_match_the_shared_source_and_control_schemas():
    root = Path(__file__).resolve().parents[2]
    specification = importlib.util.spec_from_file_location("product_scenes", root / "moondeck/build/gen_product_scenes.py")
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    for path, text in module.outputs().items():
        assert path.read_text(encoding="utf-8") == text, "Regenerate with gen_product_scenes.py"


def test_product_scene_defaults_expose_only_implemented_controls_with_effect_help():
    root = Path(__file__).resolve().parents[2]
    catalog = json.loads((root / "src/helio/scene-catalog.json").read_text())
    for scene in catalog["factoryScenes"]:
        for name, metadata in scene.get("controlMetadata", {}).items():
            if metadata["visible"]:
                assert metadata["label"]
                assert metadata["description"]
            else:
                assert name not in scene["controls"], (scene["id"], name)


def test_product_scene_parameters_keep_original_labels_and_current_port_limitations():
    root = Path(__file__).resolve().parents[2]
    catalog = json.loads((root / "src/helio/scene-catalog.json").read_text())
    scenes = {scene["id"]: scene for scene in catalog["factoryScenes"]}
    assert scenes["colortwinkles"]["controlMetadata"]["speed"]["label"] == "Fade speed"
    assert "custom 2" not in scenes["geq"]["controls"]
    assert scenes["geq"]["controls"]["option 2"] is False
    assert scenes["freqwave"]["controls"]["option 1"] is False
    assert "logarithmically" in scenes["freqwave"]["controlMetadata"]["option 1"]["description"]
    assert scenes["ripple"]["type"] == "RingsEffect"
    assert scenes["ripple"]["controlMetadata"]["effect intensity"]["portVisible"]


def test_rings_background_has_one_shared_dark_default_and_visible_palette_help():
    root = Path(__file__).resolve().parents[2]
    catalog = json.loads((root / "src/helio/scene-catalog.json").read_text())
    ripple = next(scene for scene in catalog["factoryScenes"] if scene["id"] == "ripple")
    assert ripple["type"] == "RingsEffect"
    assert ripple["controls"]["background brightness"] == 0
    assert ripple["controls"]["max pause ms"] == 500
    pause = ripple["controlMetadata"]["max pause ms"]
    assert pause["visible"]
    assert "mapped LED" in pause["description"]
    metadata = ripple["controlMetadata"]["background brightness"]
    assert metadata["visible"] is True
    assert metadata["label"] == "Background brightness"
    assert "Hue shift" in metadata["description"]
    assert "0 for black" in metadata["description"]
