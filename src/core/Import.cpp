#include "core/Import.hpp"
#include "core/Enclosures.hpp"
#include "core/Markdown.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace jot::core {
namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Strict enough to keep a GtkTextBuffer happy: well-formed sequences, no
// overlongs, no surrogates, nothing past U+10FFFF.
bool valid_utf8(const std::string& s) {
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const std::size_t n = s.size();
    for (std::size_t i = 0; i < n;) {
        const unsigned c = p[i];
        if (c < 0x80) { ++i; continue; }
        int len = 0;
        unsigned cp = 0;
        if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
        else return false;
        if (i + static_cast<std::size_t>(len) > n) return false;
        for (int k = 1; k < len; ++k) {
            if ((p[i + static_cast<std::size_t>(k)] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (p[i + static_cast<std::size_t>(k)] & 0x3F);
        }
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) ||
            cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        i += static_cast<std::size_t>(len);
    }
    return true;
}

// `%20` -> ' '. A malformed escape leaves the string as it was.
std::string percent_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out.push_back(static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

// A scheme is letters then ':' before any '/': `https:`, `jot:`, `data:`.
bool has_scheme(const std::string& t) {
    for (std::size_t i = 0; i < t.size(); ++i) {
        const char c = t[i];
        if (c == ':') return i > 0;
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.'))
            return false;
    }
    return false;
}

}  // namespace

bool is_markdown_filename(const std::string& name) {
    const std::string ext = lower(fs::path(name).extension().string());
    return ext == ".md" || ext == ".markdown" || ext == ".txt";
}

std::string import_title(const std::string& body, const std::string& path) {
    const Scan sc = scan(body);
    for (const Line& ln : sc.lines) {
        if (ln.block != Block::Heading || ln.level != 1) continue;
        std::string t = body.substr(static_cast<std::size_t>(ln.begin),
                                    static_cast<std::size_t>(ln.end - ln.begin));
        std::size_t i = 0;
        while (i < t.size() && (t[i] == ' ' || t[i] == '\t')) ++i;
        while (i < t.size() && t[i] == '#') ++i;
        t = t.substr(i);
        // Closing hashes (`# Title #`) are decoration.
        while (!t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == '#' ||
                              t.back() == '\r'))
            t.pop_back();
        std::size_t s = 0;
        while (s < t.size() && (t[s] == ' ' || t[s] == '\t')) ++s;
        t = t.substr(s);
        if (!t.empty()) return t;
    }
    return fs::path(path).stem().string();
}

std::string localize_images(const std::string& body, const std::string& base_dir, int* n) {
    if (n) *n = 0;
    const Scan sc = scan(body);
    std::string out = body;
    // From the END, so an earlier link's byte offsets survive a later rewrite.
    for (auto it = sc.links.rbegin(); it != sc.links.rend(); ++it) {
        const Link& lk = *it;
        if (!lk.image || lk.target.empty() || has_scheme(lk.target)) continue;
        std::error_code ec;
        fs::path p = lk.target[0] == '/' ? fs::path(lk.target) : fs::path(base_dir) / lk.target;
        if (!fs::is_regular_file(p, ec)) {
            const std::string d = percent_decode(lk.target);
            p = d[0] == '/' ? fs::path(d) : fs::path(base_dir) / d;
            if (!fs::is_regular_file(p, ec)) continue;
        }
        const std::string uri = file_uri(p.lexically_normal().string());
        if (uri.empty()) continue;
        // The target sits between `](` and the closing `)` at lk.end - 1.
        const int t_end   = lk.end - 1;
        const int t_begin = t_end - static_cast<int>(lk.target.size());
        out.replace(static_cast<std::size_t>(t_begin), lk.target.size(), uri);
        if (n) ++*n;
    }
    return out;
}

bool import_markdown(const std::string& path, ImportedNote& out, std::string& err) {
    err.clear();
    std::error_code ec;
    if (fs::is_directory(path, ec)) { err = "a folder cannot be imported"; return false; }
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "cannot read " + path; return false; }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string body = ss.str();

    if (body.size() >= 3 && body.compare(0, 3, "\xEF\xBB\xBF") == 0) body.erase(0, 3);
    body.erase(std::remove(body.begin(), body.end(), '\r'), body.end());
    if (!valid_utf8(body)) { err = "not UTF-8 text"; return false; }
    if (body.find('\0') != std::string::npos) { err = "not a text file"; return false; }

    out.title = import_title(body, path);
    out.body  = localize_images(body, fs::path(path).parent_path().string(), &out.pictures);
    return true;
}

namespace {

bool less_ci(const std::string& a, const std::string& b) {
    return lower(a) < lower(b);
}

// Returns true if anything markdown was found under `dir`.
bool plan_into(const fs::path& dir, ImportItem& item, int& budget, bool& truncated) {
    std::vector<fs::path> dirs, files;
    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        const fs::path p = it->path();
        const std::string name = p.filename().string();
        if (name.empty() || name[0] == '.') continue;   // .obsidian, .git, .trash
        std::error_code e2;
        if (it->is_symlink(e2) && it->is_directory(e2)) continue;   // never follow a folder link
        if (it->is_directory(e2)) dirs.push_back(p);
        else if (it->is_regular_file(e2) && is_markdown_filename(name)) files.push_back(p);
    }
    auto by_name = [](const fs::path& a, const fs::path& b) {
        return less_ci(a.filename().string(), b.filename().string());
    };
    std::sort(dirs.begin(), dirs.end(), by_name);
    std::sort(files.begin(), files.end(), by_name);

    for (const auto& d : dirs) {
        if (budget <= 0) { truncated = true; break; }
        ImportItem sub;
        sub.title  = d.filename().string();
        sub.path   = d.string();
        sub.folder = true;
        if (plan_into(d, sub, budget, truncated)) item.children.push_back(std::move(sub));
    }
    for (const auto& f : files) {
        if (budget <= 0) { truncated = true; break; }
        ImportItem fi;
        fi.title = f.stem().string();   // the file's own H1 decides at import time
        fi.path  = f.string();
        item.children.push_back(std::move(fi));
        --budget;
    }
    return !item.children.empty();
}

}  // namespace

bool plan_folder_import(const std::string& dir, ImportItem& out, int limit, bool* truncated) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;
    fs::path d = fs::path(dir).lexically_normal();
    if (d.has_filename() == false) d = d.parent_path();   // "/a/b/" -> "/a/b"
    out = ImportItem{};
    out.title  = d.filename().string();
    out.path   = d.string();
    out.folder = true;
    int budget = limit;
    bool trunc = false;
    const bool any = plan_into(d, out, budget, trunc);
    if (truncated) *truncated = trunc;
    return any;
}

int count_files(const ImportItem& item) {
    if (!item.folder) return 1;
    int n = 0;
    for (const auto& c : item.children) n += count_files(c);
    return n;
}

}  // namespace jot::core
