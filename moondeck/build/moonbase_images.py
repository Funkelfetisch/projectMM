#!/usr/bin/env python3
"""Build the images that carry MoonBase beside the app: MoonBase itself and the migration image.

build_esp32.py calls both after the app build for a variant that carries them.
"""

import subprocess
import sys
from pathlib import Path

import compute_version   # sibling: the one place a version string is derived
from generate_build_info import build_id   # sibling: the one place the build id is derived
import sdkconfig_fragments   # sibling: a changed fragment regenerates a build dir's sdkconfig

ROOT = Path(__file__).resolve().parent.parent.parent

# MoonBase's output name, the `project()` name in moonbase/CMakeLists.txt; build_esp32.py re-exports it beside the app's.
MOONBASE_BIN = "MoonLight-moonbase.bin"
S3_MIGRATE_BIN = "MoonLight-migrate-s3.bin"
# WLED's Update.begin reserves one 4 KB sector from its 2 MB OTA slot.
WLED_S3_UPLOAD_LIMIT = 0x200000 - 0x1000


def check_s3_migrate_size(image: Path) -> int:
    """Require a nonempty migration image within WLED's upload budget and return its size."""
    size = image.stat().st_size
    if not 0 < size <= WLED_S3_UPLOAD_LIMIT:
        raise ValueError(f"S3 migration image is {size} bytes; WLED accepts 1..{WLED_S3_UPLOAD_LIMIT}")
    return size


def build_moonbase(cmd: list[str], env: dict, chip: str, version: str = "") -> None:
    """Build the MoonBase image for `chip` into build/moonbase-<chip>.

    MoonBase (moonbase/) is the second boot image the MoonBase variants carry in their factory
    partition: a small firmware whose job is installing the application, since a board with one
    app slot cannot rewrite the partition it is executing from. It is chip-specific but variant-
    agnostic, so every classic variant shares one build. Its size budget lives in
    moonbase/sdkconfig.defaults; the shared partition table keeps the two images provably agreed
    on where everything lives.

    `version` plus the build id becomes PROJECT_VER, `6.0.0+990a3d84` in SemVer's build-metadata form, which IDF writes into the image's app descriptor.
    The app reads it back from the factory partition and shows the version and the build id as it shows its own, so a device can say which MoonBase it carries.
    A changed id also changes the descriptor's compile flags, so IDF recompiles it and its date is the build's rather than the build folder's first.
    A version is variant-independent, so passing it keeps one image per chip valid.
    """
    moonbase_dir = ROOT / "moonbase"
    build_dir = ROOT / "build" / f"moonbase-{chip}"
    b_arg = ["-B", str(build_dir), f"-DSDKCONFIG={build_dir}/sdkconfig"]
    if not version:
        # The library.json default the app resolves through build_info.cpp's #ifndef, rather than a git-describe string the app cannot compare against.
        version = compute_version.compute("local", "")
    b_arg.append(f"-DPROJECT_VER={version}+{build_id()}")
    # An edited moonbase/sdkconfig.defaults regenerates the sdkconfig, as the app's fragments do.
    digest = sdkconfig_fragments.refresh(cmd + b_arg, build_dir, [moonbase_dir / "sdkconfig.defaults"],
                                         moonbase_dir, env)
    if not build_dir.exists():
        print(f"Setting MoonBase target to {chip}...")
        r = subprocess.run(cmd + b_arg + ["set-target", chip], cwd=moonbase_dir, env=env)
        if r.returncode != 0:
            sys.exit(r.returncode)
    print(f"Building MoonBase for {chip}...")
    r = subprocess.run(cmd + b_arg + ["build"], cwd=moonbase_dir, env=env)
    if r.returncode != 0:
        sys.exit(r.returncode)
    sdkconfig_fragments.record(build_dir, digest)
    binp = build_dir / MOONBASE_BIN
    if binp.exists():
        kb = binp.stat().st_size / 1024
        # Slot fit is printed by IDF itself ("Smallest app partition ... free"); repeating a
        # hardcoded slot size here would lie the day the table changes.
        print(f"MoonBase image: {kb:.0f} KB")


def build_migrate(cmd: list[str], env: dict, chip: str, app_build_dir: Path, pause: int = 0, variant: str = "") -> None:
    """Build the migration image into build/migrate-<name>, or with `pause` the rehearsal image into build/migrate-<name>-pause.

    The C3 image, named after its chip, embeds MoonBase and the app's table. An S3 image, one per product `variant` (nebulite-s3, helio1) and named after it, embeds the dual-OTA table and receives that product's signed image over HTTP.
    """
    s3 = chip == "esp32s3"
    if s3 and not variant:
        sys.exit("an S3 migration image is built for one product variant")
    name = variant if s3 else chip
    project = ROOT / "moonbase" / ("migrate-s3" if s3 else "migrate")
    build_dir = ROOT / "build" / (f"migrate-{name}-pause" if pause else f"migrate-{name}")
    moonbase_bin = ROOT / "build" / f"moonbase-{chip}" / MOONBASE_BIN
    table_bin = app_build_dir / "partition_table" / "partition-table.bin"
    b_arg = ["-B", str(build_dir), f"-DSDKCONFIG={build_dir}/sdkconfig", f"-DTABLE_BIN={table_bin}"]
    if s3:
        b_arg += [f"-DPROJECT_VER={compute_version.compute('local', '')}+{build_id()}", f"-DMM_MIGRATE_VARIANT={variant}"]
    else:
        b_arg.append(f"-DMOONBASE_BIN={moonbase_bin}")
    fragments = [project / "sdkconfig.defaults"]
    if pause:
        # The pause rides in a defaults file of its own, layered after the project's, so the shipped image never carries it.
        extra = build_dir.with_suffix(".defaults")
        extra.write_text(f"CONFIG_MM_MIGRATE_PAUSE_S={pause}\n")
        fragments.append(extra)
    b_arg.append("-DSDKCONFIG_DEFAULTS=" + ";".join(str(path) for path in fragments))
    digest = sdkconfig_fragments.refresh(cmd + b_arg, build_dir, fragments, project, env)
    if not (build_dir / "CMakeCache.txt").exists():
        print(f"Setting migrate target to {chip}...")
        r = subprocess.run(cmd + b_arg + ["set-target", chip], cwd=project, env=env)
        if r.returncode != 0:
            sys.exit(r.returncode)
    print(f"Building the migration image for {chip}...")
    r = subprocess.run(cmd + b_arg + ["build"], cwd=project, env=env)
    if r.returncode != 0:
        sys.exit(r.returncode)
    sdkconfig_fragments.record(build_dir, digest)
    if s3:
        try:
            size = check_s3_migrate_size(build_dir / S3_MIGRATE_BIN)
        except (OSError, ValueError) as failure:
            sys.exit(str(failure))
        print(f"S3 migration image for {variant}: {size} bytes; {WLED_S3_UPLOAD_LIMIT - size} bytes below WLED's upload limit")
