#!/usr/bin/env python3
"""Inventory a historical combined WLED release without importing its settings.

The output is private: it can contain preset names and colours.  Effect-ID
matches are leads for review, not proof of equivalent rendering or wiring.
"""
import argparse
import base64
import binascii
import hashlib
import io
import json
import re
import zipfile
from pathlib import Path

MAX_MEMBER = 16 * 1024 * 1024
SEGMENT_FIELDS = ("id", "start", "stop", "grp", "spc", "of", "on", "frz", "bri", "cct", "col", "fx", "sx", "ix", "pal", "c1", "c2", "c3", "rev", "mi", "o1", "o2", "o3", "si", "m12")
PRESET_FIELDS = ("n", "on", "bri", "transition", "mainseg", "ledmap")


def member(archive, name):
    matches = [x for x in archive.infolist() if x.filename == name]
    if len(matches) != 1 or matches[0].file_size > MAX_MEMBER:
        raise ValueError(f"missing, duplicated or oversized archive member: {name}")
    return archive.read(matches[0])


def digest(data):
    return hashlib.sha256(data).hexdigest()


def compare_candidate(candidate, source_chip, assumed_slot_bytes):
    """Reject obvious mismatches; a fit is not device or OTA authorization."""
    if not isinstance(assumed_slot_bytes, int) or isinstance(assumed_slot_bytes, bool) or assumed_slot_bytes <= 0:
        raise ValueError("a positive explicit legacy slot size is required")
    image = candidate.read_bytes()
    if len(image) < 24 or len(image) > MAX_MEMBER or image[0] != 0xE9:
        raise ValueError("candidate is not a bounded ESP application image")
    chip = int.from_bytes(image[12:14], "little")
    return {"candidate_sha256": digest(image), "candidate_bytes": len(image), "candidate_chip_id": chip,
            "source_chip_matches": chip == source_chip,
            "assumed_legacy_slot_bytes": assumed_slot_bytes,
            "fits_assumed_slot": len(image) <= assumed_slot_bytes,
            "qualification": "Assumed layout only; a fit does not establish the live partition, firmware identity, product wiring, update mechanism or recovery."}


def inventory(path, effects_header, catalog):
    data = path.read_bytes()
    if len(data) > MAX_MEMBER:
        raise ValueError("oversized release archive")
    effects = {int(number): name for name, number in re.findall(
        r"^#define\s+(FX_MODE_[A-Z0-9_]+)\s+(\d+)\b", effects_header, re.M)}
    scenes = {}
    for scene in catalog.get("factoryScenes", []):
        fx = scene.get("source", {}).get("effect")
        if isinstance(fx, int):
            scenes.setdefault(fx, []).append(scene["id"])
    with zipfile.ZipFile(io.BytesIO(data)) as release:
        image = member(release, "firmware.bin")
        files = member(release, "files.zip")
    with zipfile.ZipFile(io.BytesIO(files)) as archive:
        raw_presets = member(archive, "presets.json")
        # Copy only rendering settings needed to interpret a look. cfg.json
        # also holds Wi-Fi and other private data, which must never be emitted.
        config_names = [x for x in archive.infolist() if x.filename == "cfg.json"]
        config = json.loads(member(archive, "cfg.json")) if config_names else {}
        led = config.get("hw", {}).get("led", {})
        light = config.get("light", {})
        gamma = light.get("gc", {})
        render_settings = {"target_fps": led.get("fps"), "palette_blend_mode": light.get("pal-mode"),
                           "color_gamma_control": gamma.get("col"), "color_gamma_value": gamma.get("val")}
    presets = json.loads(raw_presets)
    if not isinstance(presets, dict) or len(image) < 24 or image[0] != 0xE9:
        raise ValueError("invalid preset document or ESP image header")
    result = []
    for preset_id, preset in presets.items():
        if not isinstance(preset, dict):
            result.append({"id": preset_id, "unsupported_shape": True})
            continue
        entry = {"id": preset_id, **{key: preset[key] for key in PRESET_FIELDS if key in preset}}
        # WLED may store extra values such as per-pixel data, playlists or
        # future controls.  This summary must never imply it captured a full
        # preset; the original archive remains the lossless source.
        entry["unreported_fields"] = sorted(set(preset) - set(PRESET_FIELDS) - {"seg"})
        entry["canonical_preset_sha256"] = digest(json.dumps(
            preset, sort_keys=True, ensure_ascii=False, separators=(",", ":")
        ).encode("utf-8"))
        # Some historical presets contain a large opaque `r` field. Preserve
        # it only in the original archive, but expose bounded provenance and
        # size for later analysis; its format and frame rate are not assumed.
        opaque = preset.get("r")
        if isinstance(opaque, str):
            try:
                decoded = base64.b64decode(opaque, validate=True)
                entry["opaque_r"] = {"valid_base64": True, "decoded_bytes": len(decoded),
                                     "sha256": digest(decoded)}
            except (binascii.Error, ValueError):
                entry["opaque_r"] = {"valid_base64": False}
        segments = preset.get("seg", [])
        if isinstance(segments, dict):
            segments = [segments]
        if not isinstance(segments, list):
            entry["unsupported_segments"] = True
            segments = []
        entry["segments"] = []
        for segment in segments:
            if not isinstance(segment, dict):
                entry["segments"].append({"unsupported_shape": True})
                continue
            # Empty WLED placeholders carry no configuration. Keep all other
            # segments, including disabled and incomplete segments, for review.
            if segment == {"stop": 0}:
                continue
            item = {key: segment[key] for key in SEGMENT_FIELDS if key in segment}
            item["unreported_fields"] = sorted(set(segment) - set(SEGMENT_FIELDS))
            fx = segment.get("fx")
            if isinstance(fx, int):
                item["source_effect_name"] = effects.get(fx)
                item["catalogue_id_candidates"] = scenes.get(fx, [])
            entry["segments"].append(item)
        result.append(entry)
    chip = int.from_bytes(image[12:14], "little")
    return {"archive_sha256": digest(data), "firmware_sha256": digest(image),
            "firmware_bytes": len(image), "esp_image_chip_id": chip,
            "presets_sha256": digest(raw_presets), "render_settings": render_settings, "presets": result,
            "review": "Effect IDs are candidates only; segment mapping, palette, parameters, wiring and output require verification. No configuration was imported."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--fx-header", required=True, type=Path)
    parser.add_argument("--catalog", required=True, type=Path)
    parser.add_argument("--candidate", type=Path, help="optional target app-only image")
    parser.add_argument("--legacy-slot-bytes", type=int, help="explicit assumed old OTA slot size")
    args = parser.parse_args()
    if bool(args.candidate) != (args.legacy_slot_bytes is not None):
        parser.error("--candidate and --legacy-slot-bytes must be supplied together")
    result = inventory(args.archive, args.fx_header.read_text(), json.loads(args.catalog.read_text()))
    if args.candidate:
        result["candidate_review"] = compare_candidate(args.candidate, result["esp_image_chip_id"], args.legacy_slot_bytes)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
