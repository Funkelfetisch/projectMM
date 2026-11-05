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
