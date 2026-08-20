import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import path from "node:path";
import test from "node:test";

const root = path.resolve(import.meta.dirname, "../..");

function between(source, startNeedle, endNeedle) {
    const start = source.indexOf(startNeedle);
    const end = source.indexOf(endNeedle, start + startNeedle.length);
    assert.ok(start >= 0 && end > start, `${startNeedle} body exists`);
    return source.slice(start, end);
}

test("ESP32 RMT loopback tasks retain their capture storage", async () => {
    const source = await readFile(
        path.join(root, "src", "platform", "esp32", "platform_esp32_rmt.cpp"), "utf8");
    const shortLoopback = between(source, "RmtLoopbackResult rmtWs2812Loopback(",
                                  "RmtLoopbackResult rmtWs2812LoopbackFrame(");
    const frameLoopback = between(source, "RmtLoopbackResult rmtWs2812LoopbackFrame(",
                                  "} // namespace mm::platform");

    assert.match(source, /struct LoopbackCapture[\s\S]*std::atomic<uint8_t> refs[\s\S]*std::atomic<size_t> got[\s\S]*std::atomic<bool> done/);
    assert.match(source, /capture\.retain\(\)[\s\S]*xTaskCreate\([\s\S]*capture\.release\(\)/);
    assert.equal((source.match(/xTaskCreate\(/g) || []).length, 1,
                 "every RMT RX task starts through the retained capture helper");
    assert.doesNotMatch(source, /volatile\s+(?:size_t|bool)\s+(?:got|done)/);

    for (const body of [shortLoopback, frameLoopback]) {
        assert.match(body, /makeLoopbackCapture\(/);
        assert.match(body, /startLoopbackCapture\(/);
        assert.match(body, /done\.load\(std::memory_order_acquire\)/);
        assert.match(body, /cap->release\(\)/);
    }
});

test("ESP32 generic workers have watchdog-backed join semantics", async () => {
    const source = await readFile(
        path.join(root, "src", "platform", "esp32", "platform_esp32_worker.cpp"), "utf8");
    const trampoline = between(source, "void workerTrampoline(", "}  // namespace");
    const stop = between(source, "void stopPinnedTask(", "void taskWdtSubscribe(");

    assert.match(trampoline, /taskWdtSubscribe\(\)[\s\S]*w->fn\(w->user\)[\s\S]*taskWdtUnsubscribe\(\)/);
    assert.match(trampoline, /finished\.store\(true, std::memory_order_release\)[\s\S]*vTaskSuspend\(nullptr\)/);
    assert.match(stop, /while \(!w->finished\.load\(std::memory_order_acquire\)\)[\s\S]*vTaskDelay/);
    assert.match(stop, /vTaskDelete\(w->handle\)[\s\S]*delete w/);
    assert.doesNotMatch(stop, /kStopJoinTimeoutMs|w->detached|deadline\s*=|xTaskGetTickCount\(/);
});

test("RMT driver destruction drains the final borrowed frame", async () => {
    const source = await readFile(
        path.join(root, "src", "light", "drivers", "RmtLedDriver.h"), "utf8");
    const destructor = between(source, "~RmtLedDriver() override", "/// WS2812/SK6812");

    assert.match(destructor, /while \(!deinitAll\(\)\) platform::yield\(\)/);
    assert.match(destructor, /freeFrame\(\)/);
});
