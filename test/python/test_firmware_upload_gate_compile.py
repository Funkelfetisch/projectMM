"""The real platform OTA port must not be wrapped in a second update gate."""
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


def test_platform_gated_port_rejects_an_additional_gate():
    compiler = shutil.which('c++')
    if not compiler:
        pytest.skip('C++ compiler unavailable')
    common = '''#include "core/util/FirmwareOtaPort.h"
#include "core/util/FirmwareUploadBackend.h"
#include "core/util/FirmwareUploadSession.h"
#include "core/util/FirmwareUpdateGate.h"
'''
    with tempfile.TemporaryDirectory(prefix='ota-gate-check-') as directory:
        source = Path(directory) / 'check.cpp'
        def compile_body(body):
            source.write_text(common + body)
            return subprocess.run([compiler, '-std=c++20', '-fsyntax-only', '-I', str(ROOT / 'src'),
                                   '-I', str(ROOT / 'src/platform/desktop'),
                                   str(source)], capture_output=True, text=True)
        direct = compile_body('mm::FirmwareOtaPort<> port; mm::FirmwareUploadSession session(port);\n')
        assert direct.returncode == 0, direct.stderr
        doubled = compile_body('mm::FirmwareOtaPort<> port; mm::FirmwareUpdateGate gate; mm::FirmwareUploadBackend bad(port,gate);\n')
        assert doubled.returncode != 0
        assert 'A platform-gated port must be used directly' in doubled.stderr
