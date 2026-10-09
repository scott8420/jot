#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core/Backup -- s075. Daily snapshots of a jots folder, the rsync way.
//
// (Scott, Oct 8: "a backup process that can be automatic"; "rsync is the linux
// way"; "a pref for where the backups go and the default is in the standard
// dot app folder".)
//
// On disk:
//
//   <root>/                         ~/.local/share/jot/backups by default
//     home.jots/                    one slot per jots folder (its folder name)
//       .source                     the jots folder's path -- two folders with
//                                   the same name get "home.jots-1a2b3c"
//       last-ok                     epoch seconds of the last finished backup
//       2026-10-08/                 a whole copy of the folder, as it was that day
//       2026-10-09/                 rsync --link-dest=../2026-10-08: unchanged
//                                   files are HARD LINKS, so a day costs only
//                                   what changed, and each is a folder you can
//                                   open in Files
//
// Today's snapshot is refreshed while you work (at most hourly) and on quit;
// a new day starts a new one. Kept: the 7 newest days, plus the newest of
// each of the 4 weeks before them. Pruning deletes whole day folders -- no
// day depends on another, which is the point of hard links over tar chains.
//
// GTK-free: the decisions are here under the selftest; Shell spawns rsync.
// ─────────────────────────────────────────────────────────────────────────────
#include <ctime>
#include <string>
#include <vector>

namespace jot::core {

// "" pref = <data_dir>/jot/backups.
std::string backup_default_root(const std::string& data_dir);
std::string backup_root(const std::string& data_dir, const std::string& pref_dir);

// The slot's folder name for a jots folder: its own folder name, or that plus
// "-" and six hex of a hash of the full path when `taken_by` (the .source of
// the plain-named slot, "" if none) is some OTHER folder.
std::string backup_slot_name(const std::string& jots_dir, const std::string& taken_by);

// Is `day` a snapshot folder name, YYYY-MM-DD?
bool is_backup_day(const std::string& day);
// Local calendar day of `t` as YYYY-MM-DD.
std::string backup_day_of(std::time_t t);

// The day folders in a slot, oldest first. Ignores anything else.
std::vector<std::string> backup_days(const std::string& slot_dir);

// Which days to DELETE so that the 7 newest stay, plus the newest day of each
// of the 4 ISO weeks older than those. Input in any order.
std::vector<std::string> backup_prune(std::vector<std::string> days,
                                      int keep_daily = 7, int keep_weekly = 4);

// The newest day strictly before `today` -- the --link-dest -- or "".
std::string backup_link_day(const std::vector<std::string>& days, const std::string& today);

// Is `child` the same as, or inside, `parent`? (A backup root inside the jots
// folder would back itself up into itself.)
bool path_inside(const std::string& child, const std::string& parent);

// The newest modification time of anything in a folder (0 if none / unreadable).
std::time_t newest_change(const std::string& dir);

// last-ok: read (0 = never) / the line a finished backup writes.
std::time_t read_last_ok(const std::string& slot_dir);

// "Last backup: today 09:02" / "yesterday 21:10" / "Thu 1 Oct 09:02" /
// "No backup yet".
std::string backup_when_text(std::time_t last_ok, std::time_t now);

// What a backup should do right now, decided from the facts.
enum class BackupNeed { None, Run };
// Changed since the last finished backup (or never backed up), and not
// already run within `min_gap` seconds unless `force`.
BackupNeed backup_need(std::time_t last_ok, std::time_t newest, std::time_t now,
                       long min_gap, bool force);

// The sh -c script and its argv: rsync, then stamp last-ok. rsync's
// complaints go to <last-ok>.err, removed again on success. Kept as data so
// the selftest can read it; Shell spawns it.
std::vector<std::string> backup_command(const std::string& rsync, const std::string& src_dir,
                                        const std::string& dest_dir, const std::string& link_dir,
                                        const std::string& last_ok_file);

// "home (restored 8 Oct 2026).jots" for a jots folder named "home.jots".
std::string backup_restore_name(const std::string& jots_dir, const std::string& day);
// "Thu 8 Oct 2026" for "2026-10-08".
std::string backup_day_label(const std::string& day);

}  // namespace jot::core
