// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/module/MoonModule.h"
#include "core/module/Scheduler.h"
#include "core/util/CivilTime.h"
#include "core/util/JsonFetch.h"   // the worker the location lookup goes out on
#include "core/util/JsonUtil.h"
#include "platform/platform.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace mm {

/// Turns the lights on and off by the clock, or at sunset and sunrise.
/// @card TimerService.png
/// @attribution IPinfo
///
/// @guide
///
/// ## What it does
///
/// The lights go on at a time or sunset and off at a time or sunrise, on ticked days; on at 20:00, off at 01:00 is one evening. Switching by hand holds until the next moment.
///
/// Its place and time zone come once from its internet address, or by hand under Manual.
///
/// @technical
///
/// A moment fires once, as the clock passes it, like cron: one the device was off for is skipped. Sunrise and sunset are NOAA's General Solar Position equations at the stored place, computed on the device.
///
/// The zone is a POSIX `TZ` string, which keeps daylight time without a lookup; the lookup turns the IANA zone it answers into one by the tables @ref CivilTime holds. It asks @xref{the-location-service} once and stores the answer.
///
/// ## The location service
///
/// `http://ipinfo.io/json`, IPinfo's: no key, and it answers the place (`loc`) and the IANA zone of the address asking; it sees that address once. Plain HTTP, because a board without PSRAM lacks the RAM for a TLS session, and the answer is no secret: a forged one could only move sunset.
class TimerService : public MoonModule {
public:
    /// What turns the lights on.
    enum TurnOn : uint8_t { kOnAtTime, kOnAtSunset, kTurnOnCount };
    static constexpr const char* kTurnOnOptions[kTurnOnCount] = {"At a time", "At sunset"};
    /// What turns them off.
    enum TurnOff : uint8_t { kOffAtTime, kOffAtSunrise, kTurnOffCount };
    static constexpr const char* kTurnOffOptions[kTurnOffCount] = {"At a time", "At sunrise"};
    /// Where the place and the time zone come from.
    enum Location : uint8_t { kLocationInternet, kLocationManual, kLocationCount };
    static constexpr const char* kLocationOptions[kLocationCount] = {"From the internet", "Manual"};

    /// The service that answers where an internet address is.
    static constexpr const char* kLocationUrl = "http://ipinfo.io/json";
    /// The wait between lookups that failed, which JsonFetch backs off toward.
    static constexpr uint32_t kLookupRetryMs = 3600u * 1000u;
    /// Minutes in a day, the range of a time of day.
    static constexpr uint16_t kMinutesPerDay = 24 * 60;

    /// A service, so the container accepts it as a child.
    ModuleRole role() const MM_NONBLOCKING override { return ModuleRole::Service; }

    /// What turns the lights on.
    uint8_t turnOn = kOnAtSunset;
    /// The time they go on, in minutes after midnight.
    uint16_t onTime = 18 * 60;
    /// What turns them off.
    uint8_t turnOff = kOffAtTime;
    /// The time they go off, in minutes after midnight.
    uint16_t offTime = 23 * 60;
    /// The days the lights go on, one bit a day from Sunday in bit 0.
    uint8_t days = 0x7F;
    /// Where the place and the time zone come from.
    uint8_t location = kLocationInternet;
    /// The place's latitude, in hundredths of a degree north.
    int16_t latitude = 0;
    /// The place's longitude, in hundredths of a degree east.
    int16_t longitude = 0;
    /// The time zone as a POSIX `TZ` string, such as `CET-1CEST,M3.5.0,M10.5.0/3`.
    char timezone[48] = "";

    /// The schedule, the place, and what the timer does next.
    void defineControls() override {
        controls_.addSelect("turnOn", turnOn, kTurnOnOptions, kTurnOnCount);
        controls_.addControl("onTime", onTime, 0, kMinutesPerDay - 1);
        controls_.addSelect("turnOff", turnOff, kTurnOffOptions, kTurnOffCount);
        controls_.addControl("offTime", offTime, 0, kMinutesPerDay - 1);
        controls_.addControl("days", days, 0, 0x7F);
        controls_.addSelect("location", location, kLocationOptions, kLocationCount);
        controls_.addControl("latitude", latitude, -9000, 9000);
        controls_.addControl("longitude", longitude, -18000, 18000);
        controls_.addText("timezone", timezone, sizeof(timezone));
        /// The local time and weekday the timer reads.
        controls_.addReadOnly("clock", clock_, sizeof(clock_));
        /// Today's sunrise and sunset at the place.
        controls_.addReadOnly("sun", sun_, sizeof(sun_));
        /// What the timer does next, or what it waits for.
        controls_.addReadOnly("next", next_, sizeof(next_));
        MoonModule::defineControls();
    }

    /// A changed zone parses at once; choosing the internet again looks the place up again.
    void onControlChanged(const char* name) override {
        if (std::strcmp(name, "timezone") == 0) zoneValid_ = civil::parseZone(timezone, zone_);
        if (std::strcmp(name, "location") == 0 && location == kLocationInternet) { timezone[0] = 0; zoneValid_ = false; lookupStatus_ = 0; }
        refresh(platform::utcNow(), false);
    }

    /// Read the saved zone and start from the window the clock is in, without switching the lights.
    void prepare() override {
        zoneValid_ = civil::parseZone(timezone, zone_);
        baselined_ = false;
        MoonModule::prepare();
    }

    /// Drop a lookup under way.
    void release() override {
        JsonFetch::instance().release(this);
        lookup_ = 0;
        MoonModule::release();
    }

    /// Look the place up when it is missing, then switch the lights when the clock passes a moment.
    void tick1s() MM_NONBLOCKING override {
        platform::startTimeSync();
        const uint32_t utc = platform::utcNow();
        if (location == kLocationInternet && !zoneValid_) lookUp();
        refresh(utc, true);
        MoonModule::tick1s();
    }

    /// The schedule as a pure value, for the window and the next moment.
    struct Schedule {
        uint8_t turnOn = kOnAtTime;     ///< what turns the lights on
        uint8_t turnOff = kOffAtTime;   ///< what turns them off
        uint16_t onTime = 0;            ///< minutes after midnight they go on, at a time
        uint16_t offTime = 0;           ///< minutes after midnight they go off, at a time
        uint8_t days = 0x7F;            ///< the ticked days, Sunday in bit 0
        float latitude = 0;             ///< degrees north
        float longitude = 0;            ///< degrees east
        civil::Zone zone;               ///< the local time zone
    };

    /// The evening that starts on local day `day`: its on and off moments as local seconds, false when it has none, the day not ticked or the sun not setting.
    static bool evening(const Schedule& s, int32_t day, int64_t& on, int64_t& off) {
        if (!(s.days >> civil::weekday(day) & 1)) return false;
        if (!onMoment(s, day, on)) return false;
        for (int32_t d = day; d <= day + 1; d++)
            if (offMoment(s, d, off) && off > on) return true;
        return false;
    }

    /// Whether the local second `now` falls in an evening, the one that started today or the one from yesterday that runs past midnight.
    static bool lightsWanted(const Schedule& s, int64_t now) {
        const int32_t today = civil::dayOf(now);
        for (int32_t day = today - 1; day <= today; day++) {
            int64_t on, off;
            if (evening(s, day, on, off) && now >= on && now < off) return true;
        }
        return false;
    }

    /// The next moment after the local second `now`, and whether it turns the lights on; false when none falls in the coming week.
    static bool nextMoment(const Schedule& s, int64_t now, int64_t& at, bool& turnsOn) {
        const int32_t today = civil::dayOf(now);
        bool found = false;
        for (int32_t day = today - 1; day <= today + 7; day++) {
            int64_t on, off;
            if (!evening(s, day, on, off)) continue;
            if (on > now && (!found || on < at)) { at = on; turnsOn = true; found = true; }
            if (off > now && (!found || off < at)) { at = off; turnsOn = false; found = true; }
        }
        return found;
    }

    /// Read the location service's answer, `"loc":"lat,lon"` and an IANA `"timezone"`, into a place and a POSIX zone; false when it lacks one.
    static bool readLocation(const json::JsonDoc& doc, int16_t& lat, int16_t& lon, char* zone, size_t zoneLen) {
        const json::JsonNode* root = json::at(doc, "");
        char loc[32], iana[48];
        if (!json::readString(json::member(doc, root, "loc"), loc, sizeof(loc))) return false;
        if (!json::readString(json::member(doc, root, "timezone"), iana, sizeof(iana))) return false;
        const char* p = loc;
        int16_t north = 0, east = 0;
        if (!readHundredths(p, 9000, north) || *p++ != ',' || !readHundredths(p, 18000, east) || *p) return false;
        if (!civil::posixZoneFor(iana, zone, zoneLen)) return false;
        lat = north;
        lon = east;
        return true;
    }

    /// Read a decimal degree such as `-33.8688` at `p` as hundredths, rounded, up to `limit` either way; `p` moves past it.
    static bool readHundredths(const char*& p, int32_t limit, int16_t& out) {
        const bool negative = *p == '-';
        if (negative) p++;
        int32_t whole = 0, thousandths = 0;
        if (!readDigits(p, 4, whole)) return false;
        int kept = 0;
        if (*p == '.') kept = readDigits(++p, 3, thousandths);
        for (; kept < 3; kept++) thousandths *= 10;
        const int32_t value = whole * 100 + (thousandths + 5) / 10;
        if (value > limit) return false;
        out = static_cast<int16_t>(negative ? -value : value);
        return true;
    }

    /// Read the digits at `p`, keeping the first `keep` of them in `value`; how many it kept.
    static int readDigits(const char*& p, int keep, int32_t& value) {
        int kept = 0;
        for (; *p >= '0' && *p <= '9'; p++)
            if (kept < keep) { value = value * 10 + (*p - '0'); kept++; }
        return kept;
    }

private:
    static constexpr const char* kDays[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    civil::Zone zone_;
    bool zoneValid_ = false;
    bool baselined_ = false;   ///< whether `wanted_` holds the window of an earlier second
    bool wanted_ = false;      ///< whether the lights were wanted on at that second
    int lookup_ = 0;           ///< the location lookup on its way, 0 when none is
    int lookupStatus_ = 0;     ///< the lookup's last HTTP status, -1 for an answer without a usable place or zone
    char clock_[16] = "";
    char sun_[32] = "";
    char next_[48] = "";

    static bool onMoment(const Schedule& s, int32_t day, int64_t& at) {
        if (s.turnOn == kOnAtTime) { at = static_cast<int64_t>(day) * civil::kSecondsPerDay + s.onTime * 60; return true; }
        const civil::SunTimes sun = civil::sunTimes(day, s.latitude, s.longitude);
        if (sun.kind != civil::SunDay::RisesAndSets) return false;
        at = s.zone.toLocal(sun.set);
        return true;
    }

    static bool offMoment(const Schedule& s, int32_t day, int64_t& at) {
        if (s.turnOff == kOffAtTime) { at = static_cast<int64_t>(day) * civil::kSecondsPerDay + s.offTime * 60; return true; }
        const civil::SunTimes sun = civil::sunTimes(day, s.latitude, s.longitude);
        if (sun.kind != civil::SunDay::RisesAndSets) return false;
        at = s.zone.toLocal(sun.rise);
        return true;
    }

    Schedule schedule() const {
        Schedule s;
        s.turnOn = turnOn; s.turnOff = turnOff; s.onTime = onTime; s.offTime = offTime; s.days = days;
        s.latitude = static_cast<float>(latitude) / 100.0f;
        s.longitude = static_cast<float>(longitude) / 100.0f;
        s.zone = zone_;
        return s;
    }

    bool usesSun() const { return turnOn == kOnAtSunset || turnOff == kOffAtSunrise; }

    /// Ask the location service, once a lookup can go, and store what it answers.
    void lookUp() {
        if (!platform::httpsAvailable() || !platform::networkReady()) return;
        JsonFetch& fetch = JsonFetch::instance();
        if (!lookup_) lookup_ = fetch.fetch(this, kLocationUrl, kLookupRetryMs);
        json::JsonDoc* doc = lookup_ ? fetch.doc(this, lookup_) : nullptr;
        lookupStatus_ = lookup_ ? fetch.status(this, lookup_) : 0;
        if (!doc) return;
        int16_t lat = 0, lon = 0;
        char zone[sizeof(timezone)];
        if (!readLocation(*doc, lat, lon, zone, sizeof(zone))) { if (lookupStatus_ == 200) lookupStatus_ = -1; return; }
        latitude = lat; longitude = lon;
        std::snprintf(timezone, sizeof(timezone), "%s", zone);
        zoneValid_ = civil::parseZone(timezone, zone_);
        fetch.release(this);   // found once, and kept
        lookup_ = 0;
        markDirty();
        notifyValuesChanged();
    }

    /// Show the clock and what comes next, and switch the lights when the window changed since the last second.
    void refresh(uint32_t utc, bool act) {
        if (!ready(utc)) return;
        const Schedule s = schedule();
        const int64_t now = zone_.toLocal(utc);
        const int32_t today = civil::dayOf(now);
        const unsigned minute = minuteOfDay(now);
        std::snprintf(clock_, sizeof(clock_), "%s %02u:%02u", kDays[civil::weekday(today)], minute / 60, minute % 60);
        writeSun(today);
        writeNext(s, now, today);
        if (act) follow(lightsWanted(s, now));
    }

    /// Whether the clock and the zone are known; says what is missing when not.
    bool ready(uint32_t utc) {
        if (utc && zoneValid_) return true;
        clock_[0] = sun_[0] = 0;
        if (!utc) say("Waiting for the time from the internet");
        else if (location == kLocationManual) say("Enter a time zone");
        else if (!platform::httpsAvailable() || lookupStatus_ < 0) say("Enter the location by hand");
        else if (lookupStatus_ && lookupStatus_ != 200) {
            std::snprintf(next_, sizeof(next_), "The location service answered %d", lookupStatus_);
            setStatus(next_);
        } else say("Finding the location");
        return false;
    }

    void writeNext(const Schedule& s, int64_t now, int32_t today) {
        int64_t at = 0;
        bool turnsOn = false;
        if (!s.days) { say("Tick a day for the lights to go on"); return; }
        if (!nextMoment(s, now, at, turnsOn)) { say(usesSun() ? "The sun does not rise and set here this week" : "Nothing this week"); return; }
        const int32_t day = civil::dayOf(at);
        const unsigned m = minuteOfDay(at);
        const char* when = day == today ? "today" : day == today + 1 ? "tomorrow" : kDays[civil::weekday(day)];
        std::snprintf(next_, sizeof(next_), "Turns %s %s %02u:%02u", turnsOn ? "on" : "off", when, m / 60, m % 60);
        setStatus(next_);
    }

    /// Switches the lights only when the wanted state changes; the first call records the baseline, so a manual switch holds until the next moment.
    void follow(bool want) {
        if (baselined_ && want != wanted_) switchLights(want);
        wanted_ = want;
        baselined_ = true;
    }

    void writeSun(int32_t today) {
        const civil::SunTimes t = civil::sunTimes(today, static_cast<float>(latitude) / 100.0f, static_cast<float>(longitude) / 100.0f);
        if (t.kind != civil::SunDay::RisesAndSets) { std::snprintf(sun_, sizeof(sun_), "%s", t.kind == civil::SunDay::AlwaysUp ? "Up all day" : "Down all day"); return; }
        const unsigned rise = minuteOfDay(zone_.toLocal(t.rise)), set = minuteOfDay(zone_.toLocal(t.set));
        std::snprintf(sun_, sizeof(sun_), "Sunrise %02u:%02u, sunset %02u:%02u", rise / 60, rise % 60, set / 60, set % 60);
    }

    /// The minute of its day a local second falls in, 0 to 1439.
    static unsigned minuteOfDay(int64_t local) {
        return static_cast<unsigned>((local - static_cast<int64_t>(civil::dayOf(local)) * civil::kSecondsPerDay) / 60) % kMinutesPerDay;
    }

    void say(const char* text) {
        std::snprintf(next_, sizeof(next_), "%s", text);
        setStatus(next_);
    }

    /// The lights' own switch, through the one path every surface sets a control by.
    void switchLights(bool on) {
        if (Scheduler* scheduler = Scheduler::instance()) scheduler->setControl("Drivers", "on", on ? "{\"value\":true}" : "{\"value\":false}", true);
    }
};

}  // namespace mm
