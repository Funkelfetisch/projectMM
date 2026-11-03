"""Exercise actual ESP32 fsList with SDK declarations and recording POSIX calls.

No LittleFS timing or physical flash claim: the fixture proves stat admission,
fallback classification, default sizes and the SD mode dispatch contract.
"""

from pathlib import Path
import shutil
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "test/fixtures/esp32_fs"
CXX = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
pytestmark = pytest.mark.skipif(CXX is None or sys.platform == "win32", reason="requires a POSIX C++ compiler")


@pytest.fixture(scope="module")
def listing_binary(tmp_path_factory):
    binary = tmp_path_factory.mktemp("esp32-fs-listing") / "listing"
    # Unused platform functions stay outside this directory-only executable.
    discard_unused = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
    result = subprocess.run(
        [CXX, "-std=c++20", "-Wall", "-Wextra", "-ffunction-sections",
         "-fdata-sections", "-I", str(FIXTURE), "-I", str(ROOT / "src"),
         "-I", str(ROOT / "src/platform/desktop"), str(FIXTURE / "listing.cpp"),
         discard_unused, "-o", str(binary)],
        capture_output=True, text=True, timeout=30,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    return binary


@pytest.mark.parametrize("case", ["known", "fallback", "metadata", "root", "sd"])
def test_esp32_fs_listing(listing_binary, case):
    result = subprocess.run([str(listing_binary), case], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
