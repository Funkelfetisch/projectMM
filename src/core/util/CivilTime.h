// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/// Calendar dates, POSIX time-zone rules and the sun's rise and set: the wall clock a timer reads.
///
/// Days count from 1970-01-01, the Unix epoch, and seconds are UTC unless named local. Dates follow the proleptic Gregorian calendar by Howard Hinnant's `days_from_civil`; a zone is a POSIX `TZ` string (IEEE Std 1003.1, §8.3); the sun is NOAA's General Solar Position equations; the zone tables follow the IANA tz database.
/// @attribution Howard Hinnant, NOAA, IANA tz database
namespace mm::civil {

constexpr int32_t kSecondsPerDay = 86400;

/// A calendar date.
struct Date { int16_t year; uint8_t month; uint8_t day; };

/// Days since 1970-01-01 for a date, Hinnant's `days_from_civil`.
constexpr int32_t daysFromCivil(int32_t y, uint32_t m, uint32_t d) {
    y -= m <= 2;
    const int32_t era = (y >= 0 ? y : y - 399) / 400;
    const uint32_t yoe = static_cast<uint32_t>(y - era * 400);
    const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

/// The date of a day since 1970-01-01, Hinnant's `civil_from_days`.
constexpr Date civilFromDays(int32_t z) {
    z += 719468;
    const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    const uint32_t doe = static_cast<uint32_t>(z - era * 146097);
    const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const uint32_t mp = (5 * doy + 2) / 153;
    const uint32_t d = doy - (153 * mp + 2) / 5 + 1;
    const uint32_t m = mp < 10 ? mp + 3 : mp - 9;
    return {static_cast<int16_t>(static_cast<int32_t>(yoe) + era * 400 + (m <= 2)), static_cast<uint8_t>(m), static_cast<uint8_t>(d)};
}

/// The weekday of a day since 1970-01-01, 0 for Sunday as `tm_wday` counts; the epoch was a Thursday.
constexpr uint8_t weekday(int32_t days) { return static_cast<uint8_t>((days % 7 + 11) % 7); }

/// The day since 1970-01-01 that a second falls in, rounding toward the past for a negative one.
constexpr int32_t dayOf(int64_t seconds) {
    return static_cast<int32_t>(seconds >= 0 ? seconds / kSecondsPerDay : (seconds + 1) / kSecondsPerDay - 1);
}

/// The day of the year, 1 on 1 January.
constexpr uint16_t dayOfYear(int32_t days) {
    return static_cast<uint16_t>(days - daysFromCivil(civilFromDays(days).year, 1, 1) + 1);
}

/// Whether a year has 29 February.
constexpr bool leapYear(int32_t y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

/// The days in a month.
constexpr uint8_t monthLength(int32_t y, uint8_t m) {
    return m == 2 ? (leapYear(y) ? 29 : 28) : (m == 4 || m == 6 || m == 9 || m == 11) ? 30 : 31;
}

// ---- POSIX time-zone rules ----------------------------------------------------------------------------------------

/// When daylight time starts or ends: day `d` (0 Sunday) of week `w` (1 to 5, 5 the last) of month `m`, at `seconds` local, which may run below 0 or past 24 h.
struct Transition { uint8_t month = 0, week = 0, day = 0; int32_t seconds = 7200; };

/// A parsed POSIX `TZ` string: the standard offset and, where the zone keeps daylight time, its offset and its two transitions. Offsets are east of UTC, the opposite sign of the string's.
struct Zone {
    int32_t standardOffset = 0;   ///< seconds east of UTC outside daylight time
    int32_t daylightOffset = 0;   ///< seconds east of UTC in daylight time
    bool hasDaylight = false;     ///< whether the zone keeps daylight time
    Transition start;             ///< where daylight time starts, in local standard time
    Transition end;               ///< where it ends, in local daylight time

    /// Seconds east of UTC at the UTC second `utc`.
    int32_t offsetAt(int64_t utc) const {
        if (!hasDaylight) return standardOffset;
        const int32_t year = civilFromDays(dayOf(utc + standardOffset)).year;
        // The start is reckoned in standard time and the end in daylight time, as POSIX defines them.
        const int64_t on = localSecond(year, start) - standardOffset;
        const int64_t off = localSecond(year, end) - daylightOffset;
        const bool daylight = on < off ? (utc >= on && utc < off) : (utc < off || utc >= on);
        return daylight ? daylightOffset : standardOffset;
    }

    /// The local second of `utc`.
    int64_t toLocal(int64_t utc) const { return utc + offsetAt(utc); }

    /// The UTC second a local second names; a local time skipped or repeated by a transition reads by the offset before it.
    int64_t toUtc(int64_t local) const {
        const int64_t guess = local - standardOffset;
        return local - offsetAt(guess - (daylightOffset - standardOffset));
    }

    /// The local second a transition falls on in `year`.
    static int64_t localSecond(int32_t year, const Transition& t) {
        const int32_t first = daysFromCivil(year, t.month, 1);
        int32_t day = 1 + (t.day - weekday(first) + 7) % 7 + 7 * (t.week - 1);
        while (day > monthLength(year, t.month)) day -= 7;
        return static_cast<int64_t>(first + day - 1) * kSecondsPerDay + t.seconds;
    }
};

namespace detail {

/// Skip a zone abbreviation: letters, or anything quoted in angle brackets; false when it is missing.
inline bool skipName(const char*& p) {
    if (*p == '<') {
        const char* close = std::strchr(p, '>');
        if (!close) return false;
        p = close + 1;
        return true;
    }
    const char* from = p;
    while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) p++;
    return p - from >= 3;
}

/// Read `[+-]hh[:mm[:ss]]` as seconds; false when no digit starts it.
inline bool readClock(const char*& p, int32_t& out) {
    int32_t sign = 1;
    if (*p == '+' || *p == '-') sign = *p++ == '-' ? -1 : 1;
    if (*p < '0' || *p > '9') return false;
    int32_t parts[3] = {0, 0, 0};
    for (int i = 0; i < 3; i++) {
        if (i && *p != ':') break;
        if (i) p++;
        char* end = nullptr;
        parts[i] = static_cast<int32_t>(std::strtol(p, &end, 10));
        if (end == p) return false;
        p = end;
    }
    out = sign * (parts[0] * 3600 + parts[1] * 60 + parts[2]);
    return true;
}

/// Read `Mm.w.d[/time]`; the Julian forms `Jn` and `n` are refused.
inline bool readTransition(const char*& p, Transition& t) {
    if (*p++ != 'M') return false;
    char* end = nullptr;
    const long m = std::strtol(p, &end, 10);
    if (*end != '.') return false;
    const long w = std::strtol(end + 1, &end, 10);
    if (*end != '.') return false;
    const long d = std::strtol(end + 1, &end, 10);
    p = end;
    if (m < 1 || m > 12 || w < 1 || w > 5 || d < 0 || d > 6) return false;
    t.month = static_cast<uint8_t>(m); t.week = static_cast<uint8_t>(w); t.day = static_cast<uint8_t>(d);
    t.seconds = 7200;
    if (*p == '/') { p++; if (!readClock(p, t.seconds)) return false; }
    return true;
}

}  // namespace detail

/// Parse a POSIX `TZ` string such as `CET-1CEST,M3.5.0,M10.5.0/3`; false, leaving `out` unchanged, when it is not one.
inline bool parseZone(const char* text, Zone& out) {
    if (!text || !*text) return false;
    const char* p = text;
    Zone z;
    int32_t west = 0;
    if (!detail::skipName(p) || !detail::readClock(p, west)) return false;
    z.standardOffset = -west;
    if (*p) {
        if (!detail::skipName(p)) return false;
        z.daylightOffset = z.standardOffset + 3600;
        if (*p && *p != ',') { if (!detail::readClock(p, west)) return false; z.daylightOffset = -west; }
        // A daylight name without rules takes the US rules, as POSIX leaves it to the implementation and glibc does.
        z.start = {3, 2, 0, 7200}; z.end = {11, 1, 0, 7200};
        if (*p == ',') {
            p++;
            if (!detail::readTransition(p, z.start) || *p++ != ',' || !detail::readTransition(p, z.end)) return false;
        }
        if (*p) return false;
        z.hasDaylight = true;
    }
    out = z;
    return true;
}

/// A zone abbreviation for an offset, the numeric form POSIX quotes: `<+01>`, `<-0330>`.
inline int formatOffsetName(char* out, size_t len, int32_t eastSeconds) {
    const int32_t minutes = (eastSeconds < 0 ? -eastSeconds : eastSeconds) / 60;
    const char sign = eastSeconds < 0 ? '-' : '+';
    return minutes % 60 ? std::snprintf(out, len, "<%c%02ld%02ld>", sign, static_cast<long>(minutes / 60), static_cast<long>(minutes % 60))
                        : std::snprintf(out, len, "<%c%02ld>", sign, static_cast<long>(minutes / 60));
}

/// The daylight-time rules of the zones that keep it, by IANA name. `utcSynced` rules move at 01:00 UTC, the European Union's, so their local hour follows the zone's offset.
struct DaylightRule {
    const char* zones;     ///< IANA names, space-separated; a name ending in `/` matches every zone under it
    const char* except;    ///< names under such a prefix that keep no daylight time
    const char* rule;      ///< `,start,end` for the rules fixed in local time
    bool utcSynced;        ///< the European Union's rule, whose moves follow the zone's offset
};

/// The rules in force in 2026; a zone not listed keeps its standard offset all year, which is wrong only where the rule is no POSIX rule (Morocco, Gaza, Lord Howe, Troll).
inline constexpr DaylightRule kDaylightRules[] = {
    {"Europe/Chisinau", "", ",M3.5.0,M10.5.0/3", false},
    {"Europe/ Atlantic/Canary Atlantic/Madeira Atlantic/Faroe Atlantic/Azores Africa/Ceuta Asia/Nicosia Asia/Famagusta "
     "America/Nuuk America/Scoresbysund Arctic/Longyearbyen",
     "Europe/Moscow Europe/Minsk Europe/Istanbul Europe/Kaliningrad Europe/Samara Europe/Volgograd Europe/Saratov "
     "Europe/Ulyanovsk Europe/Astrakhan Europe/Kirov Europe/Simferopol", "", true},
    {"America/New_York America/Chicago America/Denver America/Los_Angeles America/Anchorage America/Juneau America/Sitka "
     "America/Yakutat America/Nome America/Metlakatla America/Adak America/Boise America/Detroit America/Indiana/ "
     "America/Kentucky/ America/North_Dakota/ America/Menominee America/Toronto America/Winnipeg America/Edmonton "
     "America/Vancouver America/Halifax America/Moncton America/Glace_Bay America/Goose_Bay America/St_Johns "
     "America/Iqaluit America/Rankin_Inlet America/Resolute America/Cambridge_Bay America/Inuvik America/Ojinaga "
     "America/Matamoros America/Ciudad_Juarez America/Tijuana America/Nassau America/Port-au-Prince America/Grand_Turk "
     "America/Miquelon America/Thule Atlantic/Bermuda US/ Canada/", "", ",M3.2.0,M11.1.0", false},
    {"America/Havana", "", ",M3.2.0/0,M11.1.0/1", false},
    {"America/Santiago", "", ",M9.1.6/24,M4.1.6/24", false},
    {"Pacific/Easter", "", ",M9.1.6/22,M4.1.6/22", false},
    {"Australia/Sydney Australia/Melbourne Australia/Hobart Australia/Canberra Australia/ACT Australia/NSW "
     "Australia/Victoria Australia/Tasmania Australia/Currie Australia/Adelaide Australia/Broken_Hill Australia/South "
     "Antarctica/Macquarie Pacific/Norfolk",
     "", ",M10.1.0,M4.1.0/3", false},
    {"Pacific/Auckland Antarctica/McMurdo", "", ",M9.5.0,M4.1.0/3", false},
    {"Pacific/Chatham", "", ",M9.5.0/2:45,M4.1.0/3:45", false},
    {"Asia/Jerusalem Asia/Tel_Aviv", "", ",M3.4.4/26,M10.5.0", false},
    {"Asia/Beirut", "", ",M3.5.0/0,M10.5.0/0", false},
    {"Africa/Cairo", "", ",M4.5.5/0,M10.5.4/24", false},
};

/// The standard offset of every zone in tzdata's `zone.tab`, in minutes east of UTC, as of 2026. A name without its region, such as `Paris` after `Europe/Berlin`, takes the region of the name before it.
struct StandardOffset {
    int16_t minutes;       ///< minutes east of UTC outside daylight time
    const char* zones;     ///< the zones on it, space-separated
};

inline constexpr StandardOffset kStandardOffsets[] = {
    {-660, "Pacific/Midway Niue Pago_Pago"},
    {-600, "America/Adak Pacific/Honolulu Rarotonga Tahiti"},
    {-570, "Pacific/Marquesas"},
    {-540, "America/Anchorage Juneau Metlakatla Nome Sitka Yakutat Pacific/Gambier"},
    {-480, "America/Los_Angeles Tijuana Vancouver Pacific/Pitcairn"},
    {-420, "America/Boise Cambridge_Bay Ciudad_Juarez Creston Dawson Dawson_Creek Denver Edmonton Fort_Nelson "
     "Hermosillo Inuvik Mazatlan Phoenix Whitehorse"},
    {-360, "America/Bahia_Banderas Belize Chicago Chihuahua Costa_Rica El_Salvador Guatemala "
     "America/Indiana/Knox Tell_City America/Managua Matamoros Menominee Merida Mexico_City Monterrey "
     "America/North_Dakota/Beulah Center New_Salem America/Ojinaga Rankin_Inlet Regina Resolute "
     "Swift_Current Tegucigalpa Winnipeg Pacific/Easter Galapagos"},
    {-300, "America/Atikokan Bogota Cancun Cayman Detroit Eirunepe Grand_Turk Guayaquil Havana "
     "America/Indiana/Indianapolis Marengo Petersburg Vevay Vincennes Winamac America/Iqaluit Jamaica "
     "America/Kentucky/Louisville Monticello America/Lima Nassau New_York Panama Port-au-Prince Rio_Branco "
     "Toronto"},
    {-240, "America/Anguilla Antigua Aruba Barbados Blanc-Sablon Boa_Vista Campo_Grande Caracas Cuiaba Curacao "
     "Dominica Glace_Bay Goose_Bay Grenada Guadeloupe Guyana Halifax Kralendijk La_Paz Lower_Princes "
     "Manaus Marigot Martinique Moncton Montserrat Port_of_Spain Porto_Velho Puerto_Rico Santiago "
     "Santo_Domingo St_Barthelemy St_Kitts St_Lucia St_Thomas St_Vincent Thule Tortola Atlantic/Bermuda"},
    {-210, "America/St_Johns"},
    {-180, "America/Araguaina America/Argentina/Buenos_Aires Catamarca Cordoba Jujuy La_Rioja Mendoza "
     "Rio_Gallegos Salta San_Juan San_Luis Tucuman Ushuaia America/Asuncion Bahia Belem Cayenne Coyhaique "
     "Fortaleza Maceio Miquelon Montevideo Paramaribo Punta_Arenas Recife Santarem Sao_Paulo "
     "Antarctica/Palmer Rothera Atlantic/Stanley"},
    {-120, "America/Noronha Nuuk Scoresbysund Atlantic/South_Georgia"},
    {-60, "Atlantic/Azores Cape_Verde"},
    {0, "Africa/Abidjan Accra Bamako Banjul Bissau Conakry Dakar Freetown Lome Monrovia Nouakchott "
     "Ouagadougou Sao_Tome America/Danmarkshavn Antarctica/Troll Atlantic/Canary Faroe Madeira Reykjavik "
     "St_Helena Europe/Dublin Guernsey Isle_of_Man Jersey Lisbon London"},
    {60, "Africa/Algiers Bangui Brazzaville Casablanca Ceuta Douala El_Aaiun Kinshasa Lagos Libreville Luanda "
     "Malabo Ndjamena Niamey Porto-Novo Tunis Arctic/Longyearbyen Europe/Amsterdam Andorra Belgrade Berlin "
     "Bratislava Brussels Budapest Busingen Copenhagen Gibraltar Ljubljana Luxembourg Madrid Malta Monaco "
     "Oslo Paris Podgorica Prague Rome San_Marino Sarajevo Skopje Stockholm Tirane Vaduz Vatican Vienna "
     "Warsaw Zagreb Zurich"},
    {120, "Africa/Blantyre Bujumbura Cairo Gaborone Harare Johannesburg Juba Khartoum Kigali Lubumbashi Lusaka "
     "Maputo Maseru Mbabane Tripoli Windhoek Asia/Beirut Famagusta Gaza Hebron Jerusalem Nicosia "
     "Europe/Athens Bucharest Chisinau Helsinki Kaliningrad Kyiv Mariehamn Riga Sofia Tallinn Vilnius"},
    {180, "Africa/Addis_Ababa Asmara Dar_es_Salaam Djibouti Kampala Mogadishu Nairobi Antarctica/Syowa "
     "Asia/Aden Amman Baghdad Bahrain Damascus Kuwait Qatar Riyadh Europe/Istanbul Kirov Minsk Moscow "
     "Simferopol Volgograd Indian/Antananarivo Comoro Mayotte"},
    {210, "Asia/Tehran"},
    {240, "Asia/Baku Dubai Muscat Tbilisi Yerevan Europe/Astrakhan Samara Saratov Ulyanovsk Indian/Mahe "
     "Mauritius Reunion"},
    {270, "Asia/Kabul"},
    {300, "Antarctica/Mawson Vostok Asia/Almaty Aqtau Aqtobe Ashgabat Atyrau Dushanbe Karachi Oral Qostanay "
     "Qyzylorda Samarkand Tashkent Yekaterinburg Indian/Kerguelen Maldives"},
    {330, "Asia/Colombo Kolkata"},
    {345, "Asia/Kathmandu"},
    {360, "Asia/Bishkek Dhaka Omsk Thimphu Urumqi Indian/Chagos"},
    {390, "Asia/Yangon Indian/Cocos"},
    {420, "Antarctica/Davis Asia/Bangkok Barnaul Ho_Chi_Minh Hovd Jakarta Krasnoyarsk Novokuznetsk Novosibirsk "
     "Phnom_Penh Pontianak Tomsk Vientiane Indian/Christmas"},
    {480, "Antarctica/Casey Asia/Brunei Hong_Kong Irkutsk Kuala_Lumpur Kuching Macau Makassar Manila Shanghai "
     "Singapore Taipei Ulaanbaatar Australia/Perth"},
    {525, "Australia/Eucla"},
    {540, "Asia/Chita Dili Jayapura Khandyga Pyongyang Seoul Tokyo Yakutsk Pacific/Palau"},
    {570, "Australia/Adelaide Broken_Hill Darwin"},
    {600, "Antarctica/DumontDUrville Macquarie Asia/Ust-Nera Vladivostok Australia/Brisbane Hobart Lindeman "
     "Melbourne Sydney Pacific/Chuuk Guam Port_Moresby Saipan"},
    {630, "Australia/Lord_Howe"},
    {660, "Asia/Magadan Sakhalin Srednekolymsk Pacific/Bougainville Efate Guadalcanal Kosrae Norfolk Noumea "
     "Pohnpei"},
    {720, "Antarctica/McMurdo Asia/Anadyr Kamchatka Pacific/Auckland Fiji Funafuti Kwajalein Majuro Nauru "
     "Tarawa Wake Wallis"},
    {765, "Pacific/Chatham"},
    {780, "Pacific/Apia Fakaofo Kanton Tongatapu"},
    {840, "Pacific/Kiritimati"},
};

/// Whether `zones`, written as kStandardOffsets writes them, holds `zone`.
inline bool listsZone(const char* zones, const char* zone) {
    const char* slash = std::strrchr(zone, '/');
    if (!slash) return false;
    const size_t regionLen = static_cast<size_t>(slash + 1 - zone);
    const char* city = slash + 1;
    const size_t cityLen = std::strlen(city);
    bool inRegion = false;
    for (const char* p = zones; *p;) {
        while (*p == ' ') p++;
        const char* end = p;
        const char* last = nullptr;
        for (; *end && *end != ' '; end++)
            if (*end == '/') last = end;
        if (last) inRegion = static_cast<size_t>(last + 1 - p) == regionLen && std::strncmp(p, zone, regionLen) == 0;
        const char* name = last ? last + 1 : p;
        if (inRegion && static_cast<size_t>(end - name) == cityLen && std::strncmp(name, city, cityLen) == 0) return true;
        p = end;
    }
    return false;
}

/// The standard offset of an IANA zone, in seconds east; false for a name the table does not know.
inline bool standardOffsetFor(const char* zone, int32_t& eastSeconds) {
    if (!zone || !*zone) return false;
    for (const StandardOffset& o : kStandardOffsets)
        if (listsZone(o.zones, zone)) { eastSeconds = o.minutes * 60; return true; }
    return false;
}

/// Whether the space-separated `list` names `zone`, a `/`-ended entry naming everything under it.
inline bool listNames(const char* list, const char* zone) {
    const size_t zoneLen = std::strlen(zone);
    for (const char* p = list; *p;) {
        while (*p == ' ') p++;
        const char* end = p;
        while (*end && *end != ' ') end++;
        const size_t len = static_cast<size_t>(end - p);
        if (len && ((len == zoneLen && std::strncmp(p, zone, len) == 0) ||
                    (p[len - 1] == '/' && len < zoneLen && std::strncmp(p, zone, len) == 0))) return true;
        p = end;
    }
    return false;
}

/// The daylight rule an IANA zone keeps, or null.
inline const DaylightRule* daylightRuleFor(const char* zone) {
    if (!zone || !*zone) return nullptr;
    for (const DaylightRule& r : kDaylightRules)
        if (listNames(r.zones, zone)) return listNames(r.except, zone) ? nullptr : &r;
    return nullptr;
}

/// Write the POSIX `TZ` string for a zone `standard` seconds east of UTC that keeps `rule`, or none. False when it does not fit.
inline bool writeZone(char* out, size_t len, int32_t standard, const DaylightRule* rule) {
    char standardName[12], daylightName[12], clock[12], moves[40] = "";
    formatOffsetName(standardName, sizeof(standardName), standard);
    const int32_t westMinutes = -standard / 60;
    const int32_t minutes = westMinutes < 0 ? -westMinutes : westMinutes;
    if (minutes % 60) std::snprintf(clock, sizeof(clock), "%s%ld:%02ld", westMinutes < 0 ? "-" : "", static_cast<long>(minutes / 60), static_cast<long>(minutes % 60));
    else std::snprintf(clock, sizeof(clock), "%s%ld", westMinutes < 0 ? "-" : "", static_cast<long>(minutes / 60));
    if (!rule) {
        const int n = std::snprintf(out, len, "%s%s", standardName, clock);
        return n > 0 && static_cast<size_t>(n) < len;
    }
    formatOffsetName(daylightName, sizeof(daylightName), standard + 3600);
    const long hour = static_cast<long>(standard / 3600);
    if (rule->utcSynced) std::snprintf(moves, sizeof(moves), ",M3.5.0/%ld,M10.5.0/%ld", 1 + hour, 2 + hour);
    else std::snprintf(moves, sizeof(moves), "%s", rule->rule);
    const int n = std::snprintf(out, len, "%s%s%s%s", standardName, clock, daylightName, moves);
    return n > 0 && static_cast<size_t>(n) < len;
}

/// Write the POSIX `TZ` string for an IANA zone: its standard offset and, where it keeps one, its daylight rule. False for a zone the tables do not know or a string that does not fit.
inline bool posixZoneFor(const char* zone, char* out, size_t len) {
    int32_t standard = 0;
    return standardOffsetFor(zone, standard) && writeZone(out, len, standard, daylightRuleFor(zone));
}

// ---- Sunrise and sunset ---------------------------------------------------------------------------------------------

/// Whether the sun rises and sets on a day, or stays up or down all of it.
enum class SunDay : uint8_t { RisesAndSets, AlwaysUp, AlwaysDown };

/// A day's sunrise and sunset, as UTC seconds.
struct SunTimes { SunDay kind = SunDay::RisesAndSets; int64_t rise = 0; int64_t set = 0; };

/// Sunrise and sunset on the UTC day `day` at `latitude` and `longitude`, degrees north and east, by NOAA's General Solar Position equations: within three minutes of NOAA's calculator away from the poles.
inline SunTimes sunTimes(int32_t day, float latitude, float longitude) {
    constexpr float kPi = 3.14159265f, kRad = kPi / 180.0f;
    const int32_t year = civilFromDays(day).year;
    // The fractional year at the location's solar noon, in radians.
    const float noonHour = 12.0f - longitude / 15.0f;
    const float g = 2.0f * kPi / (leapYear(year) ? 366.0f : 365.0f) * (static_cast<float>(dayOfYear(day)) - 1.0f + (noonHour - 12.0f) / 24.0f);
    const float eqTime = 229.18f * (0.000075f + 0.001868f * std::cos(g) - 0.032077f * std::sin(g)
                                    - 0.014615f * std::cos(2 * g) - 0.040849f * std::sin(2 * g));
    const float decl = 0.006918f - 0.399912f * std::cos(g) + 0.070257f * std::sin(g) - 0.006758f * std::cos(2 * g)
                     + 0.000907f * std::sin(2 * g) - 0.002697f * std::cos(3 * g) + 0.00148f * std::sin(3 * g);
    const float lat = latitude * kRad;
    // Risen means the upper limb clears the horizon: the 90.833° zenith adds refraction and the sun's radius.
    const float cosHourAngle = std::cos(90.833f * kRad) / (std::cos(lat) * std::cos(decl)) - std::tan(lat) * std::tan(decl);
    SunTimes t;
    if (cosHourAngle > 1.0f) { t.kind = SunDay::AlwaysDown; return t; }
    if (cosHourAngle < -1.0f) { t.kind = SunDay::AlwaysUp; return t; }
    const float hourAngle = std::acos(cosHourAngle) / kRad;
    const int64_t midnight = static_cast<int64_t>(day) * kSecondsPerDay;
    t.rise = midnight + static_cast<int64_t>(std::lround((720.0f - 4.0f * (longitude + hourAngle) - eqTime) * 60.0f));
    t.set = midnight + static_cast<int64_t>(std::lround((720.0f - 4.0f * (longitude - hourAngle) - eqTime) * 60.0f));
    return t;
}

}  // namespace mm::civil
