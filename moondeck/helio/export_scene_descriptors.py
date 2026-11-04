"""Export every product scene's control descriptors, so the app ships them and reads no scene-info.

The descriptors come from the firmware's own writer (BleSceneCatalog::writeInfo) in the product's
desktop build, whose firmware name registers the product effects as the device does, probed as the
device probes them, with no user palette scripts. The file is
keyed by the tag the device reports as `info.descriptors`: a client whose device reports the same
tag uses the file; any other tag means the device differs, and the client streams `scene-info`.
Format: docs/reference/product-ble.md#complete-scenes-and-their-controls.
"""
import argparse
import json
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "moondeck" / "build"))
from generate_build_info import build_id  # noqa: E402

FIRMWARES = ("helio1", "nebulite-s3", "nebulite-esp32")

HARNESS = r'''
#include "helio/BleSceneCatalog.h"
#include "light/drivers/Drivers.h"
#include <cstdio>
#include <vector>
namespace mm { void registerModuleTypes(); }
int main() {
    mm::registerModuleTypes();
    // The device's Drivers publishes its palette scripts before any request; this one finds the catalog's alone.
    mm::Drivers drivers; drivers.defineControls();
    std::printf("{\"catalog\":\"%s\",\"descriptors\":\"%016llx\"}\n", mm::BleSceneCatalog::revision(),
                static_cast<unsigned long long>(mm::BleSceneCatalog::descriptors()));
    // One page as large as a whole list, so the writer that pages over BLE writes every control here at once.
    std::vector<char> page(1 << 16);
    for (uint16_t i = 0; i < mm::BleSceneCatalog::count(); ++i) {
        const char* id = mm::product_scenes::kEntries[i].id;
        mm::JsonSink sink(page.data(), page.size()); sink.append("{");
        if (const char* error = mm::BleSceneCatalog::writeInfo(sink, id, 0)) { std::fprintf(stderr, "%s: %s\n", id, error); return 1; }
        sink.append("}");
        if (sink.overflowed()) { std::fprintf(stderr, "%s: overflow\n", id); return 1; }
        std::puts(page.data());
    }
    return 0;
}
'''


def compile_flags(build: Path) -> list[str]:
    """The flags the desktop build compiled the type registry with, so the harness lays out every class as the libraries do."""
    for entry in json.loads((build / "compile_commands.json").read_text(encoding="utf-8")):
        if entry["file"].endswith("src/module_types.cpp"):
            args = entry.get("arguments") or shlex.split(entry["command"])
            flags, skip = [], False
            for arg in args[1:]:
                if skip:
                    skip = False
                elif arg in ("-o", "-c", "-MF", "-MT", "-MQ"):
                    skip = True
                elif not arg.endswith("module_types.cpp") and arg not in ("-MD", "-MMD"):
                    flags.append(arg)
            return [args[0], *flags]
    raise SystemExit(f"{build}/compile_commands.json has no src/module_types.cpp")


def scenes(lines: list[str]) -> dict:
    """Each scene's controls, with the paging fields gone since one list holds every option."""
    out = {}
    for line in lines:
        info = json.loads(line)
        for control in info["controls"]:
            control.pop("optionCount", None)
            control.pop("optionOffset", None)
        out[info["scene"]] = info["controls"]
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--firmware", choices=FIRMWARES, required=True, help="the product firmware the file ships for")
    parser.add_argument("--out", help="where to write it (default build/esp32-<firmware>/scene-descriptors.json)")
    args = parser.parse_args()
    # The product's desktop build, brought up to date, holds the libraries the writer links against.
    subprocess.run([sys.executable, str(ROOT / "moondeck" / "build" / "build_desktop.py"), "--product", args.firmware], check=True)
    build = ROOT / "build" / f"linux-{args.firmware}"
    compiler, *flags = compile_flags(build)
    with tempfile.TemporaryDirectory(prefix="scene-descriptors-") as tmp:
        harness, binary, data = Path(tmp) / "export.cpp", Path(tmp) / "export", Path(tmp) / "fs"
        harness.write_text(HARNESS, encoding="utf-8")
        data.mkdir()
        subprocess.run([compiler, *flags, str(harness), str(ROOT / "src" / "module_types.cpp"),
                        "-Wl,--start-group", str(build / "libmm_core.a"), str(build / "libmm_platform.a"), "-Wl,--end-group",
                        "-ldl", "-lpthread", "-o", str(binary)], check=True)
        run = subprocess.run([str(binary)], capture_output=True, text=True, env={"MM_DATA_DIR": str(data), "PATH": "/usr/bin:/bin"})
        if run.returncode:
            raise SystemExit(f"export failed: {run.stderr.strip() or run.returncode}")
        lines = run.stdout.splitlines()
    head = json.loads(lines[0])
    document = {"v": 1, "firmware": args.firmware, "build": build_id(), "catalog": head["catalog"],
                "descriptors": head["descriptors"], "scenes": scenes(lines[1:])}
    out = Path(args.out) if args.out else ROOT / "build" / f"esp32-{args.firmware}" / "scene-descriptors.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(document, separators=(",", ":"), ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"{out}: {len(document['scenes'])} scenes, catalog {document['catalog']}, descriptors {document['descriptors']}")


if __name__ == "__main__":
    main()
