"""The WLED S3 migrator builds separately from MoonBase and fits the updater's smaller budget."""

import sys
from pathlib import Path
from types import SimpleNamespace

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "moondeck" / "build"))

import build_esp32  # noqa: E402
import moonbase_images  # noqa: E402


@pytest.mark.parametrize("size", [1, 2093056])
def test_the_wled_upload_budget_includes_its_exact_limit(tmp_path, size):
    image = tmp_path / "migrator.bin"
    with image.open("wb") as out:
        out.truncate(size)
    assert moonbase_images.check_s3_migrate_size(image) == size


@pytest.mark.parametrize("size", [0, 2093057])
def test_an_empty_or_oversized_wled_migrator_is_rejected(tmp_path, size):
    image = tmp_path / "migrator.bin"
    with image.open("wb") as out:
        out.truncate(size)
    with pytest.raises(ValueError, match="WLED accepts 1..2093056"):
        moonbase_images.check_s3_migrate_size(image)


@pytest.fixture
def migration_build(monkeypatch, tmp_path):
    """Capture IDF commands while keeping fragment regeneration and image-size validation real."""
    monkeypatch.setattr(moonbase_images, "ROOT", tmp_path)
    monkeypatch.setattr(moonbase_images, "build_id", lambda: "dbce2f47")
    monkeypatch.setattr(moonbase_images.compute_version, "compute", lambda *_: "7.0.0")
    (tmp_path / "build").mkdir()
    for name in ("migrate", "migrate-s3"):
        project = tmp_path / "moonbase" / name
        project.mkdir(parents=True)
        (project / "sdkconfig.defaults").write_text("CONFIG_TEST=y\n")
    calls = []

    def run(cmd, *, cwd, env):
        """A successful IDF build writes its output where the command requests it."""
        calls.append((cmd, cwd))
        build_dir = Path(cmd[cmd.index("-B") + 1])
        build_dir.mkdir(exist_ok=True)
        (build_dir / "CMakeCache.txt").touch()
        if cmd[-1] == "build":
            (build_dir / moonbase_images.S3_MIGRATE_BIN).write_bytes(b"S3")
        return SimpleNamespace(returncode=0)

    monkeypatch.setattr(moonbase_images.subprocess, "run", run)
    return tmp_path, calls


def test_the_s3_build_embeds_only_the_product_table_and_records_its_defaults(migration_build, capsys):
    root, calls = migration_build
    app = root / "build" / "helio1"
    moonbase_images.build_migrate(["idf"], {}, "esp32s3", app, variant="helio1")
    assert [cmd[-1] for cmd, _ in calls] == ["esp32s3", "build"]
    assert all(cwd == root / "moonbase" / "migrate-s3" for _, cwd in calls)
    args = calls[-1][0]
    assert f"-DTABLE_BIN={app}/partition_table/partition-table.bin" in args
    assert "-DPROJECT_VER=7.0.0+dbce2f47" in args
    assert "-DMM_MIGRATE_VARIANT=helio1" in args
    assert all(not arg.startswith("-DMOONBASE_BIN=") for arg in args)
    build = root / "build" / "migrate-helio1"
    assert (build / moonbase_images.sdkconfig_fragments.RECORD).is_file()
    assert "2093054 bytes below WLED's upload limit" in capsys.readouterr().out


def test_a_changed_pause_regenerates_the_rehearsal_config_only(migration_build):
    root, calls = migration_build
    app = root / "build" / "helio1"
    moonbase_images.build_migrate(["idf"], {}, "esp32s3", app, 5, variant="helio1")
    build = root / "build" / "migrate-helio1-pause"
    record = build / moonbase_images.sdkconfig_fragments.RECORD
    first_digest = record.read_text()
    (build / "sdkconfig").write_text("CONFIG_MM_MIGRATE_PAUSE_S=5\n")
    calls.clear()
    moonbase_images.build_migrate(["idf"], {}, "esp32s3", app, 10, variant="helio1")
    assert [cmd[-1] for cmd, _ in calls] == ["reconfigure", "build"]
    assert (root / "build" / "migrate-helio1-pause.defaults").read_text() == "CONFIG_MM_MIGRATE_PAUSE_S=10\n"
    assert record.read_text() != first_digest
    assert not (root / "build" / "migrate-helio1").exists()


def test_the_c3_migrator_keeps_its_moonbase_payload(migration_build):
    root, calls = migration_build
    moonbase_images.build_migrate(["idf"], {}, "esp32c3", root / "build" / "esp32c3")
    assert all(cwd == root / "moonbase" / "migrate" for _, cwd in calls)
    assert f"-DMOONBASE_BIN={root}/build/moonbase-esp32c3/MoonLight-moonbase.bin" in calls[-1][0]


@pytest.mark.parametrize("firmware", ["nebulite-s3", "helio1"])
def test_the_s3_product_build_requests_a_migrator_and_keeps_moonbase_disabled(firmware):
    variant = build_esp32.FIRMWARES[firmware]
    assert variant["chip"] == "esp32s3" and variant["migrate"]
    assert not variant.get("moonbase")


def test_an_s3_migrator_is_built_for_one_product(migration_build):
    root, _ = migration_build
    with pytest.raises(SystemExit):
        moonbase_images.build_migrate(["idf"], {}, "esp32s3", root / "build" / "nebulite-s3")
