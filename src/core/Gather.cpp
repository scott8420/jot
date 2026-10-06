#include "core/Gather.hpp"

#include <algorithm>
#include <array>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace jot::core {
namespace {

std::tm local(std::int64_t when) {
    std::tm tm{};
    const std::time_t t = static_cast<std::time_t>(when);
    localtime_r(&t, &tm);
    return tm;
}

std::string fmt(std::int64_t when, const char* f) {
    const std::tm tm = local(when);
    char buf[64];
    std::strftime(buf, sizeof buf, f, &tm);
    return buf;
}

// "6 Oct 2026" -- %e pads with a space, so trim it.
std::string day_text(std::int64_t when) {
    std::string s = fmt(when, "%e %b %Y");
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    return s;
}

std::string pad_num(int n, int width) {
    std::string s = std::to_string(n);
    while (static_cast<int>(s.size()) < width) s.insert(s.begin(), '0');
    return s;
}

bool read_all(const std::string& path, std::string& out, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { err = "could not read " + path; return false; }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool write_all(const fs::path& path, const std::string& bytes, std::string& err) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) { err = "could not write " + path.string(); return false; }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) { err = "could not finish writing " + path.string(); return false; }
    return true;
}

void put16(std::string& o, std::uint32_t v) {
    o += static_cast<char>(v & 0xff);
    o += static_cast<char>((v >> 8) & 0xff);
}
void put32(std::string& o, std::uint32_t v) {
    put16(o, v & 0xffff);
    put16(o, v >> 16);
}

}  // namespace

std::string safe_file_name(const std::string& in) {
    std::string out;
    bool space = false;
    for (unsigned char c : in) {
        const bool bad = c < 0x20 || c == 0x7f || c == '/' || c == '\\' || c == ':' || c == '*' ||
                         c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
        if (bad || c == ' ' || c == '\t') { space = true; continue; }
        if (space && !out.empty()) out += ' ';
        space = false;
        out += static_cast<char>(c);
    }
    // No leading dot (a hidden file), no trailing dot or space (Windows drops them).
    while (!out.empty() && (out.front() == '.' || out.front() == ' ')) out.erase(out.begin());
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    if (out.size() > 80) {
        std::size_t cut = 80;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;   // not mid-character
        out.resize(cut);
        while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    }
    return out.empty() ? std::string("untitled") : out;
}

GatherPlan gather_plan(const std::string& title, const PacketState& st, const AttachStore& store,
                       std::int64_t when) {
    GatherPlan p;
    const std::string date = fmt(when, "%Y-%m-%d");
    p.name = safe_file_name(title) + " - " + date;

    const int width = std::max<int>(2, static_cast<int>(std::to_string(st.items.size()).size()));
    std::string c = (title.empty() ? std::string("Untitled") : title) + "\n";
    c += "Gathered by jot on " + fmt(when, "%a ") + day_text(when) + ".\n";
    c += std::to_string(st.total) + (st.total == 1 ? " item" : " items");
    int files = 0;
    for (const auto& it : st.items) files += static_cast<int>(it.files.size());
    c += ", " + std::to_string(files) + (files == 1 ? " file.\n\n" : " files.\n\n");

    for (std::size_t i = 0; i < st.items.size(); ++i) {
        const PacketItem& it = st.items[i];
        const std::string num = pad_num(static_cast<int>(i) + 1, width);
        c += num + "  " + it.label + "\n";
        if (!it.in()) {
            p.problems.push_back(it.label + ": not in yet");
            c += "      (missing)\n";
            continue;
        }
        if (it.files.empty()) {
            ++p.paper;
            c += "      paper copy -- not in here\n";
            continue;
        }
        for (std::size_t k = 0; k < it.files.size(); ++k) {
            const std::string& key = it.files[k];
            const std::string from = enclosure_path(store, key);
            const std::string shown = from.empty() ? key : fs::path(from).filename().string();
            std::error_code ec;
            if (from.empty() || !fs::is_regular_file(from, ec)) {
                p.problems.push_back(it.label + ": " + shown + " is not there");
                c += "      " + shown + " (not there)\n";
                continue;
            }
            std::string stem = num + " " + safe_file_name(it.label);
            if (it.files.size() > 1) stem += " - " + std::to_string(k + 1);
            const std::string ext = fs::path(from).extension().string();
            GatherFile g;
            g.from = from;
            g.as   = stem + ext;
            g.item = static_cast<int>(i);
            c += "      " + g.as;
            if (fs::path(from).stem().string() != stem) c += "   (was " + shown + ")";
            c += "\n";
            p.files.push_back(std::move(g));
        }
    }
    p.contents = std::move(c);
    return p;
}

bool gather_to_folder(const GatherPlan& plan, const std::string& parent, std::string& made,
                      std::string& err) {
    err.clear();
    made.clear();
    if (!plan.problems.empty()) { err = plan.problems.front(); return false; }
    std::error_code ec;
    if (parent.empty() || !fs::is_directory(parent, ec)) { err = "not a folder: " + parent; return false; }

    fs::path dest = fs::path(parent) / plan.name;
    for (int n = 2; fs::exists(dest, ec); ++n) {
        if (n > 999) { err = "too many folders called " + plan.name; return false; }
        dest = fs::path(parent) / (plan.name + " (" + std::to_string(n) + ")");
    }
    const fs::path part = fs::path(parent) / ("." + dest.filename().string() + ".part");
    fs::remove_all(part, ec);
    if (!fs::create_directory(part, ec) || ec) {
        err = "could not make a folder in " + parent + ": " + ec.message();
        return false;
    }
    const auto fail = [&](const std::string& why) {
        err = why;
        std::error_code ig;
        fs::remove_all(part, ig);
        return false;
    };
    if (!write_all(part / kContentsName, plan.contents, err)) return fail(err);
    for (const auto& f : plan.files) {
        fs::copy_file(f.from, part / f.as, fs::copy_options::none, ec);
        if (ec) return fail("could not copy " + f.from + ": " + ec.message());
    }
    fs::rename(part, dest, ec);
    if (ec) return fail("could not name the folder " + dest.string() + ": " + ec.message());
    made = dest.string();
    return true;
}

bool gather_to_zip(const GatherPlan& plan, const std::string& zip_path, std::string& err) {
    err.clear();
    if (!plan.problems.empty()) { err = plan.problems.front(); return false; }
    if (zip_path.empty()) { err = "no file chosen"; return false; }
    std::vector<ZipEntry> es;
    es.push_back({plan.name + "/" + kContentsName, plan.contents});
    for (const auto& f : plan.files) {
        std::error_code ec;
        const auto size = fs::file_size(f.from, ec);
        if (ec) { err = "could not read " + f.from + ": " + ec.message(); return false; }
        if (size >= 0xFFFFFFFFull) { err = f.from + " is too big for a zip jot can make"; return false; }
        ZipEntry e;
        e.name = plan.name + "/" + f.as;
        if (!read_all(f.from, e.bytes, err)) return false;
        es.push_back(std::move(e));
    }
    const std::string bytes = zip_store(es, static_cast<std::int64_t>(std::time(nullptr)));
    const fs::path tmp = zip_path + ".jot-tmp";
    if (!write_all(tmp, bytes, err)) {
        std::error_code ig;
        fs::remove(tmp, ig);
        return false;
    }
    std::error_code ec;
    fs::rename(tmp, zip_path, ec);
    if (ec) {
        err = "could not replace " + zip_path + ": " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

std::uint32_t crc32(const std::string& bytes) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : bytes) c = table[(c ^ b) & 0xff] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::string zip_store(const std::vector<ZipEntry>& entries, std::int64_t when) {
    const std::tm tm = local(when);
    const std::uint32_t dos_time = (static_cast<std::uint32_t>(tm.tm_hour) << 11) |
                                   (static_cast<std::uint32_t>(tm.tm_min) << 5) |
                                   (static_cast<std::uint32_t>(tm.tm_sec) / 2);
    const int year = tm.tm_year + 1900 < 1980 ? 1980 : tm.tm_year + 1900;
    const std::uint32_t dos_date = (static_cast<std::uint32_t>(year - 1980) << 9) |
                                   (static_cast<std::uint32_t>(tm.tm_mon + 1) << 5) |
                                   static_cast<std::uint32_t>(tm.tm_mday);
    const std::uint32_t utf8 = 1u << 11;

    std::string out, central;
    for (const auto& e : entries) {
        const std::uint32_t crc  = crc32(e.bytes);
        const std::uint32_t size = static_cast<std::uint32_t>(e.bytes.size());
        const std::uint32_t at   = static_cast<std::uint32_t>(out.size());
        // Local file header.
        put32(out, 0x04034b50);
        put16(out, 20);          // version needed: 2.0
        put16(out, utf8);
        put16(out, 0);           // stored
        put16(out, dos_time);
        put16(out, dos_date);
        put32(out, crc);
        put32(out, size);
        put32(out, size);
        put16(out, static_cast<std::uint32_t>(e.name.size()));
        put16(out, 0);
        out += e.name;
        out += e.bytes;
        // Its central directory record.
        put32(central, 0x02014b50);
        put16(central, (3u << 8) | 20);   // made by: Unix, 2.0
        put16(central, 20);
        put16(central, utf8);
        put16(central, 0);
        put16(central, dos_time);
        put16(central, dos_date);
        put32(central, crc);
        put32(central, size);
        put32(central, size);
        put16(central, static_cast<std::uint32_t>(e.name.size()));
        put16(central, 0);       // extra
        put16(central, 0);       // comment
        put16(central, 0);       // disk
        put16(central, 0);       // internal attributes
        put32(central, 0100644u << 16);   // external: a plain rw-r--r-- file
        put32(central, at);
        central += e.name;
    }
    const std::uint32_t cd_at = static_cast<std::uint32_t>(out.size());
    out += central;
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, static_cast<std::uint32_t>(entries.size()));
    put16(out, static_cast<std::uint32_t>(entries.size()));
    put32(out, static_cast<std::uint32_t>(central.size()));
    put32(out, cd_at);
    put16(out, 0);
    return out;
}

std::string sent_line(std::int64_t when, const std::string& to) {
    if (when == 0) return {};
    std::string s = "Sent " + day_text(when);
    if (!to.empty()) s += "  ·  " + fs::path(to).filename().string();
    return s;
}

}  // namespace jot::core
