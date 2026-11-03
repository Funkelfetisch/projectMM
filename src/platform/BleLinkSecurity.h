#pragma once

/// A BLE link's encryption state, apart from the rest of the platform API so core code that tracks peers includes only what it uses.

#include <cstdint>

#include "platform/nonblocking.h"

namespace mm::platform {

/// How far one connection's encryption has come: control works on any link, and secrets travel only on Encrypted.
enum class BleLinkSecurity : uint8_t { Open, Pairing, Encrypted, Failed };
/// One request for encryption: an open link starts pairing and sets `ask`, and a failure is reported once before the link counts as open again.
inline BleLinkSecurity requestLinkSecurity(BleLinkSecurity& link, bool& ask) MM_NONBLOCKING {
    ask = link == BleLinkSecurity::Open;
    const BleLinkSecurity reported = ask ? BleLinkSecurity::Pairing : link;
    link = link == BleLinkSecurity::Failed ? BleLinkSecurity::Open : reported;
    return reported;
}

}  // namespace mm::platform
