#include "core/Enclosures.hpp"
#include "core/Markdown.hpp"

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

#include <sys/stat.h>

// src/core/Enclosures.cpp -- see the header. Everything here is plain files and
// strings; the selftest drives it against a real temp directory.

namespace jot::core {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Split "a.b.PNG" into ("a.b", ".png"). A leading dot is a name, not an
// extension (".hidden" has no extension), and so is a trailing one.
void split_ext(const std::string& name, std::string& stem, std::string& ext) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == name.size()) {
        stem = name;
        ext.clear();
        return;
    }
    stem = name.substr(0, dot);
    ext  = name.substr(dot);
}

std::string base_name(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string slug_part(const std::string& s) {
    std::string out;
    bool dash = false;
    for (unsigned char c : s) {
        if (std::isalnum(c) && c < 0x80) {
            out += static_cast<char>(std::tolower(c));
            dash = false;
        } else if (!out.empty() && !dash) {
            out += '-';
            dash = true;
        }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

bool present_at(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

}  // namespace

// ── naming ──────────────────────────────────────────────────────────────────

std::string slug_filename(const std::string& original) {
    std::string stem, ext;
    split_ext(base_name(original), stem, ext);
    std::string s = slug_part(stem);
    if (s.empty()) s = is_image_filename(original) ? "image" : "file";
    std::string e = slug_part(ext);          // ".JPG" -> "jpg"
    return e.empty() ? s : s + "." + e;
}

std::string paste_filename(std::int64_t unix_time, const std::string& ext) {
    const std::time_t t = static_cast<std::time_t>(unix_time);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", &tm);
    return std::string("pasted-") + buf + "." + (ext.empty() ? std::string("png") : lower(ext));
}

std::string unique_filename(const std::string& dir, const std::string& name) {
    if (!present_at(fs::path(dir) / name)) return name;
    std::string stem, ext;
    split_ext(name, stem, ext);
    for (int i = 2; i < 100000; ++i) {
        std::string cand = stem + "-" + std::to_string(i) + ext;
        if (!present_at(fs::path(dir) / cand)) return cand;
    }
    return {};   // a hundred thousand collisions is not a folder, it is an attack
}

bool is_image_filename(const std::string& name) {
    std::string stem, ext;
    split_ext(base_name(name), stem, ext);
    ext = lower(ext);
    static const char* const kExt[] = {".png", ".jpg", ".jpeg", ".gif", ".webp", ".svg",
                                       ".bmp", ".tif", ".tiff", ".avif", ".heic", ".ico"};
    for (const char* e : kExt)
        if (ext == e) return true;
    return false;
}

std::string attachment_name(const std::string& target) {
    const std::string pre = kAttachPrefix;
    if (target.size() <= pre.size() || target.compare(0, pre.size(), pre) != 0) return {};
    std::string rest = target.substr(pre.size());
    if (rest.find('/') != std::string::npos || rest == "." || rest == "..") return {};
    return rest;
}

std::string image_label(const std::string& original) {
    std::string stem, ext;
    split_ext(base_name(original), stem, ext);
    std::string out;
    for (char c : stem)
        if (c != '[' && c != ']' && c != '\n' && c != '\r') out += c;
    return out;
}

std::string image_markdown(const std::string& label, const std::string& name) {
    std::string l;
    for (char c : label)
        if (c != '[' && c != ']' && c != '\n' && c != '\r') l += c;
    return "![" + l + "](" + kAttachPrefix + name + ")";
}

std::string enclosure_markdown(const std::string& label, const std::string& name) {
    if (is_linked_key(name)) {
        std::string l;
        for (char c : label)
            if (c != '[' && c != ']' && c != '\n' && c != '\r') l += c;
        const std::string md = "[" + l + "](" + name + ")";
        return is_image_filename(linked_path(name)) ? "!" + md : md;
    }
    return is_image_filename(name) ? image_markdown(label, name)
                                   : image_markdown(label, name).substr(1);   // drop the bang
}

// ── linked (s019) ───────────────────────────────────────────────────────────

std::string file_uri(const std::string& abs_path) {
    if (abs_path.empty() || abs_path[0] != '/') return {};
    static const char* const kHex = "0123456789ABCDEF";
    std::string out = "file://";
    for (unsigned char c : abs_path) {
        if (std::isalnum(c) && c < 0x80) out += static_cast<char>(c);
        else if (c == '/' || c == '-' || c == '.' || c == '_' || c == '~') out += static_cast<char>(c);
        else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0xF];
        }
    }
    return out;
}

std::string linked_path(const std::string& target_in) {
    // Tolerate surrounding space: a link is typed by hand.
    std::size_t a = target_in.find_first_not_of(" \t");
    std::size_t b = target_in.find_last_not_of(" \t");
    if (a == std::string::npos) return {};
    const std::string target = target_in.substr(a, b - a + 1);

    std::string rest;
    if (target.rfind("file:///", 0) == 0)                rest = target.substr(7);
    else if (target.rfind("file://localhost/", 0) == 0)  rest = target.substr(16);
    else return {};

    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == '%' && i + 2 < rest.size()) {
            const int h = hex(rest[i + 1]), l = hex(rest[i + 2]);
            if (h < 0 || l < 0) return {};
            const char c = static_cast<char>(h * 16 + l);
            if (c == '\0') return {};
            out += c;
            i += 2;
        } else if (rest[i] == '%') {
            return {};                       // a truncated escape
        } else if (rest[i] == '?' || rest[i] == '#') {
            break;                           // a query or fragment names no part of the file
        } else {
            out += rest[i];
        }
    }
    if (out.empty() || out[0] != '/' || out == "/") return {};
    return out;
}

std::string linked_key(const std::string& target) {
    const std::string p = linked_path(target);
    return p.empty() ? std::string{} : file_uri(p);
}

bool is_linked_key(const std::string& key) {
    return !key.empty() && linked_key(key) == key;
}

bool file_stamp(const std::string& path, std::int64_t& size, std::int64_t& mtime) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    size  = static_cast<std::int64_t>(st.st_size);
    mtime = static_cast<std::int64_t>(st.st_mtim.tv_sec);
    return true;
}

std::string link_file(AttachStore& store, const std::string& abs_path, std::int64_t now,
                      std::string& err) {
    err.clear();
    std::error_code ec;
    if (fs::is_directory(abs_path, ec)) { err = "a folder cannot be enclosed"; return {}; }
    const std::string key = file_uri(abs_path);
    if (key.empty()) { err = "not an absolute path: " + abs_path; return {}; }
    EnclosureMeta m;
    if (!file_stamp(abs_path, m.size, m.mtime)) { err = "not a file: " + abs_path; return {}; }
    m.mode   = "linked";
    m.source = abs_path;
    m.added  = now;
    store.metas[key] = m;
    return key;
}

bool accept_change(AttachStore& store, const std::string& key, std::string& err) {
    err.clear();
    const std::string path = linked_path(key);
    if (path.empty()) { err = "not a linked file: " + key; return false; }
    EnclosureMeta& m = store.metas[key];
    std::int64_t size = 0, mtime = 0;
    if (!file_stamp(path, size, mtime)) { err = "missing: " + path; return false; }
    m.mode  = "linked";
    if (m.source.empty()) m.source = path;
    m.size  = size;
    m.mtime = mtime;
    return true;
}

std::string relink(AttachStore& store, const std::string& key, const std::string& new_path,
                   std::int64_t now, std::string& err) {
    err.clear();
    std::int64_t added = now;
    if (auto it = store.metas.find(key); it != store.metas.end() && it->second.added)
        added = it->second.added;
    const std::string nk = link_file(store, new_path, now, err);
    if (nk.empty()) return {};
    store.metas[nk].added = added;
    return nk;
}

std::string embed_copy(AttachStore& store, const std::string& key, std::int64_t now,
                       std::string& err) {
    err.clear();
    if (!is_linked_key(key)) { err = "not a linked file: " + key; return {}; }
    const std::string path = linked_path(key);
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) { err = "not there: " + path; return {}; }
    return ingest_file(store, path, now, err);
}

std::string original_of(const AttachStore& store, const std::string& name) {
    if (is_linked_key(name)) return {};
    const auto it = store.metas.find(name);
    if (it == store.metas.end()) return {};
    const std::string& s = it->second.source;
    // "clipboard" and anything else that is not an absolute path is not an
    // original jot can point at.
    return (!s.empty() && s[0] == '/') ? s : std::string{};
}

std::string link_instead(AttachStore& store, const std::string& name, const std::string& abs_path,
                         std::int64_t now, std::string& err) {
    err.clear();
    if (name.empty() || is_linked_key(name)) { err = "not an embedded file: " + name; return {}; }
    return link_file(store, abs_path, now, err);
}

namespace {
// The key a markdown target names, whichever mode: an attachment name or a
// canonical linked key. Empty for anything that is not an enclosure.
std::string target_key(const std::string& target) {
    std::string k = attachment_name(target);
    return k.empty() ? linked_key(target) : k;
}
}  // namespace

int retarget_references(std::string& body, const std::string& from_key, const std::string& to_key) {
    if (from_key == to_key || from_key.empty() || to_key.empty()) return 0;
    const std::string to_t = is_linked_key(to_key) ? to_key : std::string(kAttachPrefix) + to_key;
    const Scan sc = scan(body);
    int n = 0;
    for (auto it = sc.links.rbegin(); it != sc.links.rend(); ++it) {
        if (target_key(it->target) != from_key) continue;
        const std::string old_t = "(" + it->target;
        const auto pos = body.find(old_t, static_cast<std::size_t>(it->begin));
        if (pos == std::string::npos || pos >= static_cast<std::size_t>(it->end)) continue;
        body.replace(pos + 1, it->target.size(), to_t);
        ++n;
    }
    return n;
}

std::vector<std::string> linked_references(const std::string& body) {
    std::vector<std::string> out;
    for (const auto& lk : scan(body).links) {
        const std::string k = linked_key(lk.target);
        if (!k.empty() && std::find(out.begin(), out.end(), k) == out.end()) out.push_back(k);
    }
    return out;
}

std::string Enclosure::display() const {
    if (!linked) return name;
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// ── ingest ──────────────────────────────────────────────────────────────────

namespace {

std::string reserve(AttachStore& store, const std::string& wanted, std::string& err) {
    std::error_code ec;
    fs::create_directories(store.dir, ec);
    if (ec) { err = "cannot create " + store.dir + ": " + ec.message(); return {}; }
    const std::string name = unique_filename(store.dir, wanted);
    if (name.empty()) err = "no free name for " + wanted;
    return name;
}

}  // namespace

std::string ingest_file(AttachStore& store, const std::string& src_path,
                        std::int64_t now, std::string& err) {
    err.clear();
    std::error_code ec;
    if (fs::is_directory(src_path, ec)) { err = "a folder cannot be enclosed"; return {}; }
    if (!fs::is_regular_file(src_path, ec)) { err = "not a file: " + src_path; return {}; }
    const std::string name = reserve(store, slug_filename(src_path), err);
    if (name.empty()) return {};

    const fs::path dst = fs::path(store.dir) / name;
    fs::copy_file(src_path, dst, fs::copy_options::none, ec);
    if (ec) { err = "copy failed: " + ec.message(); return {}; }

    EnclosureMeta m;
    m.source = src_path;
    m.size   = static_cast<std::int64_t>(fs::file_size(dst, ec));
    m.added  = now;
    store.metas[name] = m;
    return name;
}

std::string ingest_bytes(AttachStore& store, const std::string& bytes,
                         const std::string& name_wanted, const std::string& source,
                         std::int64_t now, std::string& err) {
    err.clear();
    if (bytes.empty()) { err = "nothing to write"; return {}; }
    const std::string name = reserve(store, slug_filename(name_wanted), err);
    if (name.empty()) return {};

    const fs::path dst = fs::path(store.dir) / name;
    const fs::path tmp = dst.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) { err = "cannot write " + tmp.string(); return {}; }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) { err = "short write " + tmp.string(); return {}; }
    }
    std::error_code ec;
    fs::rename(tmp, dst, ec);
    if (ec) { fs::remove(tmp, ec); err = "rename failed"; return {}; }

    EnclosureMeta m;
    m.source = source;
    m.size   = static_cast<std::int64_t>(bytes.size());
    m.added  = now;
    store.metas[name] = m;
    return name;
}

// ── the derived list ────────────────────────────────────────────────────────

std::vector<Enclosure> enclosures(const std::string& body, const AttachStore& store) {
    std::vector<Enclosure> out;
    const Scan sc = scan(body);
    for (const auto& lk : sc.links) {
        std::string name = attachment_name(lk.target);
        const bool linked = name.empty();
        if (linked) name = linked_key(lk.target);
        if (name.empty()) continue;
        auto it = std::find_if(out.begin(), out.end(),
                               [&](const Enclosure& e) { return e.name == name; });
        if (it != out.end()) { ++it->refs; continue; }
        Enclosure e;
        e.name   = name;
        e.label  = lk.label;
        e.linked = linked;
        e.refs   = 1;
        e.line   = lk.line;
        if (auto m = store.metas.find(name); m != store.metas.end()) {
            e.has_meta = true;
            e.meta = m->second;
        }
        if (linked) {
            e.path = linked_path(name);
            std::int64_t size = 0, mtime = 0;
            e.present = file_stamp(e.path, size, mtime);
            e.status  = !e.present ? EnclosureStatus::Missing
                      : (e.has_meta && (size != e.meta.size || mtime != e.meta.mtime))
                          ? EnclosureStatus::Modified
                          : EnclosureStatus::Ok;
        } else {
            e.path    = store.dir.empty() ? std::string{} : (fs::path(store.dir) / name).string();
            e.present = !store.dir.empty() && present_at(fs::path(store.dir) / name);
            e.status  = e.present ? EnclosureStatus::Ok : EnclosureStatus::Missing;
        }
        out.push_back(std::move(e));
    }
    return out;
}

std::vector<std::string> referenced_attachments(const std::string& body) {
    std::vector<std::string> out;
    for (const auto& lk : scan(body).links) {
        const std::string name = attachment_name(lk.target);
        if (!name.empty() && std::find(out.begin(), out.end(), name) == out.end())
            out.push_back(name);
    }
    return out;
}

int rename_references(std::string& body, const std::string& from, const std::string& to) {
    if (from == to) return 0;
    // Through the scan, back to front, so an offset is never stale and a
    // matching string in plain prose or a code span is never touched.
    const Scan sc = scan(body);
    const std::string old_t = std::string("(") + kAttachPrefix + from;
    const std::string new_t = std::string("(") + kAttachPrefix + to;
    int n = 0;
    for (auto it = sc.links.rbegin(); it != sc.links.rend(); ++it) {
        if (attachment_name(it->target) != from) continue;
        const auto pos = body.find(old_t, static_cast<std::size_t>(it->begin));
        if (pos == std::string::npos || pos >= static_cast<std::size_t>(it->end)) continue;
        body.replace(pos, old_t.size(), new_t);
        ++n;
    }
    return n;
}

// ── carry ───────────────────────────────────────────────────────────────────

std::map<std::string, std::string> carry_attachments(const AttachStore& from, AttachStore& to,
                                                     const std::vector<std::string>& names,
                                                     bool move, int* carried) {
    std::map<std::string, std::string> renames;
    int n = 0;
    std::error_code ec;
    if (from.dir.empty() || to.dir.empty() || names.empty()) {
        if (carried) *carried = 0;
        return renames;
    }
    fs::create_directories(to.dir, ec);

    for (const auto& name : names) {
        const fs::path src = fs::path(from.dir) / name;
        if (!fs::is_regular_file(src, ec)) continue;     // Missing stays Missing
        const std::string dst_name = unique_filename(to.dir, name);
        if (dst_name.empty()) continue;
        const fs::path dst = fs::path(to.dir) / dst_name;

        bool ok = false;
        if (move) {
            fs::rename(src, dst, ec);
            ok = !ec;
        }
        if (!ok) {
            ec.clear();
            fs::copy_file(src, dst, fs::copy_options::none, ec);
            ok = !ec && fs::file_size(dst, ec) == fs::file_size(src, ec);
            // The source goes only once the copy is proven, and only on a move.
            if (ok && move) fs::remove(src, ec);
        }
        if (!ok) continue;

        ++n;
        if (auto m = from.metas.find(name); m != from.metas.end()) to.metas[dst_name] = m->second;
        if (dst_name != name) renames[name] = dst_name;
    }
    if (carried) *carried = n;
    return renames;
}

// ── out ─────────────────────────────────────────────────────────────────────

std::string enclosure_path(const AttachStore& store, const std::string& name) {
    if (is_linked_key(name)) return linked_path(name);
    if (store.dir.empty() || attachment_name(std::string(kAttachPrefix) + name).empty())
        return {};
    return (fs::path(store.dir) / name).string();
}

bool copy_out(const AttachStore& store, const std::string& name, const std::string& dest,
              std::string& err) {
    err.clear();
    const std::string src = enclosure_path(store, name);
    if (src.empty()) { err = "not an enclosure name: " + name; return false; }
    std::error_code ec;
    if (!fs::is_regular_file(src, ec)) { err = "missing: " + src; return false; }
    if (dest.empty()) { err = "no destination"; return false; }
    if (fs::exists(dest, ec) && fs::equivalent(src, dest, ec)) {
        err = "that is the enclosure itself";
        return false;
    }
    const fs::path tmp = dest + ".jot-tmp";
    fs::copy_file(src, tmp, fs::copy_options::overwrite_existing, ec);
    if (ec) { err = "copy failed: " + ec.message(); fs::remove(tmp, ec); return false; }
    fs::rename(tmp, dest, ec);
    if (ec) { err = "could not replace " + dest + ": " + ec.message(); fs::remove(tmp, ec); return false; }
    return true;
}

// ── jot.json ────────────────────────────────────────────────────────────────

std::string encode_metas(const EnclosureMetas& m) {
    json j = json::object();
    for (const auto& [name, e] : m) {
        json o = {{"mode", e.mode}, {"size", e.size}, {"added", e.added}};
        if (!e.source.empty()) o["source"] = e.source;
        if (e.mtime) o["mtime"] = e.mtime;
        j[name] = std::move(o);
    }
    return j.dump();
}

EnclosureMetas decode_metas(const std::string& text) {
    EnclosureMetas out;
    json j = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return out;
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (!it.value().is_object()) continue;
        if (attachment_name(std::string(kAttachPrefix) + it.key()).empty() &&
            !is_linked_key(it.key()))
            continue;
        EnclosureMeta e;
        e.mode   = it.value().value("mode", std::string("embedded"));
        e.source = it.value().value("source", std::string{});
        e.size   = it.value().value("size", std::int64_t{0});
        e.added  = it.value().value("added", std::int64_t{0});
        e.mtime  = it.value().value("mtime", std::int64_t{0});
        out[it.key()] = std::move(e);
    }
    return out;
}

}  // namespace jot::core
