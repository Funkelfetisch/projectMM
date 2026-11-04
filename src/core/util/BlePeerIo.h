#pragma once

#include "core/util/BleMessage.h"
#include <cstring>

namespace mm {
/// Request framing and the reply a peer receives in notifications, one LF-terminated line at a time; caller owns synchronization.
class BlePeerIo {
public:
    /// Append an ATT fragment of the request and report whether the request is partial, ready, busy or invalid.
    BleMessage::Result feed(const uint8_t* data, size_t length) { return incoming_.feed(data, length); }
    /// Copy and consume one complete request when `out` fits it, returning its length.
    size_t take(char* out, size_t capacity) { return incoming_.take(out, capacity); }
    /// Store the reply and queue it with its LF when `deliver`; false when it exceeds the message capacity or the previous reply is still going out.
    bool publish(const char* response, bool deliver) {
        if (!response || sending()) return false;
        const size_t length = std::strlen(response);
        if (length > BleMessage::kCapacity) return false;
        std::memcpy(outgoing_, response, length + 1);
        length_ = length + 1;   // the LF, sent where the terminator is stored
        sent_ = deliver ? 0 : length_;
        return true;
    }
    /// The latest reply, without its LF.
    const char* current() const { return outgoing_; }
    /// Whether part of the latest reply still waits to be sent.
    bool sending() const { return sent_ < length_; }
    /// Copy the next at most `capacity` bytes waiting to be sent into `out` and return how many; 0 when none wait.
    size_t pending(uint8_t* out, size_t capacity) const {
        const size_t count = length_ - sent_ < capacity ? length_ - sent_ : capacity;
        if (!out || !count) return 0;
        std::memcpy(out, outgoing_ + sent_, count);
        if (sent_ + count == length_) out[count - 1] = '\n';
        return count;
    }
    /// Mark `count` bytes of what `pending` returned as sent.
    void sent(size_t count) { sent_ = count < length_ - sent_ ? sent_ + count : length_; }
    /// Reset in place when reusing a peer, without a large temporary on the BLE host stack.
    void reset() { incoming_.reset(); outgoing_[0] = '\0'; length_ = sent_ = 0; }
    /// Drop whatever of the reply is unsent, which a peer without notifications cannot receive.
    void discard() { sent_ = length_; }
    /// Drop a partial request and the unsent reply, which a vanished link cannot receive.
    void disconnect() { incoming_.reset(); discard(); }
private:
    BleMessage incoming_;
    char outgoing_[BleMessage::kCapacity + 1] = {};
    size_t length_ = 0, sent_ = 0;
};
} // namespace mm
