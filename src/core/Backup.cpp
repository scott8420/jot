#include "core/Backup.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>

#include <sys/stat.h>

namespace fs = std::filesystem;

namespace jot::core {

namespace {

std::string strip_slash(std::string p) {
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

std::string base_name(const std::string& p) {
    const std::string s = strip_slash(p);
    const auto k = s.find_last_of('/');
    return k == std::string::npos ? s : s.substr(k + 1);
}

// Noon, so a DST shift never moves the day.
bool day_tm(const std::string& day, std::tm& out) {
    if (!is_backup_day(day)) return false;
    std::tm t{};
    t.tm_year = std::stoi(day.substr(0, 4)) - 1900;
    t.tm_mon  = std::stoi(day.substr(5, 2)) - 1;
    t.tm_mday = std::stoi(day.substr(8, 2));
    t.tm_hour = 12;
    t.tm_isdst = -1;
    if (std::mktime(&t) == -1) return false;
    out = t;
    return true;
}

std::string iso_week(const std::string& day) {
    std::tm t{};
    if (!day_tm(day, t)) return day;
    char buf[16];
    std::strftime(buf, sizeof buf, "%G-%V", &t);
    return buf;
}

}  // namespace

std::string backup_default_root(const std::string& data_dir) {
    return (fs::path(data_dir) / "jot" / "backups").string();
}

std::string backup_root(const std::string& data_dir, const std::string& pref_dir) {
    return pref_dir.empty() ? backup_default_root(data_dir) : strip_slash(pref_dir);
}

std::string backup_slot_name(const std::string& jots_dir, const std::string& taken_by) {
    const std::string name = base_name(jots_dir);
    if (taken_by.empty() || strip_slash(taken_by) == strip_slash(jots_dir)) return name;
    std::uint32_t h = 2166136261u;                       // FNV-1a
    for (unsigned char c : strip_slash(jots_dir)) { h ^= c; h *= 16777619u; }
    char hex[8];
    std::snprintf(hex, sizeof hex, "%06x", static_cast<unsigned>(h & 0xffffffu));
    return name + "-" + hex;
}

bool is_backup_day(const std::string& d) {
    if (d.size() != 10 || d[4] != '-' || d[7] != '-') return false;
    for (std::size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u})
        if (d[i] < '0' || d[i] > '9') return false;
    const int m = std::stoi(d.substr(5, 2)), dd = std::stoi(d.substr(8, 2));
    return m >= 1 && m <= 12 && dd >= 1 && dd <= 31;
}

std::string backup_day_of(std::time_t t) {
    std::tm lt{};
    localtime_r(&t, &lt);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &lt);
    return buf;
}

std::vector<std::string> backup_days(const std::string& slot_dir) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::is_directory(slot_dir, ec)) return out;
    for (const auto& e : fs::directory_iterator(slot_dir, ec)) {
        const std::string n = e.path().filename().string();
        if (is_backup_day(n) && e.is_directory(ec)) out.push_back(n);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> backup_prune(std::vector<std::string> days, int keep_daily,
                                      int keep_weekly) {
    std::sort(days.begin(), days.end(), std::greater<>());     // newest first
    days.erase(std::unique(days.begin(), days.end()), days.end());
    std::vector<std::string> drop;
    std::set<std::string> weeks;
    for (std::size_t i = 0; i < days.size(); ++i) {
        if (static_cast<int>(i) < keep_daily) continue;
        const std::string w = iso_week(days[i]);
        if (weeks.count(w) == 0 && static_cast<int>(weeks.size()) < keep_weekly) {
            weeks.insert(w);                                   // the newest of that week
            continue;
        }
        drop.push_back(days[i]);
    }
    std::sort(drop.begin(), drop.end());
    return drop;
}

std::string backup_link_day(const std::vector<std::string>& days, const std::string& today) {
    std::string best;
    for (const auto& d : days)
        if (d < today && d > best) best = d;
    return best;
}

bool path_inside(const std::string& child, const std::string& parent) {
    std::error_code ec;
    const fs::path c = fs::weakly_canonical(fs::path(child), ec);
    const fs::path p = fs::weakly_canonical(fs::path(parent), ec);
    auto ci = c.begin();
    for (auto pi = p.begin(); pi != p.end(); ++pi, ++ci) {
        if (pi->empty()) continue;                         // a trailing slash
        if (ci == c.end() || *ci != *pi) return false;
    }
    return true;
}

std::time_t newest_change(const std::string& dir) {
    std::error_code ec;
    std::time_t best = 0;
    auto consider = [&](const fs::path& p) {
        struct stat st{};
        if (::stat(p.c_str(), &st) == 0 && st.st_mtime > best) best = st.st_mtime;
    };
    if (!fs::is_directory(dir, ec)) return 0;
    consider(dir);
    for (auto it = fs::recursive_directory_iterator(
             dir, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        consider(it->path());
    }
    return best;
}

std::time_t read_last_ok(const std::string& slot_dir) {
    std::ifstream in(fs::path(slot_dir) / "last-ok");
    long long v = 0;
    if (in >> v && v > 0) return static_cast<std::time_t>(v);
    return 0;
}

std::string backup_when_text(std::time_t last_ok, std::time_t now) {
    if (last_ok <= 0) return "No backup yet";
    std::tm lt{};
    localtime_r(&last_ok, &lt);
    char hm[8];
    std::strftime(hm, sizeof hm, "%H:%M", &lt);
    const std::string d = backup_day_of(last_ok), t = backup_day_of(now);
    if (d == t) return std::string("Last backup: today ") + hm;
    if (d == backup_day_of(now - 86400)) return std::string("Last backup: yesterday ") + hm;
    char buf[32];
    std::strftime(buf, sizeof buf, "%a %-d %b", &lt);
    return std::string("Last backup: ") + buf + " " + hm;
}

BackupNeed backup_need(std::time_t last_ok, std::time_t newest, std::time_t now, long min_gap,
                       bool force) {
    if (force) return BackupNeed::Run;
    if (last_ok <= 0) return BackupNeed::Run;
    if (newest <= last_ok) return BackupNeed::None;                 // nothing changed
    if (backup_day_of(last_ok) != backup_day_of(now)) return BackupNeed::Run;   // a new day
    return (now - last_ok >= min_gap) ? BackupNeed::Run : BackupNeed::None;
}

std::vector<std::string> backup_command(const std::string& rsync, const std::string& src_dir,
                                        const std::string& dest_dir, const std::string& link_dir,
                                        const std::string& last_ok_file) {
    // $1 rsync  $2 source  $3 today's folder  $4 yesterday's ("" = none)  $5 last-ok
    // The trailing slashes say "the CONTENTS of source into dest". The stamp
    // is written only after rsync succeeds, via a rename, so a half-written
    // last-ok never reads as a finished backup.
    static const char* script =
        "set -e\n"
        "exec 2>\"$5.err\"\n"
        "if [ -n \"$4\" ]; then\n"
        "  \"$1\" -a --delete --link-dest=\"$4\" \"$2\"/ \"$3\"/\n"
        "else\n"
        "  \"$1\" -a --delete \"$2\"/ \"$3\"/\n"
        "fi\n"
        "date +%s > \"$5.tmp\" && mv -f \"$5.tmp\" \"$5\"\n"
        "rm -f \"$5.err\"\n";
    return {"/bin/sh", "-c", script, "jot-backup", rsync, strip_slash(src_dir),
            strip_slash(dest_dir), link_dir.empty() ? "" : strip_slash(link_dir), last_ok_file};
}

std::string backup_day_label(const std::string& day) {
    std::tm t{};
    if (!day_tm(day, t)) return day;
    char buf[32];
    std::strftime(buf, sizeof buf, "%a %-d %b %Y", &t);
    return buf;
}

std::string backup_restore_name(const std::string& jots_dir, const std::string& day) {
    std::string stem = base_name(jots_dir);
    if (stem.size() > 5 && stem.compare(stem.size() - 5, 5, ".jots") == 0)
        stem.resize(stem.size() - 5);
    std::string label = backup_day_label(day);
    const auto sp = label.find(' ');                      // drop the weekday
    if (sp != std::string::npos) label = label.substr(sp + 1);
    return stem + " (restored " + label + ").jots";
}

}  // namespace jot::core
