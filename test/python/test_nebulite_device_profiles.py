"""Product GPIO/count mappings follow the pinned historical source; manual W is explicit policy."""

import json
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = json.loads((ROOT / 'docs/reference/nebulite-source-profiles.json').read_text())
CATALOG = json.loads((ROOT / 'mooninstaller/deviceModels.json').read_text())
PROFILES = [
    ('croptop', 'NEBULITE Croptop S3', 9, [(0, 8, 36), (8, 1, 46)]),
    ('jacket', 'NEBULITE Jacket S3', 91, [(0, 30, 36), (30, 1, 46), (31, 30, 35), (61, 30, 48)]),
]
# The S3 controller's onboard LED, which shows the connection rather than a scene.
STATUS_LED = 46

# The LED data lines each controller drives: the S3 garment board's four sockets, the classic board's two (rev1 leaves GPIO17 unconnected, so driving it there is harmless).
SOCKETS = {'ESP32-S3': {34, 35, 36, 48}, 'ESP32': {17, 26}}
NEBULITE = [item for item in CATALOG if item['name'].startswith('NEBULITE')]


def windows(profile, types=('RmtLedDriver',)):
    """Each output's (start, length, pin), for the driver types named."""
    return sorted((item['start'], int(item.get('ledsPerPin', item.get('count', 0))), int(item['pins']))
                  for item in profile['state']['Drivers'].values() if isinstance(item, dict) and item.get('type') in types)


@pytest.mark.parametrize('profile', NEBULITE, ids=lambda item: item['name'])
def test_a_single_strand_product_lights_every_socket_and_a_multi_strand_product_continues_its_segments(profile):
    strips = windows(profile)
    if len({(start, length) for start, length, _ in strips}) == 1:
        assert {pin for *_, pin in strips} == SOCKETS[profile['chip']]
    else:
        outputs = windows(profile, ('RmtLedDriver', 'PwmLightDriver'))
        assert all(start + length == following for (start, length, _), (following, *_) in zip(outputs, outputs[1:]))
        assert len({pin for *_, pin in strips}) == len(strips)


def test_nebulite_profile_source_is_pinned_without_claiming_physical_qualification():
    assert SOURCE['source'] == {'repo': 'AuraviaLTD/WLED', 'ref': 'autobuild',
                                'sha': '8c58252cc2f1c0edb1d52b39a625052ed86b8a8c'}
    assert SOURCE['qualification'] == 'source-verified; not physically accepted on MoonLight'


@pytest.mark.parametrize('historical,name,count,mapping', PROFILES)
def test_an_s3_garment_keeps_its_first_historical_strip_on_every_pad_and_leaves_the_onboard_led_to_the_status(historical, name, count, mapping):
    source = SOURCE['profiles'][historical]
    profile = next(item for item in CATALOG if item['name'] == name)
    assert profile['chip'] == 'ESP32-S3'
    assert profile['firmwares'] == ['nebulite-s3']
    assert source['ledCount'] == count
    assert [(item['start'], item['len'], item['pin'][0]) for item in source['outputs']] == mapping
    strips = windows(profile)
    first = mapping[0]
    # The historical first strip, repeated on every LED pad so the garment lights on whichever one it is wired to.
    assert first in strips
    assert {(start, length) for start, length, _ in strips} == {first[:2]}
    assert {pin for *_, pin in strips} == SOCKETS['ESP32-S3']
    assert profile['state']['Layouts']['ProductStrip']['width'] == first[1]
    drivers = [item for item in profile['state']['Drivers'].values() if isinstance(item, dict)]
    assert all(str(STATUS_LED) not in item['pins'].split(',') for item in drivers)
    assert all(item['fixture'] == 'RGB' and item['curve'] == 'linear' for item in drivers)
    # WLED const.h: order 1 is RGB, type 22 is WS2812 RGB, type 41 is single-channel PWM: the strip the garment keeps is a WS2812 RGB output.
    assert all(item['order'] == 1 for item in source['outputs'])
    assert source['outputs'][0]['type'] == 22
    audio = profile['state']['Services']['Audio']
    assert [audio['sdPin'], audio['wsPin'], audio['sckPin']] == source['microphone']['pin'][:3] == [39, 38, 40]


@pytest.mark.parametrize('historical,name,count,mapping', PROFILES)
def test_nebulite_profile_does_not_autofill_unqualified_battery_calibration(historical, name, count, mapping):
    profile = next(item for item in CATALOG if item['name'] == name)
    assert SOURCE['profiles'][historical]['battery']['pin'] == 15
    assert 'Battery' not in profile.get('supported', [])
    assert 'Battery' in profile.get('planned', [])
    services = profile['state']['Services']
    assert not any('Battery' in item.get('type', '') for item in services.values() if isinstance(item, dict))
    assert not any(key in json.dumps(services) for key in ['calibration', 'voltage-multiplier', 'capacityMah'])
