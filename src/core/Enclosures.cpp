#include "core/Enclosures.hpp"
#include "core/Markdown.hpp"

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

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
    if (s.empty()) s = "image";
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
        const std::string name = attachment_name(lk.target);
        if (name.empty()) continue;
        auto it = std::find_if(out.begin(), out.end(),
                               [&](const Enclosure& e) { return e.name == name; });
        if (it != out.end()) { ++it->refs; continue; }
        Enclosure e;
        e.name  = name;
        e.label = lk.label;
        e.refs  = 1;
        e.line  = lk.line;
        e.present = !store.dir.empty() && present_at(fs::path(store.dir) / name);
        if (auto m = store.metas.find(name); m != store.metas.end()) {
            e.has_meta = true;
            e.meta = m->second;
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
        if (attachment_name(std::string(kAttachPrefix) + it.key()).empty()) continue;
        EnclosureMeta e;
        e.mode   = it.value().value("mode", std::string("embedded"));
        e.source = it.value().value("source", std::string{});
        e.size   = it.value().value("size", std::int64_t{0});
        e.added  = it.value().value("added", std::int64_t{0});
        out[it.key()] = std::move(e);
    }
    return out;
}

}  // namespace jot::core
