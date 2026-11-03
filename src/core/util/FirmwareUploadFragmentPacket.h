#pragma once

#include "core/util/FirmwareUploadFragmentBridge.h"

namespace mm {
/// Experimental packet encoding for sequential upload-frame fragments. Header: frame length and offset, each uint16 little-endian, followed by at least one byte; a default 20-byte ATT value carries up to 16 frame bytes. It supplies framing only: it does not authorize or expose a BLE endpoint.
struct FirmwareUploadFragmentPacket {
    /// The header's size: the frame length and the offset, two bytes each.
    static constexpr size_t kHeaderBytes = 4;
    /// The whole frame's length, this fragment's offset into it, and the fragment's own byte count.
    size_t total = 0, offset = 0, length = 0;
    /// The fragment's bytes, a view into the packet `parse` read.
    const uint8_t* data = nullptr;

    /// Reads `bytes` as one fragment into `out`, false for a packet that is empty, oversized or out of range.
    static bool parse(const uint8_t* bytes, size_t size, FirmwareUploadFragmentPacket& out) {
        out = {};
        if (!bytes || size <= kHeaderBytes || size > FirmwareUploadFrame::kMaxBytes) return false;
        const size_t total = size_t(bytes[0]) | (size_t(bytes[1]) << 8);
        const size_t offset = size_t(bytes[2]) | (size_t(bytes[3]) << 8);
        const size_t length = size - kHeaderBytes;
        if (total < 2 || total > FirmwareUploadFrame::kMaxBytes || offset > total || length > total - offset) return false;
        out = {total, offset, length, bytes + kHeaderBytes};
        return true;
    }
};

template <typename Backend>
typename FirmwareUploadFragmentBridge<Backend>::FragmentResult appendFirmwareUploadPacket(
    FirmwareUploadFragmentBridge<Backend>& bridge, uint64_t owner,
    const uint8_t* bytes, size_t size, uint32_t now) {
    FirmwareUploadFragmentPacket packet;
    if (!FirmwareUploadFragmentPacket::parse(bytes, size, packet)) {
        using Result = typename FirmwareUploadFragmentBridge<Backend>::AssemblyResult;
        return {Result::Invalid, false, {}};
    }
    return bridge.append(owner, packet.total, packet.offset, packet.data, packet.length, now);
}
} // namespace mm
