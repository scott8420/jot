#include "core/Repeat.hpp"

#include <cctype>
#include <cmath>
#include <ctime>
#include <sstream>
#include <vector>

// core/Repeat.cpp -- see the header. Calendar arithmetic through mktime, so a
// "day" is a calendar day and a DST change does not move 23:59:59 to 22:59:59.

namespace jot::core {

namespace {

std::tm local(std::int64_t when) {
    std::tm tm{};
    const std::time_t t = static_cast<std::time_t>(when);
    localtime_r(&t, &tm);
    return tm;
}

int days_in(int year, int mon0) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mon0 == 1) {
        const int y = year + 1900;
        const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
        return leap ? 29 : 28;
    }
    return d[mon0];
}

const char* unit_word(RepeatUnit u) {
    switch (u) {
        case RepeatUnit::Day:   return "day";
        case RepeatUnit::Week:  return "week";
        case RepeatUnit::Month: return "month";
        case RepeatUnit::Year:  return "year";
    }
    return "day";
}

bool unit_of(std::string w, RepeatUnit& u) {
    if (!w.empty() && w.back() == 's') w.pop_back();
    if (w == "day")   { u = RepeatUnit::Day;   return true; }
    if (w == "week")  { u = RepeatUnit::Week;  return true; }
    if (w == "month") { u = RepeatUnit::Month; return true; }
    if (w == "year")  { u = RepeatUnit::Year;  return true; }
    return false;
}

}  // namespace

std::string repeat_text(const Repeat& r) {
    if (!r.on()) return {};
    if (r.every == 1) return std::string("every ") + unit_word(r.unit);
    return "every " + std::to_string(r.every) + " " + unit_word(r.unit) + "s";
}

bool repeat_parse(const std::string& text, Repeat& out) {
    std::vector<std::string> w;
    {
        std::string low;
        for (char c : text) low += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::istringstream in(low);
        for (std::string s; in >> s;) w.push_back(s);
    }
    Repeat r = out;
    if (w.empty() || (w.size() == 1 && (w[0] == "none" || w[0] == "never"))) {
        r.every = 0;
        out = r;
        return true;
    }
    if (w.size() == 1) {
        struct { const char* word; int n; RepeatUnit u; } k[] = {
            {"daily", 1, RepeatUnit::Day},     {"weekly", 1, RepeatUnit::Week},
            {"fortnightly", 2, RepeatUnit::Week}, {"biweekly", 2, RepeatUnit::Week},
            {"monthly", 1, RepeatUnit::Month}, {"yearly", 1, RepeatUnit::Year},
            {"annually", 1, RepeatUnit::Year},
        };
        for (const auto& e : k)
            if (w[0] == e.word) { r.every = e.n; r.unit = e.u; out = r; return true; }
    }
    std::size_t i = 0;
    if (w[i] == "every") ++i;
    if (i >= w.size()) return false;
    int n = 1;
    if (w[i] == "other") { n = 2; ++i; }
    else if (std::isdigit(static_cast<unsigned char>(w[i][0]))) {
        std::size_t used = 0;
        try { n = std::stoi(w[i], &used); } catch (...) { return false; }
        if (used != w[i].size() || n < 1 || n > 999) return false;
        ++i;
    }
    if (i + 1 != w.size()) return false;
    RepeatUnit u;
    if (!unit_of(w[i], u)) return false;
    r.every = n;
    r.unit  = u;
    out = r;
    return true;
}

std::int64_t repeat_add(std::int64_t when, const Repeat& r, int k) {
    if (!r.on() || when == 0) return when;
    std::tm tm = local(when);
    const int n = r.every * k;
    switch (r.unit) {
        case RepeatUnit::Day:  tm.tm_mday += n; break;
        case RepeatUnit::Week: tm.tm_mday += 7 * n; break;
        case RepeatUnit::Month: {
            const int total = tm.tm_mon + n;
            tm.tm_year += total / 12;
            tm.tm_mon   = total % 12;
            if (tm.tm_mday > days_in(tm.tm_year, tm.tm_mon)) tm.tm_mday = days_in(tm.tm_year, tm.tm_mon);
            break;
        }
        case RepeatUnit::Year:
            tm.tm_year += n;
            if (tm.tm_mday > days_in(tm.tm_year, tm.tm_mon)) tm.tm_mday = days_in(tm.tm_year, tm.tm_mon);
            break;
    }
    tm.tm_isdst = -1;
    return static_cast<std::int64_t>(std::mktime(&tm));
}

void repeat_next(const Repeat& r, std::int64_t now, std::int64_t& due, std::int64_t& defer) {
    if (!r.on()) return;
    const std::int64_t anchor = due != 0 ? due : defer;
    if (anchor == 0) return;                       // nothing dated: nothing to move
    std::int64_t next = 0;
    if (r.from_done) {
        // The anchor's time of day, on the day it was done, plus the interval.
        std::tm a = local(anchor);
        const std::tm t = local(now);
        a.tm_year = t.tm_year; a.tm_mon = t.tm_mon; a.tm_mday = t.tm_mday; a.tm_isdst = -1;
        next = repeat_add(static_cast<std::int64_t>(std::mktime(&a)), r, 1);
    } else {
        // Each candidate from the ORIGINAL anchor, never chained: chaining
        // months would walk Jan 31 -> Feb 28 -> Mar 28 and never get back.
        for (int k = 1; k < 100000; ++k) {
            next = repeat_add(anchor, r, k);
            if (next > now) break;
        }
    }
    // The other date moves by the same number of CALENDAR days, not seconds:
    // across a DST change a start-of-day defer moved by seconds lands at 23:00
    // the day before.
    auto noon = [](std::int64_t w) {
        std::tm t = local(w);
        t.tm_hour = 12; t.tm_min = 0; t.tm_sec = 0; t.tm_isdst = -1;
        return static_cast<std::int64_t>(std::mktime(&t));
    };
    const int days = static_cast<int>(std::lround(double(noon(next) - noon(anchor)) / 86400.0));
    auto shift_days = [days](std::int64_t w) {
        std::tm t = local(w);
        t.tm_mday += days; t.tm_isdst = -1;
        return static_cast<std::int64_t>(std::mktime(&t));
    };
    if (due != 0) { due = next; if (defer != 0) defer = shift_days(defer); }
    else          { defer = next; }
}

}  // namespace jot::core
