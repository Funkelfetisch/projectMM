# Reliable BLE sessions and scene catalog

This design implements the existing app owner's return in `docs/moonlight-ble-firmware-return-20261026.md` of the app checkout. RPC2 sessions, epochs and complete scenes are implemented, documented in [Product BLE](../../reference/product-ble.md), and installed on the HELIO bench as image `a41701b9`. The old-central same-ID read counterexample is a failed mock regression, not a physically observed BLE fault. Native firmware correlation/reboot checks pass; first-pair app flow, real mixed-device groups and customer acceptance remain open. This file retains the design and remaining evidence checklist; its location does not imply that the installed contract is absent.

## Correlation and retry

Retain framing version1 and the existing UUID family. Advertise an implemented additive contract with compact `rpc:2` in identity, keeping the256-byte bound. A hello echoes a32-hex client challenge and returns a new32-hex device service epoch `e`, monotonic session `s` and result revision `r`. Epoch changes only on genuine service startup, not tree prepare. Repeated identical hello is idempotent; explicit resume succeeds only for the current epoch/session. These identify requests, not accounts or exclusive ownership.

Every operation, including get/clock/catalog/battery, carries epoch, session and request ID; every result carries its correlation and revision. IDs increase within a session; exact retained retries never reapply, changed bytes under the same ID fail, expired IDs never execute again. [Product BLE](../../reference/product-ble.md) specifies the implemented error correlation, ID exhaustion and fresh-session rules. A new session does not erase an already accepted pending operation.

## Live state and pending operations

Require an explicit boolean live choice for negotiated patches. Live uses StateSource::Live and never schedules a configuration save; saved configuration is deliberate. Live does not provide an isolated persistent shadow tree: a later whole-subtree save can serialize its then-current values. Verify scalar, enable, create, replace, remove and deferred paths from a clean persistence baseline.

Return pending owner/session/ID/deadline and a retained terminal result. Status and cancel name a precise target; distinguish cancelled, already applied, failed and not retained. Preserve the existing uint32 deadline limits and apply-due-before-dispatch ordering. An applied result requires completing necessary prepare and deferred controls, not an ATT receipt. Retain partial-mutation failure semantics and actual readback.

## Complete scenes

Use a single canonical source for the existing HELIO scene definitions and generate UI/C++ data, replacing the hand-maintained UI-only table. Keep stable scene IDs separate from native type IDs, palettes and product geometry. Bound catalog pages and scene-info control descriptors to512 bytes; metadata includes actual ranges/defaults/options and audio/RGBW semantics. A scene operation references catalog revision and ID plus validated overrides, live policy and optional deadline.

Apply the complete scene to its explicit owned scene region in one state-document application, replacing competing content while preserving hardware/layout. Recognize a scene from actual structure and controls; an unknown or externally composed tree reports no canonical scene instead of a remembered selection. Cover RGB and native W independently.

## Required evidence

- Preserve the old-central same-ID failure, then prove get/clock/battery freshness, hello/resume/reboot, exact retries, changed bytes and expired IDs.
- Interleave publication with each MTU23 blob and prove a coherent e/s/id/r snapshot. Keep the existing512-byte response and256-byte identity limits, including worst-case two-pack battery fields and errors.
- Verify transient/persistent saves and structural/deferred preparation with actual scheduler paths.
- Exercise named pending/cancel, newer sessions retaining old jobs, due/cancel races, wrap and non-retained outcomes.
- Switch from multiple effects/modifiers, prove complete defaults/overrides, reject unknown controls/revisions, and retain honest memory/prepare failures and noncanonical readback.
- Integrate through the existing app owner and normal connection/group ownership. Test Basic3/4/5 mixed groups and mini-HELIO separately; single-device scheduled starts do not prove phase or multi-device skew.
