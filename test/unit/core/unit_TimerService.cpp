// SPDX-License-Identifier: GPL-3.0-or-later
/// @module TimerService
/// @also CivilTime

/// When the timer turns the lights on and off: calendar dates, time zones with daylight time, sunrise and sunset, the evenings a schedule makes, and the one lookup that finds the place.

#include "doctest.h"
#include "core/module/Scheduler.h"
#include "core/services/TimerService.h"
#include "core/util/CivilTime.h"
#include "core/util/JsonFetch.h"
#include "core/util/JsonUtil.h"
#include "platform/platform.h"

#include <cstdlib>
#include <cstring>
#include <string>

using namespace mm;

namespace {

/// The UTC second of a date and a time of day.
int64_t utcOf(int y, int m, int d, int hour, int minute = 0) {
    return static_cast<int64_t>(civil::daysFromCivil(y, static_cast<uint32_t>(m), static_cast<uint32_t>(d))) * civil::kSecondsPerDay + hour * 3600 + minute * 60;
}

/// Minutes after the UTC midnight of `y-m-d` that `utc` falls on, negative for the day before.
double minutesInto(int y, int m, int d, int64_t utc) { return static_cast<double>(utc - utcOf(y, m, d, 0)) / 60.0; }

civil::Zone zoneOf(const char* text) {
    civil::Zone z;
    REQUIRE(civil::parseZone(text, z));
    return z;
}

constexpr const char* kBerlin = "<+01>-1<+02>,M3.5.0/2,M10.5.0/3";

/// A schedule in Berlin's zone.
TimerService::Schedule berlinSchedule(uint8_t turnOn, uint16_t onTime, uint8_t turnOff, uint16_t offTime, uint8_t days = 0x7F) {
    TimerService::Schedule s;
    s.turnOn = turnOn; s.onTime = onTime; s.turnOff = turnOff; s.offTime = offTime; s.days = days;
    s.latitude = 52.52f; s.longitude = 13.40f;
    s.zone = zoneOf(kBerlin);
    return s;
}

/// The local second of a Berlin wall-clock time.
int64_t berlinLocal(int y, int m, int d, int hour, int minute = 0) { return utcOf(y, m, d, hour, minute); }

}  // namespace

TEST_CASE("a calendar date and its day number convert both ways, across leap years and before 1970") {
    for (int32_t day : {-719468, -1, 0, 59, 60, 11016, 20772, 47482}) {
        const civil::Date d = civil::civilFromDays(day);
        CHECK(civil::daysFromCivil(d.year, d.month, d.day) == day);
    }
    const civil::Date leap = civil::civilFromDays(civil::daysFromCivil(2028, 2, 29));
    CHECK(leap.year == 2028); CHECK(leap.month == 2); CHECK(leap.day == 29);
    CHECK(civil::dayOfYear(civil::daysFromCivil(2026, 12, 31)) == 365);
    CHECK(civil::dayOfYear(civil::daysFromCivil(2028, 12, 31)) == 366);
}

TEST_CASE("weekdays count from Sunday, the epoch being a Thursday") {
    CHECK(civil::weekday(0) == 4);
    CHECK(civil::weekday(-1) == 3);
    CHECK(civil::weekday(civil::daysFromCivil(2026, 11, 15)) == 0);
    CHECK(civil::weekday(civil::daysFromCivil(2026, 11, 16)) == 1);
    CHECK(civil::dayOf(-1) == -1);
    CHECK(civil::dayOf(civil::kSecondsPerDay) == 1);
}

TEST_CASE("a POSIX time zone keeps daylight time on its own, north and south of the equator") {
    const civil::Zone berlin = zoneOf("CET-1CEST,M3.5.0,M10.5.0/3");
    CHECK(berlin.offsetAt(utcOf(2026, 3, 29, 0, 59)) == 3600);
    CHECK(berlin.offsetAt(utcOf(2026, 3, 29, 1, 0)) == 7200);
    CHECK(berlin.offsetAt(utcOf(2026, 10, 25, 0, 59)) == 7200);
    CHECK(berlin.offsetAt(utcOf(2026, 10, 25, 1, 0)) == 3600);

    const civil::Zone newYork = zoneOf("EST5EDT,M3.2.0,M11.1.0");
    CHECK(newYork.offsetAt(utcOf(2026, 3, 8, 6, 59)) == -5 * 3600);
    CHECK(newYork.offsetAt(utcOf(2026, 3, 8, 7, 0)) == -4 * 3600);
    CHECK(newYork.offsetAt(utcOf(2026, 11, 1, 6, 0)) == -5 * 3600);

    // Sydney's summer spans the new year: daylight time from October to April.
    const civil::Zone sydney = zoneOf("AEST-10AEDT,M10.1.0,M4.1.0/3");
    CHECK(sydney.offsetAt(utcOf(2026, 1, 15, 12)) == 11 * 3600);
    CHECK(sydney.offsetAt(utcOf(2026, 4, 4, 15, 59)) == 11 * 3600);
    CHECK(sydney.offsetAt(utcOf(2026, 4, 4, 16, 0)) == 10 * 3600);
    CHECK(sydney.offsetAt(utcOf(2026, 10, 3, 16, 0)) == 11 * 3600);

    const civil::Zone india = zoneOf("<+0530>-5:30");
    CHECK(india.offsetAt(utcOf(2026, 6, 1, 0)) == 5 * 3600 + 1800);
    CHECK_FALSE(india.hasDaylight);
}

TEST_CASE("a local time converts to UTC and back, by the offset in force") {
    const civil::Zone berlin = zoneOf(kBerlin);
    for (int64_t utc : {utcOf(2026, 1, 10, 17), utcOf(2026, 7, 10, 17), utcOf(2026, 10, 25, 3)})
        CHECK(berlin.toUtc(berlin.toLocal(utc)) == utc);
}

TEST_CASE("a time zone that is not a POSIX TZ string is refused rather than guessed") {
    civil::Zone z;
    for (const char* bad : {"", "Europe/Berlin", "CET", "CET-1CEST,J60,J300", "CET-1CEST,M13.5.0,M10.5.0", "CET-1CEST,M3.5.0", "CE-1"})
        CHECK_FALSE(civil::parseZone(bad, z));
    z.standardOffset = 42;
    CHECK_FALSE(civil::parseZone("nonsense", z));
    CHECK(z.standardOffset == 42);
}

TEST_CASE("an IANA zone from the lookup becomes a POSIX TZ string with its standard offset and daylight rules") {
    char out[48];
    const auto posix = [&](const char* zone) { return civil::posixZoneFor(zone, out, sizeof(out)) ? std::string(out) : std::string("refused"); };
    CHECK(posix("Europe/Berlin") == kBerlin);
    CHECK(posix("Europe/Paris") == kBerlin);
    CHECK(posix("Europe/London") == "<+00>0<+01>,M3.5.0/1,M10.5.0/2");
    CHECK(posix("Europe/Dublin") == "<+00>0<+01>,M3.5.0/1,M10.5.0/2");
    CHECK(posix("Europe/Athens") == "<+02>-2<+03>,M3.5.0/3,M10.5.0/4");
    CHECK(posix("America/New_York") == "<-05>5<-04>,M3.2.0,M11.1.0");
    CHECK(posix("America/Los_Angeles") == "<-08>8<-07>,M3.2.0,M11.1.0");
    CHECK(posix("America/Indiana/Indianapolis") == "<-05>5<-04>,M3.2.0,M11.1.0");
    CHECK(posix("America/Indiana/Knox") == "<-06>6<-05>,M3.2.0,M11.1.0");
    CHECK(posix("America/St_Johns") == "<-0330>3:30<-0230>,M3.2.0,M11.1.0");
    CHECK(posix("Australia/Sydney") == "<+10>-10<+11>,M10.1.0,M4.1.0/3");
    CHECK(posix("Australia/Adelaide") == "<+0930>-9:30<+1030>,M10.1.0,M4.1.0/3");
    CHECK(posix("Pacific/Chatham") == "<+1245>-12:45<+1345>,M9.5.0/2:45,M4.1.0/3:45");
}

TEST_CASE("a zone without daylight time keeps its standard offset all year") {
    char out[48];
    const auto posix = [&](const char* zone) { return civil::posixZoneFor(zone, out, sizeof(out)) ? std::string(out) : std::string("refused"); };
    CHECK(posix("Asia/Tokyo") == "<+09>-9");
    CHECK(posix("Asia/Kolkata") == "<+0530>-5:30");
    CHECK(posix("Asia/Kathmandu") == "<+0545>-5:45");
    CHECK(posix("America/Sao_Paulo") == "<-03>3");
    CHECK(posix("Pacific/Kiritimati") == "<+14>-14");
    CHECK(posix("Pacific/Pago_Pago") == "<-11>11");
    // Europe's exceptions keep no daylight time, and neither does Arizona.
    CHECK(posix("Europe/Moscow") == "<+03>-3");
    CHECK(posix("America/Phoenix") == "<-07>7");
}

TEST_CASE("a zone name the tables do not hold is refused rather than guessed") {
    char out[48] = "kept";
    for (const char* bad : {"", "Berlin", "Europe/", "Europe/Atlantis", "Paris", "Asia/Paris", "America/Argentina/Bogota", "UTC"})
        CHECK_FALSE(civil::posixZoneFor(bad, out, sizeof(out)));
    CHECK_FALSE(civil::posixZoneFor(nullptr, out, sizeof(out)));
}

TEST_CASE("every zone the standard-offset table holds turns into a POSIX TZ string that parses") {
    char out[48];
    civil::Zone z;
    int zones = 0;
    for (const civil::StandardOffset& o : civil::kStandardOffsets) {
        std::string region;
        for (const char* p = o.zones; *p;) {
            while (*p == ' ') p++;
            const char* end = p;
            while (*end && *end != ' ') end++;
            std::string token(p, end);
            const size_t slash = token.rfind('/');
            if (slash != std::string::npos) region = token.substr(0, slash + 1);
            const std::string zone = slash != std::string::npos ? token : region + token;
            CAPTURE(zone);
            REQUIRE(civil::posixZoneFor(zone.c_str(), out, sizeof(out)));
            CHECK(civil::parseZone(out, z));
            CHECK(z.standardOffset == o.minutes * 60);
            zones++;
            p = end;
        }
    }
    CHECK(zones == 418);
}

// The reference times are NOAA's solar calculator (the Meeus algorithm of its spreadsheet), which the device's shorter equations follow to three minutes.
TEST_CASE("sunrise and sunset follow NOAA's solar calculator within three minutes") {
    struct Case { const char* place; float lat, lon; int y, m, d; double rise, set; };
    const Case cases[] = {
        {"Berlin, midsummer", 52.52f, 13.40f, 2026, 6, 21, 163.1, 1173.3},
        {"Berlin, midwinter", 52.52f, 13.40f, 2026, 12, 21, 434.9, 894.0},
        {"Berlin, equinox", 52.52f, 13.40f, 2026, 3, 20, 309.3, 1039.5},
        {"Sydney, summer", -33.87f, 151.21f, 2026, 1, 15, -300.5, 549.0},
        {"New York, July", 40.71f, -74.01f, 2026, 7, 4, 570.3, 1470.5},
        {"Quito, on the equator", -0.18f, -78.47f, 2026, 11, 15, 654.7, 1382.3},
    };
    for (const Case& c : cases) {
        CAPTURE(std::string(c.place));
        const civil::SunTimes t = civil::sunTimes(civil::daysFromCivil(c.y, static_cast<uint32_t>(c.m), static_cast<uint32_t>(c.d)), c.lat, c.lon);
        REQUIRE(t.kind == civil::SunDay::RisesAndSets);
        CHECK(std::abs(minutesInto(c.y, c.m, c.d, t.rise) - c.rise) <= 3.0);
        CHECK(std::abs(minutesInto(c.y, c.m, c.d, t.set) - c.set) <= 3.0);
    }
}

TEST_CASE("above the polar circle the sun stays up in summer and down in winter") {
    CHECK(civil::sunTimes(civil::daysFromCivil(2026, 6, 21), 69.65f, 18.96f).kind == civil::SunDay::AlwaysUp);
    CHECK(civil::sunTimes(civil::daysFromCivil(2026, 12, 21), 69.65f, 18.96f).kind == civil::SunDay::AlwaysDown);
    CHECK(civil::sunTimes(civil::daysFromCivil(2026, 6, 21), -69.65f, 18.96f).kind == civil::SunDay::AlwaysDown);
}

TEST_CASE("an evening from a time to a time is on between them, through midnight when it ends next day") {
    const auto s = berlinSchedule(TimerService::kOnAtTime, 20 * 60, TimerService::kOffAtTime, 1 * 60);
    CHECK_FALSE(TimerService::lightsWanted(s, berlinLocal(2026, 11, 15, 19, 59)));
    CHECK(TimerService::lightsWanted(s, berlinLocal(2026, 11, 15, 20, 0)));
    CHECK(TimerService::lightsWanted(s, berlinLocal(2026, 11, 16, 0, 30)));
    CHECK_FALSE(TimerService::lightsWanted(s, berlinLocal(2026, 11, 16, 1, 0)));
    CHECK_FALSE(TimerService::lightsWanted(s, berlinLocal(2026, 11, 16, 12, 0)));
}

TEST_CASE("only the ticked days start an evening, and an evening runs on into the next day") {
    constexpr uint8_t kFridayOnly = 1u << 5;
    const auto s = berlinSchedule(TimerService::kOnAtTime, 22 * 60, TimerService::kOffAtTime, 2 * 60, kFridayOnly);
    CHECK(TimerService::lightsWanted(s, berlinLocal(2026, 11, 20, 23, 0)));      // Friday
    CHECK(TimerService::lightsWanted(s, berlinLocal(2026, 11, 21, 1, 0)));       // Friday's evening, on Saturday
    CHECK_FALSE(TimerService::lightsWanted(s, berlinLocal(2026, 11, 21, 23, 0)));   // Saturday
    CHECK_FALSE(TimerService::lightsWanted(s, berlinLocal(2026, 11, 19, 23, 0)));   // Thursday
    const auto none = berlinSchedule(TimerService::kOnAtTime, 22 * 60, TimerService::kOffAtTime, 2 * 60, 0);
    int64_t at = 0; bool turnsOn = false;
    CHECK_FALSE(TimerService::nextMoment(none, berlinLocal(2026, 11, 20, 12, 0), at, turnsOn));
}

TEST_CASE("dusk to dawn is on from sunset to the next sunrise, in local time") {
    const auto s = berlinSchedule(TimerService::kOnAtSunset, 0, TimerService::kOffAtSunrise, 0);
    // Berlin on 21 December 2026: sunset 15:54, sunrise next day 08:16, standard time.
    CHECK_FALSE(TimerService::lightsWanted(s, berlinLocal(2026, 12, 21, 15, 50)));
    CHECK(TimerService::lightsWanted(s, berlinLocal(2026, 12, 21, 15, 58)));
    CHECK(TimerService::lightsWanted(s, berlinLocal(2026, 12, 22, 8, 10)));
    CHECK_FALSE(TimerService::lightsWanted(s, berlinLocal(2026, 12, 22, 8, 22)));
    // On 21 June, in daylight time: sunset 21:33.
    CHECK_FALSE(TimerService::lightsWanted(s, berlinLocal(2026, 6, 21, 21, 29)));
    CHECK(TimerService::lightsWanted(s, berlinLocal(2026, 6, 21, 21, 37)));
}

TEST_CASE("dusk to a fixed time ends at that time, and the next moment says which way the lights go") {
    const auto s = berlinSchedule(TimerService::kOnAtSunset, 0, TimerService::kOffAtTime, 23 * 60);
    int64_t at = 0; bool turnsOn = false;
    REQUIRE(TimerService::nextMoment(s, berlinLocal(2026, 12, 21, 12, 0), at, turnsOn));
    CHECK(turnsOn);
    CHECK(std::abs(static_cast<double>(at - berlinLocal(2026, 12, 21, 15, 54))) <= 120.0);
    REQUIRE(TimerService::nextMoment(s, berlinLocal(2026, 12, 21, 18, 0), at, turnsOn));
    CHECK_FALSE(turnsOn);
    CHECK(at == berlinLocal(2026, 12, 21, 23, 0));
}

TEST_CASE("where the sun does not set, a dusk evening does not start") {
    auto s = berlinSchedule(TimerService::kOnAtSunset, 0, TimerService::kOffAtTime, 23 * 60);
    s.latitude = 69.65f; s.longitude = 18.96f;
    int64_t at = 0; bool turnsOn = false;
    CHECK_FALSE(TimerService::lightsWanted(s, berlinLocal(2026, 6, 21, 22, 0)));
    CHECK_FALSE(TimerService::nextMoment(s, berlinLocal(2026, 6, 21, 12, 0), at, turnsOn));
}

namespace {

/// A stand-in for the lights' master switch, the one control the timer sets.
struct Lights : MoonModule {
    bool on = true;
    void defineControls() override { controls_.addControl("on", on); MoonModule::defineControls(); }
};

/// A device with lights and a timer on Berlin time.
struct Device {
    Scheduler scheduler;
    Lights* lights = new Lights();
    TimerService* timer = new TimerService();

    Device() {
        lights->setName("Drivers");
        timer->setName("Timer");
        std::snprintf(timer->timezone, sizeof(timer->timezone), "%s", kBerlin);
        timer->location = TimerService::kLocationManual;
        timer->latitude = 5252; timer->longitude = 1340;
        timer->turnOn = TimerService::kOnAtTime; timer->onTime = 20 * 60;
        timer->turnOff = TimerService::kOffAtTime; timer->offTime = 23 * 60;
        scheduler.addModule(lights);
        scheduler.addModule(timer);
        scheduler.setup();
    }
    ~Device() { scheduler.release(); platform::setTestUtcNow(0); }

    void at(int64_t utc) { platform::setTestUtcNow(static_cast<uint32_t>(utc)); timer->tick1s(); }
    std::string read(const char* name) {
        for (uint8_t i = 0; i < timer->controls().count(); i++)
            if (std::strcmp(timer->controls()[i].name, name) == 0) return static_cast<const char*>(timer->controls()[i].ptr);
        return "";
    }
};

}  // namespace

TEST_CASE("the timer switches the lights as the clock passes a moment, and leaves a switch by hand alone") {
    Device d;
    d.at(utcOf(2026, 11, 20, 18, 59));   // 19:59 in Berlin: the first second only notes the window
    CHECK(d.lights->on);
    CHECK(d.read("clock") == "Fri 19:59");
    CHECK(d.read("next") == "Turns on today 20:00");
    d.at(utcOf(2026, 11, 20, 19, 0));
    CHECK(d.lights->on);
    d.lights->on = false;                  // switched off by hand in the evening
    d.at(utcOf(2026, 11, 20, 20, 0));
    CHECK_FALSE(d.lights->on);             // stays off: no moment passed
    d.lights->on = true;
    d.at(utcOf(2026, 11, 20, 22, 0));      // 23:00 in Berlin
    CHECK_FALSE(d.lights->on);
    CHECK(d.read("next") == "Turns on tomorrow 20:00");
}

TEST_CASE("a timer that starts inside an evening leaves the lights as they are until the next moment") {
    Device d;
    d.lights->on = false;
    d.at(utcOf(2026, 11, 20, 20, 0));
    d.at(utcOf(2026, 11, 20, 20, 1));
    CHECK_FALSE(d.lights->on);
}

TEST_CASE("without a time zone the timer waits and says so") {
    Device d;
    d.timer->timezone[0] = 0;
    d.timer->onControlChanged("timezone");
    d.at(utcOf(2026, 11, 20, 19, 0));
    CHECK(d.read("next") == "Enter a time zone");
    CHECK(d.read("clock").empty());
}

namespace {
int locationAnswer(const char*, const char* url, const char*, char* body, size_t len) {
    CHECK(std::string(url) == TimerService::kLocationUrl);
    std::snprintf(body, len, "%s", R"({"ip":"203.0.113.7","city":"Berlin","region":"State of Berlin","country":"DE","loc":"52.5244,13.4105","org":"AS64496 Example","postal":"10115","timezone":"Europe/Berlin","readme":"https://ipinfo.io/missingauth"})");
    return 200;
}
}  // namespace

TEST_CASE("from the internet, the timer finds its place and time zone once and keeps them") {
    if (!platform::httpsAvailable()) { MESSAGE("no HTTP client in this build"); return; }
    platform::setTestHttpFetch(&locationAnswer);
    Device d;
    d.timer->location = TimerService::kLocationInternet;
    d.timer->timezone[0] = 0;
    d.timer->latitude = d.timer->longitude = 0;
    d.timer->onControlChanged("timezone");
    d.at(utcOf(2026, 11, 20, 12, 0));
    REQUIRE(JsonFetch::instance().settle(2000));
    d.at(utcOf(2026, 11, 20, 12, 0));
    CHECK(std::string(d.timer->timezone) == kBerlin);
    CHECK(d.timer->latitude == 5252);
    CHECK(d.timer->longitude == 1341);
    CHECK(d.timer->dirty());
    CHECK(d.read("clock") == "Fri 13:00");
    platform::setTestHttpFetch(nullptr);
}

namespace {
int rateLimited(const char*, const char*, const char*, char* body, size_t len) {
    std::snprintf(body, len, "%s", R"({"error":{"title":"Rate limit exceeded"}})");
    return 429;
}
int unknownZone(const char*, const char*, const char*, char* body, size_t len) {
    std::snprintf(body, len, "%s", R"({"loc":"52.5244,13.4105","timezone":"Mars/Olympus_Mons"})");
    return 200;
}
}  // namespace

TEST_CASE("a lookup that fails says why on the card, and one that answers an unknown zone asks for it by hand") {
    if (!platform::httpsAvailable()) { MESSAGE("no HTTP client in this build"); return; }
    for (const auto& [answer, says] : {std::pair{&rateLimited, "The location service answered 429"}, std::pair{&unknownZone, "Enter the location by hand"}}) {
        platform::setTestHttpFetch(answer);
        Device d;
        d.timer->location = TimerService::kLocationInternet;
        d.timer->timezone[0] = 0;
        d.timer->onControlChanged("location");
        d.at(utcOf(2026, 11, 20, 12, 0));
        REQUIRE(JsonFetch::instance().settle(2000));
        d.at(utcOf(2026, 11, 20, 12, 0));
        CHECK(d.read("next") == says);
        CHECK(std::string(d.timer->timezone).empty());
        platform::setTestHttpFetch(nullptr);
    }
}

TEST_CASE("the location service's answer gives the place in hundredths of a degree and the zone as a POSIX TZ string") {
    json::JsonDoc doc;
    REQUIRE(json::parse(R"({"city":"Sydney","country":"AU","loc":"-33.8688,151.2093","timezone":"Australia/Sydney"})", doc));
    int16_t lat = 0, lon = 0;
    char zone[48];
    REQUIRE(TimerService::readLocation(doc, lat, lon, zone, sizeof(zone)));
    CHECK(lat == -3387);
    CHECK(lon == 15121);
    CHECK(std::string(zone) == "<+10>-10<+11>,M10.1.0,M4.1.0/3");
    REQUIRE(json::parse(R"({"loc":"-0.2,-78.5","timezone":"America/Guayaquil"})", doc));
    REQUIRE(TimerService::readLocation(doc, lat, lon, zone, sizeof(zone)));
    CHECK(lat == -20);
    CHECK(lon == -7850);
}

TEST_CASE("a lookup answer without a place or a known zone is not taken") {
    for (const char* answer : {R"({"error":{"title":"Rate limit exceeded"}})",
                               R"({"loc":"52.5244,13.4105"})",
                               R"({"timezone":"Europe/Berlin"})",
                               R"({"loc":"52.5244","timezone":"Europe/Berlin"})",
                               R"({"loc":"91.0,13.4","timezone":"Europe/Berlin"})",
                               R"({"loc":"52.5,181","timezone":"Europe/Berlin"})",
                               R"({"loc":"52.5,13.4x","timezone":"Europe/Berlin"})",
                               R"({"loc":"52.5,13.4","timezone":"Mars/Olympus_Mons"})"}) {
        CAPTURE(answer);
        json::JsonDoc doc;
        REQUIRE(json::parse(answer, doc));
        int16_t lat = 7, lon = 7;
        char zone[48] = "kept";
        CHECK_FALSE(TimerService::readLocation(doc, lat, lon, zone, sizeof(zone)));
        CHECK(lat == 7);
        CHECK(std::string(zone) == "kept");
    }
}
