#include "core/Forecast.hpp"

#include <algorithm>
#include <map>

// core/Forecast.cpp -- see the header.

namespace jot::core {

std::size_t ForecastDay::count() const {
    std::size_t n = due.size();
    for (const auto& id : starts)
        if (std::find(due.begin(), due.end(), id) == due.end()) ++n;
    return n;
}

std::int64_t next_day(std::int64_t day) { return day_start(day_end(day) + 1); }

Forecast forecast(const NodeSource& src, const TaskIndex& tasks, std::int64_t now, int ndays) {
    Forecast f;
    std::int64_t d = day_start(now);
    for (int i = 0; i < ndays; ++i) {
        f.days.push_back({d, {}, {}});
        d = next_day(d);
    }
    const std::int64_t horizon = d;   // 00:00 of the first day after the strip
    std::map<std::int64_t, ForecastDay> later;

    auto bucket = [&](std::int64_t when) -> ForecastDay* {
        if (when >= horizon) {
            const std::int64_t k = day_start(when);
            auto [it, fresh] = later.try_emplace(k);
            if (fresh) it->second.day = k;
            return &it->second;
        }
        for (auto& day : f.days)
            if (when >= day.day && when < next_day(day.day)) return &day;
        return nullptr;
    };

    for (const auto& id : tasks.tasks()) {
        const Avail a = availability(src, id, now);
        if (a == Avail::NotTask || a == Avail::Done || a == Avail::Dropped) continue;
        const std::int64_t due = effective_due(src, id);
        if (due != 0 && due >= now)
            if (ForecastDay* b = bucket(due)) b->due.push_back(id);
        const std::int64_t defer = effective_defer(src, id);
        if (defer != 0 && defer > now)
            if (ForecastDay* b = bucket(defer)) b->starts.push_back(id);
    }
    for (auto& [k, day] : later) f.later.push_back(std::move(day));
    return f;
}

}  // namespace jot::core
