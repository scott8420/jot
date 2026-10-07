#include "core/Errands.hpp"
#include "core/Tags.hpp"
#include "core/Tasks.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <map>

namespace jot::core {
namespace {

constexpr std::string_view kAt = "at/";

// Calendar days from a's day to b's (RowLook's rule: on dates, DST-safe).
int days_between(std::int64_t a, std::int64_t b) {
    auto local = [](std::int64_t t) {
        std::time_t tt = static_cast<std::time_t>(t);
        std::tm lt{};
        localtime_r(&tt, &lt);
        lt.tm_hour = 12;
        lt.tm_min = lt.tm_sec = 0;
        lt.tm_isdst = -1;
        return lt;
    };
    std::tm x = local(a), y = local(b);
    const double secs = std::difftime(std::mktime(&y), std::mktime(&x));
    return static_cast<int>(secs >= 0 ? (secs + 43200) / 86400 : (secs - 43200) / 86400);
}

// "at/Town/bank" -> "Town"; "at/hardware-store" -> "Hardware store".
std::string place_name(std::string_view written) {
    std::string_view rest = written.substr(std::min(written.size(), kAt.size()));
    if (const auto slash = rest.find('/'); slash != std::string_view::npos) rest = rest.substr(0, slash);
    std::string out(rest);
    for (auto& c : out) if (c == '-' || c == '_') c = ' ';
    if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

std::string plural(int n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

std::string minutes_part(int minutes, int unsized) {
    if (minutes <= 0) return {};
    std::string m = "~" + format_estimate(minutes);
    if (unsized > 0) m += " (" + std::to_string(unsized) + " not sized)";
    return m;
}

}  // namespace

bool is_place_key(std::string_view key) {
    return key.size() > kAt.size() && key.substr(0, kAt.size()) == kAt;
}

std::vector<ErrandRun> errand_runs(const NodeSource& src, std::int64_t now) {
    // The names as written, from the same list the Tags tab shows.
    std::map<std::string, std::string> spelled;
    for (const auto& t : tag_list(src, now))
        if (is_place_key(t.key)) spelled.emplace(t.key, t.name);

    std::map<std::string, ErrandRun> runs;
    std::map<std::string, std::vector<std::pair<std::int64_t, Errand>>> doable;

    std::vector<NodeId> stack;
    const auto roots = src.children({});
    for (auto it = roots.rbegin(); it != roots.rend(); ++it) stack.push_back(*it);
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        const auto kids = src.children(id);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back(*it);
        const Node* n = src.find(id);
        if (!n || !n->task.is_task) continue;
        const Avail a = availability(src, id, now);
        if (a == Avail::Done || a == Avail::Dropped || a == Avail::NotTask) continue;

        std::vector<std::string> seen;   // one place counted once per node
        for (const auto& key : node_tag_keys(*n)) {
            if (!is_place_key(key)) continue;
            const auto rest  = std::string_view(key).substr(kAt.size());
            const auto slash = rest.find('/');
            const std::string place = std::string(kAt) + std::string(rest.substr(0, slash));
            const std::string sub   = slash == std::string_view::npos ? std::string{}
                                                                      : std::string(rest.substr(slash + 1));
            if (std::find(seen.begin(), seen.end(), place) != seen.end()) continue;
            seen.push_back(place);

            ErrandRun& r = runs[place];
            if (r.key.empty()) {
                r.key = place;
                const auto sp = spelled.find(place);
                r.name = place_name(sp != spelled.end() ? sp->second : place);
            }
            if (a != Avail::Available) { ++r.later; continue; }
            const std::int64_t due = effective_due(src, id);
            if (n->task.estimate > 0) r.minutes += n->task.estimate;
            else ++r.unsized;
            if (due != 0 && due < now) ++r.late;
            else if (due != 0 && days_between(now, due) <= 6) ++r.week;
            if (due != 0 && (r.first_due == 0 || due < r.first_due)) r.first_due = due;
            doable[place].push_back({due, Errand{id, sub}});
        }
    }

    std::vector<ErrandRun> out;
    for (auto& [key, r] : runs) {
        auto& list = doable[key];
        // Soonest due first; undated after; tree order within (stable).
        std::stable_sort(list.begin(), list.end(), [](const auto& x, const auto& y) {
            if ((x.first == 0) != (y.first == 0)) return x.first != 0;
            return x.first < y.first;
        });
        for (auto& [due, e] : list) r.errands.push_back(std::move(e));
        out.push_back(std::move(r));
    }
    // The other places each errand is on -- its card says "also Town".
    for (auto& r : out)
        for (auto& e : r.errands)
            for (const auto& o : out) {
                if (&o == &r) continue;
                for (const auto& oe : o.errands)
                    if (oe.id == e.id) {
                        e.also += (e.also.empty() ? "" : ", ") + o.name;
                        break;
                    }
            }
    std::stable_sort(out.begin(), out.end(), [](const ErrandRun& x, const ErrandRun& y) {
        const bool xe = x.errands.empty(), ye = y.errands.empty();
        if (xe != ye) return ye;                          // something doable first
        if ((x.first_due == 0) != (y.first_due == 0)) return x.first_due != 0;
        if (x.first_due != y.first_due) return x.first_due < y.first_due;
        return x.name < y.name;
    });
    return out;
}

std::string run_summary(const ErrandRun& r) {
    std::string out;
    auto add = [&out](const std::string& part) {
        if (part.empty()) return;
        if (!out.empty()) out += "  ·  ";
        out += part;
    };
    const int n = static_cast<int>(r.errands.size());
    if (n == 0) add("Nothing to do there yet");
    else add(plural(n, "errand", "errands"));
    add(minutes_part(r.minutes, r.unsized));
    if (r.late > 0) add(std::to_string(r.late) + " late");
    if (r.week > 0) add(std::to_string(r.week) + " due this week");
    if (r.later > 0) add(std::to_string(r.later) + " later");
    return out;
}

std::string runs_summary(const NodeSource& src, const std::vector<ErrandRun>& runs) {
    std::vector<NodeId> seen;
    int minutes = 0;
    for (const auto& r : runs)
        for (const auto& e : r.errands) {
            if (std::find(seen.begin(), seen.end(), e.id) != seen.end()) continue;
            seen.push_back(e.id);
            if (const Node* n = src.find(e.id)) minutes += n->task.estimate;
        }
    const int errands = static_cast<int>(seen.size());
    if (runs.empty()) return "No places yet";
    std::string out = plural(static_cast<int>(runs.size()), "place", "places") + "  ·  " +
                      plural(errands, "errand", "errands");
    if (minutes > 0) out += "  ·  ~" + format_estimate(minutes);
    return out;
}

}  // namespace jot::core
