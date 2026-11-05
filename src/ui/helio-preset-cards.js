import { PresetEditor } from "./helio-presets.js";
import { prepareLegacyImport, commitLegacyImport } from "./nebulite-legacy-import.js";

function suggestedPresetName(label) {
    const candidate = String(label || 'Animation').replace(/^[♪♫]\s*/, 'Music - ');
    const safe = /^[\x20-\x7e]+$/.test(candidate) && !/[./\\]/.test(candidate) ? candidate : 'Animation';
    return `${safe.slice(0, 26)} copy`;
}

// Each card owns one native preset document and its conditional-save revision.
// The playing selection is a deliberate action, separate from editing a file.
export class HelioPresetCards {
    constructor({ client, makeControls, onSelect, onApply, onPreview, onControls, legacyProduct, showLegacyImport }) {
        this.client = client;
        this.makeControls = makeControls;
        this.onSelect = onSelect;
        this.onApply = onApply;
        this.onPreview = onPreview;
        this.onControls = onControls;
        this.legacyProduct = legacyProduct;
        this.showLegacyImport = showLegacyImport;
        this.records = new Map();
        this.selected = null;
        this.pending = null;
        this.applying = false;
        this.reading = 0;
        this.backgroundReading = 0;
        this.readQueue = [];
        this.templateSchemas = new Map();
    }

    async mount(host) {
        const generation = this.mountGeneration = (this.mountGeneration || 0) + 1;
        this.host = host;
        host.className = "helio-presets";
        const heading = document.createElement("div");
        heading.className = "helio-section-head";
        const title = document.createElement("h2");
        title.textContent = "Animations";
        this.status = document.createElement("span");
        this.status.className = "helio-inline-status";
        this.status.setAttribute("role", "status");
        heading.append(title, this.status);
        this.grid = document.createElement("div");
        this.grid.className = "helio-native-preset-grid";
        const forms = [this.addForm()];
        if (this.showLegacyImport?.()) forms.push(this.legacyImportForm());
        host.replaceChildren(heading, this.grid, ...forms);
        // A remount, as after a scene change rebuilds the page, shows the cards it knows while the listing refreshes, so the section never empties and the page keeps its scroll position.
        if (this.records.size) {
            this.grid.replaceChildren(...[...this.records.values()].map(record => this.card(record)));
            this.onPreview?.();
        } else this.status.textContent = "Loading presets…";
        try {
            const listing = await this.client.list();
            if (!host.isConnected || generation !== this.mountGeneration) return;
            const names = new Set();
            const cards = [];
            for (const item of listing.items.filter(item => item.kind === "look")) {
                names.add(item.name);
                let record = this.records.get(item.name);
                if (!record) {
                    record = { name: item.name, item, editor: null, loading: null };
                    this.records.set(item.name, record);
                }
                record.item = item;
                cards.push(this.card(record));
            }
            const removed = [];
            for (const [name, record] of this.records) {
                if (!names.has(name) && !record.editor?.dirty) {
                    this.cancelQueuedRead(record);
                    record.editor?.dispose();
                    this.records.delete(name);
                } else if (!names.has(name)) {
                    cards.push(this.card(record));
                    removed.push(record);
                }
            }
            this.grid.replaceChildren(...cards);
            for (const record of removed) record.status.textContent = "Preset removed elsewhere. Your draft is retained; save a copy.";
            this.status.textContent = this.notice || (names.size ? "Changes save automatically" : "Add an animation to begin");
            this.onPreview?.();
        } catch (error) { this.status.textContent = error.message; }
    }

    card(record) {
        const card = document.createElement("article");
        card.className = "helio-scene-card helio-native-preset-card";
        card.dataset.presetName = record.name;
        card.classList.toggle("active", this.selected === record.name);
        const play = document.createElement("button");
        play.type = "button";
        play.className = "helio-preset-play";
        play.setAttribute("aria-pressed", String(this.selected === record.name));
        play.setAttribute("aria-label", `Play ${record.name}`);
        const canvas = document.createElement("canvas");
        canvas.className = "helio-mode-preview";
        canvas.width = canvas.height = 256;
        canvas.dataset.nativePreset = record.name;
        const label = document.createElement("span");
        label.className = "helio-scene-label";
        label.textContent = record.name;
        const previewKind = document.createElement("span");
        previewKind.className = "helio-scene-meta helio-preview-kind";
        previewKind.hidden = true;
        play.append(canvas, label, previewKind);
        play.addEventListener("click", () => this.play(record));
        const details = document.createElement("details");
        details.className = "helio-preset-details";
        details.open = !!record.controlsOpen;
        const summary = document.createElement("summary");
        summary.textContent = "Controls";
        const content = document.createElement("div");
        content.className = "helio-preset-content";
        const status = document.createElement("p");
        status.className = "helio-preset-save-status";
        status.setAttribute("role", "status");
        details.append(summary, content, status);
        const actions = document.createElement("div");
        actions.className = "helio-preset-actions";
        const reset = document.createElement("button");
        reset.type = "button";
        reset.textContent = "Reset scene";
        reset.addEventListener("click", async () => {
            try {
                const editor = await this.ensureEditor(record);
                const before = JSON.stringify(editor.document);
                const origin = editor.document.$origin?.scene;
                if (!origin) throw new Error("This preset has no factory template");
                const template = await this.client.readTemplate(origin);
                if (JSON.stringify(editor.document) !== before)
                    throw new Error("Settings changed while loading the template. Reset was not applied.");
                const next = template.document;
                if (editor.document.$slot !== undefined) next.$slot = editor.document.$slot;
                editor.replace(next);
                this.controls(record);
            } catch (error) { status.textContent = error.message; }
        });
        const remove = document.createElement("button");
        remove.type = "button";
        remove.textContent = "Delete preset";
        remove.addEventListener("click", async () => {
            if (!confirm(`Delete “${record.name}”?`)) return;
            record.deleting = true;
            if (this.pending?.record === record) this.pending = null;
            if (this.selected === record.name) this.selected = null;
            try {
                const editor = await this.ensureEditor(record);
                await editor.flush();
                await this.client.remove(record.name, { etag: editor.etag });
                editor.dispose();
                this.records.delete(record.name);
                if (this.selected === record.name) this.selected = null;
                card.remove();
                this.status.textContent = "Preset deleted; current output continues";
            } catch (error) { status.textContent = error.message; }
            finally { record.deleting = false; }
        });
        const copy = document.createElement("button");
        copy.type = "button";
        copy.textContent = "Save a copy";
        copy.addEventListener("click", async () => {
            const name = prompt("Preset name", `${record.name.slice(0, 26)} copy`);
            if (!name) return;
            try {
                const editor = await this.ensureEditor(record);
                const document = editor.document;
                delete document.$slot;
                await this.client.write(name, document, { create: true });
                await this.mount(this.host);
            } catch (error) { status.textContent = error.message; }
        });
        actions.append(reset, copy, remove);
        details.append(actions);
        card.append(play, details);
        Object.assign(record, { card, details, content, status, canvas });
        details.addEventListener("toggle", () => {
            card.classList.toggle("is-expanded", details.open);
            record.controlsOpen = details.open;
            this.openControls(record, details, content);
        });
        const hydrate = () => this.ensureEditor(record, { background: true }).then(() => this.onPreview?.()).catch(error => {
            status.textContent = error.message;
        });
        card.addEventListener("pointerenter", hydrate);
        play.addEventListener("focus", hydrate);
        if (record.editor) this.controls(record);
        return card;
    }

    controls(record) {
        if (!record.content || !record.editor) return;
        const controlPanel = this.makeControls(record);
        record.content.replaceChildren(...(controlPanel ? [controlPanel] : []));
    }

    refreshOpenControls() {
        for (const record of this.records.values()) {
            if (record.editor && record.controlsOpen && record.details?.open && record.content?.isConnected)
                this.openControls(record, record.details, record.content, true);
        }
    }

    async openControls(record, details, content, refreshOnly = false) {
        const generation = record.controlsGeneration = (record.controlsGeneration || 0) + 1;
        const current = () => details.open && record.content === content && content.isConnected &&
            record.controlsGeneration === generation && !record.deleting;
        if (!details.open) return;
        try {
            if (!record.editor) {
                content.textContent = 'Loading controls…';
                await this.ensureEditor(record);
            }
            if (!current()) return;
            if (!refreshOnly) this.controls(record);
            const schema = JSON.stringify(record.editor.document.$uiSchema);
            const refresh = () => {
                if (!current()) return;
                if (content.contains(document.activeElement)) {
                    content.addEventListener('focusout', () => Promise.resolve().then(refresh), { once: true });
                    return;
                }
                this.controls(record);
            };
            // Saved controls stay usable while additive template fields arrive.
            Promise.allSettled([this.ensureControls(record),
                Promise.resolve().then(() => this.onControls?.(record))]).then(results => {
                const metadata = results[1].status === 'fulfilled' && results[1].value;
                if (current() && (metadata || JSON.stringify(record.editor.document.$uiSchema) !== schema)) refresh();
            }).catch(() => {});
        } catch (error) { if (current()) content.textContent = error.message; }
    }

    async readDocument(record, background, read = null) {
        const request = { record, background: !!background || !!read, started: false };
        if (!read) record.fileRead = request;
        await new Promise((resolve, reject) => {
            Object.assign(request, { resolve, reject });
            if (!read) record.queuedRead = request;
            if (background) this.readQueue.push(request);
            else this.readQueue.unshift(request);
            this.drainReads();
        });
        try { return await (read ? read() : this.client.read(record.name)); }
        finally {
            this.reading--;
            if (request.background) this.backgroundReading--;
            if (record.fileRead === request) record.fileRead = null;
            this.drainReads();
        }
    }

    prioritizeRead(record) {
        const request = record.fileRead;
        if (!request?.background) return;
        request.background = false;
        if (request.started) this.backgroundReading--;
        else {
            const at = this.readQueue.indexOf(request);
            if (at >= 0) this.readQueue.unshift(...this.readQueue.splice(at, 1));
        }
        this.drainReads();
    }

    cancelQueuedRead(record) {
        const request = record?.queuedRead;
        const at = this.readQueue.indexOf(request);
        if (at < 0) return;
        this.readQueue.splice(at, 1);
        record.queuedRead = null;
        if (record.fileRead === request) record.fileRead = null;
        const error = new Error('Preset selection was superseded');
        error.code = 'cancelled';
        request.reject(error);
    }

    async ensureControls(record) {
        const editor = await this.ensureEditor(record);
        const document = editor.document;
        const scene = document.$origin?.scene, type = document.$uiSchema?.effectType;
        if (!scene || !type || record.deleting) return editor;
        const key = JSON.stringify([scene, type]);
        let loading = this.templateSchemas.get(key);
        if (!loading) {
            // The native store has at most 64 cards; keep the page's template cache bounded too.
            if (this.templateSchemas.size >= 64) this.templateSchemas.delete(this.templateSchemas.keys().next().value);
            loading = this.readDocument(record, false, () => this.client.readTemplate(scene));
            this.templateSchemas.set(key, loading);
        }
        try {
            const template = await loading;
            if (template.document?.$uiSchema?.effectType !== type || template.document?.$origin?.scene !== scene)
                throw new Error('The animation template changed');
            if (record.editor === editor && this.records.get(record.name) === record && !record.deleting)
                editor.hydrateSchema(template.document);
        } catch {
            if (this.templateSchemas.get(key) === loading) this.templateSchemas.delete(key);
            if (record.editor === editor && record.status)
                record.status.textContent = 'Additional controls could not be loaded. Your settings are preserved.';
        }
        return editor;
    }

    drainReads() {
        // One of two slots stays available to a direct file read; preview and metadata share the other.
        while (this.reading < 2 && this.readQueue.length) {
            let at = this.readQueue.findIndex(request => !request.background);
            if (at < 0) {
                if (this.backgroundReading >= 1) return;
                at = 0;
            }
            const request = this.readQueue.splice(at, 1)[0];
            if (request.record.queuedRead === request) request.record.queuedRead = null;
            this.reading++;
            request.started = true;
            if (request.background) this.backgroundReading++;
            request.resolve();
        }
    }

    async ensureEditor(record, { background = false } = {}) {
        if (record.editor) return record.editor;
        if (record.loading) {
            if (!background) this.prioritizeRead(record);
            return record.loading;
        }
        record.loading = this.readDocument(record, background).then(({ document, etag }) => {
            record.editor = new PresetEditor({ client: this.client, name: record.name, document, etag,
                onChange: snapshot => {
                    const messages = { pending: "Saving…", saving: "Saving…", saved: "Saved", idle: "Saved",
                        conflict: "Changed elsewhere. Your edits are retained.", error: "Save failed" };
                    if (record.status) record.status.textContent = snapshot.error?.message || messages[snapshot.status] || "";
                    this.onPreview?.();
                    if (snapshot.status === "saved" && !record.flushingForPlay && !record.deleting && record.outputEtag && this.selected === record.name)
                        this.play(record, { outputEtag: record.outputEtag });
                } });
            return record.editor;
        }).finally(() => { record.loading = null; });
        return record.loading;
    }

    async play(record, { outputEtag = null } = {}) {
        if (record.deleting) return;
        if (this.playReading && this.playReading !== record && !this.playReading.controlsOpen)
            this.cancelQueuedRead(this.playReading);
        const generation = this.selectionGeneration = (this.selectionGeneration || 0) + 1;
        this.pending = { record, outputEtag };
        this.selected = record.name;
        for (const other of this.records.values()) {
            other.card?.classList.toggle("active", other === record);
            other.card?.querySelector(".helio-preset-play")?.setAttribute("aria-pressed", String(other === record));
        }
        this.status.textContent = `Selecting ${record.name}…`;
        this.observeSelection(this.onSelect, record, generation);
        if (this.applying) return;
        this.applying = true;
        try {
            while (this.pending) {
                const { record: next, outputEtag: expectedOutput } = this.pending;
                this.pending = null;
                if (next.deleting) continue;
                try {
                    this.playReading = next;
                    let editor;
                    try { editor = await this.ensureEditor(next); }
                    finally { if (this.playReading === next) this.playReading = null; }
                    next.flushingForPlay = true;
                    try {
                        do { await editor.flush(); }
                        while (editor.dirty && !this.pending && !next.deleting);
                    } finally { next.flushingForPlay = false; }
                    if (this.pending || next.deleting) continue;
                    const result = await this.client.apply(next.name, { etag: editor.etag, outputEtag: expectedOutput });
                    next.outputEtag = result.outputEtag || null;
                    if (!this.pending) {
                        this.status.textContent = result.outputEtag ? next.name
                            : `${next.name} · Press Play after further edits to update playback`;
                        this.observeSelection(this.onApply, next, this.selectionGeneration, true);
                    }
                } catch (error) {
                    if (this.pending) continue;
                    this.status.textContent = expectedOutput && error.status === 412
                        ? "Settings saved. Playback changed elsewhere; press Play to select this preset."
                        : error.message;
                    this.selected = null;
                    next.outputEtag = null;
                    for (const item of this.records.values()) {
                        item.card?.classList.remove("active");
                        item.card?.querySelector(".helio-preset-play")?.setAttribute("aria-pressed", "false");
                    }
                }
            }
        } finally { this.applying = false; }
    }

    observeSelection(callback, record, generation, applied = false) {
        if (!callback) return;
        const failed = error => {
            if (generation !== this.selectionGeneration || this.selected !== record.name) return;
            this.status.textContent = `${applied ? "Preset applied. " : ""}Live status unavailable: ${error.message || error}`;
        };
        try { Promise.resolve(callback(record)).catch(failed); }
        catch (error) { failed(error); }
    }

    addForm() {
        const details = document.createElement("details");
        details.className = "helio-add-preset";
        const summary = document.createElement("summary");
        summary.textContent = "Add animation";
        const form = document.createElement("form");
        const label = document.createElement("label");
        label.textContent = "Animation template";
        const select = document.createElement("select");
        label.appendChild(select);
        const nameLabel = document.createElement("label");
        nameLabel.textContent = "Preset name";
        const name = document.createElement("input");
        name.required = true;
        name.maxLength = 31;
        name.pattern = '[\\x20-\\x2D\\x30-\\x5B\\x5D-\\x7E]+';
        name.title = 'Use up to 31 printable ASCII characters; /, \\ and . are not allowed.';
        nameLabel.appendChild(name);
        const create = document.createElement("button");
        create.type = "submit";
        create.textContent = "Add preset";
        const status = document.createElement("p");
        status.setAttribute("role", "status");
        form.append(label, nameLabel, create, status);
        details.append(summary, form);
        details.addEventListener("toggle", async () => {
            if (!details.open || select.options.length) return;
            try {
                const index = await this.client.listTemplates();
                for (const template of index.items) {
                    const option = document.createElement("option");
                    option.value = template.id;
                    option.textContent = template.name;
                    select.appendChild(option);
                }
                name.value = suggestedPresetName(select.selectedOptions[0]?.textContent);
            } catch (error) { status.textContent = error.message; }
        });
        select.addEventListener("change", () => { name.value = suggestedPresetName(select.selectedOptions[0]?.textContent); });
        form.addEventListener("submit", async event => {
            event.preventDefault();
            create.disabled = true;
            try {
                const template = await this.client.readTemplate(select.value);
                delete template.document.$slot;
                await this.client.write(name.value, template.document, { create: true });
                await this.mount(this.host);
            } catch (error) { status.textContent = error.message; }
            finally { create.disabled = false; }
        });
        return details;
    }

    legacyImportForm() {
        const details = document.createElement('details'); details.className = 'helio-add-preset helio-legacy-import';
        const summary = document.createElement('summary'); summary.textContent = 'Import historical WLED looks';
        const form = document.createElement('form');
        const help = document.createElement('p'); help.textContent = 'Choose a prepared review file for this product. Existing looks are never replaced. Historical recordings show old settings; colors and wiring may differ.';
        const label = document.createElement('label'); label.textContent = 'Prepared JSON file';
        const file = document.createElement('input'); file.type = 'file'; file.accept = '.json,application/json'; file.required = true; label.append(file);
        const reviewButton = document.createElement('button'); reviewButton.type = 'submit'; reviewButton.className = 'helio-secondary-action'; reviewButton.textContent = 'Review file';
        const commit = document.createElement('button'); commit.type = 'button'; commit.className = 'helio-primary-action'; commit.textContent = 'Import reviewed looks'; commit.disabled = true;
        const status = document.createElement('p'); status.setAttribute('role', 'status');
        const list = document.createElement('ul');
        let reviewed = null, generation = 0;
        file.addEventListener('change', () => { generation++; reviewed = null; this.notice = null; commit.disabled = true; list.replaceChildren(); status.textContent = ''; });
        form.addEventListener('submit', async event => {
            event.preventDefault(); const current = ++generation; reviewed = null; commit.disabled = true; list.replaceChildren();
            const selected = file.files?.[0];
            if (!selected || selected.size > 1048576) { status.textContent = 'Select a valid file for the configured product.'; return; }
            reviewButton.disabled = true; status.textContent = 'Checking file and controller templates…';
            try {
                const product = await this.legacyProduct?.();
                if (generation !== current) return;
                if (!product) throw new Error('Configure a matching product setup before importing.');
                const result = await prepareLegacyImport(await selected.text(), product, this.client);
                if (generation !== current) return;
                if (await this.legacyProduct?.() !== product) throw new Error('Product configuration changed; review again');
                if (generation !== current) return;
                for (const entry of result.ready) {
                    const row = document.createElement('li');
                    const label = entry.sourceName !== entry.name ? `${entry.sourceName} → ${entry.name}` : entry.name;
                    row.textContent = `${label}: ready${entry.warnings.length ? ' · ' + entry.warnings.join(' ') : ''}`;
                    list.append(row);
                }
                for (const entry of result.skipped) { const row = document.createElement('li'); row.textContent = `${entry.name}: ${entry.reason}`; list.append(row); }
                reviewed = { ...result, product }; commit.disabled = !result.ready.length;
                status.textContent = `${result.ready.length} ready · ${result.skipped.length} skipped. Review before importing.`;
            } catch (error) { if (generation === current) status.textContent = error.message; }
            finally { reviewButton.disabled = false; }
        });
        commit.addEventListener('click', async () => {
            if (!reviewed) return;
            const selected = reviewed; reviewed = null; commit.disabled = true; reviewButton.disabled = true; file.disabled = true;
            try {
                if (await this.legacyProduct?.() !== selected.product) throw new Error('Product configuration changed; review again.');
                const result = await commitLegacyImport(selected, this.client, (done, total) => { status.textContent = `Importing ${done} of ${total}…`; });
                const message = result.failed
                    ? `${result.saved.length} saved. ${result.failed} could not be confirmed; check the list before retrying.`
                    : `${result.saved.length} historical looks imported.`;
                this.notice = message;
                await this.mount(this.host);
                this.status.textContent = message;
            } catch (error) { status.textContent = error.message; }
            finally { reviewButton.disabled = false; file.disabled = false; }
        });
        form.append(help, label, reviewButton, commit, status, list); details.append(summary, form); return details;
    }
}
