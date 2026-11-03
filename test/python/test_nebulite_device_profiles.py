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


def test_nebulite_profile_source_is_pinned_without_claiming_physical_qualification():
    assert SOURCE['source'] == {'repo': 'AuraviaLTD/WLED', 'ref': 'autobuild',
                                'sha': '8c58252cc2f1c0edb1d52b39a625052ed86b8a8c'}
    assert SOURCE['qualification'] == 'source-verified; not physically accepted on MoonLight'


@pytest.mark.parametrize('historical,name,count,mapping', PROFILES)
def test_nebulite_profile_preserves_historical_gpio_counts_and_manual_white(historical, name, count, mapping):
    source = SOURCE['profiles'][historical]
    profile = next(item for item in CATALOG if item['name'] == name)
    assert profile['chip'] == 'ESP32-S3'
    assert profile['firmwares'] == ['nebulite-s3']
    assert source['ledCount'] == count
    assert [(item['start'], item['len'], item['pin'][0]) for item in source['outputs']] == mapping
    drivers = [item for item in profile['state']['Drivers'].values() if isinstance(item, dict)]
    actual = sorted((item['start'], int(item.get('ledsPerPin', item.get('count', 0))), int(item['pins'])) for item in drivers)
    assert actual == mapping
    # WLED const.h: order 1 is RGB, type 22 is WS2812 RGB, type 41 is single-channel PWM.
    # Match output roles as well as pin/count; equal ranges alone cannot catch swapped channels.
    assert all(item['order'] == 1 for item in source['outputs'])
    assert [item['type'] for item in source['outputs']] == [
        41 if item['fixture'] == 'White' else 22
        for item in sorted(drivers, key=lambda item: item['start'])
    ]
    assert all(item['fixture'] == 'RGB' for item in drivers if item['type'] == 'RmtLedDriver')
    # Setup establishes the same explicit policy on every output even when
    # matching module names already held a different inherited correction.
    # This is not an optical match to WLED's separate color-gamma setting.
    assert all(item['curve'] == 'linear' for item in drivers)
    accent = profile['state']['Drivers']['Accent']
    assert accent['type'] == 'PwmLightDriver'
    assert accent['fixture'] == 'White'
    assert accent['pins'] == '46'
    assert accent['whiteMode'] == 'None'
    assert accent['count'] == 1
    assert accent['bits'] == 12
    assert profile['state']['Layouts']['ProductStrip']['width'] == count
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
