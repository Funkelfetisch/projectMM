#pragma once

#include "core/util/BleMessage.h"
#include <cstdio>
#include <cstring>

namespace mm {
/// Request framing and a stable long-read response for one peer; caller owns synchronization.
class BlePeerIo {
public:
    /// Append an ATT fragment of the request and report whether the request is partial, ready, busy or invalid.
    BleMessage::Result feed(const uint8_t* data, size_t length) { return incoming_.feed(data, length); }
    /// Copy and consume one complete request when `out` fits it, returning its length.
    size_t take(char* out, size_t capacity) { return incoming_.take(out, capacity); }
    /// Store the response the peer reads next; false when it exceeds the message capacity.
    bool publish(const char* response) {
        if (!response || std::strlen(response) > BleMessage::kCapacity) return false;
        std::snprintf(response_, sizeof(response_), "%s", response);
        return true;
    }
    /// The published response, which a long read serves in slices.
    const char* current() const { return response_; }
    /// The value a Read Blob at `offset` returns, one whole snapshot per read sequence.
    const char* read(uint16_t offset) { return BleMessage::valueForRead(offset, response_, snapshot_); }
    /// Reset in place when reusing a peer, without a large temporary on the BLE host stack.
    void reset() { incoming_.reset(); response_[0] = '\0'; snapshot_[0] = '\0'; }
    /// Preserve the published response but never inherit a partial request or blob snapshot.
    void disconnect() { incoming_.reset(); snapshot_[0] = '\0'; }
    /// Drop the long-read snapshot so the next read starts from the published response.
    void clearSnapshot() { snapshot_[0] = '\0'; }
private:
    BleMessage incoming_;
    char response_[BleMessage::kCapacity + 1] = "{\"v\":1,\"ok\":true}";
    char snapshot_[BleMessage::kCapacity + 1] = {};
};
} // namespace mm
