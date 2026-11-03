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
}  // namespace mm::platform
