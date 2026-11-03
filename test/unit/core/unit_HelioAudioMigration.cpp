#include "doctest.h"
#include "helio/HelioAudioMigration.h"

TEST_CASE("saved HELIO audio modes retain their meaning on MoonLight") {
    constexpr const char* oldLocal = R"({"0.type":"AudioService","0.mode":0,"0.sckPin":40,"0.wsPin":38,"0.sdPin":39})";
    constexpr const char* oldSimulate = R"({"0.type":"AudioService","0.mode":2,"0.sckPin":40,"0.wsPin":38,"0.sdPin":39})";
    constexpr const char* current = R"({"0.type":"AudioService","0.mode":0,"0.levels":1,"0.sckPin":40,"0.wsPin":38,"0.sdPin":39})";
    CHECK(mm::helio::migratedAudioMode(oldLocal) == 2);
    CHECK(mm::helio::migratedAudioMode(oldSimulate) == 0);
    CHECK(mm::helio::migratedAudioMode(current) == -1);
    CHECK(mm::helio::migratedAudioMode("{}") == -1);
}
