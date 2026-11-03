// Select the product UI from the firmware identity before either application
// starts. Every other firmware retains the current MoonLight module editor.
let genericAssets;

function prepareGenericAssets() {
    if (genericAssets) return genericAssets;
    const logo = document.getElementById("brand-logo");
    if (logo) { logo.src = "/moonmodules-logo.png"; logo.hidden = false; }
    const favicon = document.getElementById("favicon");
    if (favicon) { favicon.type = "image/png"; favicon.href = "/moonmodules-logo.png"; }
    genericAssets = new Promise(resolve => {
        const script = document.createElement("script");
        script.src = "/prism.js";
        script.onload = script.onerror = () => resolve();
        document.head.appendChild(script);
    });
    return genericAssets;
}

async function startGeneric() {
    await prepareGenericAssets();
    await import("./app.js");
}

function prefetchInitialIndex() {
    if (typeof AbortController === "undefined") return null;
    const controller = new AbortController();
    let timer, response, discarded = false;
    const finish = () => {
        if (timer === null) return;
        clearTimeout(timer);
        timer = null;
        globalThis.removeEventListener?.("pagehide", cancel);
    };
    const cancel = () => {
        if (discarded) return;
        discarded = true;
        controller.abort();
        response?.then(value => value.body?.cancel()).catch(() => {});
        finish();
    };
    // The startup transfer has the same seven-second budget as apiFetch, including its unread body.
    timer = setTimeout(cancel, 7000);
    globalThis.addEventListener?.("pagehide", cancel, { once: true });
    response = Promise.resolve().then(() => fetch("/api/presets", { cache: "no-store", signal: controller.signal }));
    response.catch(finish); // Observe rejection without changing what the preset client receives.
    return { response, finish, cancel, get discarded() { return discarded; } };
}

async function start() {
    if (location.pathname === "/modules") {
        await startGeneric();
        return;
    }
    let initialIndex = null;
    try {
        const response = await fetch("/api/modules/Firmware", { cache: "no-store" });
        const module = response.ok ? await response.json() : null;
        const firmware = (module?.controls || []).find(control => control.name === "firmware")?.value;
        if (/^(helio|nebulite)/.test(firmware || "")) {
            initialIndex = prefetchInitialIndex();
            const product = await import("./helio-app.js");
            if (initialIndex && !product.acceptInitialPresetIndexResponse?.(initialIndex.response, initialIndex, module))
                initialIndex.cancel();
        } else await startGeneric();
    } catch {
        initialIndex?.cancel();
        await startGeneric();
    }
}

start();
