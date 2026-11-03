namespace mm::platform {
bool bleControlAvailable() MM_NONBLOCKING { return false; }
bool bleControlStart(BleControlHandle&, const char*, const char*) { return false; }
bool bleControlStart(BleControlHandle&, const char*, const char*, const BleBatteryConfig&) { return false; }
bool bleControlEpoch(const BleControlHandle&, std::array<uint8_t, 16>&) { return false; }
bool bleControlPublishBattery(BleControlHandle&, const BleBatterySnapshot&) MM_NONBLOCKING { return false; }
bool bleControlStop(BleControlHandle& handle) { handle.impl = nullptr; return true; }
void bleControlAbandon(BleControlHandle& handle) { handle.impl = nullptr; }
size_t bleControlReceive(BleControlHandle&, char*, size_t, uint64_t&) MM_NONBLOCKING { return 0; }
bool bleControlOwners(BleControlHandle&, uint64_t*, size_t, size_t&) MM_NONBLOCKING { return false; }
bool bleControlPublish(BleControlHandle&, const char*, uint32_t, uint64_t) MM_NONBLOCKING { return false; }
bool bleControlPublishAdvertisement(BleControlHandle&, const std::array<uint8_t, 13>&) MM_NONBLOCKING { return false; }
bool bleControlConnected(const BleControlHandle&) MM_NONBLOCKING { return false; }
bool bleControlPairingState(const BleControlHandle&, BlePairingState& state) MM_NONBLOCKING { state = {}; return false; }
bool bleControlLinkState(const BleControlHandle&, BleLinkState& state) MM_NONBLOCKING { state = {}; return false; }
bool bleControlAllowRepair(BleControlHandle&) MM_NONBLOCKING { return false; }
bool bleControlUpdateStream(BleControlHandle&, BleUpdateStream*) MM_NONBLOCKING { return false; }
void bleControlUpdateRelease(BleControlHandle&) {}
#ifndef ESP_PLATFORM
namespace { std::vector<BleUpdateAck> testUpdateAcks; }
bool bleControlUpdateAck(BleControlHandle&, uint64_t generation, uint32_t stored, uint32_t asked, bool failed) MM_NONBLOCKING {
    testUpdateAcks.push_back({generation, stored, asked, failed});
    return true;
}
std::vector<BleUpdateAck> takeTestBleUpdateAcks() { std::vector<BleUpdateAck> acks; acks.swap(testUpdateAcks); return acks; }
namespace { BleLinkSecurity testLink = BleLinkSecurity::Open; uint32_t testSecurityRequests = 0; }
void setTestBleLinkSecurity(BleLinkSecurity link) { testLink = link; testSecurityRequests = 0; }
uint32_t testBleSecurityRequests() { return testSecurityRequests; }
BleLinkSecurity bleControlSecurity(BleControlHandle&, uint64_t, bool request) MM_NONBLOCKING {
    bool ask = false;
    const BleLinkSecurity reported = request ? requestLinkSecurity(testLink, ask) : testLink;
    testSecurityRequests += ask;
    return reported;
}
#else
BleLinkSecurity bleControlSecurity(BleControlHandle&, uint64_t, bool) MM_NONBLOCKING { return BleLinkSecurity::Open; }
bool bleControlUpdateAck(BleControlHandle&, uint64_t, uint32_t, uint32_t, bool) MM_NONBLOCKING { return false; }
#endif
}  // namespace mm::platform

namespace mm::platform {
bool bleObserverStart(BleObserverHandle&) { return false; }
bool bleObserverStop(BleObserverHandle& h) { h.impl = nullptr; return true; }
bool bleObserverReceive(BleObserverHandle&, BleAdvertisement&) MM_NONBLOCKING { return false; }
uint32_t bleObserverDropped(const BleObserverHandle&) MM_NONBLOCKING { return 0; }
}
