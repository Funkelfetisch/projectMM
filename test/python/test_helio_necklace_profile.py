"""The HELIO necklace profile follows its board, the MINI80 SIDE43 draft in helio-pcb.

The numbers come from the native EasyEDA board `after-variable-USB-pair.json` (source SHA256 70036c56…, strict DRC 0):
43 SK6805SIDE LEDs, LED1 at 4.186° and one every 360/43° counter-clockwise seen from the dome, LED k's DOUT feeding LED k+1,
LED1's DIN buffered from module pad 27 (GPIO47) and the LED rail enabled from pad 21 (GPIO17). The SK6805SIDE datasheet
(OSK-SPC SK6805SIDE-FRGBW-BW-P6 A/0, section 15) sends green, red, blue, white."""

import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CATALOG = json.loads((ROOT / 'mooninstaller/deviceModels.json').read_text())
NECKLACE = next(item for item in CATALOG if item['name'] == 'HELIO necklace')

LEDS = 43
FIRST_DEG = 180 / LEDS        # half a pitch past the board's +x axis, the USB edge
RADIUS = round(LEDS / (2 * math.pi))   # RingLayout::getRadius: lights one unit apart


def ring_points():
    """Board angles to field points: +y up on the board is up in the field, whose y grows downward."""
    points = []
    for k in range(LEDS):
        angle = math.radians(FIRST_DEG + k * 360 / LEDS)
        points.append((round(RADIUS + RADIUS * math.cos(angle)), round(RADIUS - RADIUS * math.sin(angle))))
    return points


def test_necklace_is_a_nebulite_s3_setup_choice():
    assert NECKLACE['chip'] == 'ESP32-S3'
    assert NECKLACE['firmwares'] == ['nebulite-s3']
    assert NECKLACE['state']['System'] == {'deviceModel': 'HELIO necklace'}


def test_necklace_drives_one_grbw_chain_of_43_on_gpio47_with_its_rail_on_gpio17():
    drivers = NECKLACE['state']['Drivers']
    assert drivers['$patch'] == 'replace'
    assert drivers['relayPins'] == '17'
    outputs = [item for item in drivers.values() if isinstance(item, dict)]
    assert outputs == [drivers['RmtLed']]
    ring = drivers['RmtLed']
    assert (ring['type'], ring['pins'], ring['ledsPerPin'], ring['fixture']) == ('RmtLedDriver', '47', str(LEDS), 'GRBW')
    # The default 800 kHz preset (350/700/1250 ns) meets the datasheet's T0H 0.2-0.4 us and T1H 0.62-1.0 us; the SK6812 RGBW preset's 600 ns T1H does not.
    assert 'timing' not in ring


def test_necklace_map_places_the_chain_counter_clockwise_from_the_usb_edge():
    layouts = NECKLACE['state']['Layouts']
    assert layouts['$patch'] == 'replace'
    assert list(layouts) == ['$patch', 'ProductStrip']
    layout = layouts['ProductStrip']
    assert layout['type'] == 'PixelMapLayout'
    points = [tuple(int(v) for v in pair.split(',')) for pair in layout['points'].split(';')]
    assert points == ring_points()
    assert len(set(points)) == LEDS, 'every LED keeps its own field point'
    assert points[0] == (2 * RADIUS, RADIUS - 1), 'LED1 sits at the USB edge, just above center'
    assert points[10][1] == 0 and points[21] == (0, RADIUS), 'a quarter of the way round is the top, halfway is the far edge'


def test_necklace_does_not_claim_battery_or_charging():
    assert 'Battery' not in NECKLACE['supported'] and 'Charging' not in NECKLACE['supported']
    services = NECKLACE['state']['Services']
    assert not any('Battery' in item.get('type', '') for item in services.values())
    audio, ambient = services['Audio'], services['Ambient Light']
    assert [audio['sdPin'], audio['wsPin'], audio['sckPin']] == [39, 38, 40]
    assert [ambient['sda'], ambient['scl'], ambient['address']] == [26, 11, 35]
