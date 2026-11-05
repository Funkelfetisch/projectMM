// Product setup changes output wiring and layout through the existing state API.
// Radio, battery, scene and preset configuration belong to their own controls.
function value(module, name) {
    return module?.controls?.find(control => control.name === name)?.value;
}

const displayName = profile => profile?.displayName || profile?.name || "";

// The archived artwork identifies a product family; it does not describe LED wiring.
const PRODUCT_ART = {
    "NEBULITE Croptop S3": { src: "/products/croptop_top.png", label: "Crop-top illustration" },
    "NEBULITE Croptop ESP32": { src: "/products/croptop_top.png", label: "Crop-top illustration" },
    "NEBULITE Jacket S3": { src: "/products/jacket_leds.png", label: "Jacket illustration; this setup also supports the vest" },
};

export function nebuliteProductArt(model) { return PRODUCT_ART[model] || null; }

export function compatibleProductProfiles(profiles, identity) {
    const chip = String(identity?.chip || "").split(/[\s(]/)[0];
    return profiles.filter(profile => profile.firmwares.includes(identity?.firmware) && profile.chip === chip);
}

// Coordinates are approximate samples of the archived crop-top illustration.
// The ninth position is the separate PWM white output, not a garment RGB LED.
const CROPTOP_POINTS = '1,8;12,8;10,3;16,2;4,0;5,8;13,4;16,8;8,4';
// Provisional output order: bottom, top, hood, first sleeve, second sleeve,
// then the separate PWM white slot. Parallel Y branches need a different map.
const JACKET_POINTS = '5,14;7,14;9,14;11,14;13,14;15,14;5,6;7,6;9,6;11,6;13,6;15,6;7,2;8,1;9,1;11,1;12,1;13,2;0,5;1,6;2,7;1,9;2,10;3,11;20,5;19,6;18,7;19,9;18,10;17,11;10,10';
export function productProjectionProfile(profile, projection = "strip") {
    if (projection === "strip") return { ...profile, projection: "strip" };
    // A product whose own layout is a point map, such as the necklace's ring, is already two-dimensional.
    if (projection === "virtual2d" && profile?.state?.Layouts?.ProductStrip?.type === "PixelMapLayout") return { ...profile, projection };
    const dimensions = profile?.name === "NEBULITE Croptop S3" || profile?.name === "NEBULITE Croptop ESP32" ? [3, 3] :
        profile?.name === "NEBULITE Jacket S3" ? [7, 13] : null;
    if (projection !== "virtual2d" || !dimensions) throw new Error("This product has no virtual 2D layout.");
    const state = JSON.parse(JSON.stringify(profile.state));
    state.Layouts = { $patch: "replace", ProductStrip: {type: "PixelMapLayout",
        points: profile.name === "NEBULITE Croptop S3" ? CROPTOP_POINTS :
            profile.name === "NEBULITE Croptop ESP32" ? CROPTOP_POINTS.split(';').slice(0, 8).join(';') : JACKET_POINTS} };
    if (profile.name === "NEBULITE Jacket S3") {
        // The archived profile has three 30-slot outputs; this candidate uses
        // the first one only, subject to a physical wiring check.
        delete state.Drivers.RGB3;
        delete state.Drivers.RGB4;
    }
    return { ...profile, state, projection: "virtual2d" };
}

function parseProjectionPoints(text, expectedCount) {
    if (typeof text !== 'string' || text.length >= 1024) return null;
    const pairs = text.split(';');
    if (pairs.length !== expectedCount || pairs.length > 128) return null;
    const result = [];
    for (const pair of pairs) {
        const match = /^(\d+),(\d+)$/.exec(pair);
        if (!match || Number(match[1]) > 63 || Number(match[2]) > 63) return null;
        result.push([Number(match[1]), Number(match[2])]);
    }
    return result;
}

export function productProjectionPoints(profile, modules = null) {
    if (!profile) return null;
    const layout = productProjectionProfile(profile, "virtual2d").state.Layouts.ProductStrip;
    const expected = layout.points.split(';').length;
    const child = modules?.find(m => m.name === 'Layouts')?.children?.find(m => m.name === 'ProductStrip');
    const points = parseProjectionPoints(modules ? value(child, 'points') : layout.points, expected);
    if (!points) return null;
    const width = 1 + Math.max(...points.map(point => point[0]));
    const height = 1 + Math.max(...points.map(point => point[1]));
    return {points, width, height};
}

export function productProfilePatch(profile, identity) {
    if (!compatibleProductProfiles([profile], identity).length) throw new Error("This setup does not match the controller.");
    const patch = JSON.parse(JSON.stringify({
        System: { deviceModel: profile.name },
        Drivers: profile.state.Drivers,
        Layouts: profile.state.Layouts,
    }));
    for (const group of [patch.Drivers, patch.Layouts]) {
        for (const child of Object.values(group)) {
            if (child && typeof child === "object" && child.type) child.enabled = true;
        }
    }
    return patch;
}

function sameSetting(module, name, expected) {
    const control = module?.controls?.find(candidate => candidate.name === name);
    if (!control) return false;
    const actual = Array.isArray(control.options) && typeof control.value === "number"
        ? control.options[control.value] : control.value;
    return typeof expected === "string" && typeof actual === "string"
        ? expected.toLowerCase() === actual.toLowerCase() : expected === actual;
}

function productProfileMatchesExact(profile, modules, customPoints = false, geometryOnly = false) {
    const system = modules?.find(module => module.name === "System");
    if (value(system, "deviceModel") !== profile.name) return false;
    for (const group of ["Drivers", "Layouts"]) {
        const module = modules.find(candidate => candidate.name === group);
        if (!module || module.enabled === false) return false;
        const expectedNames = Object.entries(profile.state[group]).filter(([, setting]) => setting?.type).map(([name]) => name);
        const physical = (module.children || []).filter(child =>
            (!geometryOnly || child.enabled !== false) && (group === "Layouts" ||
            (child.role === "driver" && child.type !== "PreviewDriver")));
        if (physical.some(child => !expectedNames.includes(child.name))) return false;
        for (const [name, settings] of Object.entries(profile.state[group])) {
            if (name === "$patch") continue;
            // A plain value is the group's own setting, such as the Drivers relay list.
            if (!settings || typeof settings !== "object") {
                if (!sameSetting(module, name, settings)) return false;
                continue;
            }
            const child = module.children?.find(candidate => candidate.name === name);
            if (!child || child.enabled === false || child.type !== settings.type) return false;
            for (const [key, expected] of Object.entries(settings)) {
                if (key === "$patch" || key === "type") continue;
                // Preview frames are pre-driver RGB. These settings change the
                // physical response, not output order or the garment geometry.
                if (geometryOnly && group === 'Drivers' && ['curve', 'whiteMode', 'bits'].includes(key)) continue;
                if (customPoints && group === 'Layouts' && settings.type === 'PixelMapLayout' && key === 'points') {
                    if (!parseProjectionPoints(value(child, key), expected.split(';').length)) return false;
                    continue;
                }
                if (!sameSetting(child, key, expected)) return false;
            }
        }
    }
    return true;
}

export function productProfileMatches(profile, modules) {
    if (!profile) return false;
    if (profile.projection) return productProfileMatchesExact(profile, modules);
    if (productProfileMatchesExact(profile, modules, false, true)) return true;
    try { return productProfileMatchesExact(productProjectionProfile(profile, "virtual2d"), modules, true, true); }
    catch { return false; }
}

export function productProjection(profile, modules) {
    if (!profile) return null;
    try { if (productProfileMatchesExact(productProjectionProfile(profile, "virtual2d"), modules, true, true)) return "virtual2d"; }
    catch { /* Products without a 2D prototype retain their ordinary layout. */ }
    return productProfileMatchesExact(profile, modules, false, true) ? "strip" : null;
}

export function renderProductSetup({ profiles, identity, modules, apply, verify, session = {}, getModules = () => modules, loadMetadata = null }) {
    const choices = compatibleProductProfiles(profiles, identity);
    let metadataReady = !loadMetadata || !choices.length, metadataFailed = false;
    let current = choices.find(profile => productProfileMatches(profile, modules));
    const hasPhysicalOutput = state => state?.find(module => module.name === "Drivers")?.children?.some(
        child => child.type !== "PreviewDriver" && child.role === "driver");
    const hasOutput = hasPhysicalOutput(modules);
    const details = document.createElement("details");
    details.className = "helio-dashboard-menu nebulite-setup-menu";
    details.open = !current && !hasOutput;
    const summary = document.createElement("summary");
    const title = document.createElement("span");
    title.textContent = "Product setup";
    const selected = document.createElement("span");
    selected.textContent = current ? displayName(current) : (hasOutput ? "Custom setup" : "Choose your NEBULITE");
    summary.append(title, selected);
    const panel = document.createElement("section");
    panel.className = "helio-setup-panel helio-setup-panel-embedded nebulite-setup-panel";
    const intro = document.createElement("p");
    intro.className = "helio-helper-text";
    intro.textContent = "Choose the product connected to this controller. Applying a setup replaces the current light outputs and layout, including any custom wiring and LED positions. Battery settings are kept; check for pin conflicts after changing outputs.";
    const label = document.createElement("label");
    label.className = "nebulite-product-choice";
    const caption = document.createElement("span");
    caption.textContent = "Product";
    const select = document.createElement("select");
    select.setAttribute("aria-label", "NEBULITE product");
    const empty = document.createElement("option");
    empty.value = "";
    empty.textContent = "Choose a product";
    select.append(empty);
    for (const profile of choices) {
        const option = document.createElement("option");
        option.value = profile.name;
        option.textContent = displayName(profile);
        select.append(option);
    }
    const attempt = session.attempt ||= {
        profileName: current?.name || "", busy: false, written: false,
        confirmed: false, message: "", phase: "", touched: false,
        projection: productProjection(current, modules) || "strip",
    };
    attempt.projection ||= productProjection(current, modules) || "strip";
    if (metadataReady && attempt.confirmed && (current?.name !== attempt.profileName ||
        productProjection(current, modules) !== attempt.projection)) {
        attempt.confirmed = false;
        attempt.message = "The controller setup has changed. Check its current setup.";
    }
    select.value = attempt.profileName;
    label.append(caption, select);
    const projectionLabel = document.createElement("label");
    projectionLabel.className = "nebulite-product-choice";
    const projectionCaption = document.createElement("span");
    projectionCaption.textContent = "Animation layout";
    const projectionSelect = document.createElement("select");
    projectionSelect.setAttribute("aria-label", "Animation layout");
    for (const [id, text] of [["virtual2d", "Approximate 2D layout"], ["strip", "1D output order"]]) {
        const option = document.createElement("option"); option.value = id; option.textContent = text; projectionSelect.append(option);
    }
    projectionLabel.append(projectionCaption, projectionSelect);
    const illustration = document.createElement("figure");
    illustration.className = "nebulite-product-illustration";
    illustration.hidden = true;
    const picture = document.createElement("img");
    picture.loading = "lazy";
    picture.decoding = "async";
    picture.width = 400;
    picture.height = 400;
    const captionText = document.createElement("figcaption");
    illustration.append(picture, captionText);
    const note = document.createElement("p");
    note.className = "helio-helper-text";
    note.textContent = choices.length
        ? "The 2D layout is approximate. For a 30-LED jacket it uses the first RGB output; verify the pin and any parallel branches. Battery settings are separate."
        : "The controller type is unavailable or has no matching product setup.";
    const action = document.createElement("button");
    action.type = "button";
    action.className = "helio-secondary-action";
    action.textContent = "Apply product setup";
    action.disabled = !select.value;
    const status = document.createElement("p");
    status.className = "helio-helper-text nebulite-setup-status";
    status.setAttribute("role", "status");
    status.setAttribute("aria-live", "polite");
    // A state resync replaces the DOM while the request may still be pending.
    session.render = () => {
        const latestModules = getModules();
        current = choices.find(profile => productProfileMatches(profile, latestModules));
        if (metadataReady && !metadataFailed && !attempt.touched && !attempt.busy && !attempt.written && !attempt.confirmed && current) {
            attempt.profileName = current.name;
            attempt.projection = productProjection(current, latestModules) || "strip";
        }
        selected.textContent = metadataFailed ? "Setup unknown" : !metadataReady ? "Checking setup…" :
            current ? displayName(current) : (hasPhysicalOutput(latestModules) ? "Custom setup" : "Choose your NEBULITE");
        select.value = attempt.profileName;
        note.textContent = attempt.profileName === "NEBULITE Croptop ESP32"
            ? "Experimental archived configuration: verify GPIO26, LED wiring, power and recovery on this controller. Battery and charging wiring are not configured. The 2D layout is approximate."
            : attempt.profileName === "NEBULITE Croptop S3"
            ? "Experimental configuration: verify GPIO36 and GPIO46, LED wiring, power and recovery on this controller. Matching the chip does not verify the board wiring. Battery settings are separate; the 2D layout is approximate."
            : attempt.profileName === "NEBULITE Jacket S3"
            ? "Experimental configuration: verify the GPIOs, LED wiring, power and recovery on this controller. Matching the chip does not verify the board wiring. The approximate 30-LED 2D layout uses the first RGB output; check for parallel branches. Battery settings are separate."
            : choices.length ? "Product configurations are experimental; verify the wiring on the controller. Battery settings are separate; the 2D layout is approximate."
            : "The controller type is unavailable or has no matching product setup.";
        projectionSelect.value = attempt.projection;
        const artwork = nebuliteProductArt(attempt.profileName);
        illustration.hidden = !artwork;
        if (artwork) {
            picture.src = artwork.src;
            picture.alt = artwork.label;
            captionText.textContent = artwork.label;
        }
        select.disabled = attempt.busy;
        projectionSelect.disabled = attempt.busy;
        action.disabled = attempt.busy || attempt.confirmed || !attempt.profileName;
        action.textContent = attempt.busy ? (attempt.phase === "checking" ? "Checking setup…" : "Applying setup…")
            : attempt.confirmed ? "Setup saved" : attempt.written ? "Check setup" : "Apply product setup";
        status.textContent = attempt.message;
        panel.setAttribute("aria-busy", String(attempt.busy));
        if (attempt.confirmed && !metadataFailed) selected.textContent = displayName(choices.find(profile => profile.name === attempt.profileName));
    };
    select.addEventListener("change", () => {
        if (attempt.busy) return;
        attempt.profileName = select.value;
        attempt.touched = true;
        attempt.written = attempt.confirmed = false;
        attempt.message = "";
        session.render();
    });
    projectionSelect.addEventListener("change", () => {
        if (attempt.busy) return;
        attempt.projection = projectionSelect.value;
        attempt.touched = true;
        attempt.written = attempt.confirmed = false;
        attempt.message = "";
        session.render();
    });
    action.addEventListener("click", async () => {
        const base = choices.find(candidate => candidate.name === attempt.profileName);
        const profile = base ? productProjectionProfile(base, attempt.projection) : null;
        if (attempt.busy || !profile || attempt.confirmed) return;
        attempt.busy = true;
        try {
            if (!attempt.written) {
                attempt.message = "Applying product setup…";
                attempt.phase = "applying";
                attempt.written = true;
                session.render();
                await apply(productProfilePatch(profile, identity));
            }
            attempt.message = "Checking controller setup…";
            attempt.phase = "checking";
            session.render();
            if (!await verify(profile)) throw new Error("The controller has not confirmed this setup.");
            metadataReady = true;
            metadataFailed = false;
            attempt.confirmed = true;
            attempt.message = "Product setup saved.";
        } catch (error) {
            attempt.message = "Setup not confirmed. " + (error?.message || "Refresh to check the controller.");
        } finally {
            attempt.busy = false;
            session.render();
        }
    });
    session.render();
    if (loadMetadata && choices.length) {
        const markMetadataFailed = () => {
            metadataReady = true;
            metadataFailed = true;
            if (!attempt.busy && attempt.confirmed) {
                attempt.confirmed = false;
                attempt.written = true;
                attempt.message = "Setup could not be verified. Check the controller setup.";
            }
        };
        Promise.resolve().then(loadMetadata).then(ready => {
            if (!ready) { markMetadataFailed(); session.render?.(); return; }
            metadataReady = true;
            metadataFailed = false;
            if (ready && !attempt.busy && attempt.confirmed &&
                !choices.some(profile => profile.name === attempt.profileName &&
                    productProfileMatches(productProjectionProfile(profile, attempt.projection), getModules()))) {
                attempt.confirmed = false;
                attempt.message = "The controller setup has changed. Check its current setup.";
            }
            session.render?.();
        }).catch(() => {
            markMetadataFailed();
            session.render?.();
        });
    }
    panel.append(intro, label, projectionLabel, note, action, status, illustration);
    details.append(summary, panel);
    return details;
}
