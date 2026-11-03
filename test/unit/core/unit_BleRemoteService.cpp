/// @module BleRemoteService
/// @also Scheduler
#include "doctest.h"
#include "core/services/BleRemoteService.h"
#include "core/module/Scheduler.h"
#include <vector>

using namespace mm;
namespace {
std::vector<uint8_t> payload(const char* hex) {
    std::vector<uint8_t> bytes;
    for (; *hex; hex += 2) { unsigned b = 0; std::sscanf(hex, "%2x", &b); bytes.push_back(static_cast<uint8_t>(b)); }
    return bytes;
}
constexpr const char* on = "525302000e0c000e0c000501b0a65a310202ff0403000201";
constexpr const char* onNext = "525302000e0c000e0c000501b1a75b200202ff0403000201";
constexpr const char* off = "525302000e0c000e0c0005019d9469200202ff0403000200";
constexpr const char* up = "525302000e0c000e0c000501b9a85fd40202ff0403000901";
constexpr const char* upNext = "525302000e0c000e0c000501a897601a0202ff0403000901";
constexpr const char* down = "525302000e0c000e0c000501a292644e0202ff0403000900";
struct Surface : MoonModule {
    bool switch1 = false; uint8_t fader1 = 100, encoder1 = 100;
    void defineControls() override {
        controls_.addControl("switch1", switch1);
        controls_.addControl("fader1", fader1, 0, 255);
        controls_.addControl("encoder1", encoder1, 0, 255);
    }
};
struct Rig {
    Scheduler scheduler;
    Surface* surface = new Surface();
    BleRemoteService* remote = new BleRemoteService();
    Rig() { surface->setName("Control"); remote->setName("Remote"); scheduler.addModule(surface); scheduler.addModule(remote); scheduler.setup(); }
    ~Rig() { scheduler.release(); }
    void fire(const char* hex, uint32_t ms) { auto bytes = payload(hex); remote->injectAdvertisementForTest(bytes, ms); }
};
}

TEST_CASE("RS remote captured keys decode without an AD field parser") {
    struct Fixture { const char* hex; RsKey key; };
    for (const auto& f : std::initializer_list<Fixture>{
        {on,RsKey::On},{off,RsKey::Off},{up,RsKey::Brighter},{down,RsKey::Dimmer},
        {"525302000e0c000e0c000501a2966c060202ff0403000401",RsKey::ModeUp},
        {"525302000e0c000e0c0005019f946f260202ff0403000400",RsKey::ModeDown},
        {"525302000e0c000e0c0005019a87720b0202ff0403000b01",RsKey::SpeedUp},
        {"525302000e0c000e0c0005019381756c0202ff0403000b00",RsKey::SpeedDown},
        {"525302000e0c000e0c0005019e8878f10202ff0403000d02",RsKey::Auto},
        {"525302000e0c000e0c000501af947b310202ff0403001202",RsKey::Music},
        {"525302000e0c000e0c000501d8a87ef30202ff0403002900",RsKey::Timer},
        {"525302000e0c000e0c000501825384bc0202ff0403002800",RsKey::ColorDown},
        {"525302000e0c000e0c0005018e5e88600202ff0403002801",RsKey::ColorUp},
        {"525302000e0c000e0c00050196898c150202ff06030005ffff00",RsKey::Yellow},
        {"525302000e0c000e0c000501786a90370202ff0603000500ffff",RsKey::Cyan},
        {"525302000e0c000e0c000501756892150202ff06030005ff00ff",RsKey::Pink},
        {"525302000e0c000e0c0005015f52a88d0202ff06030005ff0000",RsKey::Red},
        {"525302000e0c000e0c000501d2c5c09d0202ff06030005ffffff",RsKey::White},
        {"525302000e0c000e0c0005016e609a9c0202ff0603000500ff00",RsKey::Green},
        {"525302000e0c000e0c000501ac9e9b710202ff060300050000ff",RsKey::Blue}}) {
        CAPTURE(f.hex);
        RsRemoteEvent e; CHECK(decodeRsRemote(payload(f.hex),e)); CHECK(e.key==f.key);
    }
    RsRemoteEvent e;
    auto truncated=payload(on); truncated.pop_back(); CHECK_FALSE(decodeRsRemote(truncated,e));
    auto bad=payload(on); bad[19]=6; CHECK_FALSE(decodeRsRemote(bad,e));
    bad=payload(on); bad[0]=2; CHECK_FALSE(decodeRsRemote(bad,e));
    bad=payload(on); bad[23]=99; CHECK(decodeRsRemote(bad,e)); CHECK(e.key==RsKey::Unknown);
}

TEST_CASE("RS duplicate bursts and interleaved repeats do not swallow rapid presses") {
    RsRemoteDecoder decoder; RsRemoteEvent event;
    CHECK(decoder.receive(payload(on),100,event));
    CHECK_FALSE(decoder.receive(payload(on),935,event));
    CHECK(decoder.receive(payload(onNext),936,event));
    CHECK_FALSE(decoder.receive(payload(on),937,event));
    CHECK(decoder.receive(payload(on),2937,event));
    RsRemoteDecoder wrapped;
    CHECK(wrapped.receive(payload(on),0xfffffff0u,event));
    CHECK_FALSE(wrapped.receive(payload(on),20,event));
}

TEST_CASE("RS ON and OFF persist independently and brightness duplicates act once") {
    Rig r;
    r.fire(on,100); CHECK(r.surface->switch1);
    r.fire(onNext,101); CHECK(r.surface->switch1);
    r.fire(off,102); CHECK_FALSE(r.surface->switch1);
    r.fire(off,800); CHECK_FALSE(r.surface->switch1);
    r.fire(up,1000); CHECK(r.surface->fader1==116);
    r.fire(up,1800); CHECK(r.surface->fader1==116);
    r.fire(upNext,1801); CHECK(r.surface->fader1==132);
    r.fire(down,1802); CHECK(r.surface->fader1==116);
}

TEST_CASE("RS key bindings restore and absolute Set stays available") {
    Rig r;
    REQUIRE(r.remote->setListRowField(5,"target","{\"value\":\"Control.encoder1\"}"));
    REQUIRE(r.remote->setListRowField(5,"kind","{\"value\":\"delta\"}"));
    REQUIRE(r.remote->setListRowField(5,"value","{\"value\":7}"));
    r.fire("525302000e0c000e0c000501a2966c060202ff0403000401",100);
    CHECK(r.surface->encoder1==107);
    char text[512]; JsonSink row(text,sizeof(text)); r.remote->writeListRow(row,4);
    CHECK(std::strstr(text,"Control.encoder1")!=nullptr);
    REQUIRE(r.remote->restoreList("{\"keys\":[{\"key\":\"mode+\",\"target\":\"Control.encoder1\",\"kind\":\"set\",\"value\":42}]}","keys"));
    r.fire("525302000e0c000e0c000501a2966c060202ff0403000401",2200);
    CHECK(r.surface->encoder1==42);
    JsonSink detail(text,sizeof(text)); r.remote->writeListRowDetail(detail,0);
    CHECK(std::strstr(text,"needs release")==nullptr);
}
