import datetime
import importlib.util
import json
import sys
import types
from pathlib import Path
import unittest
from unittest import mock

path = Path(__file__).resolve().parents[2] / 'moondeck/helio/ble_shared_read_probe.py'
spec = importlib.util.spec_from_file_location('ble_shared_read_probe', path)
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class LeaseTests(unittest.TestCase):
    def setUp(self):
        self.now = datetime.datetime(2026, 10, 29, tzinfo=datetime.timezone.utc)
        self.lease = {'active_writer': probe.OWNER, 'shared_ble_readonly': {
            'active': True, 'leaseId': 'test', 'appReady': True, 'linuxReady': True,
            'scope': 'two-central-hello-get-only', 'systemMac': '34:CD:B0:37:4E:A4',
            'bleMac': '34:CD:B0:37:4E:A6', 'expiresUtc': '2026-10-29T00:02:00Z'}}

    def test_only_explicit_shared_bounded_scope_passes(self):
        self.assertIs(probe.check_lease(self.lease, 'test', self.now), self.lease['shared_ble_readonly'])
        for key, value in [('appReady', False), ('linuxReady', False), ('active', False),
                           ('scope', 'anything'), ('systemMac', 'other'), ('bleMac', 'other'),
                           ('expiresUtc', '2026-10-29T00:10:01Z')]:
            with self.subTest(key=key):
                previous = self.lease['shared_ble_readonly'][key]
                self.lease['shared_ble_readonly'][key] = value
                with self.assertRaises((RuntimeError, ValueError, KeyError)):
                    probe.check_lease(self.lease, 'test', self.now)
                self.lease['shared_ble_readonly'][key] = previous
        with self.assertRaises(RuntimeError): probe.check_lease(self.lease, 'wrong', self.now)
        with self.assertRaises(RuntimeError): probe.check_lease(self.lease, 'test', self.now + datetime.timedelta(minutes=2))
        self.lease['active_writer'] = 'app'
        with self.assertRaises(RuntimeError): probe.check_lease(self.lease, 'test', self.now)


class RpcProbeTests(unittest.IsolatedAsyncioTestCase):
    async def test_probe_sends_only_fragmented_hello_and_get_without_pairing(self):
        calls = []
        lease = {'systemMac':'34:CD:B0:37:4E:A4','bleMac':'34:CD:B0:37:4E:A6'}
        class Response:
            def __enter__(self): return self
            def __exit__(self, *_): return None
            def read(self): return json.dumps({'controls':[{'name':'mac','value':lease['systemMac']}]}).encode()
        class Client:
            def __init__(self, address, **kwargs):
                calls.append(('connect',address,kwargs)); self.partial = b''; self.notify = None
            async def __aenter__(self): return self
            async def __aexit__(self, *_): calls.append(('disconnect',))
            async def write_gatt_char(self, characteristic, fragment, response):
                self.partial += fragment
                calls.append(('fragment', characteristic, len(fragment), response))
                if self.partial.endswith(b'\n'):
                    request = json.loads(self.partial); self.partial = b''
                    calls.append(('request',request['$op']))
                    if request['$op'] == 'hello':
                        reply = {'id':request['$id'],'op':'hello','ok':True,'nonce':request['$nonce'],
                                 'e':'a'*32,'s':17}
                    else:
                        reply = {'id':request['$id'],'op':'get','ok':True,'e':'a'*32,'s':17}
                    line = json.dumps(reply).encode() + b'\n'
                    for offset in range(0, len(line), 20):   # notifications at the minimum ATT MTU
                        self.notify(probe.STATE, line[offset:offset + 20])
            async def start_notify(self, characteristic, callback):
                calls.append(('notify', characteristic)); self.notify = callback
            async def read_gatt_char(self, characteristic):
                assert characteristic == probe.IDENTITY, 'replies arrive in notifications, never by reading State'
                return json.dumps({'id':lease['systemMac'],'rpc':3,'security':'open'}).encode()
        with mock.patch.object(probe, 'require_lease', return_value=lease), \
             mock.patch.object(probe.urllib.request, 'urlopen', return_value=Response()), \
             mock.patch.dict(sys.modules, {'bleak':types.SimpleNamespace(BleakClient=Client)}):
            result = await probe.run('approved', 0.01)
        self.assertEqual(result['rpcGets'], 1)
        self.assertIn(('notify', probe.STATE), calls)
        self.assertEqual([c[1] for c in calls if c[0] == 'request'], ['hello','get'])
        self.assertFalse(calls[0][2]['pair'])
        self.assertTrue(all(c[2] <= 20 and c[3] for c in calls if c[0] == 'fragment'))
        self.assertEqual(calls[-1], ('disconnect',))


if __name__ == '__main__':
    unittest.main()
