// Pinned worker task + wake notification — the platform half of the multicore render↔encode split
// (Drivers owns the domain logic; this file owns the one seam so no FreeRTOS type escapes
// src/platform/, the platform-boundary rule). xTaskCreatePinnedToCore + a direct-to-task
// notification (xTaskNotifyGive / ulTaskNotifyTake) is the textbook lock-free single-producer/
// single-consumer wake FreeRTOS documents as the lightweight binary-semaphore replacement — same
// pairing Espressif's own examples use for a "wake this worker" handoff. The Drivers render-split is
// the first user; the async-ArtNet send wants the identical primitive, which is why it lives here.

#include "platform/platform.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"   // the render loop and the encode worker each subscribe themselves so a wedge self-heals (reboot) not hangs

#include <atomic>
#include <new>

namespace mm::platform {

namespace {
// The opaque WorkerTask::impl. Holds the RTOS handle, the caller's fn/user, and a stop flag the
// spawned trampoline checks.
struct EspWorker {
    TaskHandle_t handle = nullptr;
    WorkerFn fn = nullptr;
    void* user = nullptr;
    std::atomic<bool> stop{false};
    std::atomic<bool> finished{false};
};

// Trampoline: run the caller's fn under this worker task's own watchdog subscription. waitNotify feeds
// the idle loop; bounded jobs return to it, while a callback stuck inside a peripheral operation trips
// this worker's watchdog independently of a joining caller. On return the task suspends at the published
// join point; stopPinnedTask deletes it before releasing the callback's owner.
void workerTrampoline(void* arg) {
    auto* w = static_cast<EspWorker*>(arg);
    taskWdtSubscribe();
    w->fn(w->user);                     // runs until stopPinnedTask flips w->stop and wakes it
    taskWdtUnsubscribe();
    // Publish the join point as the final access to EspWorker/user memory. The stopper deletes this
    // suspended RTOS task and then releases `w`; it therefore returns only after the callback has
    // ceased touching every borrowed object.
    w->finished.store(true, std::memory_order_release);
    vTaskSuspend(nullptr);
    // Defensive fallback if another task resumes this internal worker unexpectedly.
    vTaskDelete(nullptr);
}
}  // namespace

bool spawnPinnedTask(WorkerTask& t, const char* name, WorkerFn fn, void* user,
                     size_t stackBytes, uint8_t priority, int core) {
    auto* w = new (std::nothrow) EspWorker();
    if (!w) return false;
    w->fn = fn;
    w->user = user;
    // t.impl BEFORE the create: a task pinned to the SPAWNER'S OWN core preempts inside
    // xTaskCreatePinnedToCore (equal/higher priority), and its fn may call waitNotify(t) immediately —
    // with impl still null that returns false without blocking, and a retry loop becomes a hot spin
    // that starves this caller from ever assigning impl (a live-lock; measured: board offline at boot).
    // Assigning first closes the window; on create-failure it is reset before anyone can be woken.
    t.impl = w;
    const BaseType_t coreId = (core < 0) ? tskNO_AFFINITY : static_cast<BaseType_t>(core);
    const BaseType_t ok = xTaskCreatePinnedToCore(
        &workerTrampoline, name, static_cast<uint32_t>(stackBytes), w,
        static_cast<UBaseType_t>(priority), &w->handle, coreId);
    if (ok != pdPASS) { t.impl = nullptr; delete w; return false; }   // caller runs inline (degrade)
    return true;
}

void notifyTask(WorkerTask& t) {
    auto* w = static_cast<EspWorker*>(t.impl);
    if (w && w->handle) xTaskNotifyGive(w->handle);
}

bool waitNotify(WorkerTask& t, uint32_t timeoutMs) {
    auto* w = static_cast<EspWorker*>(t.impl);
    if (!w) return false;
    // ulTaskNotifyTake(pdTRUE, …) clears the notification count on return (the binary-semaphore
    // form). Non-zero return = a notify (or stop-wake) landed; 0 = timed out.
    const bool notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeoutMs)) != 0;
    taskWdtReset();   // every generic worker feeds while parked; active jobs keep their own bound
    return notified;
}

void stopPinnedTask(WorkerTask& t) {
    auto* w = static_cast<EspWorker*>(t.impl);
    if (!w) return;
    w->stop.store(true, std::memory_order_release);   // the fn re-checks this after its next wake
    if (w->handle) xTaskNotifyGive(w->handle);         // wake it so it observes the stop
    // This is a true join: every caller frees the worker's user object or hardware immediately after
    // return, so a timeout/detach would turn a slow peripheral drain into a use-after-free. vTaskDelay
    // lets a same-core worker run; the caller feeds its own watchdog while the worker's independent
    // subscription remains the failure detector for a genuine wedge.
    while (!w->finished.load(std::memory_order_acquire)) {
        taskWdtReset();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (w->handle) vTaskDelete(w->handle);
    delete w;
    t.impl = nullptr;
}

// Whether the CALLING task subscribed to the WDT — so taskWdtReset only feeds a real subscription. The
// WDT subscription is per-task (esp_task_wdt_add/_reset act on the current task), so this flag is
// thread_local, NOT one global: the render loop AND the core-1 encode worker each feed the WDT, and a
// single global would let the worker call esp_task_wdt_reset() on a task the render loop subscribed —
// which the IDF rejects as "task not found", flooding the log every tick and starving the network stack.
// Each task reads/writes its own copy, on its own task, so no synchronization is needed.
static thread_local bool s_wdtSubscribed = false;

// Subscribe the CURRENT task to the task WDT. The sdkconfig runs the TWDT with idle-task checking OFF (a
// saturated core is healthy), so nothing is watched unless a task subscribes. The render loop subscribes
// directly and each generic worker is subscribed by its trampoline: if one stops feeding (a genuine wedge,
// not a busy frame), it panics and reboots (the self-heal) instead of hanging silently, and leaves a backtrace.
// Idempotent per task; a failure (WDT not inited) just leaves s_wdtSubscribed false and reset a no-op,
// degrading to unwatched behavior rather than crashing.
void taskWdtSubscribe() {
    if (s_wdtSubscribed) return;
    if (esp_task_wdt_add(nullptr) == ESP_OK) s_wdtSubscribed = true;
}

// Unsubscribe THIS task before it exits (esp_task_wdt_delete), so a torn-down task leaves no stale WDT
// entry the IDF would keep checking. No-op if this task never subscribed.
void taskWdtUnsubscribe() {
    if (!s_wdtSubscribed) return;
    if (esp_task_wdt_delete(nullptr) == ESP_OK) s_wdtSubscribed = false;
}

// Feed THIS task's WDT subscription (esp_task_wdt_reset). No-op until/unless taskWdtSubscribe ran on this
// same task, so a task that never subscribed (or a build/config without the WDT) is unaffected.
void taskWdtReset() {
    if (s_wdtSubscribed) esp_task_wdt_reset();
}

}  // namespace mm::platform
