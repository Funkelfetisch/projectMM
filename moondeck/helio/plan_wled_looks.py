#!/usr/bin/env python3
"""Prepare a private review of archived WLED looks; never writes a device.

Candidates retain the source preset's digest. Original archives are required
for lossless backup. A candidate is not an optical match or an automatic import.
"""
import argparse
import base64
import binascii
import hashlib
import importlib.util
import io
import json
import os
import re
from pathlib import Path
import zipfile

from inventory_wled_release import MAX_MEMBER, member


def byte(value):
    return type(value) is int and 0 <= value <= 255


def rgbw(value):
    return isinstance(value, list) and len(value) in (3, 4) and all(byte(v) for v in value)


def source_summary(preset):
    """Expose review-relevant render fields, never the opaque preview or credentials."""
    if not isinstance(preset, dict):
        return None
    segments = preset.get("seg")
    if isinstance(segments, dict):
        segments = [segments]
    if not isinstance(segments, list):
        return None
    numeric = ("id", "start", "stop", "startY", "stopY", "fx", "pal", "sx", "ix", "grp", "spc", "of",
               "c1", "c2", "c3", "set", "si", "m12")
    flags = ("rev", "mi", "frz", "o1", "o2", "o3")
    result = []
    for segment in segments:
        if not isinstance(segment, dict) or segment == {"stop": 0}:
            continue
        item = {key: segment[key] for key in numeric if type(segment.get(key)) is int}
        item.update({key: segment[key] for key in flags if type(segment.get(key)) is bool})
        colors = segment.get("col")
        if isinstance(colors, list) and all(rgbw(color) for color in colors):
            item["col"] = colors
        result.append(item)
    return result


def plan_preset(preset, fps, palette_blend, source_is_matrix=False):
    """Return a candidate or a reason; reject unsupported structure, not just names."""
    if not isinstance(preset, dict):
        return {"status": "unsupported", "reason": "preset is not an object"}
    segments = preset.get("seg")
    if isinstance(segments, dict):
        segments = [segments]
    if not isinstance(segments, list):
        return {"status": "unsupported", "reason": "segments missing"}
    active = [s for s in segments if s != {"stop": 0}]
    if len(active) != 1 or not isinstance(active[0], dict):
        return {"status": "unsupported", "reason": "requires exactly one segment"}
    seg = active[0]
    if source_is_matrix is not False:
        return {"status": "unsupported", "reason": "2D or unknown source geometry requires separate migration"}
    start, stop = seg.get("start"), seg.get("stop")
    if type(start) is not int or type(stop) is not int or not 0 <= start < stop <= 65535:
        return {"status": "unsupported", "reason": "invalid or missing segment bounds"}
    if preset.get("on") is not True or not byte(preset.get("bri")) or preset["bri"] != 255 or seg.get("on") is not True or not byte(seg.get("bri")) or seg["bri"] != 255:
        return {"status": "unsupported", "reason": "power or brightness needs separate migration"}
    expected = {"id": 0, "grp": 1, "spc": 0, "of": 0, "set": 0,
                "rev": False, "mi": False, "frz": False, "o1": False, "o2": False, "o3": False}
    if any(type(seg.get(k)) is not type(v) or seg.get(k) != v for k, v in expected.items()) or any(
        type(preset.get(k)) is not int or preset.get(k) != 0 for k in ("mainseg", "ledmap")
    ):
        return {"status": "unsupported", "reason": "segment geometry or freeze is not supported"}
    mapping = seg.get("m12")
    noisefire_arc = seg.get("fx") == 143 and type(mapping) is int and mapping == 2
    if type(seg.get("si")) is not int or seg["si"] != 0 or type(mapping) is not int or (mapping != 0 and not noisefire_arc):
        return {"status": "unsupported", "reason": "sound simulation or 1D-to-2D mapping requires separate migration"}
    colors = seg.get("col")
    if not isinstance(colors, list) or not colors or not rgbw(colors[0]):
        return {"status": "unsupported", "reason": "primary RGBW is missing"}
    primary = (colors[0] + [0])[:4]
    common = {"segment start": start, "segment stop": stop}
    fx, palette = seg.get("fx"), seg.get("pal")
    if not byte(fx) or not byte(palette):
        return {"status": "unsupported", "reason": "invalid effect or palette"}
    if fx == 0:
        scene = "nebulite-solid"
        controls = {**dict(zip(("red", "green", "blue", "white"), primary)),
                    "brightness": 255, **common}
    elif fx == 9 and palette == 11 and byte(seg.get("sx")) and byte(seg.get("ix")):
        scene = "nebulite-rainbow"
        controls = {"speed": seg["sx"], "size": seg["ix"], **common}
    elif fx == 1 and palette == 3 and byte(seg.get("sx")) and byte(seg.get("ix")):
        if primary[3] or len(colors) < 2 or not rgbw(colors[1]) or type(fps) is not int or not 1 <= fps <= 255 or type(palette_blend) is not int or not 0 <= palette_blend <= 3:
            return {"status": "unsupported", "reason": "blink colors or WLED timing are not supported"}
        secondary = (colors[1] + [0])[:4]
        scene = "nebulite-blink"
        controls = {"speed": seg["sx"], "effect intensity": seg["ix"],
                    **dict(zip(("red", "green", "blue"), primary[:3])),
                    **dict(zip(("secondary red", "secondary green", "secondary blue", "secondary white"), secondary)),
                    "palette blend": palette_blend, "frame ms": max(1, 1000 // fps), **common}
    elif fx == 113 and palette == 1 and byte(seg.get("sx")) and byte(seg.get("ix")):
        if type(fps) is not int or not 1 <= fps <= 255 or palette_blend != 0 or type(palette_blend) is not int:
            return {"status": "unsupported", "reason": "washing timing or palette mode is not supported"}
        scene = "nebulite-washing"
        controls = {"speed": seg["sx"], "effect intensity": seg["ix"], "frame ms": 1000 // fps, **common}
    elif fx == 108 and palette == 1 and byte(seg.get("sx")) and byte(seg.get("ix")):
        if len(colors) < 2 or not rgbw(colors[1]) or type(fps) is not int or not 1 <= fps <= 255 or type(palette_blend) is not int or palette_blend != 0:
            return {"status": "unsupported", "reason": "sine colors, timing or palette mode are not supported"}
        secondary = (colors[1] + [0])[:4]
        scene = "nebulite-sine"
        controls = {"speed": seg["sx"], "effect intensity": seg["ix"], "frame ms": 1000 // fps,
                    **dict(zip(("secondary red", "secondary green", "secondary blue", "secondary white"), secondary)), **common}
    elif fx == 74 and palette == 36 and byte(seg.get("sx")) and byte(seg.get("ix")):
        scene = "nebulite-snowflakes"
        controls = {"speed": seg["sx"], "effect intensity": seg["ix"],
                    "source brightness": 255, "follow master": True, "frame ms": 24, **common}
    elif fx == 179 and palette == 74 and byte(seg.get("sx")) and byte(seg.get("ix")):
        if type(fps) is not int or not 1 <= fps <= 255 or stop - start < 2:
            return {"status": "unsupported", "reason": "Flow Stripe needs valid frame timing and at least two LEDs"}
        scene = "nebulite-flow-stripe"
        controls = {"speed": seg["sx"], "effect intensity": seg["ix"], "frame ms": 1000 // fps, **common}
    elif fx == 143 and byte(seg.get("sx")) and byte(seg.get("ix")):
        if type(fps) is not int or not 1 <= fps <= 255:
            return {"status": "unsupported", "reason": "Noisefire needs valid frame timing"}
        scene = "nebulite-noisefire"
        controls = {"speed": seg["sx"], "effect intensity": seg["ix"], "audio floor": 0, "simulate if no source": True,
                    "frame ms": 1000 // fps, **common}
    elif fx == 133 and palette == 74 and byte(seg.get("sx")) and byte(seg.get("ix")):
        if len(colors) < 2 or not rgbw(colors[1]) or type(fps) is not int or not 1 <= fps <= 255 or type(palette_blend) is not int or palette_blend != 0:
            return {"status": "unsupported", "reason": "Plasmoid colors, timing or palette mode are not supported"}
        secondary = (colors[1] + [0])[:4]
        scene = "nebulite-plasmoid"
        controls = {"speed": seg["sx"], "effect intensity": seg["ix"], "audio floor": 0, "simulate if no source": True,
                    **dict(zip(("secondary red", "secondary green", "secondary blue", "secondary white"), secondary)),
                    "frame ms": 1000 // fps, **common}
    elif fx == 106 and palette == 35 and byte(seg.get("sx")) and byte(seg.get("ix")):
        if len(colors) < 2 or not rgbw(colors[1]) or type(fps) is not int or not 1 <= fps <= 255 or type(palette_blend) is not int or palette_blend != 0:
            return {"status": "unsupported", "reason": "firefly colors, timing or palette mode are not supported"}
        secondary = (colors[1] + [0])[:4]
        scene = "nebulite-firefly"
        controls = {"speed": seg["sx"], "effect intensity": seg["ix"], "frame ms": 1000 // fps,
                    **dict(zip(("secondary red", "secondary green", "secondary blue", "secondary white"), secondary)), **common}
    elif fx == 65 and palette == 3 and byte(seg.get("sx")):
        if primary[3] or len(colors) < 2 or not rgbw(colors[1]) or type(fps) is not int or not 1 <= fps <= 255 or type(palette_blend) is not int or palette_blend != 0:
            return {"status": "unsupported", "reason": "palette colors, timing or blend mode are not supported"}
        secondary = (colors[1] + [0])[:4]
        if secondary[3]:
            return {"status": "unsupported", "reason": "palette white is not supported"}
        scene = "nebulite-palette"
        controls = {"speed": seg["sx"], "frame ms": 1000 // fps,
                    **dict(zip(("red", "green", "blue"), primary[:3])),
                    **dict(zip(("secondary red", "secondary green", "secondary blue"), secondary[:3])), **common}
    else:
        return {"status": "unsupported", "reason": "effect or palette has no reviewed mapping"}
    return {"status": "review", "scene": scene, "controls": controls,
            "warnings": (["Audio input levels are not calibrated to the original device; the no-source beat simulation is approximate." ] if scene in ("nebulite-plasmoid", "nebulite-noisefire") else []) +
                        (["The source specifies a 1D-to-2D arc; it is inactive for its configured 1D strip."] if noisefire_arc else []) +
                        ["Source rendering, WLED gamma and transitions are not proven equivalent.",
                         "Only one segment is represented; non-render fields and original preview remain in the archive.",
                         "Confirm board wiring and physical output before applying."]}


def catalog_revision():
    root = Path(__file__).resolve().parents[2]
    generator = root / "moondeck/build/gen_product_scenes.py"
    spec = importlib.util.spec_from_file_location("product_scene_generator", generator)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    header = root / "src/helio/BleSceneData.h"
    expected = module.outputs()[header]
    if header.read_text(encoding="utf8") != expected:
        raise ValueError("generated scene catalogue is stale")
    match = re.search(r'kRevision = "([0-9a-f]{12})"', expected)
    if not match:
        raise ValueError("scene catalogue revision is missing")
    return match.group(1)


def plan_archive(path):
    raw = path.read_bytes()
    if len(raw) > MAX_MEMBER:
        raise ValueError("archive too large")
    with zipfile.ZipFile(io.BytesIO(raw)) as outer:
        image, files = member(outer, "firmware.bin"), member(outer, "files.zip")
    if len(image) < 24 or image[0] != 0xe9:
        raise ValueError("invalid ESP image")
    with zipfile.ZipFile(io.BytesIO(files)) as inner:
        presets = json.loads(member(inner, "presets.json"))
        config = json.loads(member(inner, "cfg.json"))
    if not isinstance(presets, dict) or not isinstance(config, dict):
        raise ValueError("invalid source documents")
    hardware = config.get("hw")
    led_config = hardware.get("led") if isinstance(hardware, dict) else None
    fps = led_config.get("fps") if isinstance(led_config, dict) else None
    source_is_matrix = None if not isinstance(led_config, dict) else led_config.get("matrix") is not None
    light = config.get("light")
    blend = light.get("pal-mode") if isinstance(light, dict) else None
    results = []
    for key, value in presets.items():
        canonical = json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":")).encode()
        name = value.get("n") if isinstance(value, dict) else None
        recording_sha = None
        encoded = value.get("r") if isinstance(value, dict) else None
        if isinstance(encoded, str) and len(encoded) <= 1048576:
            try:
                recording_sha = hashlib.sha256(base64.b64decode(encoded, validate=True)).hexdigest()
            except (ValueError, binascii.Error):
                pass
        result = ({"status": "retained-only", "reason": "reserved, autosave or unnamed entry requires review"}
                  if key in ("0", "250") or not isinstance(name, str) else plan_preset(value, fps, blend, source_is_matrix))
        results.append({"source_id": key, "name": name if isinstance(name, str) else None,
                        "source_preset_sha256": hashlib.sha256(canonical).hexdigest(),
                        "source_recording_sha256": recording_sha,
                        "source_segments": source_summary(value), "source_is_matrix": source_is_matrix, **result})
    return {"source_archive_sha256": hashlib.sha256(raw).hexdigest(), "source_chip_id": int.from_bytes(image[12:14], "little"),
            "target_catalog": catalog_revision(), "source_frame_rate": fps, "source_palette_blend": blend, "looks": results,
            "qualification": "Review only; no device or source archive was changed."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--output", type=Path, required=True, help="new private review file; existing files are never overwritten")
    args = parser.parse_args()
    plan = plan_archive(args.archive)
    fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        json.dump(plan, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print(f"Prepared {len(plan['looks'])} private review entries; no device changes.")


if __name__ == "__main__":
    main()
