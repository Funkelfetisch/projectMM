/// The module tree's owner: registration, naming, the tick order and control routing; the public surface lives in Scheduler.h.
#include "core/module/Scheduler.h"
#include "core/module/StateDocument.h"

#include "core/module/Control.h"    // applyControlValue + ApplyResult in setControl
#include "core/util/JsonUtil.h"   // mm::json::parseBool for the "enabled" pseudo-control
#include "core/util/PinList.h"
#include "platform/platform.h"

#include <cstdio>   // std::snprintf in freeName
#include <cstring>  // std::strcmp in firstInTree

namespace mm {

bool Scheduler::pinControlConflicts(const ControlDescriptor& control, const char* valueJson, ApplyPolicy policy, const void* owner) const {
    if (!pinReservations_.any()) return false;
    const bool list = control.type == ControlType::Text && isPinListControl(control.name);
    if (control.type != ControlType::Pin && !list) return false;
    ControlDescriptor probe = control;
    if (control.type == ControlType::Pin) {
        int8_t pin = -1; probe.ptr = &pin;
        if (applyControlValue(probe, valueJson, "value", policy) != ApplyResult::Ok) return false;
        return pin >= 0 && pinReservations_.reservedByOther(owner, static_cast<uint8_t>(pin));
    }
    char text[1024] = {};
    if (probe.max <= 0 || static_cast<size_t>(probe.max) > sizeof(text)) return true;
    probe.ptr = text;
    if (applyControlValue(probe, valueJson, "value", policy) != ApplyResult::Ok) return false;
    if (!text[0]) return false;
    uint16_t pins[64] = {}; uint8_t count = 0;
    if (parsePinList(text, pins, 64, count)) return true;
    for (uint8_t i = 0; i < count; ++i) if (pinReservations_.reservedByOther(owner, pins[i])) return true;
    return false;
}

void Scheduler::addModule(MoonModule* mod) {
    if (!mod || moduleCount_ >= modules_.size()) return;
    modules_[moduleCount_++] = mod;
}

void Scheduler::setup() {
    instance_ = this;   // the one live Scheduler, reachable via Scheduler::instance()
    preparationRevision_ = 0;
    failedPreparationRevision_ = 0;
    preparationResult_ = {};
    prepareRequested_.store(false, std::memory_order_relaxed);
    for (auto& scope : preparePending_) scope.store(nullptr, std::memory_order_relaxed);
    startTime_ = platform::millis();

    // Phase 1: bind each module's controls from scratch, so the persistence hook can apply file values to (name → variable pointer) descriptors.
    for (uint8_t i = 0; i < moduleCount_; i++) {
        modules_[i]->rebuildControls();
    }

    // Phase 2: persistence load. No-op if no hook is set.
    if (loadAllHook_) loadAllHook_(this);

    // Phase 2b: re-run defineControls with persisted values in place, so conditional hidden flags see the loaded state rather than the default.
    if (loadAllHook_) {
        for (uint8_t i = 0; i < moduleCount_; i++) {
            modules_[i]->rebuildControls();
        }
    }

    // Phase 3: each module's own init, with the persisted values already in its members.
    for (uint8_t i = 0; i < moduleCount_; i++) {
        modules_[i]->setup();
    }

    // Phase 4: build derived state, buffers/peripherals, via the applyState() router, which per node builds when effectively-enabled and releases (release) when not, recursing the tree.
    for (uint8_t i = 0; i < moduleCount_; i++) {
        modules_[i]->applyState();
    }

    // Phase 5: a MoonLive script's declared controls exist only once phase 4 has compiled it, so the values the load held for them land now.
    preparationResult_ = applyDeferredControls(*this);
    if (!preparationResult_.ok) ++failedPreparationRevision_;
    ++preparationRevision_;

    lastLoop20ms_ = platform::millis();
    lastLoop1s_ = platform::millis();
    lastTimingUpdate_ = platform::millis();
}

void Scheduler::tick() MM_NONBLOCKING {
    uint32_t now = platform::millis();
    uint32_t tickStart = platform::micros();

    // A requested rebuild runs here at a frame boundary; the analyzer reports it transitively and that finding stays on purpose, the only signal if the gate is lost.
    preparePending();

    // Disabled modules do not tick, except system modules that override `respectsEnabled()` to return false so users can re-enable others through them.
    auto shouldRun = [](MoonModule* m) {
        return !m->respectsEnabled() || m->enabled();
    };
    for (uint8_t i = 0; i < moduleCount_; i++) {
        if (!shouldRun(modules_[i])) continue;
        uint32_t modStart = platform::micros();
        modules_[i]->tick();
        modules_[i]->addAccumUs(platform::micros() - modStart);
    }

    if (now - lastLoop20ms_ >= 20) {
        lastLoop20ms_ = now;
        for (uint8_t i = 0; i < moduleCount_; i++) {
            if (!shouldRun(modules_[i])) continue;
            uint32_t modStart = platform::micros();
            modules_[i]->tick20ms();
            modules_[i]->addAccumUs(platform::micros() - modStart);
        }
    }

    if (now - lastLoop1s_ >= 1000) {
        lastLoop1s_ = now;
        for (uint8_t i = 0; i < moduleCount_; i++) {
            if (!shouldRun(modules_[i])) continue;
            uint32_t modStart = platform::micros();
            modules_[i]->tick1s();
            modules_[i]->addAccumUs(platform::micros() - modStart);
        }
    }

    tickAccumUs_ += platform::micros() - tickStart;
    frameCount_++;

    // Every 1 second: compute averages, recurse into children
    if (now - lastTimingUpdate_ >= 1000) {
        tickTimeUs_ = frameCount_ > 0 ? tickAccumUs_ / frameCount_ : 0;

        for (uint8_t i = 0; i < moduleCount_; i++) {
            modules_[i]->publishTiming(frameCount_);
        }

        tickAccumUs_ = 0;
        frameCount_ = 0;
        lastTimingUpdate_ = now;
    }
}

void Scheduler::shutdownStep(uint32_t now) {
    for (uint8_t i = moduleCount_; i > 0; --i)
        if (modules_[i - 1]) modules_[i - 1]->shutdownStep(now);
}

bool Scheduler::release() {
    // Inspect the whole tree before touching any sibling. An owner must finish
    // its own checked handover before the caller retries shutdown.
    for (uint8_t i = 0; i < moduleCount_; ++i)
        if (modules_[i] && !modules_[i]->subtreeRemovalAllowed()) return false;
    // A worker can discover an unsuccessful hardware handover only while
    // quiescing. Check again before releasing or deleting any module.
    for (uint8_t i = moduleCount_; i > 0; --i)
        if (modules_[i - 1]) modules_[i - 1]->quiesceSubtreeForRemoval();
    for (uint8_t i = 0; i < moduleCount_; ++i)
        if (modules_[i] && !modules_[i]->subtreeRemovalAllowed()) return false;
    // Two passes: release every module first so a release can still observe its siblings, then delete the trees.
    for (uint8_t i = moduleCount_; i > 0; i--) {
        modules_[i - 1]->release();
    }
    for (uint8_t i = moduleCount_; i > 0; i--) {
        deleteTree(modules_[i - 1]);
    }
    moduleCount_ = 0;
    instance_ = nullptr;
    // A document never applied goes with the tree it was meant for, and is answered as refused, so whoever queued it stops waiting.
    for (auto& slot : pendingLive_) {
        if (!slot.text) continue;
        const PendingLive entry = slot;
        slot = {};
        platform::free(entry.text);
        if (entry.done) {
            StateDocumentResult refused;
            refused.ok = false;
            refused.error = "the device stopped before applying it";
            entry.done(entry.tag, refused);
        }
    }
    prepareRequested_.store(false, std::memory_order_relaxed);
    for (auto& scope : preparePending_) scope.store(nullptr, std::memory_order_relaxed);
    preparationResult_ = {};
    preparationRevision_ = 0;
    failedPreparationRevision_ = 0;
    return true;
}

uint32_t Scheduler::elapsed() const {
    return platform::millis() - startTime_;
}

void Scheduler::preparePending() {
    // A live document first, so the rebuild it asks for runs in this same frame.
    applyPendingLive();
    MoonModule* scopes[kPendingScopes];
    uint8_t scopeCount = 0;
    for (auto& slot : preparePending_)
        if (MoonModule* m = slot.exchange(nullptr, std::memory_order_relaxed)) scopes[scopeCount++] = m;
    if (prepareRequested_.exchange(false, std::memory_order_relaxed)) prepareTree();   // the tree covers every scope
    else if (scopeCount) prepareSubtrees(scopes, scopeCount);
}

bool Scheduler::preparationPending() const MM_NONBLOCKING {
    if (prepareRequested_.load(std::memory_order_relaxed)) return true;
    for (const auto& scope : preparePending_)
        if (scope.load(std::memory_order_relaxed)) return true;
    return false;
}

bool Scheduler::applyLive(char* text, LiveDone done, uintptr_t tag) {
    if (!text) return false;
    for (auto& slot : pendingLive_) {
        if (slot.text) continue;
        slot = {text, done, tag};
        return true;
    }
    return false;
}

void Scheduler::applyPendingLive() {
    for (auto& slot : pendingLive_) {
        if (!slot.text) continue;
        const PendingLive entry = slot;
        slot = {};   // free before the apply, so a done callback may queue the next one
        const StateDocumentResult r = applyStateDocument(*this, entry.text, StateSource::Live);
        platform::free(entry.text);
        if (entry.done) entry.done(entry.tag, r);
    }
}

void Scheduler::prepareTree() {
    // This walk is the one a pending request asked for; a request made during it still stands.
    prepareRequested_.store(false, std::memory_order_relaxed);
    for (auto& scope : preparePending_) scope.store(nullptr, std::memory_order_relaxed);
    for (uint8_t i = 0; i < moduleCount_; i++) {
        modules_[i]->applyState();
    }
    // Controls a state document named before their script compiled; this prepare compiled it.
    preparationResult_ = applyDeferredControls(*this);
    if (!preparationResult_.ok) ++failedPreparationRevision_;
    // Last, so a module reading across the tree sees every script's controls.
    for (uint8_t i = 0; i < moduleCount_; i++) modules_[i]->onTreePrepared();
    ++preparationRevision_;
}

namespace {
// Whether `m` is `root` or below it, so a pending scope removed since it was requested is never touched.
bool holds(const MoonModule* root, const MoonModule* m) {
    if (root == m) return true;
    for (uint8_t i = 0; i < root->childCount(); i++)
        if (const MoonModule* c = root->child(i); c && holds(c, m)) return true;
    return false;
}
}  // namespace

void Scheduler::prepareSubtrees(MoonModule* const* scopes, uint8_t count) {
    if (!count) return;
    for (uint8_t s = 0; s < count; s++) {
        bool present = false;
        for (uint8_t i = 0; i < moduleCount_ && !present; i++) present = modules_[i] && holds(modules_[i], scopes[s]);
        if (!present) { prepareTree(); return; }   // gone since the request: the tree it changed is rebuilt whole
    }
    for (auto& slot : preparePending_) {
        for (uint8_t s = 0; s < count; s++) {
            MoonModule* expected = scopes[s];
            slot.compare_exchange_strong(expected, nullptr, std::memory_order_relaxed);
        }
    }
    for (uint8_t s = 0; s < count; s++) scopes[s]->applyState();
    preparationResult_ = applyDeferredControls(*this);
    if (!preparationResult_.ok) ++failedPreparationRevision_;
    for (uint8_t i = 0; i < moduleCount_; i++) modules_[i]->onTreePrepared();
    ++preparationRevision_;
}

namespace {
// One walk for every tree-wide notification, so each hook reaches the same set of modules.
template <class F>
void forEachInTree(MoonModule* m, F&& f) {
    f(*m);
    for (uint8_t i = 0; i < m->childCount(); i++)
        if (MoonModule* c = m->child(i)) forEachInTree(c, f);
}
}  // namespace

void Scheduler::notifyFileChanged(const char* path) {
    for (uint8_t i = 0; i < moduleCount_; i++)
        if (modules_[i]) forEachInTree(modules_[i], [path](MoonModule& m) { m.onFileChanged(path); });
}

void Scheduler::notifyListChanged(const MoonModule& owner) {
    for (uint8_t i = 0; i < moduleCount_; i++)
        if (modules_[i]) forEachInTree(modules_[i], [&owner](MoonModule& m) { m.onListChanged(owner); });
}

void Scheduler::deleteTree(MoonModule* mod) {
    if (!mod) return;
    for (uint8_t i = 0; i < mod->childCount(); i++) {
        deleteTree(mod->child(i));
    }
    delete mod;
}

MoonModule* Scheduler::topOfType(const char* typeName) const {
    for (uint8_t i = 0; typeName && i < moduleCount_; i++)
        if (modules_[i] && std::strcmp(modules_[i]->typeName(), typeName) == 0) return modules_[i];
    return nullptr;
}

// The separator is '-' because the name is a URL path segment; a suffix that would not fit is refused rather than truncated.
bool Scheduler::freeName(const char* base, char* out, size_t cap) {
    if (!base || !base[0] || std::strlen(base) >= MoonModule::kNameLen || cap < MoonModule::kNameLen) return false;
    char candidate[MoonModule::kNameLen];
    std::snprintf(candidate, sizeof(candidate), "%s", base);
    for (int suffix = 2; firstByName(candidate); suffix++) {
        const int n = std::snprintf(candidate, sizeof(candidate), "%s-%d", base, suffix);
        if (suffix >= 100 || n < 0 || n >= static_cast<int>(sizeof(candidate))) return false;
    }
    std::snprintf(out, cap, "%s", candidate);
    return true;
}


MoonModule* Scheduler::firstByName(const char* name) {
    if (!name || name[0] == 0) return nullptr;   // firstInTree strcmps name; a null would be UB
    for (uint8_t i = 0; i < moduleCount_; i++) {
        if (auto* m = firstInTree(modules_[i], name)) return m;
    }
    return nullptr;
}

Scheduler::SetControlResult Scheduler::setControl(const char* moduleName,
                                                 const char* controlName,
                                                 const char* valueJson, bool live) {
    MoonModule* target = firstByName(moduleName);
    if (!target) return SetControlResult::ModuleNotFound;
    if (!target->acceptsControlWrite(controlName) ||
        (std::strcmp(controlName, "enabled") == 0 && !target->subtreeRemovalAllowed()))
        return SetControlResult::ReadOnly;
    // Restored however this returns, so only the writes this one makes inherit it.
    struct LiveScope {
        bool& flag; bool was;
        LiveScope(bool& f, bool l) : flag(f), was(f) { flag = flag || l; }
        ~LiveScope() { flag = was; }
    } liveScope(liveWrite_, live);

    // Module-level "enabled" pseudo-control, toggles the flag, then a full rebuild so the disabled subtree stops/starts ticking.
    if (std::strcmp(controlName, "enabled") == 0) {
        const bool enabled = mm::json::parseBool(valueJson, "value");
        if (!enabled && target->enabledSetting() && !target->prepareSubtreeRemoval()) return SetControlResult::ReadOnly;
        target->setEnabled(enabled);
        if (!liveWrite_) {   // a service flipping a look is driving it, as for any other control
            target->markDirty();
            if (noteDirtyHook_) noteDirtyHook_();
        }
        requestPrepareTree();
        // `enabled` rides the FULL state, not the per-leaf patch, so a full resync (the schema-change signal) is what tells the client.
        MoonModule::notifySchemaChanged();
        return SetControlResult::Ok;
    }

    auto& ctrls = target->controls();
    for (uint8_t i = 0; i < ctrls.count(); i++) {
        auto& c = ctrls[i];
        if (std::strcmp(c.name, controlName) != 0) continue;

        if (pinControlConflicts(c, valueJson, ApplyPolicy::Strict, target->pinReservationOwner())) return SetControlResult::ReadOnly;
        if (!target->prepareControlWrite(controlName)) return SetControlResult::ReadOnly;

        // Per-type parse, validate and apply live in Control.cpp; a non-Ok result leaves the storage untouched.
        switch (applyControlValue(c, valueJson, "value")) {
            case ApplyResult::Ok:         break;
            case ApplyResult::OutOfRange: return SetControlResult::OutOfRange;
            case ApplyResult::Malformed:  return SetControlResult::Malformed;
            case ApplyResult::ReadOnly:   return SetControlResult::ReadOnly;
        }
        reactToControlChange(target, controlName);
        // A live control (ControlDescriptor::live), or a live write such as a service's, is driven rather than configured, so it never marks the tree dirty; `c` is the descriptor just applied.
        if (!c.live && !liveWrite_) {
            target->markDirty();
            if (noteDirtyHook_) noteDirtyHook_();
            // Every other page shows a configured write on the next transport tick rather than the next second; a driven one rides the second's walk.
            target->notifyValuesChanged();
        }
        return SetControlResult::Ok;
    }
    return SetControlResult::ControlNotFound;
}

void Scheduler::reactToControlChange(MoonModule* target, const char* controlName) {
    // The list rebuilds first, so defineControls() re-evaluates conditional visibility for the new value.
    target->rebuildControls();
    target->onControlChanged(controlName);
    if (!target->affectsPrepare(controlName)) return;
    switch (target->prepareScope()) {
        case PrepareScope::Self:   requestPrepare(target); break;
        case PrepareScope::Parent: requestPrepare(target->parent()); break;   // a top module has none: the tree
        case PrepareScope::Tree:   requestPrepareTree(); break;
    }
}

namespace {

/// Is this control a Bool (or the `enabled` pseudo-control), the one type the byte reader scales to full range?
bool boolTyped(MoonModule* target, const char* controlName) {
    if (std::strcmp(controlName, "enabled") == 0) return true;
    auto& ctrls = target->controls();
    for (uint8_t i = 0; i < ctrls.count(); i++)
        if (std::strcmp(ctrls[i].name, controlName) == 0)
            return ctrls[i].type == ControlType::Bool;
    return false;
}

}  // namespace

bool Scheduler::getControl(const char* moduleName, const char* controlName,
                           uint8_t& out) const {
    // Derived from the wide reader: a surface has 8 bits of travel, so a wider value clamps and a Bool reads back 255.
    int32_t wide = 0;
    if (!getControlWide(moduleName, controlName, wide)) return false;

    // A Bool is 0/1 at its own width; the surface wants it at full scale.
    MoonModule* target = const_cast<Scheduler*>(this)->firstByName(moduleName);
    if (target && boolTyped(target, controlName)) { out = wide ? 255 : 0; return true; }

    out = static_cast<uint8_t>(wide < 0 ? 0 : (wide > 255 ? 255 : wide));
    return true;
}

bool Scheduler::getControlWide(const char* moduleName, const char* controlName,
                               int32_t& out) const {
    if (!moduleName || !controlName) return false;
    MoonModule* target = const_cast<Scheduler*>(this)->firstByName(moduleName);
    if (!target) return false;

    // 0/1 here, not the byte reader's 0/255: this answers in the control's own units.
    if (std::strcmp(controlName, "enabled") == 0) {
        out = target->enabled() ? 1 : 0;
        return true;
    }

    auto& ctrls = target->controls();
    for (uint8_t i = 0; i < ctrls.count(); i++) {
        const auto& c = ctrls[i];
        if (std::strcmp(c.name, controlName) != 0) continue;
        if (!c.ptr) return false;
        switch (c.type) {
            case ControlType::Bool:
                out = *static_cast<const bool*>(c.ptr) ? 1 : 0;
                return true;
            case ControlType::Uint8:
            case ControlType::Select:
            case ControlType::Palette:
                out = *static_cast<const uint8_t*>(c.ptr);
                return true;
            case ControlType::Uint16:
                out = *static_cast<const uint16_t*>(c.ptr);
                return true;
            case ControlType::Int16:
                out = *static_cast<const int16_t*>(c.ptr);
                return true;
            case ControlType::Int32:
                out = *static_cast<const int32_t*>(c.ptr);
                return true;
            // A Pin is int8_t storage (ControlList::addPin), so it cannot share the Int32 case.
            case ControlType::Pin:
                out = *static_cast<const int8_t*>(c.ptr);
                return true;
            // Text, a file path, a password and a button hold no number.
            default:
                return false;
        }
    }
    return false;
}


MoonModule* Scheduler::firstInTree(MoonModule* mod, const char* name) {
    if (!mod) return nullptr;
    if (mod->name() && std::strcmp(mod->name(), name) == 0) return mod;
    for (uint8_t i = 0; i < mod->childCount(); i++) {
        if (auto* m = firstInTree(mod->child(i), name)) return m;
    }
    return nullptr;
}

} // namespace mm
