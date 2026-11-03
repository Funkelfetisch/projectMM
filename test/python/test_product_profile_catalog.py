"""Product setup follows the same hardware catalogue as the installer."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def test_product_setup_table_matches_its_canonical_catalogue():
    spec = importlib.util.spec_from_file_location("product_profiles", ROOT / "moondeck/build/gen_product_profiles.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    assert module.OUTPUT.read_text(encoding="utf-8") == module.output()
