// Battery measurements stay separate from presence and charging state.
const PRESENCE = new Set(["unknown", "present", "absent"]);
const SAMPLE_STATE = new Set(["unknown", "fresh", "stale"]);
const SVG_NS = "http://www.w3.org/2000/svg";

const object = value => value !== null && typeof value === "object" && !Array.isArray(value);
const integer = (value, min, max) => Number.isInteger(value) && value >= min && value <= max;
const slotId = value => integer(value, 1, 2);

// The web-only revision changes on each firmware telemetry refresh, even when
// the voltage is unchanged. A legacy reading without a revision has no
// provable refresh signal, so it is only displayed for a bounded interval.
export function batteryReceiptTracker(timeoutMs = 12000) {
    // Full state snapshots replace module objects; retain the receipt by slot.
    const receipts = new Map();
    return {
        fresh(slot, revision, now) {
            const previous = receipts.get(slot);
            if (!integer(revision, 0, 0xffffffff)) {
                if (previous && previous.revision !== null) return false;
                if (!previous || now < previous.at) {
                    receipts.set(slot, { revision: null, at: now });
                    return true;
                }
                return now - previous.at <= timeoutMs;
            }
            if (!previous || previous.revision !== revision || now < previous.at) {
                receipts.set(slot, { revision, at: now });
                return true;
            }
            return now - previous.at <= timeoutMs;
        },
    };
}

function control(module, name) {
    const matches = Array.isArray(module.controls) ? module.controls.filter(item => item?.name === name) : [];
    return matches.length === 1 ? matches[0].value : undefined;
}

function unknownPack(id, reason = null) {
    return { id, presence: "unknown", state: "unknown", voltageMv: null, percent: null,
        ageMs: null, estimate: null, charging: "unknown", reason };
}

function normalizeSample(id, sample, reason = null) {
    if (reason || !object(sample) || !PRESENCE.has(sample.presence) || !SAMPLE_STATE.has(sample.state))
        return unknownPack(id, reason || "invalid-data");
    const pack = { ...unknownPack(id), presence: sample.presence, state: sample.state };
    if (pack.presence === "absent") return { ...pack, state: "unknown" };
    if (pack.state === "unknown") return pack;
    if ((sample.voltageMv != null && !integer(sample.voltageMv, 1, 65535)) ||
        (sample.percent != null && !integer(sample.percent, 0, 100)) ||
        (sample.ageMs != null && !integer(sample.ageMs, 0, 0xffffffff)) ||
        (sample.estimate != null && sample.estimate !== "voltage"))
        return unknownPack(id, "invalid-data");
    pack.voltageMv = sample.voltageMv ?? null;
    pack.ageMs = sample.ageMs ?? null;
    if (pack.presence === "present" && pack.state === "fresh" && pack.voltageMv !== null &&
        pack.ageMs !== null && sample.estimate === "voltage" && sample.percent != null) {
        pack.percent = sample.percent;
        pack.estimate = "voltage";
    }
    return pack;
}

function batteryServices(modules) {
    const pending = Array.isArray(modules) ? [...modules].reverse() : [];
    const seen = new Set();
    const found = [];
    while (pending.length) {
        const module = pending.pop();
        if (!object(module) || seen.has(module) || module.enabled === false) continue;
        seen.add(module);
        if (module.type === "BatteryService") found.push(module);
        if (Array.isArray(module.children)) pending.push(...[...module.children].reverse());
    }
    return found;
}

// Null means no enabled battery channels, including an AC-only product.
export function batteryView(modules, { connected = true, receipts = null, now = 0 } = {}) {
    const services = batteryServices(modules);
    if (services.length > 1) return { packs: [], error: "invalid-configuration" };
    const service = services[0];
    if (!service) return null;
    const children = Array.isArray(service.children) ? service.children : [];
    const declared = new Set();
    let invalidConfiguration = control(service, "configuration") === "invalid";
    // Firmware rejects the entire service when any enabled child or slot is
    // invalid. Check the tree before accepting any sample or refreshing its age.
    for (const channel of children) {
        if (!object(channel)) { invalidConfiguration = true; continue; }
        if (channel.enabled === false) continue;
        if (channel.type !== "BatteryChannel") {
            invalidConfiguration = true;
            continue;
        }
        const id = control(channel, "slot");
        if (!slotId(id) || declared.has(id)) invalidConfiguration = true;
        else declared.add(id);
    }
    const slots = new Map();
    for (const channel of children) {
        if (!object(channel) || channel.type !== "BatteryChannel" || channel.enabled === false) continue;
        const id = control(channel, "slot");
        if (!slotId(id)) continue;
        if (invalidConfiguration) { slots.set(id, unknownPack(id, "invalid-configuration")); continue; }
        if (!connected) { slots.set(id, unknownPack(id, "disconnected")); continue; }
        if (slots.has(id)) { slots.set(id, unknownPack(id, "duplicate-slot")); continue; }
        try {
            const raw = control(channel, "sample");
            const wire = typeof raw === "string" ? JSON.parse(raw) : null;
            const pack = object(wire) && wire.id === id
                ? normalizeSample(id, wire.sample) : unknownPack(id, "invalid-data");
            if (!pack.reason && Object.prototype.hasOwnProperty.call(wire, "revision") && !integer(wire.revision, 0, 0xffffffff)) {
                slots.set(id, unknownPack(id, "invalid-data"));
                continue;
            }
            // Invalid messages must never refresh a previous valid reading.
            if (!pack.reason && receipts && !receipts.fresh(id, wire.revision, now)) {
                slots.set(id, unknownPack(id, "telemetry-timeout"));
                continue;
            }
            slots.set(id, pack);
        } catch {
            slots.set(id, unknownPack(id, "invalid-data"));
        }
    }
    const packs = [...slots.values()].sort((a, b) => a.id - b.id);
    if (invalidConfiguration) return { packs, error: "invalid-configuration" };
    return packs.length ? { packs } : null;
}

function voltageText(millivolts) {
    if (millivolts === null) return "";
    return millivolts < 1000 ? `${millivolts} mV` : `${(millivolts / 1000).toFixed(2)} V`;
}

function ageText(milliseconds) {
    if (milliseconds === null) return "Age unknown";
    if (milliseconds === 0xffffffff) return "More than 49 d ago";
    if (milliseconds < 1000) return "just now";
    if (milliseconds < 60000) return `${Math.floor(milliseconds / 1000)} s ago`;
    if (milliseconds < 3600000) return `${Math.floor(milliseconds / 60000)} min ago`;
    if (milliseconds < 86400000) return `${Math.floor(milliseconds / 3600000)} h ago`;
    return `${Math.floor(milliseconds / 86400000)} d ago`;
}

// Visible and accessible text share one interpretation of the normalized sample.
export function batteryPresentation(input) {
    if (!object(input) || !slotId(input.id)) return null;
    const pack = normalizeSample(input.id, input, input.reason);
    const label = `Battery ${pack.id}`;
    const voltage = voltageText(pack.voltageMv);
    let kind = "unknown", value = "Unknown", detail = "Battery presence unknown", fillPercent = null;
    if (pack.reason) {
        kind = "unavailable"; value = "Unavailable";
        detail = pack.reason === "duplicate-slot" ? "Conflicting battery slot IDs" :
            pack.reason === "invalid-configuration" ? "Battery configuration error" :
            pack.reason === "disconnected" ? "Controller disconnected" : "Battery data unavailable";
    } else if (pack.presence === "absent") {
        kind = "absent"; value = "Not installed"; detail = "Battery absent";
    } else if (pack.state === "stale") {
        kind = "stale"; value = "Stale";
        detail = `${voltage ? `Last ${voltage} · ` : ""}${ageText(pack.ageMs)}`;
        if (pack.presence === "unknown") detail += " · Presence unknown";
    } else if (pack.presence === "unknown") {
        if (voltage) detail += ` · ${voltage}`;
    } else if (pack.percent !== null) {
        kind = pack.percent === 0 ? "empty" : "level";
        value = `~${pack.percent}%`; detail = `Voltage estimate · ${voltage}`;
        fillPercent = pack.percent;
    } else {
        kind = "unavailable"; value = voltage || "Unavailable";
        detail = voltage ? "Charge estimate unavailable" : "No valid battery reading yet";
    }
    const valueLabel = fillPercent !== null ? `Approximately ${pack.percent} percent` : value;
    const description = `${label}: ${valueLabel}. ${detail}. Charging state unknown.`;
    return { id: pack.id, label, kind, value, valueLabel, detail, description, fillPercent };
}

function batteryIcon(document, presentation) {
    const svg = document.createElementNS(SVG_NS, "svg");
    for (const [name, value] of Object.entries({ viewBox: "0 0 24 18", width: "24", height: "18",
        fill: "none", stroke: "currentColor", "stroke-width": "1.4", "aria-hidden": "true", focusable: "false",
        class: "helio-battery-icon" })) svg.setAttribute(name, value);
    const shape = (name, attributes) => {
        const element = document.createElementNS(SVG_NS, name);
        for (const [key, value] of Object.entries(attributes)) element.setAttribute(key, String(value));
        svg.append(element);
        return element;
    };
    shape("rect", { x: 1, y: 3, width: 19, height: 12, rx: 2 });
    shape("path", { d: "M22 7v4", "stroke-width": 2 });
    if (presentation.fillPercent > 0) {
        shape("rect", { x: 3, y: 5, width: 15 * presentation.fillPercent / 100, height: 8,
            rx: 1, fill: "currentColor", stroke: "none", class: "helio-battery-fill" });
    } else if (presentation.kind === "absent") {
        shape("path", { d: "M4 13L17 5", class: "helio-battery-absent-mark" });
    } else if (presentation.kind === "stale") {
        shape("circle", { cx: 10.5, cy: 9, r: 3.2 });
        shape("path", { d: "M10.5 6.8v2.4l1.6 1" });
    } else if (presentation.kind !== "empty") {
        shape("text", { x: 10.5, y: 12, "text-anchor": "middle", "font-size": 9,
            "font-family": "system-ui, sans-serif", fill: "currentColor", stroke: "none" }).textContent = "?";
    }
    return svg;
}

// The dashboard owns placement and responsive styling; this status list has no interactive controls.
export function renderBatteryStatus(document, view) {
    const presentations = Array.isArray(view?.packs) ? view.packs.map(batteryPresentation).filter(Boolean) : [];
    if (!presentations.length && !view?.error) return null;
    const list = document.createElement("ul");
    list.className = "helio-battery-status";
    list.setAttribute("aria-label", "Battery status");
    if (view?.error && (!presentations.length || view.error === "invalid-data")) {
        const error = document.createElement("li");
        error.className = "helio-battery-chip is-unavailable";
        error.textContent = view.error === "invalid-configuration" ? "Battery configuration error" : "Battery data unavailable";
        list.append(error);
    }
    for (const presentation of presentations) {
        const chip = document.createElement("li");
        chip.className = `helio-battery-chip is-${presentation.kind}`;
        chip.setAttribute("data-battery-id", String(presentation.id));
        chip.setAttribute("aria-label", presentation.description);
        chip.setAttribute("title", presentation.description);
        const copy = document.createElement("div"); copy.className = "helio-battery-copy";
        for (const [tag, className, text] of [["span", "helio-battery-label", presentation.label],
            ["strong", "helio-battery-value", presentation.value], ["span", "helio-battery-detail", presentation.detail]]) {
            const element = document.createElement(tag); element.className = className; element.textContent = text;
            if (className === "helio-battery-value") element.setAttribute("aria-label", presentation.valueLabel);
            copy.append(element);
        }
        chip.append(batteryIcon(document, presentation), copy); list.append(chip);
    }
    return list;
}
