import { test } from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { existsSync } from "node:fs";
import path from "node:path";

const root = path.resolve(import.meta.dirname, "../..");
const uiDir = path.join(root, "src", "ui");

function importedUiScripts(source) {
    const scripts = new Set();
    const staticRe = /from\s+["']\/([^"']+\.js)(?:\?[^"']*)?["']/g;
    const dynamicRe = /import\(\s*["']\/([^"']+\.js)(?:\?[^"']*)?["']\s*\)/g;
    for (const match of source.matchAll(staticRe)) scripts.add(match[1]);
    for (const match of source.matchAll(dynamicRe)) scripts.add(match[1]);
    return [...scripts].sort();
}

function escapeRegExp(value) {
    return String(value).replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

function sourceSection(source, startNeedle, endNeedle, label = startNeedle) {
    const start = source.indexOf(startNeedle);
    const end = source.indexOf(endNeedle, start);
    assert.ok(start >= 0 && end > start, `${label} body exists`);
    return source.slice(start, end);
}

function embedTokenFor(script, embed) {
    const match = embed.match(new RegExp(`gzip_file_hex\\("${escapeRegExp(script)}"\\s+([A-Z0-9_]+)\\)`));
    assert.ok(match, `${script} is embedded`);
    return match[1];
}

function embedVariableFor(token, embed) {
    const match = embed.match(new RegExp(`hex_to_c_array\\("\\$\\{${escapeRegExp(token)}\\}"\\s+"([A-Za-z0-9_]+)"`));
    assert.ok(match, `${token} converts to a C array`);
    return match[1];
}

test("device UI cache-buster stays in sync across entry modules", async () => {
    const sources = await Promise.all([
        readFile(path.join(uiDir, "index.html"), "utf8"),
        readFile(path.join(uiDir, "app.js"), "utf8"),
        readFile(path.join(uiDir, "helio-preview.js"), "utf8"),
    ]);
    const tags = new Set();
    for (const source of sources) {
        for (const match of source.matchAll(/\?ui=(\d{8}[a-z]+)/g)) tags.add(match[1]);
    }
    assert.deepEqual([...tags], ["20260820a"]);
});

test("device UI reloads once when firmware serves a different UI build", async () => {
    const app = await readFile(path.join(uiDir, "app.js"), "utf8");
    const http = await readFile(path.join(root, "src", "core", "HttpServerModule.cpp"), "utf8");

    assert.match(app, /const UI_RUNTIME_VERSION = "20260820a"/);
    assert.match(app, /checkUiRuntimeVersion\(\);/);
    assert.match(app, /apiFetch\("\/api\/system", \{\}, 3500\)/);
    assert.match(app, /sessionStorage\.setItem\(key, "1"\);/);
    assert.match(app, /location\.reload\(\);/);
    assert.match(http, /\\"ui\\":\\"20260820a/);
});

test("initial UI paint waits for branding and the first state render", async () => {
    const index = await readFile(path.join(uiDir, "index.html"), "utf8");
    const app = await readFile(path.join(uiDir, "app.js"), "utf8");
    const criticalStyle = index.indexOf("html.ui-booting body { visibility: hidden; }");
    const stylesheet = index.indexOf('<link rel="stylesheet"');
    const renderStart = app.indexOf("function renderStateSnapshot()");
    const renderEnd = app.indexOf("// The message for a failed fetch Response", renderStart);
    const renderBody = app.slice(renderStart, renderEnd);

    assert.match(index, /<html lang="en" class="ui-booting">/);
    assert.ok(criticalStyle >= 0 && criticalStyle < stylesheet, "paint gate precedes the external stylesheet");
    assert.match(index, /setTimeout[\s\S]*classList\.remove\("ui-booting"\)[\s\S]*8000/);
    assert.match(index, /<link id="favicon" rel="icon">/);
    assert.match(index, /<img id="brand-logo" hidden alt="">[\s\S]*<span class="brand"><\/span>/);
    assert.match(renderBody, /applyBranding\(\);[\s\S]*renderNav\(\);[\s\S]*renderCards\(\);[\s\S]*updateValues\(\);[\s\S]*revealInitialUi\(\);/);
    assert.match(app, /requestAnimationFrame\(\(\) => \{[\s\S]*classList\.remove\("ui-booting"\)/);
    assert.equal((app.match(/renderStateSnapshot\(\);/g) || []).length, 2);
});

test("diagnostics fetches state and system sequentially", async () => {
    const app = await readFile(path.join(uiDir, "app.js"), "utf8");
    const start = app.indexOf('diag.addEventListener("click"');
    const end = app.indexOf('const blob = new Blob', start);
    const body = app.slice(start, end);

    assert.ok(start >= 0 && end > start);
    assert.doesNotMatch(body, /Promise\.all/);
    assert.match(body, /const stateResp = await apiFetch\("\/api\/state"\);[\s\S]*const systemResp = await apiFetch\("\/api\/system"\);/);
    assert.match(body, /buildDiagnosticBundle\(stateJson, systemJson\)/);
    assert.doesNotMatch(body, /state:\s*stateJson|system:\s*systemJson|origin:\s*location\.origin/);
});

test("diagnostic bundle uses an explicit aggregate allowlist", async () => {
    const app = await readFile(path.join(uiDir, "app.js"), "utf8");
    const start = app.indexOf("function buildDiagnosticBundle");
    const end = app.indexOf("// Footer pinned", start);
    const body = app.slice(start, end);

    assert.ok(start >= 0 && end > start);
    assert.match(body, /health:\s*\{[\s\S]*ui:[\s\S]*fps:[\s\S]*tickTimeUs:[\s\S]*freeHeap:[\s\S]*freeInternal:[\s\S]*maxBlock:[\s\S]*uptime:/);
    assert.match(body, /const summary = \{[\s\S]*moduleCount[\s\S]*warningCount[\s\S]*errorCount[\s\S]*\};/);
    assert.match(body, /return \{[\s\S]*health:[\s\S]*summary,/);
    assert.doesNotMatch(body, /\.controls|\.name|\.status|ssid|password|staticIp|gateway|subnet|dns|location\.origin/i);
});

test("public state serializes passwords as set-only values", async () => {
    const http = await readFile(path.join(root, "src", "core", "HttpServerModule.cpp"), "utf8");
    const header = await readFile(path.join(root, "src", "core", "HttpServerModule.h"), "utf8");

    assert.match(http, /writeControlValue\(sink, c, ControlValueVisibility::Public\)/);
    assert.match(http, /writeControlValue\(vs, c, ControlValueVisibility::Public\)/);
    assert.doesNotMatch(http + header, /PASSWORD_XOR_KEY|XOR-obfuscat/);
});

test("initial UI state follows the WebSocket resync notice with streamed REST", async () => {
    const app = await readFile(path.join(uiDir, "app.js"), "utf8");
    const http = await readFile(path.join(root, "src", "core", "HttpServerModule.cpp"), "utf8");
    const start = app.indexOf("async function init()");
    const end = app.indexOf("// The message for a failed fetch Response", start);
    const body = app.slice(start, end);

    assert.ok(start >= 0 && end > start);
    assert.match(body, /connectWs\(\);/);
    assert.match(body, /initialStateFallback = setTimeout/);
    assert.match(app, /data\.resync === true[\s\S]*refetchState\(2, API_FETCH_TIMEOUT_MS\)/);
    assert.match(http, /static constexpr char kResync\[\] = "\{\\"resync\\":true\}";/);
    assert.doesNotMatch(http, /startBufferedTextSend/);
});

test("every WebSocket client-table access holds the cross-core sender lock", async () => {
    const http = await readFile(path.join(root, "src", "core", "HttpServerModule.cpp"), "utf8");
    const body = (name, nextDeclaration) =>
        sourceSection(http, `void HttpServerModule::${name}`, nextDeclaration, name);

    for (const source of [
        body("release", "void HttpServerModule::ensureListener"),
        body("handleWebSocketUpgrade", "void HttpServerModule::pushStateToWebSockets"),
        body("pushStateToWebSockets", "void HttpServerModule::pushWledStateToWebSockets"),
        body("pollWledStateFromWebSockets", "bool HttpServerModule::sendWsTextFrame"),
    ]) {
        const lock = source.indexOf("LockGuard lease{wsLock_};");
        const clients = source.indexOf("wsClients_");
        assert.ok(lock >= 0 && clients > lock, "the lock precedes the first client-table access");
    }

    const release = body("release", "void HttpServerModule::ensureListener");
    assert.ok(release.indexOf("MoonModule::notifyQuiesceRender();") < release.indexOf("LockGuard lease{wsLock_};"),
              "release quiesces the encode worker before taking the sender lock");
    assert.doesNotMatch(release, /if\s*\(\s*!lease\s*\)\s*return/,
                        "release cannot skip server cleanup when the sender lock is briefly busy");
});

test("editable-list and preset writes quiesce before render-visible data mutates", async () => {
    const [http, control] = await Promise.all([
        readFile(path.join(root, "src", "core", "HttpServerModule.cpp"), "utf8"),
        readFile(path.join(root, "src", "core", "ControlModule.h"), "utf8"),
    ]);
    const add = sourceSection(http, "void HttpServerModule::handleListAddRow",
                              "void HttpServerModule::handleListPatchRow", "handleListAddRow");
    const patch = sourceSection(http, "void HttpServerModule::handleListPatchRow",
                                "void HttpServerModule::handleListDeleteRow", "handleListPatchRow");
    const remove = sourceSection(http, "void HttpServerModule::handleListDeleteRow",
                                 "void HttpServerModule::handleReboot", "handleListDeleteRow");
    const after = sourceSection(http, "void HttpServerModule::afterListMutation",
                                "void HttpServerModule::handleListAddRow", "afterListMutation");
    const preset = sourceSection(control, "bool applyPreset(const char* presetName)",
                                 "bool renamePreset(const char* from", "applyPreset");

    const assertMutationOrder = (source, mutation, label) => {
        const mutationAt = source.indexOf(mutation);
        const quiesceAt = source.lastIndexOf("MoonModule::notifyQuiesceRender();", mutationAt);
        const refreshAt = source.indexOf("afterListMutation();", mutationAt);
        assert.ok(mutationAt >= 0, `${label} mutation exists`);
        assert.ok(quiesceAt >= 0 && quiesceAt < mutationAt, `${label} quiesces before mutation`);
        assert.ok(refreshAt > mutationAt, `${label} refreshes dependants after mutation`);
    };

    assertMutationOrder(add, "src->addListRow", "list add");
    assertMutationOrder(patch, "src->moveListRow", "list move");
    assertMutationOrder(patch, "src->setListRowField", "list field edit");
    assertMutationOrder(remove, "src->deleteListRow", "list delete");
    assert.equal((patch.match(/MoonModule::notifyQuiesceRender\(\);/g) || []).length, 2,
                 "each mutually exclusive patch mutation owns its frame boundary");
    assert.match(after, /drivers->rebuildAllCorrections\(\);/);

    const applyAt = preset.indexOf("fs->applySubtree");
    const quiesceAt = preset.lastIndexOf("MoonModule::notifyQuiesceRender();", applyAt);
    assert.ok(applyAt >= 0 && quiesceAt >= 0 && quiesceAt < applyAt,
              "direct preset apply quiesces before subtree controls change");
});

test("synchronous ESP32 OTA feeds the render-task watchdog after flash progress", async () => {
    const ota = await readFile(path.join(root, "src", "platform", "esp32", "platform_esp32_ota.cpp"), "utf8");
    const body = sourceSection(ota, "bool otaWriteStream(", "int httpGet(", "otaWriteStream");
    const writeAt = body.indexOf("esp_ota_write(handle, buf, n)");
    const progressAt = body.indexOf("*bytesReadOut = written", writeAt);
    const watchdogAt = body.indexOf("taskWdtReset();", progressAt);

    assert.ok(writeAt >= 0, "the OTA loop writes one received chunk");
    assert.ok(progressAt > writeAt, "the byte count advances only after a successful write");
    assert.ok(watchdogAt > progressAt, "the watchdog is fed only after durable OTA progress");
});

test("HTTP listener retries use a bounded backoff", async () => {
    const http = await readFile(path.join(root, "src", "core", "HttpServerModule.cpp"), "utf8");
    const start = http.indexOf("void HttpServerModule::ensureListener()");
    const end = http.indexOf("void HttpServerModule::tick20ms()", start);
    const body = http.slice(start, end);

    assert.ok(start >= 0 && end > start);
    assert.match(body, /nextListenerAttemptMs_ != 0/);
    assert.match(body, /nextListenerAttemptMs_ = now \+ 1000/);
});

test("HELIO scene replacement consumes the returned module without a full-state fetch", async () => {
    const app = await readFile(path.join(uiDir, "app.js"), "utf8");
    const http = await readFile(path.join(root, "src", "core", "HttpServerModule.cpp"), "utf8");
    const start = app.indexOf("async function applyHelioMode");
    const end = app.indexOf("async function applyHelioPlaylist", start);
    const body = app.slice(start, end);

    assert.match(app, /compact && state \? "\/api\/state\?compact=1" : "\/api\/state"/);
    assert.match(app, /state = next\.compact && state \? mergeCompactState\(state, next\) : next;/);
    assert.match(app, /fresh = payload\?\.module \|\| null;/);
    assert.match(app, /replaceModuleInState\(name, fresh, state\.modules\)/);
    assert.match(body, /replacement && replacement !== true/);
    assert.match(body, /Compatibility fallback for an older server response or the rare add path/);
    assert.equal((body.match(/refreshStateData/g) || []).length, 1);
    assert.doesNotMatch(body, /refreshStateData\([^)]*,[^)]*, true\)/);
    assert.match(http, /const bool compact = queryStart && std::strstr\(queryStart \+ 1, "compact=1"\);/);
    assert.match(http, /serveState\(conn, !compact\);/);
    assert.match(http, /buildStateJson\(sink, includeMetadata\);/);
    assert.match(http, /sink\.append\("\{\\"ok\\":true,\\"module\\":"\);/);
    assert.match(http, /writeModuleJson\(sink, fresh, true\);/);
});

test("embedded UI serves every absolute JavaScript module import", async () => {
    const app = await readFile(path.join(uiDir, "app.js"), "utf8");
    const http = await readFile(path.join(root, "src", "core", "HttpServerModule.cpp"), "utf8");
    const embed = await readFile(path.join(uiDir, "embed_ui.cmake"), "utf8");
    const rootCmake = await readFile(path.join(root, "CMakeLists.txt"), "utf8");
    const espCmake = await readFile(path.join(root, "esp32", "main", "CMakeLists.txt"), "utf8");

    for (const script of importedUiScripts(app)) {
        const token = embedTokenFor(script, embed);
        const varName = embedVariableFor(token, embed);
        assert.equal(existsSync(path.join(uiDir, script)), true, `${script} exists in src/ui`);
        assert.match(http, new RegExp(`path, "/${escapeRegExp(script)}"`), `${script} has a GET route`);
        assert.match(http, new RegExp(`serveFile\\(conn, "${escapeRegExp(script)}"`), `${script} routes through serveFile`);
        assert.match(http, new RegExp(`filename, "${escapeRegExp(script)}"`), `${script} has embedded data lookup`);
        assert.match(http, new RegExp(`ui::${varName}`), `${script} uses its embedded array`);
        assert.match(embed, new RegExp(`constexpr uint8_t ${varName}\\[\\]`), `${script} emits an array`);
        assert.match(rootCmake, new RegExp(`/src/ui/${escapeRegExp(script)}`), `${script} is a desktop embed dependency`);
        assert.match(espCmake, new RegExp(`\\$\\{UI_DIR\\}/${escapeRegExp(script)}`), `${script} is an ESP32 embed dependency`);
    }
});
