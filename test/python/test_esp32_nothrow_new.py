"""Exercise the ESP32 nothrow operator new against the abort ESP-IDF builds in with C++ exceptions off.

ESP-IDF links libstdc++ statically and wraps __cxa_allocate_exception to abort() when
CONFIG_COMPILER_CXX_EXCEPTIONS is off, so libstdc++'s nothrow new, which calls the throwing one,
crashes instead of returning null. This links the same way on the host: without the platform's
replacement a failed nothrow allocation aborts, with it the allocation is null.
"""
from pathlib import Path
import shutil
import signal
import subprocess
import pytest

ROOT = Path(__file__).resolve().parents[2]
CXX = shutil.which('c++') or shutil.which('g++')
pytestmark = pytest.mark.skipif(CXX is None, reason='no C++ compiler')

MAIN = r'''
#include <cstdlib>
#include <new>
// ESP-IDF's stand-in with C++ exceptions off: a throw becomes an abort.
extern "C" void* __wrap___cxa_allocate_exception(std::size_t) { std::abort(); }
struct Huge { char bytes[1ull << 50]; };   // a petabyte: more than the address space holds
int main() {
    volatile std::size_t count = static_cast<std::size_t>(-1) / 4;
    char* bytes = new (std::nothrow) char[count];
    Huge* huge = new (std::nothrow) Huge;
    return bytes || huge ? 1 : 0;
}
'''


def build(tmp_path, name, source):
    cpp, binary = tmp_path / f'{name}.cpp', tmp_path / name
    cpp.write_text(source)
    result = subprocess.run([CXX, '-std=c++20', '-fno-exceptions', '-static-libstdc++', '-Wl,--wrap=__cxa_allocate_exception',
                             str(cpp), '-o', str(binary)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return subprocess.run([str(binary)], capture_output=True, timeout=10).returncode


def test_a_failed_nothrow_allocation_returns_null_rather_than_aborting(tmp_path):
    source = (ROOT / 'src/platform/esp32/platform_esp32.cpp').read_text()
    start = source.index('void* operator new(std::size_t size, const std::nothrow_t&) noexcept')
    replacement = source[start:source.index('\nnamespace mm::platform {', start)]
    assert 'operator new[](std::size_t size, const std::nothrow_t&) noexcept' in replacement
    # The harness reproduces the board: libstdc++'s own nothrow new aborts.
    assert build(tmp_path, 'stock', MAIN) == -signal.SIGABRT
    assert build(tmp_path, 'replaced', '#include <cstdlib>\n#include <new>\n' + replacement + MAIN) == 0
