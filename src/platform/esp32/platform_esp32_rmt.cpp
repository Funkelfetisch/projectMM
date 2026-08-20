// RMT WS2812 LED output — the peripheral half of the LED driver.
//
// The driver (src/light/drivers/RmtLedDriver.h) does all the domain work and
// applies Correction into a byte frame. This file owns only the peripheral:
// channel setup, the standard byte encoder, transmit + wait, and the copy
// encoder/RX side the on-device loopback test uses. No domain logic here.
//
// Normal frames use ESP-IDF's byte encoder. The copy encoder remains for the
// loopback rig, which deliberately transmits already-built RMT symbols.

#include "platform/platform.h"

#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_encoder.h"
#include "driver/gpio.h"   // continuity pre-check in the loopback self-test
#include "soc/soc_caps.h"  // SOC_RMT_MEM_WORDS_PER_CHANNEL (64 classic, 48 S3)
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"   // ets_delay_us for the reset gap

#include "esp_heap_caps.h"   // capture buffer alloc for the shared frame loopback
#include "esp_timer.h"       // timed first transmit
#include "esp_log.h"

#include <cstdlib>
#include <cstring>
#include <functional>  // the transmit callback the shared frame loopback takes
#include <new>      // std::nothrow
#include <atomic>

namespace mm::platform {

namespace {

#if SOC_RMT_SUPPORT_DMA
// ESP-IDF uses this size in its large-transfer RMT DMA test. Each ping-pong half
// carries 512 WS2812 symbols (640 us), leaving ample refill time while WiFi is active.
constexpr size_t kTxDmaBufferSymbols = 1024;
#endif

// Per-channel peripheral state, hidden behind RmtWs2812Handle::impl so the
// domain header never sees an ESP type. The byte encoder drives normal frames;
// the copy encoder remains for the raw-symbol loopback test.
struct RmtTxState {
    rmt_channel_handle_t channel = nullptr;
    rmt_encoder_handle_t byteEncoder = nullptr;
    rmt_encoder_handle_t symbolEncoder = nullptr;
    uint32_t resolutionHz = 0;
};

// A timed-out diagnostic transfer still owns both an RMT channel and its borrowed source bytes.
// Keep that pair in a fixed reclamation table and retry it on later RMT initialization. The table
// matches the hardware TX-channel ceiling: every retained entry consumes one channel, so another
// valid handle cannot exceed this capacity.
struct QuarantinedLoopbackTx {
    RmtWs2812Handle handle;
    void* source = nullptr;
};
constexpr size_t kLoopbackQuarantineSlots = rmtTxChannels > 0 ? rmtTxChannels : 1;
QuarantinedLoopbackTx s_loopbackQuarantine[kLoopbackQuarantineSlots];

void reapLoopbackTransfers() {
    for (auto& entry : s_loopbackQuarantine) {
        if (!entry.handle.impl || !rmtWs2812Deinit(entry.handle)) continue;
        heap_caps_free(entry.source);
        entry.source = nullptr;
    }
}

void releaseLoopbackTransfer(RmtWs2812Handle& handle, void* source) {
    if (rmtWs2812Deinit(handle)) {
        heap_caps_free(source);
        return;
    }
    reapLoopbackTransfers();
    for (auto& entry : s_loopbackQuarantine) {
        if (entry.handle.impl) continue;
        entry.handle.impl = handle.impl;
        entry.source = source;
        handle.impl = nullptr;
        return;
    }

    // This path requires more live TX handles than the SoC exposes and is therefore unreachable for
    // valid driver state. Preserve ownership even under a broken platform report: wait for completion
    // instead of dropping the only references to an active DMA source.
    while (!rmtWs2812Deinit(handle)) vTaskDelay(pdMS_TO_TICKS(1));
    heap_caps_free(source);
}

} // namespace

bool rmtWs2812Init(RmtWs2812Handle& h, uint8_t gpio, uint32_t resolutionHz, bool invert,
                   RmtWs2812Timing timing) {
    reapLoopbackTransfers();
    if (resolutionHz == 0 || timing.periodNs <= timing.t0hNs || timing.periodNs <= timing.t1hNs)
        return false;
    auto* st = new (std::nothrow) RmtTxState();
    if (!st) return false;

    rmt_tx_channel_config_t txCfg = {};
    txCfg.gpio_num = static_cast<gpio_num_t>(gpio);
    txCfg.clk_src = RMT_CLK_SRC_DEFAULT;
    txCfg.resolution_hz = resolutionHz;
    // Long LED frames need enough refill headroom to survive WiFi and preview traffic. On chips
    // with RMT DMA, use a large software ping-pong buffer; the S3's single hardware block leaves
    // only 24 symbols (30 us) per half and can otherwise insert a latch-sized gap mid-frame.
#if SOC_RMT_SUPPORT_DMA
    txCfg.mem_block_symbols = kTxDmaBufferSymbols;
    txCfg.flags.with_dma = 1;
#else
    txCfg.mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL;
#endif
    txCfg.trans_queue_depth = 4;
    txCfg.flags.invert_out = invert ? 1 : 0;

    esp_err_t channelErr = rmt_new_tx_channel(&txCfg, &st->channel);
#if SOC_RMT_SUPPORT_DMA
    // Some chips expose only one DMA-capable TX channel per RMT group. Keep multi-pin drivers
    // functional by giving the first available lane DMA and using hardware ping-pong thereafter.
    if (channelErr != ESP_OK) {
        txCfg.mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL;
        txCfg.flags.with_dma = 0;
        channelErr = rmt_new_tx_channel(&txCfg, &st->channel);
    }
#endif
    if (channelErr != ESP_OK) {
        delete st;
        return false;
    }

    rmt_copy_encoder_config_t copyCfg = {};
    if (rmt_new_copy_encoder(&copyCfg, &st->symbolEncoder) != ESP_OK) {
        rmt_del_channel(st->channel);
        delete st;
        return false;
    }

    auto durationTicks = [resolutionHz](uint32_t ns) -> uint16_t {
        uint64_t ticks = (static_cast<uint64_t>(ns) * resolutionHz) / 1'000'000'000ull;
        if (ticks == 0) ticks = 1;
        if (ticks > 0x7FFFu) ticks = 0x7FFFu;
        return static_cast<uint16_t>(ticks);
    };
    const uint16_t periodTicks = durationTicks(timing.periodNs);
    const uint16_t t0hTicks = durationTicks(timing.t0hNs);
    const uint16_t t1hTicks = durationTicks(timing.t1hNs);
    if (periodTicks <= t0hTicks || periodTicks <= t1hTicks) {
        rmt_del_encoder(st->symbolEncoder);
        rmt_del_channel(st->channel);
        delete st;
        return false;
    }
    rmt_bytes_encoder_config_t byteCfg = {};
    byteCfg.bit0.level0 = 1;
    byteCfg.bit0.duration0 = t0hTicks;
    byteCfg.bit0.level1 = 0;
    byteCfg.bit0.duration1 = static_cast<uint16_t>(periodTicks - t0hTicks);
    byteCfg.bit1.level0 = 1;
    byteCfg.bit1.duration0 = t1hTicks;
    byteCfg.bit1.level1 = 0;
    byteCfg.bit1.duration1 = static_cast<uint16_t>(periodTicks - t1hTicks);
    byteCfg.flags.msb_first = 1;
    if (rmt_new_bytes_encoder(&byteCfg, &st->byteEncoder) != ESP_OK) {
        rmt_del_encoder(st->symbolEncoder);
        rmt_del_channel(st->channel);
        delete st;
        return false;
    }

    if (rmt_enable(st->channel) != ESP_OK) {
        rmt_del_encoder(st->byteEncoder);
        rmt_del_encoder(st->symbolEncoder);
        rmt_del_channel(st->channel);
        delete st;
        return false;
    }

    st->resolutionHz = resolutionHz;
    h.impl = st;
    return true;
}

uint32_t rmtWs2812Resolution(const RmtWs2812Handle& h) MM_NONBLOCKING {
    auto* st = static_cast<RmtTxState*>(h.impl);
    return st ? st->resolutionHz : 0;
}

bool rmtWs2812Transmit(RmtWs2812Handle& h, const uint32_t* symbols, size_t symbolCount) {
    auto* st = static_cast<RmtTxState*>(h.impl);
    if (!st || !symbols || symbolCount == 0) return false;

    rmt_transmit_config_t txCfg = {};
    txCfg.loop_count = 0;   // single shot, no hardware loop

    // Our symbols are already rmt_symbol_word_t-shaped; the copy encoder takes a
    // byte size. This only *starts* the transfer — channels started back-to-back
    // clock out concurrently, which is what makes a multi-pin frame cost the
    // longest strand instead of the sum. The caller pairs this with
    // rmtWs2812Wait and owns the inter-frame latch after the last wait.
    return rmt_transmit(st->channel, st->symbolEncoder, symbols,
                        symbolCount * sizeof(uint32_t), &txCfg) == ESP_OK;
}

bool rmtWs2812TransmitBytes(RmtWs2812Handle& h, const uint8_t* bytes, size_t byteCount) {
    auto* st = static_cast<RmtTxState*>(h.impl);
    if (!st || !bytes || byteCount == 0) return false;

    rmt_transmit_config_t txCfg = {};
    txCfg.loop_count = 0;
    return rmt_transmit(st->channel, st->byteEncoder, bytes, byteCount, &txCfg) == ESP_OK;
}

bool rmtWs2812Wait(RmtWs2812Handle& h, uint32_t timeoutMs) {
    auto* st = static_cast<RmtTxState*>(h.impl);
    if (!st) return true;
    // Finite timeout so a wedged DMA can't hang the render tick forever. Even the
    // longest realistic frame (thousands of pixels) clocks out well under 1 s.
    // On timeout the caller retains the source frame and polls again, because the
    // byte encoder or DMA can still hold a pointer into that frame.
    //
    // We deliberately do NOT cancel a timed-out transfer with rmt_disable(): on
    // classic ESP32, rmt_disable() while a transmission is still active triggers an
    // interrupt-WDT panic (espressif/esp-idf#17692, classic-only — S3/C6/P4 are
    // unaffected). A panic is a worse failure than a dropped frame, so we leave the
    // stuck transfer alone. The driver keeps its frame immutable until a later
    // poll reports completion, preventing a half-old/half-new wire frame.
    return rmt_tx_wait_all_done(st->channel, timeoutMs) == ESP_OK;
}

bool rmtWs2812Deinit(RmtWs2812Handle& h) {
    auto* st = static_cast<RmtTxState*>(h.impl);
    if (!st) return true;
    // The encoder borrows the caller's byte frame. Disabling an active classic-ESP32 channel can
    // panic in the interrupt watchdog, and freeing that frame first is a DMA use-after-free on every
    // target. A bounded failed teardown therefore keeps the complete channel state intact so the
    // owner can retry after a later completion poll.
    if (!rmtWs2812Wait(h, 1000)) return false;
    if (st->channel) {
        rmt_disable(st->channel);
        rmt_del_channel(st->channel);
    }
    if (st->byteEncoder) rmt_del_encoder(st->byteEncoder);
    if (st->symbolEncoder) rmt_del_encoder(st->symbolEncoder);
    delete st;
    h.impl = nullptr;
    return true;
}

// ---------------------------------------------------------------------------
// RX loopback capture — on-device test only. Opens a one-shot RX channel on the
// jumpered pin, captures raw pulse symbols, returns how many landed. The test
// decodes those symbols back to bytes and asserts == sent.
// ---------------------------------------------------------------------------

namespace {

// done-callback hands the received symbol count to the waiting capture call via
// a 1-deep queue. IRAM so it survives a cache-disabled window.
struct RxDone { size_t numSymbols; };

bool IRAM_ATTR rmtRxDoneCb(rmt_channel_handle_t, const rmt_rx_done_event_data_t* edata,
                           void* user) {
    QueueHandle_t q = static_cast<QueueHandle_t>(user);
    RxDone d = { edata->num_symbols };
    BaseType_t high = pdFALSE;
    xQueueSendFromISR(q, &d, &high);
    return high == pdTRUE;
}

} // namespace

size_t rmtWs2812RxCapture(uint8_t gpio, uint32_t resolutionHz,
                          uint32_t* outSymbols, size_t maxSymbols, uint32_t timeoutMs) {
    if (!outSymbols || maxSymbols == 0) return 0;

    rmt_rx_channel_config_t rxCfg = {};
    rxCfg.gpio_num = static_cast<gpio_num_t>(gpio);
    rxCfg.clk_src = RMT_CLK_SRC_DEFAULT;
    rxCfg.resolution_hz = resolutionHz;
    // The RX channel's internal memory block must be even and >= one hardware
    // block (IDF requirement; 64 words classic, 48 on the S3 — a hardcoded 64
    // would silently claim part of a second S3 channel's memory). Round
    // maxSymbols up to that floor; the actual capture buffer (outSymbols /
    // maxSymbols) is separate and can be smaller.
    size_t memBlock = static_cast<size_t>(maxSymbols);
    if (memBlock < SOC_RMT_MEM_WORDS_PER_CHANNEL) memBlock = SOC_RMT_MEM_WORDS_PER_CHANNEL;
    if (memBlock & 1) memBlock++;
#if SOC_RMT_SUPPORT_DMA
    // A capture larger than one hardware block (whole-frame captures, e.g. the
    // LCD loopback's full-frame check) uses the DMA backend, which can stream
    // an arbitrarily large mem_block. Caller's buffer must then be DMA-capable
    // internal RAM.
    rxCfg.flags.with_dma = maxSymbols > SOC_RMT_MEM_WORDS_PER_CHANNEL;
#else
    // No RMT DMA (classic ESP32): mem_block_symbols larger than one hardware
    // channel silently claims neighbouring channels' memory and fails to
    // allocate ("no free rx channels"). Cap to a single channel — the caller
    // gets at most one channel's worth of symbols per capture. A whole-frame
    // check on such a chip must therefore use a frame that fits one channel
    // (the frame loopback sizes itself to maxLaneLights accordingly).
    if (memBlock > SOC_RMT_MEM_WORDS_PER_CHANNEL)
        memBlock = SOC_RMT_MEM_WORDS_PER_CHANNEL;
#endif
    rxCfg.mem_block_symbols = memBlock;

    rmt_channel_handle_t rxChan = nullptr;
    if (rmt_new_rx_channel(&rxCfg, &rxChan) != ESP_OK) return 0;

    QueueHandle_t q = xQueueCreate(1, sizeof(RxDone));
    if (!q) { rmt_del_channel(rxChan); return 0; }

    rmt_rx_event_callbacks_t cbs = {};
    cbs.on_recv_done = rmtRxDoneCb;
    rmt_rx_register_event_callbacks(rxChan, &cbs, q);

    // Accept WS2812 pulse widths: anything from a fraction of T0H up to well past
    // a bit cell, so glitches are filtered but real 0/1 pulses pass.
    rmt_receive_config_t rcfg = {};
    rcfg.signal_range_min_ns = 100;       // shorter than any real WS2812 edge
    rcfg.signal_range_max_ns = 100000;    // longer than a bit cell; ends the frame

    size_t got = 0;
    if (rmt_enable(rxChan) == ESP_OK) {
        // Once enabled, the channel must be disabled before delete — even if
        // rmt_receive or the wait fails — or rmt_del_channel rejects it.
        if (rmt_receive(rxChan, outSymbols, maxSymbols * sizeof(uint32_t), &rcfg) == ESP_OK) {
            RxDone d = {};
            if (xQueueReceive(q, &d, pdMS_TO_TICKS(timeoutMs)) == pdTRUE) {
                got = d.numSymbols;
            }
        }
        rmt_disable(rxChan);
    }

    vQueueDelete(q);
    rmt_del_channel(rxChan);
    return got;
}


// ---------------------------------------------------------------------------
// Loopback self-test (runnable from the live firmware via RmtLedDriver's
// loopbackTest control). TX a known WS2812 pattern on txGpio, capture it back on
// rxGpio (user jumpers them), decode, compare. The WS2812 symbol build is inlined
// here (trivial — two symbol shapes) so the platform stays self-contained and
// src/light/ keeps no platform dependency.
// ---------------------------------------------------------------------------

namespace {

constexpr uint32_t kLoopbackResHz = 40'000'000;  // 25 ns/tick, same as the driver
constexpr uint16_t kT0H = 14, kT1H = 28, kPeriod = 50;  // 350/700/1250 ns in ticks

} // namespace

namespace detail {

// Plain-GPIO continuity check: drive tx, read rx. Separates "wire wrong" from
// "RMT/LCD wrong" so a failed jumper is reported clearly. Shared with the LCD
// loopback in platform_esp32_i80.cpp (declared there), hence not anonymous.
bool loopbackJumperOk(uint8_t txGpio, uint8_t rxGpio) {
    gpio_set_direction(static_cast<gpio_num_t>(txGpio), GPIO_MODE_OUTPUT);
    gpio_set_direction(static_cast<gpio_num_t>(rxGpio), GPIO_MODE_INPUT);
    gpio_set_pull_mode(static_cast<gpio_num_t>(rxGpio), GPIO_PULLDOWN_ONLY);
    gpio_set_level(static_cast<gpio_num_t>(txGpio), 1);
    ets_delay_us(2000);
    int hi = gpio_get_level(static_cast<gpio_num_t>(rxGpio));
    gpio_set_level(static_cast<gpio_num_t>(txGpio), 0);
    ets_delay_us(2000);
    int lo = gpio_get_level(static_cast<gpio_num_t>(rxGpio));
    gpio_reset_pin(static_cast<gpio_num_t>(txGpio));
    gpio_reset_pin(static_cast<gpio_num_t>(rxGpio));
    // Log the raw levels — this one line is a genuine bench HAL diagnostic (it pinned the
    // MHC-WLED P4 shield loopback: hi=0 lo=0 = no signal path, hi=1 lo=1 = the Rx pin is
    // externally pulled up, hi=1 lo=0 = clean). Only runs when the loopback self-test is
    // invoked (off the hot path), so it costs nothing in normal operation.
    ESP_LOGI("mm_loopback", "continuity tx=%u->rx=%u: hi=%d lo=%d (want hi=1 lo=0)",
             txGpio, rxGpio, hi, lo);
    return hi == 1 && lo == 0;
}

// Shared frame-capture + bit-verify for the two parallel LED loopbacks (LCD_CAM
// i80 and Parlio). They differ only in the transmit call (esp_lcd_panel_io_tx_color
// vs parlio_tx_unit_transmit) and the private-bus state type; everything else —
// the capture buffer, the RX task, the timed-first/back-to-back transmit cadence,
// and the whole per-bit verification — was byte-for-byte identical, so it lives
// here once. The caller has already done the jumper pre-check and built its
// private TX bus on the data pins; it passes `transmitOnce` (transmit the frame
// AND wait for its done-callback) and the params needed to size the capture and
// log the granted clock. `r` is filled in place (jumperDetected already set).
// The capture buffer: one symbol per WS2812 bit plus slack, 64-aligned, DMA-capable internal — the
// single biggest contiguous block the loopback needs, which is why callers may allocate it FIRST
// (before their private bus fragments the heap) and hand it in via `rxSymbols`.
uint32_t* allocLoopbackCapture(size_t dataBytes) {
    const size_t capMax = dataBytes / 3 + 16;
    return static_cast<uint32_t*>(heap_caps_aligned_alloc(
        64, capMax * sizeof(uint32_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
}

// Intrusive two-owner context for an RX task and its caller. The task may finish after the caller's
// bounded diagnostic wait; reference counting keeps both the context and DMA capture buffer alive
// until the last side exits. `done` is the release/acquire publication point for the written symbols.
struct LoopbackCapture {
    std::atomic<uint8_t> refs{1};
    uint8_t rxGpio = 0;
    uint32_t resolutionHz = 0;
    uint32_t* symbols = nullptr;
    size_t capacity = 0;
    size_t need = 0;
    bool ride = false;
    std::atomic<size_t> got{0};
    std::atomic<bool> done{false};

    void retain() { refs.fetch_add(1, std::memory_order_relaxed); }
    void release() {
        if (refs.fetch_sub(1, std::memory_order_acq_rel) != 1) return;
        heap_caps_free(symbols);
        delete this;
    }
};

LoopbackCapture* makeLoopbackCapture(uint8_t rxGpio, uint32_t resolutionHz,
                                     size_t capacity, size_t need, bool ride,
                                     uint32_t* symbols = nullptr) {
    if (!symbols) {
        symbols = static_cast<uint32_t*>(heap_caps_aligned_alloc(
            64, capacity * sizeof(uint32_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    }
    if (!symbols) return nullptr;
    auto* capture = new (std::nothrow) LoopbackCapture();
    if (!capture) { heap_caps_free(symbols); return nullptr; }
    capture->rxGpio = rxGpio;
    capture->resolutionHz = resolutionHz;
    capture->symbols = symbols;
    capture->capacity = capacity;
    capture->need = need;
    capture->ride = ride;
    return capture;
}

bool startLoopbackCapture(LoopbackCapture& capture, const char* taskName) {
    capture.retain();   // task ownership is published before the task can start
    auto task = [](void* arg) {
        auto* c = static_cast<LoopbackCapture*>(arg);
        const int attempts = c->ride ? 40 : 1;
        const uint32_t perArmTimeoutMs = c->ride ? 100 : 1000;
        size_t fullest = 0;
        for (int attempt = 0; attempt < attempts; attempt++) {
            const size_t got = rmtWs2812RxCapture(
                c->rxGpio, c->resolutionHz, c->symbols, c->capacity, perArmTimeoutMs);
            if (got > fullest) fullest = got;
            if (got >= c->need) break;
        }
        c->got.store(fullest, std::memory_order_relaxed);
        c->done.store(true, std::memory_order_release);
        c->release();
        vTaskDelete(nullptr);
    };
    if (xTaskCreate(task, taskName, 4096, &capture, 5, nullptr) == pdPASS) return true;
    capture.release();   // task creation failed: return its reserved ownership
    return false;
}

void captureAndVerifyFrame(uint16_t rxGpio, size_t frameBytes, size_t dataBytes,
                           uint8_t rowBits, uint32_t pclkHz, bool pinExpanderMode, const char* tag,
                           const std::function<void()>& transmitOnce,
                           RmtLoopbackResult& r, bool rideMode, uint32_t* rxSymbols) {
    // Capture at 40 MHz. The decode threshold is DERIVED from the strand's slot rate, not a
    // constant: a "0" is HIGH for one slot, a "1" for two, so the midpoint (1.5 slots) separates
    // them at ANY rate — 375 ns direct slots give 15/30 ticks (threshold 22), the shift expander's
    // 300 ns slots give 12/24 (threshold 18). A hardcoded direct-mode threshold of 25 sat ABOVE the
    // shift-mode "1" (24 ticks), decoding every 1-bit as 0 — the first pattern bit failed and the
    // verdict blamed the transport for a decode fault. One symbol per WS2812 bit; the frame's
    // zeroed latch pad is the >100 µs idle that ends the capture.
    constexpr uint32_t kCapResHz = 40'000'000;
    const uint16_t slotTicks = static_cast<uint16_t>(kCapResHz / pclkHz);
    const uint16_t threshTicks = static_cast<uint16_t>(slotTicks + slotTicks / 2);
    const size_t kBits = dataBytes / 3;
    const size_t capMax = kBits + 16;
    auto* cap = makeLoopbackCapture(static_cast<uint8_t>(rxGpio), kCapResHz,
                                    capMax, kBits, rideMode, rxSymbols);
    if (!cap) {
        ESP_LOGE(tag, "loopback: capture buffer alloc failed (%u B)",
                 (unsigned)(capMax * sizeof(uint32_t)));
        return;
    }
    rxSymbols = cap->symbols;
    // Each rmt_receive captures ONE run of pulses ending at the next >100 µs gap (the WS2812 reset). With a
    // controlled transmit the run starts at frame start, so one arm yields the whole frame. RIDING a
    // free-running pipeline, an arm lands mid-frame and captures only the tail (< kBits) before the reset —
    // so re-arm until a run of >= kBits arrives, i.e. an arm that happened to land at/before a frame start.
    // The ring transmits continuously (~100 fps), so a full frame is caught within a few arms; bounded so a
    // dead wire still times out instead of looping forever.
    const bool taskStarted = startLoopbackCapture(*cap, "lblb");
    if (taskStarted) {
        vTaskDelay(pdMS_TO_TICKS(50));
        // First transmit timed — the wall time of a known byte count confirms the
        // granted pixel clock matches the configured slot rate (the bus driver
        // doesn't expose the granted clock directly).
        {
            const int64_t t0 = esp_timer_get_time();
            transmitOnce();
            const int64_t dt = esp_timer_get_time() - t0;
            r.txWallUs = static_cast<uint32_t>(dt);
            // Expected wire time from the STRAND's view (unit-safe at any bus width / fan-out):
            // kBits WS2812 bits × 3 slots each ÷ the slot rate. frameBytes ÷ pclkHz would mix
            // units — frameBytes counts BUS bytes while pclkHz here is the slot rate, which
            // overstates the expectation 8× in shift mode.
            r.txExpectUs = static_cast<uint32_t>(kBits * 3ull * 1000000ull / pclkHz);
            ESP_LOGI(tag, "loopback: %u bytes in %lld us (expect ~%u us at %u Hz slot rate)",
                     (unsigned)frameBytes, (long long)dt,
                     (unsigned)r.txExpectUs, (unsigned)pclkHz);
        }
        // Back-to-back frames, exactly the render loop's transmit/wait cadence.
        for (int i = 0; i < 100 && !cap->done.load(std::memory_order_acquire); i++) transmitOnce();
        // Wait for the capture task. Ride mode re-arms internally (each arm returns in ~1 frame when the
        // pipeline is live), so give it a longer ceiling than the controlled-transmit path — a live frame is
        // caught in well under this, and a dead wire still ends when the task exhausts its bounded retries.
        const int waitTicks = rideMode ? 600 : 200;   // ×10 ms = 6 s (ride) / 2 s (controlled)
        for (int i = 0; i < waitTicks && !cap->done.load(std::memory_order_acquire); i++)
            vTaskDelay(pdMS_TO_TICKS(10));
    }
    const bool captureDone = cap->done.load(std::memory_order_acquire);
    const size_t captured = cap->got.load(std::memory_order_relaxed);
    r.capturedSymbols = static_cast<uint32_t>(captured);
    r.rxIdleLevel = static_cast<int8_t>(gpio_get_level(static_cast<gpio_num_t>(rxGpio)));
    ESP_LOGI(tag, "loopback: rx captured %u symbols (need %u), idle rx level=%d",
             (unsigned)captured, (unsigned)kBits, (int)r.rxIdleLevel);

    if (captureDone && captured >= kBits) {
        // Verify EVERY bit of the frame against the per-row pattern (r.sent[],
        // zero-padded for RGBW rows), not just the first light.
        size_t mismatch = SIZE_MAX;
        uint16_t minH[2] = {0x7FFF, 0x7FFF}, maxH[2] = {0, 0};
        size_t mismatchCount = 0;
        for (size_t b = 0; b < kBits; b++) {
            const uint16_t high = static_cast<uint16_t>(rxSymbols[b] & 0x7FFF);
            const uint8_t bit = (high >= threshTicks) ? 1 : 0;
            if (high < minH[bit]) minH[bit] = high;
            if (high > maxH[bit]) maxH[bit] = high;
            const uint8_t rowPos = static_cast<uint8_t>(b % rowBits);
            const uint8_t expByte = (rowPos / 8u) < 3 ? r.sent[rowPos / 8u] : 0x00;
            const uint8_t exp = (expByte >> (7 - (rowPos & 7))) & 1u;
            if (bit != exp) { if (mismatch == SIZE_MAX) mismatch = b; mismatchCount++; }
        }
        // r.got[] reports the row holding the first mismatch (row 0 when clean).
        const size_t rowStart = (mismatch == SIZE_MAX)
                                    ? 0 : mismatch - (mismatch % rowBits);
        for (size_t b = rowStart; b < rowStart + 24 && b < captured; b++) {
            const uint8_t bit = ((rxSymbols[b] & 0x7FFF) >= threshTicks) ? 1 : 0;
            r.got[(b - rowStart) / 8] =
                static_cast<uint8_t>((r.got[(b - rowStart) / 8] << 1) | bit);
        }
        // The FRAME'S FIRST pulse is one slot short with a '595 expander: the register's outputs are
        // still settling as the first RCLK latch fires, so bit 0 of light 0 comes back ~12 ticks (a
        // "0") when the strand sent a "1". Measured on strand 15: EXACTLY 1 mismatch in 2304, always
        // bit 0, always short-clipped — the other 2303 bits and both pulse-width classes are textbook.
        // It costs the very first pixel's most-significant color bit and nothing else (invisible), so a
        // lone short-clipped bit 0 is the '595's frame-start settling, not bad output — accept it. Any
        // second mismatch, or a bit-0 miss that is not short-clipped, still fails. Direct mode drives
        // the pin straight (no latch) so its bit 0 is clean — the exception is gated on `pinExpanderMode` so a
        // real first-bit fault on the direct i80 / Parlio paths can never be excused through it.
        const bool onlyBit0Clip = pinExpanderMode && mismatchCount == 1 && mismatch == 0
                                && (static_cast<uint16_t>(rxSymbols[0] & 0x7FFF) < threshTicks);
        r.pass = (mismatch == SIZE_MAX) || onlyBit0Clip;
        r.bitsChecked = static_cast<uint32_t>(kBits);
        r.firstBadBit = r.pass ? static_cast<uint32_t>(kBits) : static_cast<uint32_t>(mismatch);
        ESP_LOGI(tag, "loopback: high ticks — 0-bits %u..%u, 1-bits %u..%u (25ns/tick, threshold %u)",
                 (unsigned)minH[0], (unsigned)maxH[0], (unsigned)minH[1], (unsigned)maxH[1],
                 (unsigned)threshTicks);
        if (!r.pass) {
            ESP_LOGE(tag, "loopback: first bad bit %u (light %u, bit-in-row %u), %u/%u bits bad; row0 got %02x%02x%02x exp %02x%02x%02x",
                     (unsigned)mismatch, (unsigned)(mismatch / rowBits),
                     (unsigned)(mismatch % rowBits), (unsigned)mismatchCount, (unsigned)kBits,
                     r.got[0], r.got[1], r.got[2], r.sent[0], r.sent[1], r.sent[2]);
        }
    }
    if (taskStarted && !captureDone)
        ESP_LOGE(tag, "loopback: RX task exceeded the diagnostic wait; task ownership retains its capture");
    cap->release();
}

} // namespace detail

// INTRUSIVE loopback (driver-agnostic; see platform.h). No bus, no transmit of our own — the live pipeline
// is already clocking `sent` past `rxGpio` every frame, so we only arm the RMT-RX (a NO-OP transmitOnce) and
// let one of those live frames land in the capture, then bit-verify it. Shares detail::captureAndVerifyFrame
// with every family's own loopback, so the decode/threshold/verdict logic is identical whatever drove the
// wire. Zero extra RAM — the reason this exists over the private-bus loopback that fragments the heap.
RmtLoopbackResult ws2812LoopbackRide(uint16_t rxGpio, const uint8_t* sent, uint8_t sentLen,
                                     size_t dataBytes, uint8_t rowBits, uint8_t clockMultiplier) {
    RmtLoopbackResult r;
    if (!sent || sentLen == 0 || sentLen > 3 || dataBytes < 3 || rowBits < 8 || clockMultiplier == 0)
        return r;
    for (uint8_t i = 0; i < sentLen; i++) r.sent[i] = sent[i];   // the per-light pattern to verify
    r.jumperDetected = true;   // proven by the bit-verify itself, not a plain-GPIO continuity pre-check
    // The STRAND's slot rate (what the RX sees), from the WS2812 physical timing every family shares: direct
    // slots at kSlotHz; an expander fits `clockMultiplier` bus words per slot, so the slot rate is the fast
    // bus clock ÷ multiplier. Same values the per-family loopbacks derive from their own kPclkHz/kShiftPclkHz
    // — canonical WS2812 timing, so the driver-agnostic ride carries them here rather than taking a family's.
    constexpr uint32_t kSlotHz = 2'666'666;         // direct-mode WS2812 slot (375 ns)
    constexpr uint32_t kShiftBusHz = 26'666'666;    // expander bus clock (300 ns slot at ÷8)
    const bool pinExpanderMode = clockMultiplier > 1;
    const uint32_t slotHz = pinExpanderMode ? (kShiftBusHz / clockMultiplier) : kSlotHz;
    auto noTransmit = []() {};  // the render loop is the transmitter
    detail::captureAndVerifyFrame(rxGpio, dataBytes, dataBytes, rowBits, slotHz, pinExpanderMode,
                                  "ws2812-ride", noTransmit, r, /*rideMode=*/true, /*rxSymbols=*/nullptr);
    return r;
}

RmtLoopbackResult rmtWs2812Loopback(uint8_t txGpio, uint8_t rxGpio) {
    RmtLoopbackResult r;
    r.sent[0] = 0xA5; r.sent[1] = 0x00; r.sent[2] = 0xFF;  // recognisable pattern

    r.jumperDetected = detail::loopbackJumperOk(txGpio, rxGpio);
    if (!r.jumperDetected) return r;   // no point running RMT through a dead wire

    // Build 24 symbols (3 bytes × 8 bits, MSB-first) for the pattern.
    const uint32_t sym0 = static_cast<uint32_t>(kT0H) | (1u << 15)
                        | (static_cast<uint32_t>(kPeriod - kT0H) << 16);
    const uint32_t sym1 = static_cast<uint32_t>(kT1H) | (1u << 15)
                        | (static_cast<uint32_t>(kPeriod - kT1H) << 16);
    constexpr size_t kBits = 24;
    auto* txSymbols = static_cast<uint32_t*>(heap_caps_malloc(
        kBits * sizeof(uint32_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!txSymbols) return r;
    size_t s = 0;
    for (int b = 0; b < 3; b++)
        for (int bit = 7; bit >= 0; bit--)
            txSymbols[s++] = (r.sent[b] & (1u << bit)) ? sym1 : sym0;

    RmtWs2812Handle tx;
    if (!rmtWs2812Init(tx, txGpio, kLoopbackResHz, /*invert=*/false)) {
        heap_caps_free(txSymbols);
        return r;
    }

    // RX must be listening while we transmit; run the blocking capture in a task and resend the
    // short frame until the receiver latches one or we give up. The task owns a reference to its
    // context, so a late RX timeout cannot outlive caller stack or DMA-buffer storage.
    constexpr size_t kCapMax = kBits + 8;
    auto* cap = detail::makeLoopbackCapture(rxGpio, kLoopbackResHz, kCapMax, kBits,
                                            /*ride=*/false);
    if (!cap) {
        releaseLoopbackTransfer(tx, txSymbols);
        return r;
    }
    const bool taskStarted = detail::startLoopbackCapture(*cap, "rmtlb");
    if (taskStarted) {
        vTaskDelay(pdMS_TO_TICKS(50));
        for (int i = 0; i < 50 && !cap->done.load(std::memory_order_acquire); i++) {
            if (rmtWs2812Transmit(tx, txSymbols, kBits)
                && !rmtWs2812Wait(tx, 1000)) break;
            ets_delay_us(300);   // inter-frame latch
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        for (int i = 0; i < 200 && !cap->done.load(std::memory_order_acquire); i++)
            vTaskDelay(pdMS_TO_TICKS(10));
    }
    releaseLoopbackTransfer(tx, txSymbols);

    const bool captureDone = cap->done.load(std::memory_order_acquire);
    const size_t captured = cap->got.load(std::memory_order_relaxed);
    r.capturedSymbols = static_cast<uint32_t>(captured);
    r.rxIdleLevel = static_cast<int8_t>(gpio_get_level(static_cast<gpio_num_t>(rxGpio)));
    if (captureDone && captured >= kBits) {
        // Decode the first 24 captured symbols → bytes (HIGH closer to T1H = 1).
        for (size_t b = 0; b < kBits; b++) {
            uint16_t high = static_cast<uint16_t>(cap->symbols[b] & 0x7FFF);
            uint8_t bit = (high >= ((kT0H + kT1H) / 2)) ? 1 : 0;
            r.got[b / 8] = static_cast<uint8_t>((r.got[b / 8] << 1) | bit);
        }
        r.pass = (r.got[0] == r.sent[0] && r.got[1] == r.sent[1] && r.got[2] == r.sent[2]);
    }
    r.bitsChecked = static_cast<uint32_t>(kBits);
    r.firstBadBit = r.pass ? static_cast<uint32_t>(kBits) : 0;
    if (taskStarted && !captureDone)
        ESP_LOGE("rmt-loopback", "RX task exceeded the diagnostic wait; task ownership retains its capture");
    cap->release();
    return r;
}

// Whole-frame variant: transmit a real `lights`-light frame back to back and
// bit-verify the WHOLE capture. The per-light pattern is 0xA5/0x00/0xFF (the
// sent[] bytes), zero-padded for any 4th (white) channel, repeated for every
// light. Unlike the 24-bit burst above, this drives the sustained DMA path and
// a long wire under whatever RF the device is doing — so it catches the
// frame-rate corruption and interference the short test is blind to.
RmtLoopbackResult rmtWs2812LoopbackFrame(uint8_t txGpio, uint8_t rxGpio,
                                         uint16_t lights, uint8_t channels) {
    RmtLoopbackResult r;
    r.sent[0] = 0xA5; r.sent[1] = 0x00; r.sent[2] = 0xFF;
    if (lights == 0 || channels < 3 || channels > 4) return r;

    r.jumperDetected = detail::loopbackJumperOk(txGpio, rxGpio);
    if (!r.jumperDetected) return r;

    const uint32_t sym0 = static_cast<uint32_t>(kT0H) | (1u << 15)
                        | (static_cast<uint32_t>(kPeriod - kT0H) << 16);
    const uint32_t sym1 = static_cast<uint32_t>(kT1H) | (1u << 15)
                        | (static_cast<uint32_t>(kPeriod - kT1H) << 16);
    const uint8_t bitsPerLight = static_cast<uint8_t>(channels * 8);
#if !SOC_RMT_SUPPORT_DMA
    // No RMT DMA (classic ESP32): the RX capture can hold at most one hardware
    // channel's symbols, so cap the verified frame to what fits whole lights in
    // that block. The frame is still transmitted back to back (the sustained-
    // output stress that exposes RF interference); we just verify a prefix that
    // the no-DMA receiver can actually capture.
    const uint16_t maxLights =
        static_cast<uint16_t>(SOC_RMT_MEM_WORDS_PER_CHANNEL / bitsPerLight);
    if (lights > maxLights) lights = maxLights ? maxLights : 1;
#endif
    const size_t kBits = static_cast<size_t>(lights) * bitsPerLight;

    // One real frame's worth of symbols, DMA-capable internal RAM (the same
    // place the driver's own symbol buffer lives). Off the hot path — this is
    // a control-driven self-test.
    auto* txSymbols = static_cast<uint32_t*>(heap_caps_malloc(
        kBits * sizeof(uint32_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    const size_t capMax = kBits + 16;
    if (!txSymbols) {
        heap_caps_free(txSymbols);
        return r;
    }
    size_t s = 0;
    for (uint16_t light = 0; light < lights; light++)
        for (uint8_t ch = 0; ch < channels; ch++) {
            const uint8_t byte = ch < 3 ? r.sent[ch] : 0x00;
            for (int bit = 7; bit >= 0; bit--)
                txSymbols[s++] = (byte & (1u << bit)) ? sym1 : sym0;
        }

    RmtWs2812Handle tx;
    if (!rmtWs2812Init(tx, txGpio, kLoopbackResHz, /*invert=*/false)) {
        heap_caps_free(txSymbols);
        return r;
    }

    auto* cap = detail::makeLoopbackCapture(rxGpio, kLoopbackResHz, capMax, kBits,
                                            /*ride=*/false);
    if (!cap) {
        releaseLoopbackTransfer(tx, txSymbols);
        return r;
    }
    const bool taskStarted = detail::startLoopbackCapture(*cap, "rmtlbf");
    if (taskStarted) {
        vTaskDelay(pdMS_TO_TICKS(50));
        // Back-to-back frames, the render loop's cadence. The capture latches
        // one whole frame; we keep resending so it can't miss the window.
        for (int i = 0; i < 100 && !cap->done.load(std::memory_order_acquire); i++) {
            if (rmtWs2812Transmit(tx, txSymbols, kBits)
                && !rmtWs2812Wait(tx, 1000)) break;
            ets_delay_us(300);   // inter-frame WS2812 latch
        }
        for (int i = 0; i < 200 && !cap->done.load(std::memory_order_acquire); i++)
            vTaskDelay(pdMS_TO_TICKS(10));
    }
    releaseLoopbackTransfer(tx, txSymbols);

    const bool captureDone = cap->done.load(std::memory_order_acquire);
    const size_t captured = cap->got.load(std::memory_order_relaxed);
    r.capturedSymbols = static_cast<uint32_t>(captured);
    r.rxIdleLevel = static_cast<int8_t>(gpio_get_level(static_cast<gpio_num_t>(rxGpio)));
    if (captureDone && captured >= kBits) {
        size_t mismatch = SIZE_MAX;
        for (size_t b = 0; b < kBits; b++) {
            const uint16_t high = static_cast<uint16_t>(cap->symbols[b] & 0x7FFF);
            const uint8_t bit = (high >= ((kT0H + kT1H) / 2)) ? 1 : 0;
            const uint8_t pos = static_cast<uint8_t>(b % bitsPerLight);
            const uint8_t expByte = (pos / 8u) < 3 ? r.sent[pos / 8u] : 0x00;
            const uint8_t exp = (expByte >> (7 - (pos & 7))) & 1u;
            if (bit != exp && mismatch == SIZE_MAX) mismatch = b;
        }
        r.pass = (mismatch == SIZE_MAX);
        r.bitsChecked = static_cast<uint32_t>(kBits);
        r.firstBadBit = (mismatch == SIZE_MAX) ? static_cast<uint32_t>(kBits)
                                               : static_cast<uint32_t>(mismatch);
        // got[] = the light holding the first mismatch (light 0 when clean).
        const size_t badLight = (mismatch == SIZE_MAX) ? 0 : mismatch / bitsPerLight;
        const size_t lightStart = badLight * bitsPerLight;
        for (size_t b = 0; b < 24 && lightStart + b < captured; b++) {
            const uint8_t bit = ((cap->symbols[lightStart + b] & 0x7FFF)
                                 >= ((kT0H + kT1H) / 2)) ? 1 : 0;
            r.got[b / 8] = static_cast<uint8_t>((r.got[b / 8] << 1) | bit);
        }
    }
    if (taskStarted && !captureDone)
        ESP_LOGE("rmt-loopback-frame", "RX task exceeded the diagnostic wait; task ownership retains its capture");
    cap->release();
    return r;
}

} // namespace mm::platform
