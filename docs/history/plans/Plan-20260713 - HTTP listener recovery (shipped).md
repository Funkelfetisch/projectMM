# HTTP listener recovery

## Intent

Keep the web UI recoverable when the LED scheduler is healthy but the HTTP listener fails to bind or its listening socket enters a hard error state.

## Plan

1. Expose the listener socket's valid state through the platform adapter.
2. Close a listener after a hard accept error while preserving normal non-blocking empty accepts.
3. Let HttpServerModule retry a missing listener once per second and rebind live when its port changes.
4. Pin live port rebinding in a desktop unit test, build HELIO, and deploy with the preview fix.

## Boundaries

- Accepted WebSocket clients keep their existing lifecycle and backpressure behavior.
- Normal empty non-blocking accepts do not trigger a rebind.
- Recovery performs no reboot and changes no persisted controls.

## Outcome

The platform listener exposes validity, hard accept errors close the listener, and `HttpServerModule` retries a missing listener once per second while applying live port changes. The ESP32 build includes the recovery path and runs on the controller as UI build `20260713b`; the live HTTP endpoint remains reachable after deployment.
