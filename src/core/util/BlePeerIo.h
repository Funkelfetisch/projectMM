#pragma once

#include "core/util/BleMessage.h"
#include <cstdio>
#include <cstring>

namespace mm {
/// Request framing and a stable long-read response for one peer; caller owns synchronization.
class BlePeerIo {
public:
    BleMessage::Result feed(const uint8_t* data, size_t length) { return incoming_.feed(data, length); }
    size_t take(char* out, size_t capacity) { return incoming_.take(out, capacity); }
    bool publish(const char* response) {
        if (!response || std::strlen(response) > BleMessage::kCapacity) return false;
        std::snprintf(response_, sizeof(response_), "%s", response);
        return true;
    }
    const char* current() const { return response_; }
    const char* read(uint16_t offset) { return BleMessage::valueForRead(offset, response_, snapshot_); }
    /// Reset in place when reusing a peer, without a large temporary on the BLE host stack.
    void reset() { incoming_.reset(); response_[0] = '\0'; snapshot_[0] = '\0'; }
    /// Preserve the published response, as before, but never inherit a partial request or blob snapshot.
    void disconnect() { incoming_.reset(); snapshot_[0] = '\0'; }
    void clearSnapshot() { snapshot_[0] = '\0'; }
private:
    BleMessage incoming_;
    char response_[BleMessage::kCapacity + 1] = "{\"v\":1,\"ok\":true}";
    char snapshot_[BleMessage::kCapacity + 1] = {};
};
} // namespace mm
