// Native preset documents stay on the controller. This adapter shares one
// conditional-write path between scene cards and their debounced editors.
export class PresetApiError extends Error {
    constructor(message, { status = 0, code = '', details = null } = {}) {
        super(message);
        this.name = 'PresetApiError';
        this.status = status;
        this.code = code;
        this.details = details;
    }
}

function quotedETag(value) {
    if (value === null || value === undefined || value === '') return null;
    const tag = String(value);
    if (/^(?:W\/)?"[^"\r\n]*"$/.test(tag)) return tag;
    if (/["\r\n]/.test(tag)) throw new TypeError('Invalid preset ETag');
    return `"${tag}"`;
}

function requiredETag(value) {
    const tag = quotedETag(value);
    if (!tag) throw new TypeError('A preset ETag is required');
    return tag;
}

function cloneDocument(value) {
    if (!value || typeof value !== 'object' || Array.isArray(value))
        throw new TypeError('A native preset document is required');
    return JSON.parse(JSON.stringify(value));
}

function objectAt(document, path) {
    if (!Array.isArray(path) || !path.length || path.some(key => typeof key !== 'string' || !key)) return null;
    let target = document;
    for (const key of path) {
        if (!target || typeof target !== 'object' || Array.isArray(target) || !Object.hasOwn(target, key)) return null;
        target = target[key];
    }
    return target && typeof target === 'object' && !Array.isArray(target) ? target : null;
}

export function createPresetClient({ fetchFn = globalThis.fetch,
    initialIndexResponse = null,
    initialIndexLifecycle = null,
    waitFn = delay => new Promise(resolve => globalThis.setTimeout(resolve, delay)) } = {}) {
    if (initialIndexResponse) Promise.resolve(initialIndexResponse).catch(() => {});
    const presetPath = name => '/api/presets/' + encodeURIComponent(name);
    const templatePath = id => '/api/preset-templates/' + encodeURIComponent(id);
    const request = async (url, options = {}, initialResponse = null, initialLifecycle = null) => {
        let response, result;
        const reading = !options.method || options.method === 'GET';
        for (let attempt = 0; ; attempt++) {
            try {
                try {
                    response = attempt === 0 && initialResponse ? await initialResponse : await fetchFn(url, options);
                } catch (error) {
                    throw new PresetApiError(error?.message || 'Preset request failed', { code: 'network', details: error });
                }
                result = null;
                try { result = await response.json(); } catch { /* Preserve an HTTP failure even with a broken error body. */ }
            } finally {
                if (attempt === 0 && initialResponse) initialLifecycle?.finish?.();
            }
            if (!reading || response.status !== 503 || attempt >= 2) break;
            await waitFn(150 * (attempt + 1));
        }
        if (!response.ok) {
            const fallback = response.status === 412 ? 'Preset changed elsewhere. Your draft is preserved.'
                : response.status === 507 ? 'Controller storage is full. Your draft is preserved.'
                : `Preset request failed (HTTP ${response.status})`;
            throw new PresetApiError(result?.message || result?.error || fallback, {
                status: response.status, code: result?.code || (response.status === 412 ? 'conflict'
                    : response.status === 507 ? 'storage-full' : 'http'), details: result,
            });
        }
        if (!result || typeof result !== 'object' || Array.isArray(result))
            throw new PresetApiError('Invalid preset response', { status: response.status, code: 'invalid-response' });
        return { result, etag: quotedETag(response.headers?.get('ETag') || null) };
    };
    const discardInitialIndex = () => {
        const pending = initialIndexResponse;
        const lifecycle = initialIndexLifecycle;
        initialIndexResponse = null;
        initialIndexLifecycle = null;
        if (lifecycle?.cancel) lifecycle.cancel();
        else if (pending) Promise.resolve(pending).then(response => response.body?.cancel()).catch(() => {});
    };
    const readDocument = async (url, requireTag) => {
        const { result, etag } = await request(url);
        if (requireTag && !etag) throw new PresetApiError('Preset response has no ETag', { code: 'missing-etag' });
        return { document: result, etag };
    };
    const mutation = async (url, options, field) => {
        discardInitialIndex();
        const { result, etag } = await request(url, options);
        if (result[field] !== true) throw new PresetApiError(result.error || 'Preset operation was refused', {
            code: 'refused', details: result,
        });
        return { ...result, etag: etag || quotedETag(result.etag) };
    };
    return {
        async list() {
            const pending = initialIndexResponse;
            const lifecycle = initialIndexLifecycle;
            initialIndexResponse = null;
            initialIndexLifecycle = null;
            return (await request('/api/presets', {}, lifecycle?.discarded ? null : pending, lifecycle)).result;
        },
        read(name) { return readDocument(presetPath(name), true); },
        async write(name, document, { etag = null, create = false } = {}) {
            if (create && etag) throw new TypeError('Create and overwrite conditions are exclusive');
            const headers = { 'Content-Type': 'application/json',
                [create ? 'If-None-Match' : 'If-Match']: create ? '*' : requiredETag(etag) };
            const result = await mutation(presetPath(name), {
                method: 'PUT', headers, body: JSON.stringify(cloneDocument(document)),
            }, 'stored');
            if (!result.etag) throw new PresetApiError('Saved preset has no ETag', { code: 'missing-etag' });
            return result;
        },
        remove(name, { etag } = {}) {
            return mutation(presetPath(name), { method: 'DELETE', headers: { 'If-Match': requiredETag(etag) } }, 'deleted');
        },
        apply(name, { etag, outputEtag } = {}) {
            const headers = { 'Content-Type': 'application/json', 'If-Match': requiredETag(etag) };
            if (outputEtag !== undefined && outputEtag !== null)
                headers['If-Output-Match'] = requiredETag(outputEtag);
            return mutation(presetPath(name) + '/apply', { method: 'POST',
                headers,
                body: JSON.stringify({ scope: 'look' }),
            }, 'applied');
        },
        async listTemplates() { return (await request('/api/preset-templates')).result; },
        readTemplate(id) { return readDocument(templatePath(id), false); },
    };
}

export class PresetEditor {
    constructor({ client, name, document, etag = null, debounceMs = 400,
        onChange = () => {}, onError = () => {}, setTimeoutFn = (callback, delay) => globalThis.setTimeout(callback, delay),
        clearTimeoutFn = timer => globalThis.clearTimeout(timer) }) {
        this._client = client;
        this._name = name;
        this._document = cloneDocument(document);
        this._etag = quotedETag(etag);
        this._debounceMs = debounceMs;
        this._onChange = onChange;
        this._onError = onError;
        this._setTimeout = setTimeoutFn;
        this._clearTimeout = clearTimeoutFn;
        this._timer = null;
        this._revision = this._etag === null ? 1 : 0;
        this._savedRevision = 0;
        this._inFlight = null;
        this._ready = false;
        this._disposed = false;
        this._conflict = false;
        this._status = 'idle';
        this._error = null;
    }

    get name() { return this._name; }
    get document() { return cloneDocument(this._document); }
    get etag() { return this._etag; }
    get dirty() { return this._revision !== this._savedRevision; }
    get status() { return this._status; }
    get error() { return this._error; }

    // Opening controls adds new factory fields locally; only a subsequent edit makes them a save.
    hydrateSchema(template) {
        if (this._disposed) return false;
        const next = this.document;
        const schema = next.$uiSchema, latest = template?.$uiSchema;
        if (!schema?.effectType || schema.effectType !== latest?.effectType ||
            !next.$origin?.scene || next.$origin.scene !== template?.$origin?.scene ||
            !Array.isArray(schema.controls) || !Array.isArray(latest.controls)) return false;
        const effect = objectAt(next, schema.effectPath), source = objectAt(template, latest.effectPath);
        if (!effect || !source || (effect.type && effect.type !== schema.effectType) ||
            (source.type && source.type !== latest.effectType)) return false;
        const samePath = (a, b) => Array.isArray(a) && Array.isArray(b) &&
            a.length === b.length && a.every((key, index) => key === b[index]);
        const names = new Set(schema.controls.map(control => control.name));
        let changed = false;
        for (const control of latest.controls) {
            if (!control || typeof control.name !== 'string' || !control.name || names.has(control.name)) continue;
            const descriptor = cloneDocument(control);
            let target = effect, defaults = source;
            if (descriptor.path !== undefined) {
                if (samePath(descriptor.path, latest.effectPath)) descriptor.path = [...schema.effectPath];
                else if (descriptor.name === 'palette' && samePath(descriptor.path, ['Drivers'])) {
                    defaults = objectAt(template, ['Drivers']);
                    if (!defaults) continue;
                    if (!Object.hasOwn(next, 'Drivers')) next.Drivers = {};
                    target = objectAt(next, ['Drivers']);
                    if (!target) continue;
                } else continue;
            }
            if (!Object.hasOwn(target, descriptor.name)) {
                const value = Object.hasOwn(defaults, descriptor.name) ? defaults[descriptor.name] : descriptor.default;
                if (value === undefined) continue;
                Object.defineProperty(target, descriptor.name, {
                    value: JSON.parse(JSON.stringify(value)), writable: true, enumerable: true, configurable: true,
                });
            }
            schema.controls.push(descriptor);
            names.add(descriptor.name);
            changed = true;
        }
        if (changed) this._document = next;
        return changed;
    }

    snapshot() {
        return { name: this.name, document: this.document, etag: this.etag,
            dirty: this.dirty, status: this.status, error: this.error };
    }

    edit(path, key, value) {
        if (this._disposed) throw new Error('Preset editor is disposed');
        if (!Array.isArray(path) || typeof key !== 'string') throw new TypeError('A document path and control key are required');
        const next = this.document;
        let target = next;
        for (const name of path) {
            if (!Object.hasOwn(target, name) || !target[name] || typeof target[name] !== 'object' || Array.isArray(target[name]))
                throw new TypeError(`Unknown preset path: ${path.join('/')}`);
            target = target[name];
        }
        // defineProperty treats a literal __proto__ control as document data.
        Object.defineProperty(target, key, { value, writable: true, enumerable: true, configurable: true });
        return this.replace(next);
    }

    replace(document) {
        if (this._disposed) throw new Error('Preset editor is disposed');
        this._document = cloneDocument(document);
        this._revision++;
        if (!this._conflict) {
            this._status = 'pending';
            this._error = null;
            this._ready = false;
            this._cancelTimer();
            this._timer = this._setTimeout(() => {
                this._timer = null;
                this.flush().catch(() => {});
            }, this._debounceMs);
        }
        this._notify();
        return this.snapshot();
    }

    flush() {
        if (this._disposed) return Promise.reject(new Error('Preset editor is disposed'));
        if (this._conflict) return Promise.reject(this._error);
        this._cancelTimer();
        this._ready = true;
        if (this._inFlight) return this._inFlight;
        this._inFlight = this._save().finally(() => { this._inFlight = null; });
        return this._inFlight;
    }

    dispose() {
        this._disposed = true;
        this._ready = false;
        this._cancelTimer();
        this._status = 'disposed';
    }

    _cancelTimer() {
        if (this._timer !== null) this._clearTimeout(this._timer);
        this._timer = null;
    }

    _notify() { if (!this._disposed) this._onChange(this.snapshot()); }

    async _save() {
        while (!this._disposed && this._ready && this.dirty) {
            this._ready = false;
            const revision = this._revision;
            const document = this.document;
            this._status = 'saving';
            this._notify();
            try {
                const result = await this._client.write(this._name, document, {
                    etag: this._etag, create: this._etag === null,
                });
                if (this._disposed) return this.snapshot();
                this._etag = requiredETag(result.etag);
                this._savedRevision = revision;
                this._status = this.dirty ? 'pending' : 'saved';
                this._error = null;
                this._notify();
            } catch (error) {
                if (this._disposed) return this.snapshot();
                this._ready = false;
                this._cancelTimer();
                this._conflict = error.status === 412;
                this._status = this._conflict ? 'conflict' : 'error';
                this._error = error;
                this._notify();
                this._onError(error, this.snapshot());
                throw error;
            }
        }
        return this.snapshot();
    }
}
