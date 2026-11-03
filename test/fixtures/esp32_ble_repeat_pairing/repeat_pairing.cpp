// The real repair helper calls a recording NimBLE SDK seam; no radio or store is used.
#include "platform/esp32/BleRepeatPairing.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {
using mm::platform::BleRepeatPairing;

ble_addr_t address(uint8_t type, uint8_t last) {
    return {type, {1, 2, 3, 4, 5, last}};
}

bool sameAddress(const ble_addr_t& left, const ble_addr_t& right) {
    return left.type == right.type && std::memcmp(left.val, right.val, sizeof(left.val)) == 0;
}

struct State {
    ble_gap_conn_desc connection{};
    std::vector<ble_addr_t> bonds;
    std::vector<ble_addr_t> deleted;
    std::vector<uint16_t> finds;
    std::vector<ble_store_key_sec> peerReads;
    ble_store_value_sec storedPeer{};
    std::vector<std::pair<uint16_t, uint8_t>> terminated;
    std::function<void()> duringFind;
    std::function<void()> duringPeerRead;
    std::function<void()> duringDelete;
    int findResult = 0;
    int peerReadResult = 0;
    int deleteResult = 0;
    int terminateResult = 0;
} state;

struct Fixture {
    BleRepeatPairing repair;
    ble_gap_repeat_pairing request{};
    Fixture() {
        state = {};
        state.connection.conn_handle = 7;
        state.connection.peer_id_addr = address(0, 9);
        state.connection.peer_ota_addr = address(1, 90);
        state.storedPeer.peer_addr = state.connection.peer_id_addr;
        state.storedPeer.key_size = 16;
        state.bonds = {address(0, 1), address(1, 2), state.connection.peer_id_addr};
        request.conn_handle = 7;
        request.cur_key_size = 16;
        request.cur_sc = 1;
        request.new_key_size = 16;
        request.new_sc = 1;
        request.new_bonding = 1;
    }
    int handle(uint64_t now = 101) { return repair.handle(request, 7, now); }
    void arm(uint64_t now = 100) { repair.arm(now); }
    int full(int objectType, uint64_t now = 101) {
        ble_store_status_event event{};
        event.event_code = BLE_STORE_EVENT_FULL;
        event.full.obj_type = objectType;
        event.full.conn_handle = 7;
        return repair.storeStatus(event, 7, now);
    }
    void untouched() const {
        assert(state.deleted.empty());
        assert(state.terminated.empty());
        assert(state.bonds.size() == 3);
    }
};

void check(const std::string& name) {
    Fixture fixture;
    auto& repair = fixture.repair;
    auto& request = fixture.request;
    if (name == "default-denial") {
        assert(repair.remaining(0) == 0);
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
        assert(state.finds.empty());
        fixture.untouched();
    } else if (name == "window-boundary") {
        fixture.arm();
        assert(repair.remaining(100) == 180000);
        assert(repair.remaining(180099) == 1);
        assert(repair.remaining(180100) == 0);
        assert(fixture.handle(180100) == BLE_GAP_REPEAT_PAIRING_IGNORE);
        fixture.untouched();
        fixture.arm();
        assert(fixture.handle(180099) == BLE_GAP_REPEAT_PAIRING_RETRY);
    } else if (name == "clock-wrap") {
        const uint64_t start = static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) - 100;
        fixture.arm(start);
        assert(repair.remaining(start) == 180000);
        assert(repair.remaining(start + 179999u) == 1);
        assert(fixture.handle(start + 179999u) == BLE_GAP_REPEAT_PAIRING_RETRY);
        assert(repair.remaining(start + 179999u) == 0);
        fixture.arm(start);
        repair.reset();
        assert(fixture.handle(start + 180000u) == BLE_GAP_REPEAT_PAIRING_IGNORE);
    } else if (name == "zero-deadline") {
        const uint64_t deadline = uint64_t{1} << 32;
        const uint64_t start = deadline - 180000u;
        fixture.arm(start);
        assert(repair.remaining(start) == 180000);
        assert(repair.remaining(deadline - 1) == 1);
        assert(fixture.handle(deadline) == BLE_GAP_REPEAT_PAIRING_IGNORE);
        fixture.untouched();
    } else if (name == "expired-never-revives") {
        fixture.arm();
        constexpr uint64_t expired = 180100;
        for (const uint64_t now : {expired, expired + (uint64_t{1} << 31), expired + (uint64_t{1} << 32)}) {
            assert(repair.remaining(now) == 0);
            assert(fixture.handle(now) == BLE_GAP_REPEAT_PAIRING_IGNORE);
            repair.reset();
            assert(repair.remaining(now) == 0);
            assert(fixture.handle(now) == BLE_GAP_REPEAT_PAIRING_IGNORE);
        }
        fixture.untouched();
        assert(state.finds.empty());
    } else if (name == "arm-overflow-stays-closed") {
        const uint64_t start = std::numeric_limits<uint64_t>::max() - 179999u;
        fixture.arm(start);
        for (const uint64_t now : {uint64_t{0}, start, std::numeric_limits<uint64_t>::max()}) {
            assert(repair.remaining(now) == 0);
            assert(fixture.handle(now) == BLE_GAP_REPEAT_PAIRING_IGNORE);
        }
        repair.reset();
        assert(repair.remaining(start) == 0);
        fixture.untouched();
    } else if (name == "last-valid-64bit-deadline") {
        const uint64_t deadline = std::numeric_limits<uint64_t>::max();
        fixture.arm(deadline - 180000u);
        assert(repair.remaining(deadline - 180000u) == 180000);
        assert(repair.remaining(deadline - 1) == 1);
        assert(fixture.handle(deadline) == BLE_GAP_REPEAT_PAIRING_IGNORE);
        fixture.untouched();
    } else if (name == "backwards-clock-denied") {
        fixture.arm(200000);
        assert(repair.remaining(199999) == 0);
        assert(fixture.handle(199999) == BLE_GAP_REPEAT_PAIRING_IGNORE);
        fixture.untouched();
        assert(repair.remaining(200000) == 180000);
    } else if (name == "reset-preserves-unspent-window") {
        fixture.arm();
        repair.reset();
        assert(repair.remaining(101) == 179999);
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_RETRY);
    } else if (name == "arm-requires-stable-primary") {
        assert(BleRepeatPairing::mayArm(BLE_HS_CONN_HANDLE_NONE, BLE_HS_CONN_HANDLE_NONE, 0));
        assert(BleRepeatPairing::mayArm(7, 7, 1));
        assert(!BleRepeatPairing::mayArm(7, 8, 1));
        assert(!BleRepeatPairing::mayArm(BLE_HS_CONN_HANDLE_NONE, 7, 1));
        assert(!BleRepeatPairing::mayArm(7, 7, 2));
        fixture.untouched();
    } else if (name == "cancel-prevents-transfer") {
        fixture.arm();
        repair.cancel();
        assert(repair.remaining(101) == 0);
        assert(fixture.handle(101) == BLE_GAP_REPEAT_PAIRING_IGNORE);
        fixture.untouched();
    } else if (name == "one-use-reset-and-rearm") {
        fixture.arm();
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_RETRY);
        repair.reset();
        assert(repair.remaining(101) == 0);
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
        assert(state.deleted.size() == 1);
        fixture.arm(200);
        assert(fixture.handle(201) == BLE_GAP_REPEAT_PAIRING_RETRY);
        assert(state.deleted.size() == 2);
    } else if (name == "rearm-keeps-connection-budget") {
        fixture.arm();
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_RETRY);
        fixture.arm(200);
        assert(fixture.handle(201) == BLE_GAP_REPEAT_PAIRING_IGNORE);
        assert(state.deleted.size() == 1);
        repair.reset();
        assert(fixture.handle(201) == BLE_GAP_REPEAT_PAIRING_RETRY);
        assert(state.deleted.size() == 2);
    } else if (name == "wrong-event-handle") {
        fixture.arm();
        request.conn_handle = 8;
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
        assert(state.finds.empty());
        fixture.untouched();
        assert(repair.remaining(101) > 0);
    } else if (name == "no-active-connection") {
        fixture.arm();
        assert(repair.handle(request, BLE_HS_CONN_HANDLE_NONE, 101) == BLE_GAP_REPEAT_PAIRING_IGNORE);
        assert(state.finds.empty());
        fixture.untouched();
    } else if (name == "unknown-connection") {
        fixture.arm();
        state.findResult = 6;
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
        fixture.untouched();
        state.findResult = 0;
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_RETRY);
    } else if (name == "descriptor-handle-mismatch") {
        fixture.arm();
        state.connection.conn_handle = 8;
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
        fixture.untouched();
    } else if (name == "encrypted-connection") {
        fixture.arm();
        state.connection.sec_state.encrypted = 1;
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
        fixture.untouched();
    } else if (name == "invalid-current-key") {
        fixture.arm();
        for (const uint8_t key : {0, 6, 17, 255}) {
            request.cur_key_size = key;
            assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
        }
        fixture.untouched();
        assert(state.finds.empty());
    } else if (name == "invalid-new-key") {
        fixture.arm();
        for (const uint8_t key : {0, 7, 15, 17, 255}) {
            request.new_key_size = key;
            assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
        }
        fixture.untouched();
    } else if (name == "sc-required") {
        fixture.arm();
        request.new_sc = 0;
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
        request.cur_sc = 0;
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
        fixture.untouched();
    } else if (name == "bonding-required") {
        fixture.arm();
        request.new_bonding = 0;
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
        fixture.untouched();
    } else if (name == "authentication-downgrade") {
        fixture.arm();
        request.cur_authenticated = 1;
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
        fixture.untouched();
        request.new_authenticated = 1;
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_RETRY);
    } else if (name == "legacy-upgrade") {
        fixture.arm();
        request.cur_key_size = 7;
        request.cur_sc = 0;
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_RETRY);
    } else if (name == "identity-scoped-store") {
        fixture.arm();
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_RETRY);
        assert(state.finds == std::vector<uint16_t>{7});
        assert(state.deleted.size() == 1 && sameAddress(state.deleted[0], state.connection.peer_id_addr));
        assert(!sameAddress(state.deleted[0], state.connection.peer_ota_addr));
        assert(state.bonds.size() == 2);
        assert(sameAddress(state.bonds[0], address(0, 1)));
        assert(sameAddress(state.bonds[1], address(1, 2)));
        assert(state.terminated.empty() && !repair.blocked());
    } else if (name == "synchronous-repeat-loop") {
        fixture.arm();
        state.duringDelete = [&] {
            assert(repair.remaining(101) == 0);
            assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
        };
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_RETRY);
        for (int i = 0; i < 100; ++i) assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
        assert(state.deleted.size() == 1);
    } else if (name == "rearm-during-connection-lookup") {
        fixture.arm();
        state.duringFind = [&] { fixture.arm(200); };
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
        fixture.untouched();
        assert(repair.remaining(200) == 180000);
        state.duringFind = {};
        assert(fixture.handle(201) == BLE_GAP_REPEAT_PAIRING_RETRY);
        assert(state.deleted.size() == 1);
        assert(repair.remaining(201) == 0);
    } else if (name == "store-full-known-peer-and-own") {
        fixture.arm();
        assert(fixture.full(BLE_STORE_OBJ_TYPE_PEER_SEC) == 0);
        assert(fixture.full(BLE_STORE_OBJ_TYPE_OUR_SEC) == 0);
        fixture.untouched();
        assert(repair.remaining(101) == 179999);
        assert(state.peerReads.size() == 2);
        for (const auto& key : state.peerReads) {
            assert(key.idx == 0 && sameAddress(key.peer_addr, state.connection.peer_id_addr));
            assert(!sameAddress(key.peer_addr, state.connection.peer_ota_addr));
        }
        ble_store_status_event event{};
        event.event_code = BLE_STORE_EVENT_FULL;
        event.full.obj_type = BLE_STORE_OBJ_TYPE_PEER_SEC;
        event.full.conn_handle = 7;
        auto status = BleRepeatPairing::PeerStatus::Unchecked;
        assert(repair.storeStatus(event, 7, 101, &status) == 0);
        assert(status == BleRepeatPairing::PeerStatus::Known);
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_RETRY);
        assert(state.deleted.size() == 1);
    } else if (name == "store-full-random-identity") {
        fixture.arm();
        state.connection.peer_id_addr.type = BLE_ADDR_RANDOM;
        state.storedPeer.peer_addr = state.connection.peer_id_addr;
        assert(fixture.full(BLE_STORE_OBJ_TYPE_PEER_SEC) == 0);
        assert(sameAddress(state.peerReads[0].peer_addr, state.connection.peer_id_addr));
        fixture.untouched();
    } else if (name == "store-full-invalid-identity-type") {
        fixture.arm();
        state.connection.peer_id_addr.type = 255;
        assert(fixture.full(BLE_STORE_OBJ_TYPE_PEER_SEC) == BLE_HS_ESTORE_CAP);
        assert(state.peerReads.empty());
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
        fixture.untouched();
    } else if (name == "store-full-rearm-during-lookup") {
        fixture.arm();
        state.duringPeerRead = [&] { fixture.arm(200); };
        assert(fixture.full(BLE_STORE_OBJ_TYPE_PEER_SEC) == BLE_HS_ESTORE_CAP);
        fixture.untouched();
        assert(repair.remaining(200) == 180000);
        state.duringPeerRead = {};
        assert(fixture.full(BLE_STORE_OBJ_TYPE_PEER_SEC, 201) == 0);
        assert(repair.remaining(201) == 179999);
        assert(fixture.handle(201) == BLE_GAP_REPEAT_PAIRING_RETRY);
        assert(state.deleted.size() == 1);
    } else if (name == "store-full-no-window") {
        assert(fixture.full(BLE_STORE_OBJ_TYPE_PEER_SEC) == BLE_HS_ESTORE_CAP);
        fixture.untouched();
    } else if (name == "store-full-expired") {
        fixture.arm();
        assert(fixture.full(BLE_STORE_OBJ_TYPE_OUR_SEC, 180100) == BLE_HS_ESTORE_CAP);
        fixture.untouched();
    } else if (name == "store-full-unknown-peer" || name == "store-full-lookup-error") {
        fixture.arm();
        state.peerReadResult = name == "store-full-unknown-peer" ? BLE_HS_ENOENT : 17;
        ble_store_status_event event{};
        event.event_code = BLE_STORE_EVENT_FULL;
        event.full.obj_type = BLE_STORE_OBJ_TYPE_PEER_SEC;
        event.full.conn_handle = 7;
        auto status = BleRepeatPairing::PeerStatus::Known;
        assert(repair.storeStatus(event, 7, 101, &status) == BLE_HS_ESTORE_CAP);
        assert(status == (state.peerReadResult == BLE_HS_ENOENT
            ? BleRepeatPairing::PeerStatus::Unknown : BleRepeatPairing::PeerStatus::Unchecked));
        fixture.untouched();
        assert(repair.remaining(101) == 179999);
    } else if (name == "store-full-wrong-event-handle" || name == "store-full-no-active-handle") {
        fixture.arm();
        ble_store_status_event event{};
        event.event_code = BLE_STORE_EVENT_FULL;
        event.full.obj_type = BLE_STORE_OBJ_TYPE_PEER_SEC;
        event.full.conn_handle = name == "store-full-wrong-event-handle" ? 8 : 7;
        const uint16_t active = name == "store-full-no-active-handle" ? BLE_HS_CONN_HANDLE_NONE : 7;
        assert(repair.storeStatus(event, active, 101) == BLE_HS_ESTORE_CAP);
        fixture.untouched();
        assert(state.finds.empty());
    } else if (name == "store-full-missing-connection" || name == "store-full-descriptor-mismatch" ||
               name == "store-full-encrypted") {
        fixture.arm();
        if (name == "store-full-missing-connection") state.findResult = BLE_HS_ENOENT;
        if (name == "store-full-descriptor-mismatch") state.connection.conn_handle = 8;
        if (name == "store-full-encrypted") state.connection.sec_state.encrypted = 1;
        assert(fixture.full(BLE_STORE_OBJ_TYPE_PEER_SEC) == BLE_HS_ESTORE_CAP);
        assert(state.peerReads.empty());
        fixture.untouched();
    } else if (name == "store-full-identity-mismatch" || name == "store-full-address-type-mismatch") {
        fixture.arm();
        if (name == "store-full-identity-mismatch") state.storedPeer.peer_addr.val[5] ^= 1;
        else state.storedPeer.peer_addr.type ^= 1;
        assert(fixture.full(BLE_STORE_OBJ_TYPE_PEER_SEC) == BLE_HS_ESTORE_CAP);
        fixture.untouched();
        assert(repair.remaining(101) == 179999);
    } else if (name == "store-full-invalid-stored-key") {
        fixture.arm();
        for (const uint8_t size : {0, 6, 17, 255}) {
            state.storedPeer.key_size = size;
            assert(fixture.full(BLE_STORE_OBJ_TYPE_PEER_SEC) == BLE_HS_ESTORE_CAP);
        }
        fixture.untouched();
        state.storedPeer.key_size = 7;
        assert(fixture.full(BLE_STORE_OBJ_TYPE_PEER_SEC) == 0);
        assert(repair.remaining(101) == 179999);
    } else if (name == "store-full-wrong-object-type") {
        fixture.arm();
        for (const int type : {0, BLE_STORE_OBJ_TYPE_CCCD, 127})
            assert(fixture.full(type) == BLE_HS_ESTORE_CAP);
        assert(state.peerReads.empty());
        fixture.untouched();
    } else if (name == "store-overflow-never-retries") {
        fixture.arm();
        ble_store_status_event event{};
        event.event_code = BLE_STORE_EVENT_OVERFLOW;
        for (int iteration = 0; iteration < 100; ++iteration) {
            for (const int type : {BLE_STORE_OBJ_TYPE_OUR_SEC, BLE_STORE_OBJ_TYPE_PEER_SEC, BLE_STORE_OBJ_TYPE_CCCD}) {
                event.overflow.obj_type = type;
                assert(repair.storeStatus(event, 7, 101) == BLE_HS_ESTORE_CAP);
            }
        }
        assert(state.peerReads.empty() && state.finds.empty());
        fixture.untouched();
        assert(repair.remaining(101) == 179999);
    } else if (name == "store-full-after-attempt") {
        fixture.arm();
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_RETRY);
        fixture.arm(200);
        assert(fixture.full(BLE_STORE_OBJ_TYPE_PEER_SEC, 201) == BLE_HS_ESTORE_CAP);
        assert(state.deleted.size() == 1);
    } else if (name == "store-full-blocked") {
        fixture.arm();
        state.deleteResult = 17;
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
        fixture.arm(200);
        assert(repair.blocked());
        assert(fixture.full(BLE_STORE_OBJ_TYPE_OUR_SEC, 201) == BLE_HS_ESTORE_CAP);
        assert(state.deleted.size() == 1 && state.terminated.size() == 1);
    } else if (name == "store-full-keeps-smp-security-policy") {
        fixture.arm();
        assert(fixture.full(BLE_STORE_OBJ_TYPE_PEER_SEC) == 0);
        assert(fixture.full(BLE_STORE_OBJ_TYPE_OUR_SEC) == 0);
        request.new_sc = 0;
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
        request.new_sc = 1;
        request.new_key_size = 15;
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
        request.new_key_size = 16;
        request.cur_authenticated = 1;
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
        fixture.untouched();
        request.new_authenticated = 1;
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_RETRY);
        assert(state.deleted.size() == 1 && sameAddress(state.deleted[0], state.connection.peer_id_addr));
        assert(state.bonds.size() == 2 && sameAddress(state.bonds[0], address(0, 1)) && sameAddress(state.bonds[1], address(1, 2)));
    } else if (name == "delete-error" || name == "terminate-error") {
        fixture.arm();
        state.deleteResult = 17;
        state.terminateResult = name == "terminate-error" ? 9 : 0;
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
        assert(repair.blocked());
        assert(repair.remaining(101) == 0);
        assert(state.deleted.size() == 1 && state.terminated.size() == 1);
        assert(state.bonds.size() == 2);
        assert(sameAddress(state.bonds[0], address(0, 1)) && sameAddress(state.bonds[1], address(1, 2)));
        assert(state.terminated[0].first == 7 && state.terminated[0].second == BLE_ERR_REM_USER_CONN_TERM);
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
        assert(state.deleted.size() == 1 && state.terminated.size() == 1);
        repair.reset();
        assert(!repair.blocked() && repair.remaining(101) == 0);
        assert(fixture.handle() == BLE_GAP_REPEAT_PAIRING_IGNORE);
    } else {
        assert(false && "Unknown fixture case");
    }
}
} // namespace

extern "C" int ble_gap_conn_find(uint16_t handle, ble_gap_conn_desc* out_desc) {
    state.finds.push_back(handle);
    if (!state.findResult) *out_desc = state.connection;
    if (state.duringFind) state.duringFind();
    return state.findResult;
}

extern "C" int ble_store_util_delete_peer(const ble_addr_t* peer_id_addr) {
    state.deleted.push_back(*peer_id_addr);
    if (state.duringDelete) state.duringDelete();
    std::erase_if(state.bonds, [&](const auto& bond) { return sameAddress(bond, *peer_id_addr); });
    return state.deleteResult;
}

extern "C" int ble_store_read_peer_sec(const ble_store_key_sec* key, ble_store_value_sec* value) {
    state.peerReads.push_back(*key);
    if (!state.peerReadResult) *value = state.storedPeer;
    if (state.duringPeerRead) state.duringPeerRead();
    return state.peerReadResult;
}

extern "C" int ble_gap_terminate(uint16_t handle, uint8_t reason) {
    state.terminated.emplace_back(handle, reason);
    return state.terminateResult;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    check(argv[1]);
}
