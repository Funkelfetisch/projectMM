#!/usr/bin/env python3
# /// script
# dependencies = ["bleak"]
# ///
"""One bounded unpaired Linux RPC3 read probe during an explicit shared lease.

Only hello/get requests are sent. No light, configuration, pairing, reset,
retry of an unknown write, or firmware operation is issued.
"""
import argparse
import asyncio
import datetime
import json
import re
import secrets
import time
import urllib.request
from pathlib import Path

OWNER = "01a0e402-48ae-7c30-827b-4326cf8bc4df"
COORD = Path("/home/veit/Documents/ChatGPT/HELIO/helio-device-coordination.json")
IDENTITY = "bee30703-a2eb-4f7a-889b-13192c8c1819"
STATE = "bee30701-a2eb-4f7a-889b-13192c8c1819"
COMMAND = "bee30702-a2eb-4f7a-889b-13192c8c1819"


def check_lease(document, lease_id, now):
    lease = document.get("shared_ble_readonly")
    if not isinstance(lease, dict) or document.get("active_writer") != OWNER or not lease.get("active"):
        raise RuntimeError("No active shared read-only lease")
    if lease.get("leaseId") != lease_id or lease.get("appReady") is not True or lease.get("linuxReady") is not True:
        raise RuntimeError("Both owners have not confirmed this lease")
    if lease.get("scope") != "two-central-hello-get-only" or lease.get("systemMac") != "34:CD:B0:37:4E:A4" or lease.get("bleMac") != "34:CD:B0:37:4E:A6":
        raise RuntimeError("Unexpected target or scope")
    end = datetime.datetime.fromisoformat(lease["expiresUtc"].replace("Z", "+00:00"))
    if end.tzinfo is None or now >= end or end - now > datetime.timedelta(minutes=10):
        raise RuntimeError("Lease is expired or exceeds the bounded window")
    return lease


def require_lease(lease_id):
    return check_lease(json.loads(COORD.read_text()), lease_id, datetime.datetime.now(datetime.timezone.utc))


async def run(lease_id, duration):
    from bleak import BleakClient
    lease = require_lease(lease_id)
    with urllib.request.urlopen("http://192.168.8.202/api/modules/System", timeout=4) as response:
        controls = json.load(response).get("controls", [])
    mac = next((x.get("value") for x in controls if x.get("name") == "mac"), None)
    if mac != lease["systemMac"]:
        raise RuntimeError("HTTP device identity does not match the lease")
    async with BleakClient(lease["bleMac"], pair=False, timeout=20) as client:
        connected_at = datetime.datetime.now(datetime.timezone.utc).isoformat()
        require_lease(lease_id)
        ident = json.loads(bytes(await client.read_gatt_char(IDENTITY)))
        if ident.get("id") != mac or ident.get("rpc") != 3 or ident.get("security") != "open":
            raise RuntimeError("BLE identity or open RPC3 capability differs")
        # Replies arrive as notifications, one LF-terminated line split across payloads.
        lines, partial = asyncio.Queue(), bytearray()

        def notified(_, data):
            partial.extend(data)
            while b"\n" in partial:
                end = partial.index(b"\n")
                lines.put_nowait(bytes(partial[:end]))
                del partial[:end + 1]
        await client.start_notify(STATE, notified)

        async def rpc(request):
            require_lease(lease_id)
            wire = json.dumps(request, separators=(",", ":")).encode() + b"\n"
            if len(wire) > 513:
                raise RuntimeError("RPC request exceeds limit")
            for offset in range(0, len(wire), 20):
                require_lease(lease_id)
                await client.write_gatt_char(COMMAND, wire[offset:offset + 20], response=True)
            deadline = time.monotonic() + 5
            while (left := deadline - time.monotonic()) > 0:
                require_lease(lease_id)
                try:
                    raw = await asyncio.wait_for(lines.get(), left)
                except asyncio.TimeoutError:
                    break
                if len(raw) > 512:
                    raise RuntimeError("RPC reply exceeds limit")
                reply = json.loads(raw)
                if reply.get("id") == request["$id"] and reply.get("op") == request["$op"]:
                    return reply
            raise RuntimeError("RPC reply was not correlated; request is not replayed")

        nonce = secrets.token_hex(16)
        hello = await rpc({"$v": 1, "$id": 1, "$op": "hello", "$nonce": nonce})
        epoch, session = hello.get("e"), hello.get("s")
        if not hello.get("ok") or hello.get("nonce") != nonce or not re.fullmatch("[0-9a-f]{32}", str(epoch)) or type(session) is not int or session <= 0:
            raise RuntimeError("Invalid hello envelope")
        end = time.monotonic() + duration
        count = 0
        while time.monotonic() < end:
            count += 1
            reply = await rpc({"$v": 1, "$e": epoch, "$s": session, "$id": count + 1, "$op": "get"})
            if not reply.get("ok") or reply.get("e") != epoch or reply.get("s") != session:
                raise RuntimeError("Get response belongs to a different session")
            await asyncio.sleep(1)
        return {"rpcGets": count, "identityMatched": True, "open": True,
                "pairingRequestedByProbe": False, "lightingOrConfigurationMutations": False,
                "epoch": epoch, "session": session, "connectedAtUtc": connected_at,
                "finishedAtUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                "note": "This records the Linux link only; simultaneous App evidence is separate."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lease-id", required=True)
    parser.add_argument("--seconds", type=int, default=15, choices=range(1, 61))
    args = parser.parse_args()
    require_lease(args.lease_id)  # before importing BLE or accessing the network
    print(json.dumps(asyncio.run(run(args.lease_id, args.seconds))))


if __name__ == "__main__":
    main()
