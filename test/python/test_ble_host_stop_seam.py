"""Exercise the actual ESP32 stop body with a failure-injectable NimBLE seam."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PRE = r'''
#include <cassert>
struct Peripheral { ~Peripheral(){++destroyed;} static int destroyed; }; int Peripheral::destroyed=0;
struct BleControlHandle { void* impl=nullptr; };
void storeStatus() {}
struct Config { void (*store_status_cb)()=nullptr; void* store_status_arg=nullptr; } ble_hs_cfg;
Peripheral* active=nullptr;
int status=0, logged=0, deinits=0, stops=0;
int nimble_port_stop(){++stops;return status;}
void nimble_port_deinit(){++deinits;}
#define ESP_LOGE(tag,fmt,value) (logged=(value))
const char* kBleTag="test";
'''
POST = r'''
int main(){
 BleControlHandle handle; auto* p=new Peripheral(); handle.impl=p; active=p;
 ble_hs_cfg.store_status_cb=storeStatus; ble_hs_cfg.store_status_arg=p;
 status=17; assert(!bleControlStop(handle));
 assert(handle.impl==p && active==p && ble_hs_cfg.store_status_cb==storeStatus && ble_hs_cfg.store_status_arg==p);
 assert(logged==17 && deinits==0 && Peripheral::destroyed==0 && stops==1);
 status=0; assert(bleControlStop(handle));
 assert(!handle.impl && !active && !ble_hs_cfg.store_status_cb && !ble_hs_cfg.store_status_arg);
 assert(deinits==1 && Peripheral::destroyed==1 && stops==2);
 assert(bleControlStop(handle) && stops==2 && deinits==1);
}
'''


class BleHostStopSeam(unittest.TestCase):
    def test_failed_stop_retains_callback_ownership_then_success_releases_once(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        source = (ROOT/'src/platform/esp32/platform_esp32_ble.cpp').read_text()
        start = source.index('bool bleControlStop(BleControlHandle& handle) {')
        brace = source.index('{', start)
        depth = 0
        for end in range(brace, len(source)):
            depth += (source[end] == '{') - (source[end] == '}')
            if not depth:
                break
        else:
            self.fail('unterminated bleControlStop')
        with tempfile.TemporaryDirectory(prefix='ble-stop-seam-') as directory:
            tmp = Path(directory)
            (tmp/'check.cpp').write_text(PRE + source[start:end+1] + POST)
            subprocess.run([compiler, '-std=c++20', str(tmp/'check.cpp'), '-o', str(tmp/'check')], check=True)
            subprocess.run([str(tmp/'check')], check=True)

    def test_destroyed_owner_transfers_context_and_next_start_retries_stop(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        source = (ROOT/'src/platform/esp32/platform_esp32_ble.cpp').read_text()

        def body(signature):
            start = source.index(signature)
            brace = source.index('{', start)
            depth = 0
            for end in range(brace, len(source)):
                depth += (source[end] == '{') - (source[end] == '}')
                if not depth:
                    return source[start:end+1]
            self.fail('unterminated function: ' + signature)

        pre = r'''
#include <cassert>
struct Peripheral { bool abandoned=false; ~Peripheral(){++destroyed;} static int destroyed; }; int Peripheral::destroyed=0;
struct BleControlHandle { void* impl=nullptr; };
void storeStatus() {}
struct Config { void (*store_status_cb)()=nullptr; void* store_status_arg=nullptr; } ble_hs_cfg;
Peripheral* active=nullptr;
int status=0, deinits=0, stops=0;
int nimble_port_stop(){++stops;return status;}
void nimble_port_deinit(){++deinits;}
#define ESP_LOGE(...) ((void)0)
const char* kBleTag="test";
'''
        post = r'''
int main(){
 BleControlHandle h; auto* p=new Peripheral(); h.impl=p; active=p;
 ble_hs_cfg.store_status_cb=storeStatus; ble_hs_cfg.store_status_arg=p;
 assert(reapAbandonedHost() && stops==0); // an owned host is not stopped
 status=17; assert(!bleControlStop(h)); bleControlAbandon(h);
 assert(!h.impl && active==p && p->abandoned && Peripheral::destroyed==0);
 assert(!reapAbandonedHost()); assert(active==p && deinits==0 && Peripheral::destroyed==0);
 status=0; assert(reapAbandonedHost());
 assert(!active && deinits==1 && Peripheral::destroyed==1 && stops==3);
 assert(reapAbandonedHost() && stops==3);
}
'''
        extracted = body('bool bleControlStop(BleControlHandle& handle) {') + '\n' + \
            body('void bleControlAbandon(BleControlHandle& handle) {') + '\n' + \
            body('static bool reapAbandonedHost() {')
        with tempfile.TemporaryDirectory(prefix='ble-orphan-seam-') as directory:
            tmp = Path(directory)
            (tmp/'check.cpp').write_text(pre + extracted + post)
            subprocess.run([compiler, '-std=c++20', str(tmp/'check.cpp'), '-o', str(tmp/'check')], check=True)
            subprocess.run([str(tmp/'check')], check=True)


if __name__ == '__main__':
    unittest.main()
