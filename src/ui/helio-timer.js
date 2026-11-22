// The dashboard's timer card: when the lights go on and off, in words an owner reads, over TimerService's controls.

// TimerService's select options, by index.
export const ON_AT_TIME = 0, ON_AT_SUNSET = 1;
export const OFF_AT_TIME = 0, OFF_AT_SUNRISE = 1;
export const LOCATION_INTERNET = 0, LOCATION_MANUAL = 1;
// The day chips run Monday first; the mask counts from Sunday in bit 0, as the firmware does.
export const DAY_CHIPS = [["Mo", 1], ["Tu", 2], ["We", 3], ["Th", 4], ["Fr", 5], ["Sa", 6], ["Su", 0]];

function control(module, name) {
    return (module?.controls || []).find(item => item?.name === name)?.value;
}

/// "18:05" for 1085 minutes after midnight.
export function timeFromMinutes(minutes) {
    const m = Math.max(0, Math.min(1439, Math.round(Number(minutes) || 0)));
    return `${String(Math.floor(m / 60)).padStart(2, "0")}:${String(m % 60).padStart(2, "0")}`;
}

/// Minutes after midnight for "18:05", or null when it is not a time of day.
export function minutesFromTime(text) {
    const match = /^(\d{1,2}):(\d{2})$/.exec(String(text || "").trim());
    if (!match) return null;
    const hours = Number(match[1]), minutes = Number(match[2]);
    return hours < 24 && minutes < 60 ? hours * 60 + minutes : null;
}

/// The day mask with one weekday flipped.
export function toggleDay(mask, weekday) {
    return (Number(mask) || 0) ^ (1 << weekday);
}

/// What the card shows, read from the module's controls.
export function timerView(module) {
    if (!module) return null;
    const days = Number(control(module, "days") ?? 127);
    return {
        name: module.name,
        enabled: !!module.enabled,
        turnOn: Number(control(module, "turnOn") ?? ON_AT_SUNSET),
        onTime: timeFromMinutes(control(module, "onTime") ?? 1080),
        turnOff: Number(control(module, "turnOff") ?? OFF_AT_TIME),
        offTime: timeFromMinutes(control(module, "offTime") ?? 1380),
        days,
        dayChips: DAY_CHIPS.map(([label, weekday]) => ({ label, weekday, on: !!(days >> weekday & 1) })),
        location: Number(control(module, "location") ?? LOCATION_INTERNET),
        latitude: Number(control(module, "latitude") ?? 0) / 100,
        longitude: Number(control(module, "longitude") ?? 0) / 100,
        timezone: String(control(module, "timezone") ?? ""),
        clock: String(control(module, "clock") ?? ""),
        sun: String(control(module, "sun") ?? ""),
        next: String(control(module, "next") ?? ""),
    };
}

/// The one line the closed card shows.
export function timerSummary(view) {
    if (!view?.enabled) return "Off";
    const on = view.turnOn === ON_AT_SUNSET ? "sunset" : view.onTime;
    const off = view.turnOff === OFF_AT_SUNRISE ? "sunrise" : view.offTime;
    return `${on} to ${off}`;
}

/// Where the place came from, in words.
export function locationText(view) {
    if (!view.timezone) return view.location === LOCATION_MANUAL ? "Enter your place and time zone below." : "Finding your location from the internet…";
    const place = `${view.latitude.toFixed(2)}°, ${view.longitude.toFixed(2)}°`;
    return view.location === LOCATION_MANUAL ? `Entered by hand: ${place}` : `Found from your internet connection: ${place}`;
}

function el(doc, tag, className, text) {
    const node = doc.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
}

function choice(doc, options, selected, onPick) {
    const group = el(doc, "div", "helio-timer-choice");
    group.setAttribute("role", "group");
    options.forEach(([label, value]) => {
        const button = el(doc, "button", value === selected ? "active" : "", label);
        button.type = "button";
        button.setAttribute("aria-pressed", value === selected ? "true" : "false");
        button.addEventListener("click", () => onPick(value));
        group.appendChild(button);
    });
    return group;
}

function timeInput(doc, value, label, onTime) {
    const input = el(doc, "input", "helio-timer-time");
    input.type = "time";
    input.value = value;
    input.setAttribute("aria-label", label);
    input.addEventListener("change", () => {
        const minutes = minutesFromTime(input.value);
        if (minutes !== null) onTime(minutes);
    });
    return input;
}

function row(doc, labelText, ...inputs) {
    const line = el(doc, "div", "helio-timer-row");
    line.append(el(doc, "span", "helio-timer-label", labelText), ...inputs);
    return line;
}

/// The card's body for one TimerService, calling `send(control, value)` for each change and `rerender()` after it.
export function renderTimerBody(doc, module, { send, rerender }) {
    const view = timerView(module);
    const body = el(doc, "div", "helio-timer");
    const set = async (name, value) => { await send(name, value); rerender(); };

    const power = el(doc, "label", "helio-timer-switch");
    const toggle = el(doc, "input");
    toggle.type = "checkbox";
    toggle.checked = view.enabled;
    toggle.addEventListener("change", () => set("enabled", toggle.checked));
    power.append(toggle, el(doc, "span", "", "Turn the lights on and off by themselves"));
    body.appendChild(power);
    if (!view.enabled) return body;

    const onParts = [choice(doc, [["At a time", ON_AT_TIME], ["At sunset", ON_AT_SUNSET]], view.turnOn, v => set("turnOn", v))];
    if (view.turnOn === ON_AT_TIME) onParts.push(timeInput(doc, view.onTime, "Time the lights go on", m => set("onTime", m)));
    body.appendChild(row(doc, "Lights on", ...onParts));

    const offParts = [choice(doc, [["At a time", OFF_AT_TIME], ["At sunrise", OFF_AT_SUNRISE]], view.turnOff, v => set("turnOff", v))];
    if (view.turnOff === OFF_AT_TIME) offParts.push(timeInput(doc, view.offTime, "Time the lights go off", m => set("offTime", m)));
    body.appendChild(row(doc, "Lights off", ...offParts));

    const days = el(doc, "div", "helio-timer-days");
    for (const chip of view.dayChips) {
        const button = el(doc, "button", chip.on ? "active" : "", chip.label);
        button.type = "button";
        button.setAttribute("aria-pressed", chip.on ? "true" : "false");
        button.addEventListener("click", () => set("days", toggleDay(view.days, chip.weekday)));
        days.appendChild(button);
    }
    body.appendChild(row(doc, "On these days", days));

    const status = el(doc, "p", "helio-timer-status", view.next);
    status.dataset.timerField = "next";
    body.appendChild(status);
    const facts = [view.clock && `Now ${view.clock}`, view.sun].filter(Boolean).join(" · ");
    if (facts) body.appendChild(el(doc, "p", "helio-timer-facts", facts));

    const where = el(doc, "details", "helio-timer-location");
    where.appendChild(el(doc, "summary", "", locationText(view)));
    where.appendChild(choice(doc, [["Find automatically", LOCATION_INTERNET], ["Enter by hand", LOCATION_MANUAL]], view.location, v => set("location", v)));
    if (view.location === LOCATION_MANUAL) {
        const number = (name, value, label, scale) => {
            const input = el(doc, "input", "helio-timer-number");
            input.type = "number"; input.step = "0.01"; input.value = String(value);
            input.setAttribute("aria-label", label);
            input.addEventListener("change", () => { const v = Number(input.value); if (Number.isFinite(v)) set(name, Math.round(v * scale)); });
            return input;
        };
        where.appendChild(row(doc, "Latitude", number("latitude", view.latitude, "Latitude in degrees north", 100)));
        where.appendChild(row(doc, "Longitude", number("longitude", view.longitude, "Longitude in degrees east", 100)));
        const zone = el(doc, "input", "helio-timer-zone");
        zone.type = "text"; zone.value = view.timezone; zone.placeholder = "CET-1CEST,M3.5.0,M10.5.0/3";
        zone.setAttribute("aria-label", "Time zone as a POSIX TZ string");
        zone.addEventListener("change", () => set("timezone", zone.value.trim()));
        where.appendChild(row(doc, "Time zone", zone));
    }
    body.appendChild(where);
    return body;
}
