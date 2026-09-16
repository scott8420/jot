#include "core/Pending.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace jot::core {

namespace {

// The delimiter, written and matched in ONE place so the encoder and the parser
// cannot come to disagree about what a front-matter fence looks like.
constexpr const char* kFence = "---";

// Strip the single trailing newline the encoder adds, and nothing else. A
// capture that genuinely ended in a blank line keeps it, because the whole
// premise of core::capture is that nothing the user typed is thrown away.
void drop_one_trailing_newline(std::string& s) {
    if (!s.empty() && s.back() == '\n') s.pop_back();
    if (!s.empty() && s.back() == '\r') s.pop_back();
}

}  // namespace

std::string pending_dir(const std::string& data_dir) {
    return (fs::path(data_dir) / "jot" / "pending").string();
}

std::string encode_pending(const std::string& text, std::int64_t when) {
    std::ostringstream o;
    o << kFence << "\n"
      << "jot: capture\n"
      << "captured: " << when << "\n"
      << kFence << "\n"
      << text << "\n";
    return o.str();
}

// TOLERANT ON PURPOSE. Anything that is not our front matter is read as a bare
// thought with no date: a file dropped in here by a shell script is a thing
// somebody wrote down, and a capture spool that ignores what it does not
// recognise is a capture spool that loses notes.
bool decode_pending(const std::string& raw, Pending& out) {
    out.text.clear();
    out.captured = 0;
    if (raw.empty()) return false;

    const std::string open = std::string(kFence) + "\n";
    if (raw.rfind(open, 0) != 0) {                 // no front matter -- take it whole
        out.text = raw;
        drop_one_trailing_newline(out.text);
        return !out.text.empty();
    }

    // Find the closing fence: a line that is exactly "---".
    std::size_t pos = open.size();
    std::size_t close = std::string::npos;
    while (pos <= raw.size()) {
        std::size_t eol = raw.find('\n', pos);
        std::string line = raw.substr(pos, (eol == std::string::npos ? raw.size() : eol) - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == kFence) { close = (eol == std::string::npos ? raw.size() : eol + 1); break; }

        if (line.rfind("captured:", 0) == 0) {
            const std::string v = line.substr(9);
            try { out.captured = std::stoll(v); } catch (const std::exception&) { out.captured = 0; }
        }
        if (eol == std::string::npos) break;
        pos = eol + 1;
    }
    if (close == std::string::npos) {              // an unterminated header is not a header
        out.text = raw;
        drop_one_trailing_newline(out.text);
        return !out.text.empty();
    }

    out.text = raw.substr(close);
    drop_one_trailing_newline(out.text);
    return !out.text.empty();
}

std::string pending_name(std::int64_t when, const std::string& uniq) {
    std::string clean;
    for (char c : uniq)                            // a filename, not a free-text field
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') clean += c;
    if (clean.empty()) clean = "x";
    return std::to_string(when) + "-" + clean + ".md";
}

bool write_pending(const std::string& dir, const std::string& text,
                   std::int64_t when, const std::string& uniq, std::string* wrote) {
    if (text.empty()) return false;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!fs::is_directory(dir, ec)) return false;

    // Never clobber. Two captures in the same second from the same process is
    // not a race we get to lose, and the loop is cheaper than reasoning about
    // whether it can happen.
    fs::path target = fs::path(dir) / pending_name(when, uniq);
    for (int n = 1; fs::exists(target, ec) && n < 1000; ++n)
        target = fs::path(dir) / pending_name(when, uniq + "-" + std::to_string(n));

    // ── TEMP + RENAME ──────────────────────────────────────────────────────
    // rename(2) within a directory is atomic, so a drain running at this
    // instant sees the file complete or not at all. Writing in place would give
    // it a window in which to read half a thought and delete the rest.
    fs::path tmp = target;
    tmp += ".part";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << encode_pending(text, when);
        if (!f) { fs::remove(tmp, ec); return false; }
    }
    fs::rename(tmp, target, ec);
    if (ec) { fs::remove(tmp, ec); return false; }
    if (wrote) *wrote = target.string();
    return true;
}

std::vector<Pending> read_pending(const std::string& dir) {
    std::vector<Pending> out;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return out;    // never captured cold -- not an error

    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;
        const fs::path p = e.path();
        if (p.extension() == ".part") continue;    // a write in flight; leave it alone

        std::ifstream f(p, std::ios::binary);
        if (!f) continue;
        std::ostringstream buf;
        buf << f.rdbuf();

        Pending item;
        if (!decode_pending(buf.str(), item)) continue;   // empty file: nothing was said
        item.path = p.string();
        out.push_back(std::move(item));
    }

    // Oldest first, so the notes land in the order they were thought of. The
    // path is the tiebreak, because two captures in one second still happened
    // in an order and an arbitrary one is worse than a stable one.
    std::sort(out.begin(), out.end(), [](const Pending& a, const Pending& b) {
        if (a.captured != b.captured) return a.captured < b.captured;
        return a.path < b.path;
    });
    return out;
}

bool remove_pending(const Pending& p) {
    if (p.path.empty()) return false;
    std::error_code ec;
    return fs::remove(p.path, ec) && !ec;
}

}  // namespace jot::core
