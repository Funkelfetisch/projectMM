"""Run the real ESP32 repair helper against a recording NimBLE SDK seam.

The seam follows ESP-IDF v6.1 host/ble_gap.h (connection and repeat-pairing
descriptors) and host/ble_store.h (identity-scoped deletion). It exercises no
radio, bond database, controller, Android phone or running firmware.
"""

from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'test/fixtures/esp32_ble_repeat_pairing'
CXX = shutil.which('c++') or shutil.which('g++') or shutil.which('clang++')
pytestmark = pytest.mark.skipif(CXX is None, reason='no C++ compiler on PATH')


@pytest.fixture(scope='module')
def repeat_pairing_binary(tmp_path_factory):
    binary = tmp_path_factory.mktemp('esp32-ble-repeat-pairing') / 'repeat-pairing'
    result = subprocess.run([CXX, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                             '-I', str(FIXTURE), '-I', str(ROOT / 'src'),
                             str(FIXTURE / 'repeat_pairing.cpp'), '-o', str(binary)],
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    return binary


@pytest.mark.parametrize('case', [
    'default-denial', 'window-boundary', 'clock-wrap', 'zero-deadline',
    'expired-never-revives', 'arm-overflow-stays-closed', 'last-valid-64bit-deadline', 'backwards-clock-denied',
    'reset-preserves-unspent-window', 'arm-requires-stable-primary', 'cancel-prevents-transfer', 'one-use-reset-and-rearm', 'rearm-keeps-connection-budget', 'wrong-event-handle',
    'no-active-connection', 'unknown-connection', 'descriptor-handle-mismatch',
    'encrypted-connection', 'invalid-current-key', 'invalid-new-key', 'sc-required',
    'bonding-required', 'authentication-downgrade', 'legacy-upgrade', 'identity-scoped-store',
    'synchronous-repeat-loop', 'rearm-during-connection-lookup', 'delete-error', 'terminate-error',
    'store-full-known-peer-and-own', 'store-full-no-window', 'store-full-expired',
    'store-full-random-identity', 'store-full-invalid-identity-type', 'store-full-rearm-during-lookup',
    'store-full-unknown-peer', 'store-full-lookup-error', 'store-full-wrong-event-handle',
    'store-full-no-active-handle', 'store-full-missing-connection', 'store-full-descriptor-mismatch',
    'store-full-encrypted', 'store-full-identity-mismatch', 'store-full-address-type-mismatch',
    'store-full-invalid-stored-key', 'store-full-wrong-object-type', 'store-overflow-never-retries',
    'store-full-after-attempt', 'store-full-blocked', 'store-full-keeps-smp-security-policy',
])
def test_esp32_ble_repeat_pairing(repeat_pairing_binary, case):
    result = subprocess.run([str(repeat_pairing_binary), case], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, f'{case}: {result.stdout}{result.stderr}'
