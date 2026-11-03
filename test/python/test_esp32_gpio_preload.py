"""Compile the actual ESP32 output-preload function against a recording GPIO seam."""
from pathlib import Path
import shutil
import subprocess
import pytest

ROOT = Path(__file__).resolve().parents[2]
CXX = shutil.which('c++') or shutil.which('g++') or shutil.which('clang++')
pytestmark = pytest.mark.skipif(CXX is None, reason='no C++ compiler')


def test_both_levels_are_preloaded_before_output_is_enabled(tmp_path):
    source = (ROOT / 'src/platform/esp32/platform_esp32_gpio.cpp').read_text()
    start = source.index('bool gpioPreloadOutput(')
    fn = source[start:source.index('\nbool gpioPreloadLowOutput(', start)]
    seam = r'''
#include "platform/GpioPreload.h"
#include <cassert>
#include <cstdint>
#include <string>
using mm::platform::preloadThenEnable;
using gpio_num_t = int;
constexpr int ESP_OK=0, GPIO_MODE_INPUT_OUTPUT=1, GPIO_INTR_DISABLE=0;
struct gpio_config_t { uint64_t pin_bit_mask; int mode; int intr_type; };
struct Capability { bool reserved=false; };
bool reserved=false, valid=true, failSet=false, failConfig=false;
std::string calls;
bool GPIO_IS_VALID_OUTPUT_GPIO(uint8_t) { return valid; }
Capability gpioCapability(uint8_t) { return {reserved}; }
int gpio_set_level(gpio_num_t, int level) { calls += level ? 'H' : 'L'; return failSet ? 1 : ESP_OK; }
int gpio_config(const gpio_config_t* cfg) { assert(cfg->pin_bit_mask == (1ULL << 2)); calls += 'E'; return failConfig ? 1 : ESP_OK; }
'''
    main = r'''
int main() {
    assert(gpioPreloadOutput(2, true) && calls == "HE");
    calls.clear(); assert(gpioPreloadOutput(2, false) && calls == "LE");
    calls.clear(); failSet=true; assert(!gpioPreloadOutput(2, true) && calls == "H");
    calls.clear(); failSet=false; failConfig=true; assert(!gpioPreloadOutput(2, false) && calls == "LE");
    calls.clear(); failConfig=false; reserved=true; assert(!gpioPreloadOutput(2, true) && calls.empty());
    reserved=false; valid=false; assert(!gpioPreloadOutput(2, false) && calls.empty());
}
'''
    cpp, binary = tmp_path / 'gpio.cpp', tmp_path / 'gpio'
    cpp.write_text(seam + fn + main)
    result = subprocess.run([CXX, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'), str(cpp), '-o', str(binary)], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    subprocess.run([str(binary)], check=True, timeout=5)
