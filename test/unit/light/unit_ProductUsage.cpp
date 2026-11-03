/// @module ProductUsage
#include "doctest.h"
#include "helio/ProductUsage.h"
#include "core/util/JsonUtil.h"

#include <cstring>
#include <string>

namespace {
struct UsageReport {
    mm::json::JsonDoc doc;
    explicit UsageReport(const mm::ProductUsage& usage) {
        mm::JsonSink sink; usage.report(sink);
        REQUIRE_FALSE(sink.overflowed()); REQUIRE(mm::json::parse(sink.data(), doc));
    }
    const mm::json::JsonNode* field(const char* name, const mm::json::JsonNode* object = nullptr) const {
        const auto* node = mm::json::member(doc, object ? object : doc.rootNode(), name);
        REQUIRE(node != nullptr); return node;
    }
    uint32_t number(const char* name, const mm::json::JsonNode* object = nullptr) const {
        return static_cast<uint32_t>(field(name, object)->intValue);
    }
    const mm::json::JsonNode* record(unsigned index = 0) const {
        const auto* node = doc.node(field("records")->firstChild);
        while (node && index--) node = doc.node(node->next);
        REQUIRE(node != nullptr); return node;
    }
    unsigned count() const {
        unsigned result = 0;
        for (const auto* node = doc.node(field("records")->firstChild); node; node = doc.node(node->next)) ++result;
        return result;
    }
    uint32_t bin(const mm::json::JsonNode* record, unsigned index) const {
        const auto* node = doc.node(field("brightnessHistogramMs", record)->firstChild);
        while (node && index--) node = doc.node(node->next);
        REQUIRE(node != nullptr); return static_cast<uint32_t>(node->intValue);
    }
};
}

TEST_CASE("Product usage is off by default, allocates only after opt-in and erases on opt-out") {
    mm::ProductUsage usage;
    CHECK(UsageReport(usage).field("optin")->intValue == 0);
    struct Restore { ~Restore() { mm::platform::setTestAllocRefusedFrom(0); } } restore;
    mm::platform::setTestAllocRefusedFrom(1);
    usage.record(false, 0, 0, "solid", "{}", 255, true);
    char bytes[2048]; mm::JsonSink empty(bytes, sizeof(bytes)); usage.report(empty);
    CHECK(std::strstr(bytes, "\"allocationFailed\":false") != nullptr);
    usage.record(true, 100, 0, "solid", "{}", 255, true);
    char refused[2048]; mm::JsonSink failed(refused, sizeof(refused)); usage.report(failed);
    CHECK(std::strstr(refused, "\"allocationFailed\":true") != nullptr);
    mm::platform::setTestAllocRefusedFrom(0);
    usage.record(true, 200, 0, "solid", "{}", 255, true);
    usage.record(true, 1200, 0, "solid", "{}", 255, true);
    CHECK(UsageReport(usage).number("onMs") == 1000);
    usage.record(false, 2200, 0, "solid", "{}", 255, true);
    UsageReport cleared(usage);
    CHECK(cleared.field("optin")->intValue == 0); CHECK(cleared.number("onMs") == 0); CHECK(cleared.count() == 0);
    usage.record(true, 3200, 0, "solid", "{}", 255, true);
    CHECK(UsageReport(usage).number("onMs") == 0);
}

TEST_CASE("Product usage integrates the previous actual power and brightness state rather than frame counts") {
    mm::ProductUsage usage;
    usage.record(true, 100, 2000000000, "solid", "{}", 0, false);
    usage.record(true, 350, 2000000000, "solid", "{}", 32, true);
    for (unsigned i = 0; i < 50; ++i) usage.record(true, 350, 2000000000, "solid", "{}", 32, true);
    usage.record(true, 1100, 2000000001, "solid", "{}", 255, true);
    usage.record(true, 2100, 2000000002, "solid", "{}", 255, false);
    UsageReport report(usage);
    CHECK(report.count() == 1); CHECK(report.number("onMs") == 1750); CHECK(report.number("offMs") == 250);
    CHECK(report.bin(report.record(), 0) == 250); CHECK(report.bin(report.record(), 2) == 750);
    CHECK(report.bin(report.record(), 15) == 1000);
    CHECK(report.number("undatedMs") == 0);
}

TEST_CASE("Product usage settings hashes are nullable and deliberately distinguish different textual structures") {
    mm::ProductUsage usage;
    usage.record(true, 0, 0, "solid", nullptr, 1, true);
    usage.record(true, 100, 0, "solid", R"({"red":1,"green":2})", 1, true);
    usage.record(true, 200, 0, "solid", R"({"green":2,"red":1})", 1, true);
    usage.record(true, 300, 0, "private-device-name", "{}", 1, true);
    usage.record(true, 400, 0, nullptr, "{}", 1, true);
    UsageReport report(usage);
    REQUIRE(report.count() == 4);
    CHECK(report.field("settingsHash", report.record(0))->type == mm::json::JsonType::Null);
    const char* first = report.field("settingsHash", report.record(1))->str;
    const char* second = report.field("settingsHash", report.record(2))->str;
    CHECK(std::strlen(first) == 32); CHECK(std::strcmp(first, second) != 0);
    CHECK(report.field("scene", report.record(3))->type == mm::json::JsonType::Null);
    mm::JsonSink raw; usage.report(raw);
    CHECK(std::strstr(raw.data(), "private-device-name") == nullptr);
    CHECK(std::strstr(raw.data(), "green") == nullptr);
}

TEST_CASE("Product usage splits monotonic duration at UTC midnight including intervals longer than a day") {
    mm::ProductUsage usage;
    const uint64_t start = 20000ULL * 86400 + 86399;
    usage.record(true, 0, start, "solid", "{}", 64, true);
    usage.record(true, 2500, start + 2, "solid", "{}", 64, true);
    usage.record(true, 86402500, start + 86402, "solid", "{}", 64, true);
    UsageReport report(usage);
    REQUIRE(report.count() == 3);
    CHECK(report.number("utcDay", report.record(0)) == 20000);
    CHECK(report.number("onMs", report.record(0)) == 1000);
    CHECK(report.number("utcDay", report.record(1)) == 20001);
    CHECK(report.number("onMs", report.record(1)) == 86400000);
    CHECK(report.number("utcDay", report.record(2)) == 20002);
    CHECK(report.number("onMs", report.record(2)) == 1500);
}

TEST_CASE("Product usage never invents UTC days across unknown clocks or wallclock jumps") {
    mm::ProductUsage usage;
    usage.record(true, 0, 0, "solid", nullptr, 128, true);
    usage.record(true, 1000, 1700000000, "solid", nullptr, 128, true);
    usage.record(true, 2000, 1700000001, "solid", nullptr, 128, true);
    usage.record(true, 3000, 1800000000, "solid", nullptr, 128, true);
    usage.record(true, 4000, 1800000001, "solid", nullptr, 128, true);
    usage.record(true, 5000, 1799999900, "solid", nullptr, 128, true);
    UsageReport report(usage);
    CHECK(report.number("onMs") == 5000); CHECK(report.number("undatedMs") == 3000);
    CHECK(report.number("clockDiscontinuityMs") == 2000);
    CHECK(report.field("utcDay", report.record(0))->type == mm::json::JsonType::Null);
    CHECK(report.number("onMs", report.record(0)) == 3000);
}

TEST_CASE("Product usage carries monotonic milliseconds across wrap and bounds record growth without losing totals") {
    mm::ProductUsage usage;
    usage.record(true, 0xfffffff0u, 0, "solid", nullptr, 255, true);
    usage.record(true, 0x10u, 0, "solid", nullptr, 255, true);
    CHECK(UsageReport(usage).number("onMs") == 32);
    mm::ProductUsage bounded;
    for (unsigned i = 0; i <= mm::ProductUsage::kMaxRecords + 1; ++i) {
        const auto settings = "{\"red\":" + std::to_string(i) + "}";
        bounded.record(true, i * 100, 0, "solid", settings.c_str(), 255, true);
    }
    UsageReport report(bounded);
    CHECK(report.count() == mm::ProductUsage::kMaxRecords);
    CHECK(report.number("onMs") == 1700); CHECK(report.number("overflowMs") == 100);
}

TEST_CASE("Product usage saturates counters visibly and rejects oversized settings without retaining them") {
    mm::ProductUsage usage;
    usage.record(true, 0, 0, "solid", nullptr, 255, true);
    usage.record(true, UINT32_MAX, 0, "solid", nullptr, 255, true);
    usage.record(true, 0, 0, "solid", nullptr, 255, true);
    UsageReport report(usage);
    CHECK(report.field("saturated")->intValue == 1);
    CHECK(report.number("onMs") == UINT32_MAX); CHECK(report.number("onMs", report.record()) == UINT32_MAX);
    CHECK(report.bin(report.record(), 15) == UINT32_MAX);
    mm::ProductUsage longSettings;
    const std::string oversized(mm::ProductUsage::kMaxSettingsBytes + 1, 'x');
    longSettings.record(true, 0, 0, "solid", oversized.c_str(), 1, true);
    longSettings.record(true, 1, 0, "solid", nullptr, 1, true);
    UsageReport rejected(longSettings);
    CHECK(rejected.field("settingsTooLong")->intValue == 1);
    CHECK(rejected.field("settingsHash", rejected.record())->type == mm::json::JsonType::Null);
}
