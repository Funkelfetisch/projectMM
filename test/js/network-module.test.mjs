import assert from "node:assert/strict";
import test from "node:test";
import { readFileSync } from "node:fs";

test("NetworkModule retries stored WiFi credentials only while the setup AP is unused", () => {
    const source = readFileSync(new URL("../../src/core/NetworkModule.h", import.meta.url), "utf8");
    const start = source.indexOf("case State::AP:");
    const end = source.indexOf("case State::Idle:", start);
    const body = source.slice(start, end);

    assert.ok(start >= 0 && end > start);
    assert.match(source, /static constexpr uint32_t kApRetryStaMs = 60000;/);
    assert.match(body, /now - stateChangeTime_ > kApRetryStaMs\s*&& platform::wifiApClientCount\(\) == 0/);
    assert.match(body, /platform::wifiStaInit\(ssid_, password_\)/);
});

test("ESP32 WiFi reconnects unexpected STA drops and tracks setup AP clients", () => {
    const source = readFileSync(new URL("../../src/platform/esp32/platform_esp32.cpp", import.meta.url), "utf8");
    const start = source.indexOf("static void wifiEventHandler");
    const end = source.indexOf("static bool ensureWifiInit", start);
    const body = source.slice(start, end);

    assert.ok(start >= 0 && end > start);
    assert.match(body, /!wifiStaStopping_\.load\(std::memory_order_relaxed\)/);
    assert.match(body, /esp_wifi_connect\(\);/);
    assert.match(body, /WIFI_EVENT_AP_STACONNECTED[\s\S]*apClients_\.fetch_add/);
    assert.match(body, /WIFI_EVENT_AP_STADISCONNECTED[\s\S]*apClients_\.fetch_sub/);
});

test("NetworkModule activates edited WiFi credentials only through connectWifi", () => {
    const source = readFileSync(new URL("../../src/core/NetworkModule.h", import.meta.url), "utf8");
    const start = source.indexOf("void onControlChanged(const char* controlName) override");
    const end = source.indexOf("void tick1s()", start);
    const body = source.slice(start, end);

    assert.ok(start >= 0 && end > start);
    assert.match(source, /controls_\.addButton\("connectWifi"\);/);
    assert.match(body, /strcmp\(controlName, "connectWifi"\) == 0[\s\S]*setWifiCredentials\(ssid_, password_\)/);
    assert.doesNotMatch(body, /strcmp\(controlName, "ssid"\)/);
    assert.doesNotMatch(body, /strcmp\(controlName, "password"\)/);
});

test("device UI stages WiFi credentials and preserves an existing set-only password", () => {
    const source = readFileSync(new URL("../../src/ui/app.js", import.meta.url), "utf8");
    const textStart = source.indexOf('case "text":');
    const textEnd = source.indexOf('case "textarea":', textStart);
    const textBody = source.slice(textStart, textEnd);
    const ssidDraftStart = textBody.indexOf("else if (wifiSsid)");
    const ssidDraftEnd = textBody.indexOf("} else {", ssidDraftStart);
    const ssidDraftBody = textBody.slice(ssidDraftStart, ssidDraftEnd);
    const passwordStart = source.indexOf('case "password":');
    const passwordEnd = source.indexOf('case "select":', passwordStart);
    const passwordBody = source.slice(passwordStart, passwordEnd);
    const passwordDraftStart = passwordBody.indexOf("if (wifiPassword)");
    const passwordDraftEnd = passwordBody.indexOf("} else {", passwordDraftStart);
    const passwordDraftBody = passwordBody.slice(passwordDraftStart, passwordDraftEnd);
    const buttonStart = source.indexOf("const wifiConnect =");
    const buttonEnd = source.indexOf("row.appendChild(btn);", buttonStart);
    const buttonBody = source.slice(buttonStart, buttonEnd);

    assert.match(source, /const wifiCredentialDrafts = new Map\(\);/);
    assert.match(ssidDraftBody, /draft\.ssid = input\.value;[\s\S]*wifiCredentialDrafts\.set\(moduleName, draft\);/);
    assert.doesNotMatch(ssidDraftBody, /debounceSend/);
    assert.match(passwordDraftBody, /draft\.password = input\.value;[\s\S]*wifiCredentialDrafts\.set\(moduleName, draft\);/);
    assert.doesNotMatch(passwordDraftBody, /debounceSend/);
    assert.match(buttonBody, /await sendControl\(moduleName, "ssid", ssid\)/);
    assert.match(buttonBody, /Object\.prototype\.hasOwnProperty\.call\(draft, "password"\)/);
    assert.match(buttonBody, /!hasPasswordDraft \|\|[\s\S]*sendControl\(moduleName, "password", draft\.password\)/);
    assert.match(buttonBody, /await sendControl\(moduleName, "connectWifi", 1\)/);
    assert.doesNotMatch(source, /decodePassword|PW_XOR_KEY/);
    assert.match(passwordBody, /input\.placeholder = ctrl\.value \? "Saved - enter to replace"/);
});
