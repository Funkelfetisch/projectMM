/// @module Scheduler

#include "doctest.h"
#include "core/module/StateDocument.h"
#include "core/module/MoonModule.h"
#include "core/module/Scheduler.h"
#include "core/util/ModuleFactory.h"
#include "core/util/JsonSink.h"
#include "light/drivers/RmtLedDriver.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

struct SdKnob : public mm::MoonModule {
    uint8_t value = 10;
    char label[16] = "init";
    void defineControls() override {
        controls_.addControl("value", value, 0, 100);
        controls_.addText("label", label, sizeof(label));
    }
};
struct SdSecret : public mm::MoonModule {
    char pass[16] = "hunter2";
    char note[16] = "visible";
    void defineControls() override {
        controls_.addPassword("pass", pass, sizeof(pass));
        controls_.addText("note", note, sizeof(note));
    }
};
// A module whose controls appear once prepare has compiled its source, as a MoonLive script's do; a new module has no source yet.
struct SdScript : public mm::MoonModule {
    char source[16] = "";
    char compiledFrom[16] = "";
    uint8_t late = 1;
    void defineControls() override {
        controls_.addText("source", source, sizeof(source));
        if (compiledFrom[0]) controls_.addControl("late", late, 0, 100);
    }
    bool declaresControlsAtPrepare() const override { return true; }
    bool affectsPrepare(const char* key) const override { return std::strcmp(key, "source") == 0; }
    void prepare() override {
        if (std::strcmp(compiledFrom, source) == 0) return;
        std::snprintf(compiledFrom, sizeof(compiledFrom), "%s", source);
        rebuildControls();
    }
};
struct SdCount : public mm::MoonModule {
    int prepares = 0;
    void prepare() override { prepares++; }
};
struct SdOther : public mm::MoonModule {
    uint8_t speed = 1;
    void defineControls() override { controls_.addControl("speed", speed, 0, 9); }
};
struct SdBox : public mm::MoonModule {
    const char* acceptsChildRoles() const override { return "generic,effect"; }
};
// A module the user cannot remove or replace, as Improv provisioning is.
struct SdFixed : public SdBox {
    bool userEditable() const override { return false; }
};
struct SdHandover : public SdKnob {
    bool active = true;
    bool userEditable() const override { return !active; }
    bool blocksAncestorRemoval() const override { return active; }
    bool acceptsControlWrite(const char*) const override { return !active; }
};
struct SdPins : public mm::MoonModule {
    int8_t gpio = -1;
    char lanePins[24] = "";
    void defineControls() override {
        controls_.addPin("gpio", gpio);
        controls_.addText("lanePins", lanePins, sizeof(lanePins));
    }
};
struct SdDriver : public mm::MoonModule {
    mm::ModuleRole role() const MM_NONBLOCKING override { return mm::ModuleRole::Driver; }
};

void registerTypes() {
    static bool done = false;
    if (done) return;
    mm::ModuleFactory::registerType<SdKnob>("SdKnob");
    mm::ModuleFactory::registerType<SdOther>("SdOther");
    mm::ModuleFactory::registerType<SdSecret>("SdSecret");
    mm::ModuleFactory::registerType<SdScript>("SdScript");
    mm::ModuleFactory::registerType<SdCount>("SdCount");
    mm::ModuleFactory::registerType<SdBox>("SdBox");
    mm::ModuleFactory::registerType<SdDriver>("SdDriver");
    mm::ModuleFactory::registerType<SdFixed>("SdFixed");
    mm::ModuleFactory::registerType<SdHandover>("SdHandover");
    mm::ModuleFactory::registerType<SdPins>("SdPins");
    done = true;
}

// A tree with one top-level container, Effects, as main.cpp wires its own.
struct Tree {
    mm::Scheduler s;
    SdBox* effects = new SdBox();
    Tree() {
        registerTypes();
        effects->setName("Effects");
        s.addModule(effects);
    }
    ~Tree() { s.release(); }
    mm::StateDocumentResult apply(const char* json) { return mm::applyStateDocument(s, json); }
    mm::MoonModule* find(const char* name) { return s.firstByName(name); }
    std::string children(mm::MoonModule* m) {
        std::string out;
        for (uint8_t i = 0; i < m->childCount(); i++) out += std::string(i ? "," : "") + m->child(i)->name();
        return out;
    }
};

uint8_t valueOf(mm::MoonModule* m) { return static_cast<SdKnob*>(m)->value; }

}  // namespace

TEST_CASE("a state document creates a module with its controls in one go") {
    Tree t;
    auto r = t.apply(R"({"Effects": {"Layer": {"type": "SdBox", "Knob": {"type": "SdKnob", "value": 42, "label": "hi"}}}})");
    REQUIRE(r.ok);
    REQUIRE(t.find("Knob") != nullptr);
    CHECK(valueOf(t.find("Knob")) == 42);
    CHECK(std::string(static_cast<SdKnob*>(t.find("Knob"))->label) == "hi");
    CHECK(t.children(t.effects) == "Layer");
    CHECK(r.changes == 4);   // two modules, two controls
}

TEST_CASE("a state document sets a control of an existing module and leaves the rest alone") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects": {"Knob": {"type": "SdKnob", "value": 5}, "Other": {"type": "SdOther", "speed": 3}}})").ok);
    REQUIRE(t.apply(R"({"Effects": {"Knob": {"value": 77}}})").ok);
    CHECK(valueOf(t.find("Knob")) == 77);
    CHECK(static_cast<SdOther*>(t.find("Other"))->speed == 3);
    CHECK(t.children(t.effects) == "Knob,Other");
}

TEST_CASE("a state document names the first failure and where it is") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects": {"Knob": {"type": "SdKnob"}}})").ok);
    auto r = t.apply(R"({"Effects": {"Knob": {"nope": 1}}})");
    CHECK_FALSE(r.ok);
    CHECK(std::string(r.error) == "no such control");
    CHECK(std::string(r.where) == "Effects.Knob.nope");

    r = t.apply(R"({"Effects": {"Ghost": {"value": 1}}})");
    CHECK(std::string(r.error) == "no such module, and no type to create it");
    CHECK(std::string(r.where) == "Effects.Ghost");

    r = t.apply(R"({"Effects": {"Knob": {"value": 200}}})");
    CHECK(std::string(r.error) == "value out of range");

    r = t.apply(R"({"Nowhere": {}})");
    CHECK(std::string(r.error) == "no such top-level module");

    r = t.apply(R"({"Effects": {"Thing": {"type": "NoSuchType"}}})");
    CHECK(std::string(r.error) == "unknown type");
}

TEST_CASE("a state document that does not parse changes nothing") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects": {"Knob": {"type": "SdKnob", "value": 5}}})").ok);
    auto r = t.apply(R"({"Effects": {"Knob": {"value": 9})");
    CHECK_FALSE(r.ok);
    CHECK(std::string(r.error) == "malformed JSON");
    CHECK(valueOf(t.find("Knob")) == 5);
    CHECK(t.apply("[1,2]").error == std::string("a document is a JSON object"));
}

TEST_CASE("null removes a module, and removing an absent one is no error") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects": {"Knob": {"type": "SdKnob"}, "Other": {"type": "SdOther"}}})").ok);
    REQUIRE(t.apply(R"({"Effects": {"Knob": null, "Missing": null}})").ok);
    CHECK(t.find("Knob") == nullptr);
    CHECK(t.children(t.effects) == "Other");
}

TEST_CASE("$patch replace keeps exactly the listed children, in the document's order") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects": {"A": {"type": "SdKnob"}, "B": {"type": "SdKnob"}, "C": {"type": "SdKnob"}}})").ok);
    REQUIRE(t.apply(R"({"Effects": {"$patch": "replace", "C": {}, "New": {"type": "SdOther"}, "A": {"value": 3}}})").ok);
    CHECK(t.children(t.effects) == "C,New,A");
    CHECK(t.find("B") == nullptr);
    CHECK(valueOf(t.find("A")) == 3);
}

TEST_CASE("$patch replace explicitly reenables an existing matching child") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects":{"RGB":{"type":"SdKnob","enabled":false}}})").ok);
    REQUIRE(t.find("RGB") != nullptr);
    CHECK_FALSE(t.find("RGB")->enabledSetting());
    REQUIRE(t.apply(R"({"Effects":{"$patch":"replace","RGB":{"type":"SdKnob","enabled":true}}})").ok);
    CHECK(t.find("RGB")->enabledSetting());
}

TEST_CASE("key order is creation order") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects": {"Z": {"type": "SdKnob"}, "M": {"type": "SdKnob"}, "A": {"type": "SdKnob"}}})").ok);
    CHECK(t.children(t.effects) == "Z,M,A");
}

TEST_CASE("a different type replaces the module in place, under the same name") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects": {"First": {"type": "SdKnob"}, "Slot": {"type": "SdKnob"}, "Last": {"type": "SdKnob"}}})").ok);
    REQUIRE(t.apply(R"({"Effects": {"Slot": {"type": "SdOther", "speed": 4}}})").ok);
    REQUIRE(t.find("Slot") != nullptr);
    CHECK(std::string(t.find("Slot")->typeName()) == "SdOther");
    CHECK(static_cast<SdOther*>(t.find("Slot"))->speed == 4);
    CHECK(t.children(t.effects) == "First,Slot,Last");
}

TEST_CASE("a state document keeps the tree's rules: roles, unique names, name length") {
    Tree t;
    auto r = t.apply(R"({"Effects": {"Drv": {"type": "SdDriver"}}})");
    CHECK(std::string(r.error) == "a module of this role cannot go here");
    CHECK(t.find("Drv") == nullptr);

    REQUIRE(t.apply(R"({"Effects": {"Layer": {"type": "SdBox", "Knob": {"type": "SdKnob"}}}})").ok);
    r = t.apply(R"({"Effects": {"Knob": {"type": "SdKnob"}}})");
    CHECK(std::string(r.error) == "that name is used elsewhere in the tree");

    r = t.apply(R"({"Effects": {"ANameLongerThanFifteen": {"type": "SdKnob"}}})");
    CHECK(std::string(r.error) == "a module name has 1 to 15 characters");
    r = t.apply(R"({"Effects": {"": {"type": "SdKnob"}}})");   // nothing could address it afterwards
    CHECK(std::string(r.error) == "a module name has 1 to 15 characters");
}

// A clash found halfway would leave a replaced container half rebuilt, so every rule the apply pass enforces is checked before anything changes.
TEST_CASE("a state document that breaks a tree rule anywhere changes nothing") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects": {"Old": {"type": "SdFixed", "Knob": {"type": "SdKnob"}}}})").ok);

    auto r = t.apply(R"({"Effects": {"$patch": "replace", "A": {"type": "SdBox", "Area": {"type": "SdKnob"}}, "B": {"type": "SdBox", "Area": {"type": "SdKnob"}}}})");
    CHECK(std::string(r.error) == "that name is used twice in the document");
    CHECK(std::string(r.where) == "Effects.A.Area");
    CHECK(r.changes == 0);
    CHECK(t.children(t.effects) == "Old");

    // Old.Knob stays: the document neither removes nor replaces Old.
    r = t.apply(R"({"Effects": {"$patch": "replace", "Old": {}, "New": {"type": "SdBox", "Knob": {"type": "SdKnob"}}}})");
    CHECK(std::string(r.error) == "that name is used elsewhere in the tree");
    CHECK(t.children(t.effects) == "Old");

    r = t.apply(R"({"Effects": {"Old": null}})");
    CHECK(std::string(r.error) == "this module cannot be removed");
    r = t.apply(R"({"Effects": {"Old": {"type": "SdKnob"}}})");
    CHECK(std::string(r.error) == "this module cannot be replaced");
    CHECK(t.children(t.find("Old")) == "Knob");
}

TEST_CASE("a state document cannot remove an ancestor of an active resource handover") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects":{"Box":{"type":"SdBox","Inner":{"type":"SdBox","Held":{"type":"SdHandover"}}}}})").ok);
    const auto* held = t.find("Held"); REQUIRE(held);
    for (const char* document : {
        R"({"Effects":{"Box":null}})",
        R"({"Effects":{"Box":{"type":"SdOther"}}})",
        R"({"Effects":{"Box":{"enabled":false}}})",
        R"({"Effects":{"$patch":"replace","Fresh":{"type":"SdBox"}}})"
    }) {
        const auto result = t.apply(document);
        CHECK_FALSE(result.ok); CHECK(result.changes == 0);
        CHECK(t.find("Held") == held); CHECK(t.find("Box") != nullptr);
        CHECK(t.find("Box")->enabledSetting());
        CHECK(t.find("Fresh") == nullptr);
    }
    static_cast<SdHandover*>(t.find("Held"))->active = false;
    REQUIRE(t.apply(R"({"Effects":{"Box":null}})").ok);
    CHECK(t.find("Held") == nullptr);
}

TEST_CASE("scheduler shutdown retains the whole tree until a resource handover is complete") {
    struct Held final : mm::MoonModule {
        bool active = true;
        unsigned steps = 0, ticks = 0;
        unsigned* released;
        explicit Held(unsigned& count) : released(&count) {}
        bool blocksAncestorRemoval() const override { return active; }
        void tick() MM_NONBLOCKING override { ++ticks; }
        void shutdownStep(uint32_t) MM_NONBLOCKING override { if (++steps == 3) active = false; }
        void release() override { ++*released; mm::MoonModule::release(); }
    };
    unsigned released = 0;
    mm::Scheduler scheduler;
    auto* parent = new SdBox(); parent->setName("Parent");
    auto* held = new Held(released); held->setName("Held");
    auto* sibling = new Held(released); sibling->setName("Sibling"); sibling->active = false;
    REQUIRE(parent->addChild(held)); REQUIRE(parent->addChild(sibling)); scheduler.addModule(parent);
    CHECK_FALSE(scheduler.release());
    CHECK(scheduler.firstByName("Held") == held); CHECK(released == 0);
    scheduler.shutdownStep(100); CHECK(held->steps == 1); CHECK(held->ticks == 0); CHECK_FALSE(scheduler.release());
    scheduler.shutdownStep(110); CHECK(held->steps == 2); CHECK_FALSE(scheduler.release());
    scheduler.shutdownStep(120); CHECK(held->steps == 3); CHECK(held->ticks == 0);
    CHECK(scheduler.release()); CHECK(released == 2); CHECK(scheduler.firstByName("Held") == nullptr);
}

TEST_CASE("a held RMT output survives ancestor removal in live and stored documents") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects":{"Box":{"type":"SdBox"}}})").ok);
    auto* box = t.find("Box"); REQUIRE(box);
    auto* driver = new mm::RmtLedDriver(); driver->setName("HeldRmt");
    REQUIRE(box->addChild(driver));
    driver->requestBlackFrame(52, 4);
    for (auto source : {mm::StateSource::Request, mm::StateSource::Live, mm::StateSource::Stored}) {
        const auto result = mm::applyStateDocument(t.s, R"({"Effects":{"Box":null}})", source);
        CHECK((!result.ok || result.skipped > 0));
        CHECK(t.find("Box") == box); CHECK(t.find("HeldRmt") == driver);
    }
    // This fixture never starts a peripheral; clear its synthetic hold for teardown.
    REQUIRE(box->removeChild(driver)); delete driver;
}

TEST_CASE("a resource handover rejects a control write before changing other request values") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects":{"Held":{"type":"SdHandover"},"Other":{"type":"SdKnob"}}})").ok);
    auto* held = static_cast<SdHandover*>(t.find("Held"));
    auto* other = static_cast<SdKnob*>(t.find("Other")); REQUIRE(held); REQUIRE(other);
    for (auto source : {mm::StateSource::Request, mm::StateSource::Live}) {
        auto result = mm::applyStateDocument(t.s, R"({"Effects":{"Other":{"value":42},"Held":{"value":31}}})", source);
        CHECK_FALSE(result.ok); CHECK(result.changes == 0); CHECK(other->value == 10); CHECK(held->value == 10);
    }
    CHECK(t.s.setControl("Held", "enabled", "false") == mm::Scheduler::SetControlResult::ReadOnly);
    CHECK(t.s.setControl("Effects", "enabled", "false") == mm::Scheduler::SetControlResult::ReadOnly);
    auto stored = mm::applyStateDocument(t.s, R"({"Effects":{"Held":{"value":31,"enabled":false}}})", mm::StateSource::Stored);
    CHECK(stored.skipped > 0); CHECK(held->value == 10); CHECK(held->enabledSetting());
    stored = mm::applyStateDocument(t.s, R"({"Effects":{"enabled":false}})", mm::StateSource::Stored);
    CHECK(stored.skipped > 0); CHECK(t.effects->enabledSetting());
    held->active = false;
}

TEST_CASE("a fixed child without an active handover does not lock its owner") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects":{"Box":{"type":"SdBox","Fixed":{"type":"SdFixed"}}}})").ok);
    REQUIRE(t.apply(R"({"Effects":{"Box":null}})").ok);
    CHECK(t.find("Box") == nullptr); CHECK(t.find("Fixed") == nullptr);
}

TEST_CASE("reserved pins reject conflicting controls without blocking other pins") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects":{"Io":{"type":"SdPins"},"Other":{"type":"SdKnob"}}})").ok);
    auto* io = static_cast<SdPins*>(t.find("Io")); auto* other = static_cast<SdKnob*>(t.find("Other"));
    REQUIRE(io); REQUIRE(other);
    int owner = 0; const uint16_t pins[] = {17, 47}; REQUIRE(t.s.reservePins(&owner, pins, 2));
    CHECK(t.s.setControl("Io", "gpio", R"({"value":47})") == mm::Scheduler::SetControlResult::ReadOnly);
    CHECK(t.s.setControl("Io", "lanePins", R"({"value":"16,47"})") == mm::Scheduler::SetControlResult::ReadOnly);
    CHECK(t.s.setControl("Io", "lanePins", R"({"value":"bad"})") == mm::Scheduler::SetControlResult::ReadOnly);
    CHECK(io->gpio == -1); CHECK(std::string(io->lanePins).empty());
    const auto denied = t.apply(R"({"Effects":{"Other":{"value":42},"Io":{"gpio":17}}})");
    CHECK_FALSE(denied.ok); CHECK(denied.changes == 0); CHECK(other->value == 10); CHECK(io->gpio == -1);
    const auto stored = mm::applyStateDocument(t.s, R"({"Effects":{"Io":{"gpio":47,"lanePins":"17"}}})", mm::StateSource::Stored);
    CHECK(stored.skipped > 0); CHECK(io->gpio == -1); CHECK(std::string(io->lanePins).empty());
    REQUIRE(t.apply(R"({"Effects":{"Io":{"gpio":16,"lanePins":"18,19"}}})").ok);
    CHECK(io->gpio == 16); CHECK(std::string(io->lanePins) == "18,19");
    REQUIRE(t.s.releasePins(&owner));
    REQUIRE(t.apply(R"({"Effects":{"Io":{"gpio":47}}})").ok); CHECK(io->gpio == 47);
}

TEST_CASE("a name the same document frees, by removal or a re-typed parent, can be created elsewhere") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects": {"L1": {"type": "SdBox", "Knob": {"type": "SdKnob"}}, "L2": {"type": "SdBox"}}})").ok);
    REQUIRE(t.apply(R"({"Effects": {"L2": {"Knob": {"type": "SdKnob", "value": 7}}, "L1": {"type": "SdOther"}}})").ok);
    CHECK(t.children(t.find("L2")) == "Knob");
    CHECK(valueOf(t.find("Knob")) == 7);
    REQUIRE(t.apply(R"({"Effects": {"L1": {"type": "SdBox", "Knob": {"type": "SdKnob"}}, "L2": {"Knob": null}}})").ok);
    CHECK(t.children(t.find("L1")) == "Knob");
}

TEST_CASE("a module can move to another branch under the same name") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects": {"L1": {"type": "SdBox"}, "L2": {"type": "SdBox", "Knob": {"type": "SdKnob", "value": 5}}}})").ok);
    // L1 comes first in the document, so its Knob is created before L2 is walked: every removal happens before that.
    auto r = t.apply(R"({"Effects": {"L1": {"$patch": "replace", "Knob": {"type": "SdKnob", "value": 9}}, "L2": {"$patch": "replace"}}})");
    REQUIRE(r.ok);
    CHECK(t.children(t.find("L1")) == "Knob");
    CHECK(t.children(t.find("L2")) == "");
    CHECK(valueOf(t.find("Knob")) == 9);

    // A null elsewhere in the document frees its name in the same way.
    REQUIRE(t.apply(R"({"Effects": {"L2": {"Knob": {"type": "SdOther"}}, "L1": {"Knob": null}}})").ok);
    CHECK(t.children(t.find("L2")) == "Knob");
    CHECK(t.children(t.find("L1")) == "");
}

TEST_CASE("a module written as a document applies back to the same state, rooted at its container") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects": {"Layer": {"type": "SdBox", "Knob": {"type": "SdKnob", "value": 42, "label": "hi"}, "Other": {"type": "SdOther"}}}})").ok);
    mm::JsonSink sink;
    sink.append("{");
    mm::writeStateMember(sink, *t.find("Knob"));
    sink.append("}");
    const std::string doc(sink.data(), sink.size());
    CHECK(doc.find(R"({"Effects":{"Layer":{"Knob":{"type":"SdKnob","$patch":"replace","value":42,"label":"hi")") == 0);

    static_cast<SdKnob*>(t.find("Knob"))->value = 1;
    REQUIRE(t.apply(doc.c_str()).ok);
    CHECK(valueOf(t.find("Knob")) == 42);
    CHECK(t.children(t.find("Layer")) == "Knob,Other");   // the path merges, so the sibling stays
}

TEST_CASE("a document leaves a secret out, and applying it leaves the secret as it is") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects": {"Vault": {"type": "SdSecret"}}})").ok);
    mm::JsonSink sink;
    sink.append("{");
    mm::writeStateMember(sink, *t.find("Vault"));
    sink.append("}");
    const std::string doc(sink.data(), sink.size());
    CHECK(doc.find("hunter2") == std::string::npos);
    CHECK(doc.find("\"pass\"") == std::string::npos);
    CHECK(doc.find("\"note\":\"visible\"") != std::string::npos);

    REQUIRE(t.apply(doc.c_str()).ok);
    CHECK(std::string(static_cast<SdSecret*>(t.find("Vault"))->pass) == "hunter2");
}

TEST_CASE("a control a script declares waits for the rebuild, and lands after it") {
    Tree t;
    auto r = t.apply(R"({"Effects": {"Script": {"type": "SdScript", "source": "a", "late": 42}}})");
    REQUIRE(r.ok);
    CHECK(r.deferred == 1);
    t.s.prepareTree();
    CHECK(static_cast<SdScript*>(t.find("Script"))->late == 42);
}

// A show pushes a script's controls with the script: replayed after the rebuild as a plain write, they were marked to save.
TEST_CASE("a script's control a live document sets lands after the rebuild, still unsaved") {
    Tree t;
    static int saves = 0;
    saves = 0;
    t.s.setNoteDirtyHook([] { saves++; });
    auto r = mm::applyStateDocument(t.s, R"({"Effects": {"Script": {"type": "SdScript", "source": "a", "late": 42}}})", mm::StateSource::Live);
    REQUIRE(r.ok);
    CHECK(r.deferred == 1);
    const uint32_t before = t.s.preparationRevision();
    CHECK(t.s.preparationPending());
    CHECK(t.s.preparationRevision() == before);
    t.s.prepareTree();
    CHECK(t.s.preparationRevision() == before + 1);
    CHECK(t.s.preparationResult().ok);
    CHECK(t.s.preparationResult().changes == 1);
    CHECK(static_cast<SdScript*>(t.find("Script"))->late == 42);
    CHECK_FALSE(t.find("Script")->dirty());
    CHECK(saves == 0);
}

TEST_CASE("a rebuild reports deferred live failures and still applies its remaining writes unsaved") {
    Tree t;
    static int saves = 0;
    saves = 0;
    t.s.setNoteDirtyHook([] { saves++; });
    const auto accepted = mm::applyStateDocument(t.s,
        R"({"Effects":{"Bad":{"type":"SdScript","source":"a","late":200},"Good":{"type":"SdScript","source":"a","late":42}}})",
        mm::StateSource::Live);
    REQUIRE(accepted.ok);
    REQUIRE(accepted.deferred == 2);
    CHECK(t.s.preparationRevision() == 0);
    t.s.preparePending();
    CHECK(t.s.preparationRevision() == 1);
    const auto& result = t.s.preparationResult();
    CHECK_FALSE(result.ok);
    CHECK(std::string(result.error) == "deferred control did not apply");
    CHECK(std::string(result.where) == "Bad.late");
    CHECK(result.changes == 1);
    CHECK(result.deferred == 0);
    CHECK(static_cast<SdScript*>(t.find("Bad"))->late == 1);
    CHECK(static_cast<SdScript*>(t.find("Good"))->late == 42);
    CHECK_FALSE(t.find("Bad")->dirty());
    CHECK_FALSE(t.find("Good")->dirty());
    CHECK(saves == 0);
    t.s.prepareTree();
    CHECK(t.s.preparationRevision() == 2);
    CHECK(t.s.preparationResult().ok);
    CHECK(t.s.preparationResult().changes == 0);
}

TEST_CASE("preparation revisions follow completed scope and tree walks including their completion hooks") {
    struct CompletionProbe : SdCount {
        mm::Scheduler* scheduler = nullptr;
        uint32_t revisionAtHook = 0;
        int completions = 0;
        bool requestAgain = false;
        void onTreePrepared() override {
            revisionAtHook = scheduler->preparationRevision();
            ++completions;
            if (requestAgain) scheduler->requestPrepare(this);
        }
    };
    mm::Scheduler scheduler;
    auto* probe = new CompletionProbe();
    probe->scheduler = &scheduler;
    probe->setName("Probe");
    scheduler.addModule(probe);
    CHECK(scheduler.preparationRevision() == 0);
    CHECK_FALSE(scheduler.preparationPending());
    scheduler.setup();
    CHECK(scheduler.preparationRevision() == 1);
    CHECK(probe->prepares == 1);
    scheduler.requestPrepare(probe);
    CHECK(scheduler.preparationPending());
    CHECK(scheduler.preparationRevision() == 1);
    scheduler.preparePending();
    CHECK(scheduler.preparationRevision() == 2);
    CHECK(probe->prepares == 2);
    CHECK(probe->completions == 1);
    CHECK(probe->revisionAtHook == 1);
    CHECK_FALSE(scheduler.preparationPending());
    scheduler.preparePending();
    scheduler.prepareSubtrees(nullptr, 0);
    CHECK(scheduler.preparationRevision() == 2);
    scheduler.requestPrepare(probe);
    scheduler.requestPrepareTree();
    scheduler.prepareTree();
    CHECK(scheduler.preparationRevision() == 3);
    CHECK_FALSE(scheduler.preparationPending());
    probe->requestAgain = true;
    scheduler.prepareTree();
    CHECK(scheduler.preparationRevision() == 4);
    CHECK(scheduler.preparationPending());
    probe->requestAgain = false;
    scheduler.preparePending();
    CHECK(scheduler.preparationRevision() == 5);
    CHECK_FALSE(scheduler.preparationPending());
    SdCount detached;
    scheduler.requestPrepare(&detached);
    scheduler.preparePending();
    CHECK(scheduler.preparationRevision() == 6);
    CHECK(probe->revisionAtHook == 5);
    CHECK_FALSE(scheduler.preparationPending());
    scheduler.requestPrepare(probe);
    scheduler.requestPrepareTree();
    scheduler.release();
    CHECK_FALSE(scheduler.preparationPending());
    CHECK(scheduler.preparationRevision() == 0);
    CHECK(scheduler.preparationResult().ok);
    scheduler.preparePending();
    CHECK(scheduler.preparationRevision() == 0);
}

TEST_CASE("an explicit scoped rebuild consumes its scope while preserving another queued scope") {
    mm::Scheduler scheduler;
    auto* root = new SdBox(); root->setName("Root");
    auto* first = new SdCount(); first->setName("First");
    auto* second = new SdCount(); second->setName("Second");
    root->addChild(first); root->addChild(second);
    scheduler.addModule(root);
    scheduler.requestPrepare(first);
    scheduler.requestPrepare(second);
    scheduler.prepareSubtree(first);
    CHECK(scheduler.preparationRevision() == 1);
    CHECK(scheduler.preparationPending());
    CHECK(first->prepares == 1);
    CHECK(second->prepares == 0);
    scheduler.preparePending();
    CHECK(scheduler.preparationRevision() == 2);
    CHECK_FALSE(scheduler.preparationPending());
    CHECK(first->prepares == 1);
    CHECK(second->prepares == 1);
    scheduler.release();
}

TEST_CASE("a mistyped control on a script the document leaves as it is is named at once") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects": {"Script": {"type": "SdScript", "source": "a"}}})").ok);
    t.s.prepareTree();
    auto r = t.apply(R"({"Effects": {"Script": {"lat": 5}}})");
    CHECK_FALSE(r.ok);
    CHECK(std::string(r.error) == "no such control");
}

TEST_CASE("a document that fails leaves no write waiting for the rebuild") {
    Tree t;
    // A value out of range is found only when it is written, after the script was created and its control deferred.
    auto r = t.apply(R"({"Effects": {"Script": {"type": "SdScript", "source": "a", "late": 42}, "Knob": {"type": "SdKnob", "value": 200}}})");
    CHECK_FALSE(r.ok);
    CHECK(r.deferred == 0);
    t.s.prepareTree();
    CHECK(static_cast<SdScript*>(t.find("Script"))->late == 1);
}

TEST_CASE("a root $ key is the file's own, so a preset file applies as it is") {
    Tree t;
    auto r = t.apply(R"({"$slot": 3, "Effects": {"Knob": {"type": "SdKnob", "value": 9}}})");
    REQUIRE(r.ok);
    CHECK(valueOf(t.find("Knob")) == 9);
}

TEST_CASE("the answer to a document names the failure, escaped, and what waits for the rebuild") {
    mm::StateDocumentResult r;
    r.changes = 2;
    r.deferred = 1;
    mm::JsonSink ok;
    mm::writeStateResult(ok, r);
    CHECK(std::string(ok.data(), ok.size()) == R"({"ok":true,"changes":2,"deferred":1})");

    r.ok = false;
    r.error = "no such control";
    r.deferred = 0;
    std::snprintf(r.where, sizeof(r.where), "%s", std::string(63, '"').c_str());   // every character needs escaping
    mm::JsonSink bad;
    mm::writeStateResult(bad, r);
    const std::string text(bad.data(), bad.size());
    CHECK(text.find(R"({"error":"no such control","at":")") == 0);
    CHECK(text.substr(text.size() - 14) == R"(","changes":2})");   // complete, nothing cut
}

TEST_CASE("a rebuild run at once satisfies the one the document requested") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects": {"Counter": {"type": "SdCount"}}})").ok);   // structural, so it requests a rebuild
    t.s.prepareTree();
    const int after = static_cast<SdCount*>(t.find("Counter"))->prepares;
    t.s.tick();
    CHECK(static_cast<SdCount*>(t.find("Counter"))->prepares == after);
}

TEST_CASE("a document that names an unknown type or container changes nothing, wherever it names it") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects": {"A": {"type": "SdKnob"}, "B": {"type": "SdKnob"}}})").ok);
    // The removals come first in the walk, so only a check before them keeps A and B.
    auto r = t.apply(R"({"Effects": {"$patch": "replace", "C": {"type": "SdKnob"}, "D": {"type": "NoSuchType"}}})");
    CHECK(std::string(r.error) == "unknown type");
    CHECK(std::string(r.where) == "Effects.D");
    CHECK(t.children(t.effects) == "A,B");
    r = t.apply(R"({"Effects": {"$patch": "replace"}, "Nowhere": {}})");
    CHECK(std::string(r.error) == "no such top-level module");
    CHECK(t.children(t.effects) == "A,B");
}

TEST_CASE("a reorder alone is a change, saved like any other") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects": {"A": {"type": "SdKnob"}, "B": {"type": "SdKnob"}}})").ok);
    auto r = t.apply(R"({"Effects": {"$patch": "replace", "B": {}, "A": {}}})");
    REQUIRE(r.ok);
    CHECK(t.children(t.effects) == "B,A");
    CHECK(r.changes == 1);
}

TEST_CASE("a re-typed module frees the names under it for elsewhere in the same document") {
    Tree t;
    REQUIRE(t.apply(R"({"Effects": {"L1": {"type": "SdBox", "Knob": {"type": "SdKnob"}}, "L2": {"type": "SdBox"}}})").ok);
    auto r = t.apply(R"({"Effects": {"L2": {"Knob": {"type": "SdKnob", "value": 7}}, "L1": {"type": "SdOther"}}})");
    REQUIRE(r.ok);
    CHECK(std::string(t.find("L1")->typeName()) == "SdOther");
    CHECK(t.children(t.find("L2")) == "Knob");
    CHECK(valueOf(t.find("Knob")) == 7);
}

namespace {
// A module with a saved setting and a live input beside it, as the Control module's save form is.
struct SdForm : public mm::MoonModule {
    uint8_t value = 1;
    uint8_t slot = 255;
    void defineControls() override {
        controls_.addControl("value", value, 0, 100);
        controls_.addControl("slot", slot, 0, 255);
        controls_.setLive(controls_.count() - 1);
    }
};
}  // namespace

// A stored file sets only what the device itself saves: a live input an older build wrote into its file stays as the device starts it, while a request may still set one.
TEST_CASE("a stored file sets only the controls the device saves, and a request sets any") {
    Tree t;
    auto* form = new SdForm();
    form->setName("Form");
    t.effects->addChild(form);
    form->defineControls();

    const char* doc = R"({"Effects":{"Form":{"value":7,"slot":0}}})";
    REQUIRE(mm::applyStateDocument(t.s, doc, mm::StateSource::Stored).ok);
    CHECK(form->value == 7);
    CHECK(form->slot == 255);
    REQUIRE(t.apply(doc).ok);
    CHECK(form->slot == 0);
}

// A device model's document lists the Ethernet card under Network with `"$patch":"replace"`; the WiFi card main.cpp wired beside it stays, since every boot puts it back.
TEST_CASE("replacing a module's children keeps the ones wired in code") {
    Tree t;
    auto* wired = new SdKnob();
    wired->setName("Wired");
    t.effects->addChild(wired);
    wired->markWiredByCode();
    REQUIRE(t.apply(R"({"Effects":{"Loose":{"type":"SdKnob"}}})").ok);

    REQUIRE(t.apply(R"({"Effects":{"$patch":"replace","Fresh":{"type":"SdKnob"}}})").ok);
    CHECK(t.children(t.effects) == "Wired,Fresh");
}

// The installer sends a device model's document over serial and may send it again; the second time changes nothing.
TEST_CASE("a state document applied twice leaves the tree as the first application did") {
    Tree t;
    const char* doc = R"({"Effects":{"$patch":"replace","A":{"type":"SdKnob","value":4},"B":{"type":"SdKnob"}}})";
    REQUIRE(t.apply(doc).ok);
    REQUIRE(t.apply(doc).ok);
    CHECK(t.children(t.effects) == "A,B");
    CHECK(valueOf(t.find("A")) == 4);
}

// A device model lists the Ethernet card under Network with `"$patch":"replace"` and leaves out the cards main.cpp wired beside it: those keep their places, and the listed ones fill their own places in the document's order.
TEST_CASE("replacing a module's children keeps the unlisted ones where they are") {
    Tree t;
    auto* wired = new SdKnob();
    wired->setName("Wired");
    t.effects->addChild(wired);
    wired->markWiredByCode();
    REQUIRE(t.apply(R"({"Effects":{"B":{"type":"SdKnob"},"A":{"type":"SdKnob"}}})").ok);
    REQUIRE(t.children(t.effects) == "Wired,B,A");
    t.effects->moveChildTo(wired, 1);
    REQUIRE(t.children(t.effects) == "B,Wired,A");

    REQUIRE(t.apply(R"({"Effects":{"$patch":"replace","A":{},"B":{}}})").ok);
    CHECK(t.children(t.effects) == "A,Wired,B");
    CHECK(t.apply(R"({"Effects":{"$patch":"replace","A":{},"B":{}}})").changes == 0);
}

TEST_CASE("a live document applies as a request does and marks nothing to save") {
    Tree t;
    static int saves = 0;
    saves = 0;
    t.s.setNoteDirtyHook([] { saves++; });
    auto r = mm::applyStateDocument(t.s, R"({"Effects": {"Knob": {"type": "SdKnob", "value": 42}}})", mm::StateSource::Live);
    REQUIRE(r.ok);
    CHECK(valueOf(t.find("Knob")) == 42);
    CHECK(saves == 0);
    CHECK_FALSE(t.effects->dirty());
    CHECK_FALSE(t.find("Knob")->dirty());
    // Checked whole as a request is: a mistake changes nothing.
    auto bad = mm::applyStateDocument(t.s, R"({"Effects": {"Knob": {"value": 7}, "X": {"type": "Nope"}}})", mm::StateSource::Live);
    CHECK_FALSE(bad.ok);
    CHECK(valueOf(t.find("Knob")) == 42);
}

TEST_CASE("a live document queued from a tick applies at the frame boundary and reports back") {
    Tree t;
    static bool reported = false;
    static uint16_t changes = 0;
    reported = false;
    const char* text = R"({"Effects": {"Knob": {"type": "SdKnob", "value": 9}}})";
    char* copy = static_cast<char*>(mm::platform::alloc(std::strlen(text) + 1));
    std::strcpy(copy, text);
    REQUIRE(t.s.applyLive(copy, [](uintptr_t tag, const mm::StateDocumentResult& r) {
        reported = tag == 7 && r.ok;
        changes = r.changes;
    }, 7));
    CHECK(t.find("Knob") == nullptr);   // nothing until the boundary
    t.s.preparePending();
    REQUIRE(t.find("Knob") != nullptr);
    CHECK(valueOf(t.find("Knob")) == 9);
    CHECK(reported);
    CHECK(changes == 2);
}

TEST_CASE("the live queue refuses a fifth document rather than growing") {
    Tree t;
    char* texts[5];
    for (auto& p : texts) {
        p = static_cast<char*>(mm::platform::alloc(3));
        std::strcpy(p, "{}");
    }
    for (int i = 0; i < 4; i++) CHECK(t.s.applyLive(texts[i]));
    CHECK_FALSE(t.s.applyLive(texts[4]));
    mm::platform::free(texts[4]);   // refused, so still the caller's
    t.s.preparePending();
    // Applied, so there is room again.
    char* again = static_cast<char*>(mm::platform::alloc(3));
    std::strcpy(again, "{}");
    CHECK(t.s.applyLive(again));
}
