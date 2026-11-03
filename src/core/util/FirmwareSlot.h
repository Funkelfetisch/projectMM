// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/util/FirmwareOtaPort.h"

#include <cstddef>
#include <cstdint>

namespace mm {
/// The inactive application slot an update fills, which the platform supplies and a host test replaces with a fake.
class FirmwareSlot {
public:
    /// Virtual, so a slot is destroyed through its base.
    virtual ~FirmwareSlot() = default;
    /// The most bytes the slot holds, zero without an inactive one.
    virtual size_t capacity() const = 0;
    /// Whether another update holds the gate that keeps every update path exclusive.
    virtual bool busy() const = 0;
    /// Open the slot for an image of `length` bytes; `write` fills it in order.
    virtual bool begin(size_t length) = 0;
    /// Append the next bytes of the image.
    virtual bool write(const uint8_t* data, size_t length) = 0;
    /// Commit the image, or discard it with `false`.
    virtual bool end(bool commit) = 0;
    /// Discard the image; true once nothing is held.
    virtual bool abort() = 0;
};

/// The platform's application slot, written through the same writer and gate as the HTTP upload.
class PlatformFirmwareSlot final : public FirmwareSlot {
public:
    /// The next application slot's size.
    size_t capacity() const override { return port_.capacity(); }
    /// The platform's shared update gate.
    bool busy() const override { return platform::otaBusy(); }
    /// Open the platform writer, which takes the gate.
    bool begin(size_t length) override { return port_.begin(length); }
    /// Append to the platform writer.
    bool write(const uint8_t* data, size_t length) override { return port_.write(data, length); }
    /// Commit through the platform writer, which releases the gate on every other outcome.
    bool end(bool commit) override { return port_.end(commit); }
    /// Abort the writer; true once the shared gate is free, since the platform frees its own writer after a failed write and keeps the gate held until reboot after a failed abort.
    bool abort() override { return port_.abort() || !busy(); }
private:
    FirmwareOtaPort<> port_;
};
} // namespace mm
