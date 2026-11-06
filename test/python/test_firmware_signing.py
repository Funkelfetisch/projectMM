"""The signed release manifest: every value the controller reports is found in the image, and the signature verifies."""
import hashlib
import importlib.util
import struct
from pathlib import Path

import pytest
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils

path = Path(__file__).resolve().parents[2] / "moondeck/helio/firmware_signing.py"
spec = importlib.util.spec_from_file_location("firmware_signing", path)
signing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(signing)

BUILD, BUILT_AT, CATALOG, VERSION = "dbce2f47", "Nov  5 2026 01:34:04", "108d1cde7304", "7.0.0-dev.231"


def image(build=BUILD, dates=(BUILT_AT,), catalog=CATALOG, descriptor=True) -> bytes:
    """An app image as the bundler sees one: the app descriptor 32 bytes in, then NUL-delimited strings."""
    head = bytearray(0x60)
    if descriptor:
        struct.pack_into("<I", head, signing.APP_DESC, signing.APP_DESC_MAGIC)
        head[signing.APP_DESC + 16:signing.APP_DESC + 16 + len(VERSION)] = VERSION.encode()
    strings = [build, *dates, catalog, "MoonLight"]
    return bytes(head) + b"\0" + b"\0".join(s.encode() for s in strings) + b"\0" + bytes(64)


@pytest.fixture
def key():
    return ec.generate_private_key(ec.SECP256R1())


def test_a_release_names_what_the_image_reports_and_signs_its_claims(key):
    data = image()
    m = signing.release(data, "nebulite-s3", 1, key, BUILD, CATALOG, key.public_key())
    assert (m["family"], m["variant"], m["version"], m["build"], m["builtAt"], m["catalog"], m["securityVersion"]) == \
        ("moonlight", "nebulite-s3", VERSION, BUILD, BUILT_AT, CATALOG, 1)
    assert (m["size"], m["sha256"]) == (len(data), hashlib.sha256(data).hexdigest())
    claims = signing.DOMAIN + struct.pack(">III", signing.target_of("nebulite-s3"), len(data), 1) + hashlib.sha256(data).digest()
    assert m["claims"] == claims.hex()
    raw = bytes.fromhex(m["signature"])
    der = utils.encode_dss_signature(int.from_bytes(raw[:32], "big"), int.from_bytes(raw[32:], "big"))
    key.public_key().verify(der, claims, ec.ECDSA(hashes.SHA256()))


@pytest.mark.parametrize("build", ["dbce2f47+", "nogit"])
def test_a_tree_that_stamps_no_clean_build_id_signs_no_release(key, build):
    with pytest.raises(ValueError, match="clean tree"):
        signing.release(image(build=build), "helio1", 1, key, build, CATALOG, key.public_key())


@pytest.mark.parametrize("data", [image(build="cc2ab293"), image(build=BUILD + "+")], ids=["another build", "dirty build"])
def test_an_image_not_built_clean_from_this_tree_is_refused(key, data):
    with pytest.raises(ValueError, match="clean build id"):
        signing.release(data, "helio1", 1, key, BUILD, CATALOG, key.public_key())


@pytest.mark.parametrize("dates", [(), (BUILT_AT, "Nov  4 2026 23:59:59")], ids=["none", "two"])
def test_an_image_needs_exactly_one_build_date(key, dates):
    with pytest.raises(ValueError, match="build dates"):
        signing.release(image(dates=dates), "helio1", 1, key, BUILD, CATALOG, key.public_key())


def test_an_image_without_this_trees_scene_catalogue_is_refused(key):
    with pytest.raises(ValueError, match="scene catalogue"):
        signing.release(image(catalog="a994af6a8f83"), "helio1", 1, key, BUILD, CATALOG, key.public_key())


def test_an_image_without_an_app_descriptor_is_refused(key):
    with pytest.raises(ValueError, match="app descriptor"):
        signing.release(image(descriptor=False), "helio1", 1, key, BUILD, CATALOG, key.public_key())


def test_a_key_that_is_not_the_compiled_publisher_signs_nothing(key):
    with pytest.raises(InvalidSignature):
        signing.release(image(), "helio1", 1, key, BUILD, CATALOG, ec.generate_private_key(ec.SECP256R1()).public_key())


def test_the_tree_values_come_from_the_compiled_headers():
    publisher, security = signing.header_values()
    assert isinstance(publisher, ec.EllipticCurvePublicKey) and security >= 1
    assert len(signing.tree_catalog()) == 12


def migrator_image(project="MoonLight-migrate-s3", chip=9, build=BUILD, date="Nov  5 2026", time="01:34:04") -> bytes:
    """An S3 migrator reports its build through the ESP-IDF descriptor, with no scene catalogue."""
    data = bytearray(512)
    data[0] = 0xE9
    struct.pack_into("<H", data, 12, chip)
    struct.pack_into("<I", data, signing.APP_DESC, signing.APP_DESC_MAGIC)
    for offset, value in ((16, VERSION + "+" + build), (48, project), (80, time), (96, date)):
        data[signing.APP_DESC + offset:signing.APP_DESC + offset + len(value)] = value.encode()
    return bytes(data)


def test_a_catalogue_free_s3_migrator_signs_for_its_own_target(key):
    data = migrator_image()
    manifest = signing.release(data, signing.MIGRATOR_VARIANTS[0], 1, key, BUILD, "", key.public_key())
    assert manifest["catalog"] == ""
    assert manifest["builtAt"] == BUILT_AT
    claims = bytes.fromhex(manifest["claims"])
    assert struct.unpack_from(">I", claims, len(signing.DOMAIN))[0] == signing.target_of(signing.MIGRATOR_VARIANTS[0])
    assert manifest["sha256"] == hashlib.sha256(data).hexdigest()
    raw = bytes.fromhex(manifest["signature"])
    der = utils.encode_dss_signature(int.from_bytes(raw[:32], "big"), int.from_bytes(raw[32:], "big"))
    key.public_key().verify(der, claims, ec.ECDSA(hashes.SHA256()))


@pytest.mark.parametrize("data", [migrator_image(project="MoonLight"), migrator_image(chip=0), image()],
                         ids=["application", "classic chip", "unidentified image"])
def test_the_migrator_variant_refuses_another_project_or_chip(key, data):
    with pytest.raises(ValueError, match="ESP32-S3 MoonLight-migrate-s3"):
        signing.release(data, signing.MIGRATOR_VARIANTS[0], 1, key, BUILD, "", key.public_key())


@pytest.mark.parametrize("variant", ["nebulite-s3", "helio1", "migrate-esp32"])
def test_a_migrator_cannot_be_signed_as_a_product_or_another_migrator(key, variant):
    with pytest.raises(ValueError, match="migrate-<product> variant"):
        signing.release(migrator_image(), variant, 1, key, BUILD, CATALOG, key.public_key())


@pytest.mark.parametrize("build", ["cc2ab293", BUILD + "+"])
def test_a_migrator_from_a_different_or_dirty_build_is_refused(key, build):
    with pytest.raises(ValueError, match="clean build id"):
        signing.release(migrator_image(build=build), signing.MIGRATOR_VARIANTS[0], 1, key, BUILD, "", key.public_key())


def test_a_migrator_with_no_descriptor_build_date_is_refused(key):
    with pytest.raises(ValueError, match="build date"):
        signing.release(migrator_image(date=""), signing.MIGRATOR_VARIANTS[0], 1, key, BUILD, "", key.public_key())


def test_the_catalogue_exception_applies_only_to_the_migrator(key):
    with pytest.raises(ValueError, match="scene catalogue"):
        signing.release(image(catalog=""), "nebulite-s3", 1, key, BUILD, "", key.public_key())


def test_a_migrator_still_requires_the_compiled_publisher(key):
    with pytest.raises(InvalidSignature):
        signing.release(migrator_image(), signing.MIGRATOR_VARIANTS[0], 1, key, BUILD, "",
                        ec.generate_private_key(ec.SECP256R1()).public_key())
