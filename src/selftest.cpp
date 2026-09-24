// jot_selftest -- the headless core harness.
//
// A seed is only trustworthy if its paradigms are EXERCISED, not just described
// (CANON: "a claim in the doc isn't a fact until a consumer exercises it"). This
// runs the GTK-free core with no display, so a future you can trust the pump and
// the mapping before wiring any widget on top:
//   * Recents  -- move-to-front + trim + the JSON round-trip with stale-prune;
//   * TextMap  -- forward(selection) -> inverse(map_range) round-trips byte-exact
//                 across single-line / multi-line / cross-paragraph / UTF-8, and
//                 chrome lines are never landed on.
//
//   cmake --build build --target jot_selftest && ./build/jot_selftest

#include "core/Links.hpp"
#include "core/Markdown.hpp"
#include "core/Nodes.hpp"
#include "core/Lifecycle.hpp"
#include "core/Prefs.hpp"
#include "core/Recents.hpp"
#include "core/Project.hpp"
#include "core/Shortcuts.hpp"
#include "core/Notify.hpp"
#include "core/Hotkey.hpp"
#include "core/Pending.hpp"
#include "core/Projection.hpp"
#include "core/Tasks.hpp"
#include "core/TextMap.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace core = jot::core;

namespace {
int g_pass = 0, g_fail = 0;
void check(const std::string& name, bool ok, const std::string& detail = "") {
    if (ok) { ++g_pass; std::cout << "  PASS  " << name; }
    else    { ++g_fail; std::cout << "  FAIL  " << name; }
    if (!detail.empty()) std::cout << "  (" << detail << ")";
    std::cout << "\n";
}

// The FORWARD mapping, mirroring what a viewer's selection() would do: a
// (al,ac)-(cl,cc) mark -> V byte endpoints -> codepoint range + quote. Returns
// the byte endpoints too, so the round-trip through map_range can be exact.
struct Fwd { int cp_start, cp_end, bstart, bend; std::string quote, V; };
Fwd forward(const std::vector<core::FlatLine>& flat, int al, int ac, int cl, int cc) {
    std::vector<int> off;
    std::string V = core::visible_text(flat, off);
    int bstart = off[static_cast<std::size_t>(al)] +
                 std::min(ac, static_cast<int>(flat[static_cast<std::size_t>(al)].text.size()));
    int bend   = off[static_cast<std::size_t>(cl)] +
                 std::min(cc, static_cast<int>(flat[static_cast<std::size_t>(cl)].text.size()));
    std::string quote = V.substr(static_cast<std::size_t>(bstart),
                                 static_cast<std::size_t>(bend - bstart));
    int cps = core::utf8_length(V.substr(0, static_cast<std::size_t>(bstart)));
    int cpe = cps + core::utf8_length(quote);
    return {cps, cpe, bstart, bend, quote, V};
}

// Assert map_range inverts forward(): endpoints mapped back through byte_off
// reproduce the forward byte endpoints and reconstruct the same quote.
void roundtrip(const std::vector<core::FlatLine>& flat,
               int al, int ac, int cl, int cc, const std::string& what) {
    Fwd f = forward(flat, al, ac, cl, cc);
    std::vector<int> off;
    (void)core::visible_text(flat, off);
    core::LineCol s, e;
    const bool ok = core::map_range(flat, f.cp_start, f.cp_end, s, e);
    check("textmap: " + what + " maps", ok);
    if (!ok) return;
    const int rb0 = off[static_cast<std::size_t>(s.line)] + s.col;
    const int rb1 = off[static_cast<std::size_t>(e.line)] + e.col;
    check("textmap: " + what + " round-trips byte-exact",
          rb0 == f.bstart && rb1 == f.bend,
          std::to_string(rb0) + "," + std::to_string(rb1) +
          " vs " + std::to_string(f.bstart) + "," + std::to_string(f.bend));
    check("textmap: " + what + " reconstructs quote",
          f.V.substr(static_cast<std::size_t>(rb0),
                     static_cast<std::size_t>(rb1 - rb0)) == f.quote);
}
}  // namespace

int main() {
    std::cout << "jot_selftest -- node model + tasks + capture + jots pump + recents + textmap\n";

    // -- Project: the persistence pump, on a real directory ---------------------
    // Round-trip fidelity is the bar: what you save is what you load, including
    // sibling ORDER, which is the half a naive format loses. And the recovery
    // path is tested for real -- delete jot.json and confirm the notes come
    // back, because that is the whole reason each file carries its id.
    {
        const std::string dir =
            (std::filesystem::temp_directory_path() / "jot_selftest_jots").string();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);

        core::NodeId a, b, c, deep;
        {
            core::Project v;
            check("jots: opens (and creates) a jots folder directory", v.open(dir));
            check("jots: a fresh jots folder is empty", v.count() == 0);

            a = v.create("", "Alpha");
            b = v.create("", "Beta");
            c = v.create(a, "Gamma");
            deep = v.create(c, "Delta");
            check("jots: create mints a uuid", a.size() == 36 && a[14] == '4');
            v.set_body(deep, "- [ ] a task line\nand a second paragraph\n");
            // Reorder BEFORE protecting: a protected node does not move, which
            // is the model working and the test's first draft not.
            check("jots: reorder before save", v.move(b, "", 0));
            v.set_protect(b, true);
            check("jots: flush writes", v.flush());

            check("jots: the project file exists",
                  std::filesystem::exists(std::filesystem::path(dir) / "jot.json"));
            check("jots: one markdown file per note",
                  std::filesystem::exists(std::filesystem::path(dir) / "notes" / (deep + ".md")));
        }

        {
            core::Project v;
            check("jots: reopens", v.open(dir));
            check("jots: every node came back", v.count() == 4);
            check("jots: sibling order survives the round trip",
                  v.children("") == std::vector<core::NodeId>{b, a});
            check("jots: the tree came back",
                  v.children(a) == std::vector<core::NodeId>{c} &&
                  v.children(c) == std::vector<core::NodeId>{deep});
            const core::Node* d = v.find(deep);
            check("jots: body round-trips byte-exact",
                  d && d->body == "- [ ] a task line\nand a second paragraph\n");
            const core::Node* nb = v.find(b);
            check("jots: flags round-trip", nb && nb->protect);
            check("jots: nothing was recovered on a clean open", v.recovered_orphans() == 0);

            // A move must not touch the filesystem: the file keeps its name.
            check("jots: a move leaves the files alone", v.move(c, "", 0) && v.flush() &&
                  std::filesystem::exists(std::filesystem::path(dir) / "notes" / (c + ".md")));
        }

        // Recovery: lose the project file, keep the notes.
        {
            std::filesystem::remove(std::filesystem::path(dir) / "jot.json", ec);
            core::Project v;
            check("jots: opens without a project file", v.open(dir));
            check("jots: every note is recovered from its front-matter id",
                  v.count() == 4 && v.recovered_orphans() == 4);
            check("jots: recovered notes are top-level orphans",
                  v.children("").size() == 4);
            const core::Node* d = v.find(deep);
            check("jots: a recovered note keeps its id and body",
                  d && d->body.rfind("- [ ] a task line", 0) == 0);
            check("jots: recovery rewrote the project file",
                  std::filesystem::exists(std::filesystem::path(dir) / "jot.json"));
        }
        std::filesystem::remove_all(dir, ec);
    }

    // -- Relocate: moving a whole jots folder ---------------------------------
    // The s005 milestone rests on this being safe, and "safe" here means the
    // refusals fire BEFORE anything is touched. Each check below is a way to
    // lose notes if it fails, which is why they are tested on real directories
    // rather than reasoned about.
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path root = fs::temp_directory_path() / "jot_selftest_relocate";
        fs::remove_all(root, ec);

        const std::string from = (root / "here").string();
        const std::string to   = (root / "elsewhere" / "jots").string();

        core::NodeId a, b;
        {
            core::Project v;
            v.open(from);
            a = v.create("", "Alpha");
            b = v.create(a, "Beta");
            v.set_body(b, "body that must survive the move\n");
            v.flush();
        }

        check("relocate: a jots folder is recognised as one", core::is_jots_dir(from));
        check("relocate: it knows how many notes are in it",
              core::jots_note_count(from) == 2);
        check("relocate: an empty directory is not a jots folder",
              !core::is_jots_dir((root / "empty").string()) &&
              core::jots_note_count((root / "empty").string()) == 0);

        // ── the refusals, each leaving the source untouched ──────────────────
        check("relocate: refuses a source that isn't there",
              !core::relocate_jots((root / "nope").string(), to).empty());
        check("relocate: refuses moving a folder inside itself",
              !core::relocate_jots(from, (fs::path(from) / "inner").string()).empty());
        fs::create_directories(root / "occupied", ec);
        { std::ofstream f((root / "occupied" / "someone.txt").string()); f << "x"; }
        check("relocate: refuses a destination that already has something in it",
              !core::relocate_jots(from, (root / "occupied").string()).empty());
        check("relocate: every refusal left the source alone",
              core::jots_note_count(from) == 2);

        // ── the move itself ─────────────────────────────────────────────────
        check("relocate: moves the folder", core::relocate_jots(from, to).empty());
        check("relocate: the old location is gone", !fs::exists(from, ec));
        check("relocate: the notes arrived", core::jots_note_count(to) == 2);

        // The reason a relocate is only a rename: nothing inside held a path.
        // If any of this had been path-keyed, the reopen below is where it
        // would show up -- as a tree that came back flat, or a missing body.
        {
            core::Project v;
            check("relocate: it reopens at the new location", v.open(to));
            check("relocate: the tree survived, addressed by id",
                  v.count() == 2 && v.children("") == std::vector<core::NodeId>{a} &&
                  v.children(a) == std::vector<core::NodeId>{b});
            const core::Node* nb = v.find(b);
            check("relocate: the body survived byte-exact",
                  nb && nb->body == "body that must survive the move\n");
            check("relocate: nothing had to be recovered",
                  v.recovered_orphans() == 0);
        }

        // An empty directory at the destination is what a file chooser leaves
        // behind when the user makes a folder in it, so it must not be a
        // refusal -- it is the commonest way this operation is asked for.
        {
            const std::string again = (root / "third" / "jots").string();
            fs::create_directories(again, ec);
            check("relocate: an EMPTY destination folder is accepted",
                  core::relocate_jots(to, again).empty() &&
                  core::jots_note_count(again) == 2);
        }

        fs::remove_all(root, ec);
    }

    // -- The .jots naming convention ------------------------------------------
    {
        check("jots name: the suffix is stripped for display",
              core::jots_display_name("/home/s/Documents/Field notes.jots") == "Field notes");
        check("jots name: a folder without the suffix shows its whole name",
              core::jots_display_name("/home/s/.local/share/jot/vault") == "vault");
        check("jots name: a folder named only the suffix keeps it",
              core::jots_display_name("/tmp/.jots") == ".jots");
        check("jots name: a chosen name gains the suffix",
              core::jots_folder_name("Field notes") == "Field notes.jots");
        check("jots name: surrounding space is trimmed, inner space kept",
              core::jots_folder_name("  Field notes  ") == "Field notes.jots");
        check("jots name: a name already carrying the suffix isn't doubled",
              core::jots_folder_name("Work.jots") == "Work.jots");
        // Empty means refuse, and the UI greys its button on it. Each of these
        // would otherwise be a folder somewhere the user did not intend.
        check("jots name: blank is refused", core::jots_folder_name("   ").empty());
        check("jots name: a path separator is refused",
              core::jots_folder_name("a/b").empty() && core::jots_folder_name("a\\b").empty());
        check("jots name: dot and dot-dot are refused",
              core::jots_folder_name(".").empty() && core::jots_folder_name("..").empty());
    }

    // -- Adopt: the scratch buffer gets a home --------------------------------
    // jot can run with no jots folder at all, holding notes in memory. Saving
    // that is not a copy: memory ids are counters and a Project's ids BECOME
    // FILENAMES, so every node is re-minted and every parent reference remapped.
    // A parent mapped to a stale id is a subtree that silently goes missing,
    // which is the one failure this block exists to make impossible.
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const std::string dir =
            (fs::temp_directory_path() / "jot_selftest_adopt").string();
        fs::remove_all(dir, ec);

        // A scratch buffer with depth, order, a body, a flag and a timestamp.
        core::MemoryNodes scratch;
        const auto r1 = scratch.create("", "Root one");
        const auto r2 = scratch.create("", "Root two");
        const auto c1 = scratch.create(r1, "Child A");
        const auto c2 = scratch.create(r1, "Child B");
        const auto g1 = scratch.create(c2, "Grandchild");
        scratch.set_body(g1, "deep body\nwith two lines\n");
        scratch.set_body(r2, "root two body\n");
        scratch.set_protect(c1, true);
        scratch.move(r2, "", 0);                  // r2 first -- order must survive
        check("adopt: the scratch buffer is as arranged",
              scratch.count() == 5 &&
              scratch.children("") == std::vector<core::NodeId>{r2, r1});

        std::size_t adopted = 0;
        {
            core::Project v;
            check("adopt: opens the destination", v.open(dir));
            adopted = v.adopt(scratch);
            check("adopt: every node came across", adopted == scratch.count());

            // Re-minting is the point: a counter id must not survive into a
            // folder where ids are filenames.
            check("adopt: ids were re-minted as uuids", [&] {
                for (const auto& id : v.children(""))
                    if (id.size() != 36 || id[14] != '4') return false;
                return true;
            }());
            check("adopt: the old ids are gone", v.find(r1) == nullptr &&
                                                 v.find(g1) == nullptr);
        }

        // The real test is the REOPEN: whatever the remap got wrong is on disk
        // now, and a lost parent shows up as a flattened tree.
        {
            core::Project v;
            check("adopt: the saved folder reopens", v.open(dir));
            check("adopt: nothing had to be recovered", v.recovered_orphans() == 0);
            check("adopt: every note is on disk", v.count() == 5);

            const auto roots = v.children("");
            check("adopt: sibling order survived", roots.size() == 2);
            const core::Node* first = roots.empty() ? nullptr : v.find(roots[0]);
            check("adopt: the reordered root is still first",
                  first && first->title == "Root two");
            check("adopt: its body came with it",
                  first && first->body == "root two body\n");

            const core::Node* second = roots.size() > 1 ? v.find(roots[1]) : nullptr;
            check("adopt: the second root kept its children",
                  second && v.children(second->id).size() == 2);

            // Depth two: the remap had to resolve a parent that was itself
            // re-minted. This is the check that fails if the map is wrong.
            check("adopt: the grandchild is still a grandchild", [&] {
                if (roots.size() < 2) return false;
                const auto kids = v.children(roots[1]);
                if (kids.size() != 2) return false;
                const auto grandkids = v.children(kids[1]);
                if (grandkids.size() != 1) return false;
                const core::Node* g = v.find(grandkids[0]);
                return g && g->title == "Grandchild" &&
                       g->body == "deep body\nwith two lines\n";
            }());
            check("adopt: the protect flag came across", [&] {
                if (roots.size() < 2) return false;
                const auto kids = v.children(roots[1]);
                const core::Node* a = kids.empty() ? nullptr : v.find(kids[0]);
                return a && a->title == "Child A" && a->protect;
            }());

            // Adopting into a folder that already has notes would merge two
            // sets of jots by accident. It is a refusal, and it changes nothing.
            core::MemoryNodes more;
            more.create("", "Intruder");
            check("adopt: refuses a jots folder that isn't empty",
                  v.adopt(more) == 0 && v.count() == 5);
        }

        // Nothing to adopt is not a failure, it is the empty app closing.
        {
            const std::string dir2 = (fs::temp_directory_path() / "jot_selftest_adopt2").string();
            fs::remove_all(dir2, ec);
            core::Project v;
            v.open(dir2);
            core::MemoryNodes empty;
            check("adopt: an empty scratch buffer adopts nothing",
                  v.adopt(empty) == 0 && v.count() == 0);
            fs::remove_all(dir2, ec);
        }

        fs::remove_all(dir, ec);
    }


    // -- Links: what a note points at, and what points at it -----------------
    // The scan half first: s004 styled links and threw the target away. The
    // drawer needs the string, and a second markdown parser in a UI file is how
    // two answers to one question drift apart.
    {
        {
            const std::string t = "see [Groceries](jot:n0007) and [docs](https://x.y)\n";
            const auto sc = core::scan(t);
            check("links: both links are found", sc.links.size() == 2);
            check("links: the label and the target come back whole",
                  sc.links.size() == 2 && sc.links[0].label == "Groceries" &&
                      sc.links[0].target == "jot:n0007" &&
                      sc.links[1].target == "https://x.y");
            check("links: a jot: target resolves to a node id",
                  core::link_node_id("jot:n0007") == "n0007");
            check("links: a non-jot target resolves to nothing",
                  core::link_node_id("https://x.y").empty() &&
                      core::link_node_id("attachments/a.png").empty() &&
                      core::link_node_id("jot:").empty());
            check("links: jot://id and stray space still resolve",
                  core::link_node_id("jot://n0007") == "n0007" &&
                      core::link_node_id("jot: n0007 ") == "n0007");
            check("links: the byte range covers the whole [label](target)",
                  sc.links.size() == 2 &&
                      t.substr((std::size_t)sc.links[0].begin,
                               (std::size_t)(sc.links[0].end - sc.links[0].begin)) ==
                          "[Groceries](jot:n0007)");
        }
        {
            // An image is a link with a bang, and the bang belongs to the range
            // -- otherwise "select the link" leaves an orphan ! behind.
            const std::string t = "![a diagram](attachments/d.png)\n";
            const auto sc = core::scan(t);
            check("links: an image scans as a link and says so",
                  sc.links.size() == 1 && sc.links[0].image &&
                      sc.links[0].label == "a diagram");
            check("links: an image's range includes the leading bang",
                  sc.links.size() == 1 && sc.links[0].begin == 0 &&
                      t.substr((std::size_t)sc.links[0].begin,
                               (std::size_t)(sc.links[0].end - sc.links[0].begin)) ==
                          "![a diagram](attachments/d.png)");
        }
        {
            // Code spans stop markdown being markdown, and that has to include
            // links -- a note ABOUT jot's link syntax is the first note anyone
            // writes, and it must not fill the drawer with links to nowhere.
            const std::string t = "write `[x](jot:n1)` to link\n";
            const auto sc = core::scan(t);
            check("links: a link inside a code span is not a link",
                  sc.links.empty());
        }
        {
            const std::string t = "```\n[x](jot:n1)\n```\n";
            const auto sc = core::scan(t);
            check("links: a link inside a fence is not a link", sc.links.empty());
        }
        {
            const std::string t = "über [ünicode](jot:n9) lïnk\n";
            const auto sc = core::scan(t);
            check("links: codepoint offsets survive multibyte text before a link",
                  sc.links.size() == 1 &&
                      sc.links[0].cp_begin == core::cp_len(t, 0, sc.links[0].begin));
        }
        {
            const std::string t = "a [one](jot:n1)\nb [two](jot:n2)\nc [three](jot:n1)\n";
            const auto sc = core::scan(t);
            check("links: each link reports the line it is on",
                  sc.links.size() == 3 && sc.links[0].line == 0 &&
                      sc.links[1].line == 1 && sc.links[2].line == 2);
        }
    }

    // -- Tags: read-only, D4 not yet answered ---------------------------------
    // The drawer SHOWS these; nothing edits them, because an editor is a
    // commitment to a storage location and D4 is open. Scanning commits nothing.
    {
        {
            const std::string t = "an #idea about #deep/work and C# and #1 thing\n";
            const auto sc = core::scan(t);
            check("tags: hashes after a space before a letter are tags",
                  sc.tags.size() == 2);
            check("tags: the name comes back without the hash",
                  sc.tags.size() == 2 && sc.tags[0].name == "idea" &&
                      sc.tags[1].name == "deep/work");
            check("tags: C# is not a tag (no space before the hash)", [&] {
                for (const auto& g : sc.tags)
                    if (g.begin > 0 && t[(std::size_t)(g.begin - 1)] == 'C') return false;
                return true;
            }());
            check("tags: #1 is not a tag (a digit, not a letter)", [&] {
                for (const auto& g : sc.tags)
                    if (g.name == "1") return false;
                return true;
            }());
        }
        {
            const std::string t = "# Heading\nbody #real\n";
            const auto sc = core::scan(t);
            check("tags: a heading's hash is not a tag",
                  sc.tags.size() == 1 && sc.tags[0].name == "real" &&
                      sc.tags[0].line == 1);
        }
        {
            const std::string t = "file this under #inbox. Then #next-up, done.\n";
            const auto sc = core::scan(t);
            check("tags: trailing sentence punctuation is not part of the tag",
                  sc.tags.size() == 2 && sc.tags[0].name == "inbox" &&
                      sc.tags[1].name == "next-up");
        }
        {
            const auto code = core::scan("a `#nope` here\n");
            check("tags: a hash inside a code span is not a tag", code.tags.empty());
            const auto fenced = core::scan("```\n#alsonope\n```\n");
            check("tags: a hash inside a fence is not a tag", fenced.tags.empty());
        }
    }

    // -- The backlink index ---------------------------------------------------
    // The failure this exists to make visible: an index that goes STALE. A
    // backlink to a note that no longer points here renders, clicks, and goes
    // somewhere -- a wrong answer wearing a right one's clothes, which nothing
    // on screen would ever reveal. So it is answered here.
    {
        core::MemoryNodes m;
        const auto a = m.create("", "A");
        const auto b = m.create("", "B");
        const auto c = m.create(a, "C");
        m.set_body(a, "see [Bee](jot:" + b + ") and [Cee](jot:" + c + ")\n");
        m.set_body(b, "back to [Ay](jot:" + a + ")\n");
        m.set_body(c, "no links here\n");

        core::LinkIndex ix;
        ix.rebuild(m);

        check("index: out-edges are per note", ix.outgoing(a).size() == 2 &&
                                                   ix.outgoing(b).size() == 1 &&
                                                   ix.outgoing(c).empty());
        check("index: backlinks are found from the other side",
              ix.incoming(b).size() == 1 && ix.incoming(b)[0].from == a);
        check("index: a note with no backlinks reports none",
              ix.incoming(c).size() == 1 && ix.incoming(a).size() == 1);
        check("index: the label travels with the edge",
              ix.incoming(b).size() == 1 && ix.incoming(b)[0].label == "Bee");
        check("index: total edge count", ix.edge_count() == 3);

        // THE ONE THAT MATTERS. A drops its mention of B. B's backlinks must
        // lose it -- in the map nobody was looking at.
        m.set_body(a, "just [Cee](jot:" + c + ") now\n");
        ix.update(m, a);
        check("index: a removed mention removes the backlink",
              ix.incoming(b).empty());
        check("index: the mention that stayed is still there",
              ix.incoming(c).size() == 1 && ix.incoming(c)[0].from == a);
        check("index: the edge count follows", ix.edge_count() == 2);

        // An edited LABEL must not leave the old edge behind. Matching mirrors
        // by value instead of by source id is exactly how it would.
        m.set_body(a, "just [See!](jot:" + c + ") now\n");
        ix.update(m, a);
        check("index: an edited label does not duplicate the backlink",
              ix.incoming(c).size() == 1 && ix.incoming(c)[0].label == "See!");

        // A note linking to the same target three times is three mentions.
        m.set_body(b, "[1](jot:" + c + ") [2](jot:" + c + ") [3](jot:" + c + ")\n");
        ix.update(m, b);
        check("index: repeated mentions are not collapsed",
              ix.incoming(c).size() == 4);

        // Deleting the SOURCE removes its edges. Deleting a TARGET leaves the
        // bodies that mention it alone -- those notes really do still say so,
        // and the drawer showing a dangling link is the truth.
        m.remove(b);
        ix.erase(b);
        check("index: deleting a note drops the edges it owned",
              ix.incoming(c).size() == 1 && ix.outgoing(b).empty());
        m.set_body(a, "[Cee](jot:" + c + ") and [ghost](jot:nope)\n");
        ix.update(m, a);
        check("index: a link to a node that does not exist is still recorded",
              ix.outgoing(a).size() == 2);
        check("index: a nonexistent target has the edge pointing at it",
              ix.incoming("nope").size() == 1);

        // A note linking to itself is a typo, not a relationship.
        m.set_body(c, "[me](jot:" + c + ")\n");
        ix.update(m, c);
        check("index: a self-link is not an edge", ix.outgoing(c).empty());

        // Rebuild must land on the same answer an incremental walk did, or one
        // of the two paths is wrong and only a restart would show which.
        core::LinkIndex fresh;
        fresh.rebuild(m);
        check("index: rebuild agrees with incremental update",
              fresh.edge_count() == 2 &&
                  fresh.incoming(c).size() == ix.incoming(c).size());
    }

    // -- Prefs: the layout pump ----------------------------------------------
    // Second pump on the Recents shape. The bar is the same: what you save is
    // what you load, a missing file is defaults rather than a failure, and a
    // hand-mangled file never throws across the seam.
    {
        const auto tmp = std::filesystem::temp_directory_path() / "jot_selftest_prefs";
        std::filesystem::remove_all(tmp);
        const std::string file = (tmp / "prefs.json").string();

        const core::Prefs d = core::load_prefs(file);
        check("prefs: a missing file loads defaults",
              d.show_tree && !d.show_drawer && d.tree_width == 280);
        check("prefs: due notifications are ON until turned off",
              core::Prefs{}.notify_due);
        check("prefs: the desktop projection is OFF until asked for",
              !d.desktop_tasks);
        // OFF by default for a different reason than the projection's: this one
        // changes what the X does, and every other application on the machine
        // exits when you close its last window.
        check("prefs: jot does NOT stay resident until asked to",
              !d.background && !core::Prefs{}.background);
        check("prefs: a first run gets a window size, not a zero",
              d.win_width == 940 && d.win_height == 620 && !d.win_maximized);

        core::Prefs p;
        p.show_tree = false;
        p.show_drawer = true;
        p.tree_width = 333;
        p.note_width = 777;
        p.desktop_tasks = true;
        p.background = true;
        p.win_width = 1440;
        p.win_height = 900;
        p.win_maximized = true;
        p.drawer_open = {{"links", false}, {"file", true}};
        check("prefs: save creates its directory", core::save_prefs(file, p));

        const core::Prefs r = core::load_prefs(file);
        check("prefs: round-trips every field",
              r.show_tree == p.show_tree && r.show_drawer == p.show_drawer &&
                  r.tree_width == p.tree_width && r.note_width == p.note_width &&
                  r.desktop_tasks == p.desktop_tasks &&
                  r.background == p.background &&
                  r.win_width == p.win_width && r.win_height == p.win_height &&
                  r.win_maximized == p.win_maximized);
        // s016a. Both directions, because a stored false is the case that
        // matters: it is the user overriding a section that defaults open.
        check("prefs: drawer section states round-trip, closed and open",
              r.drawer_open.size() == 2 && r.drawer_open.at("links") == false &&
                  r.drawer_open.at("file") == true);
        check("prefs: a first run has chosen no drawer sections",
              d.drawer_open.empty());
        {
            std::ofstream f(file);
            f << R"({"drawer_open":{"tags":false,"file":"yes","links":true},"show_tree":false})";
        }
        const core::Prefs mixed = core::load_prefs(file);
        check("prefs: a wrong-typed drawer entry drops alone, not the rest",
              mixed.drawer_open.count("file") == 0 && mixed.drawer_open.at("tags") == false &&
                  mixed.drawer_open.at("links") == true && !mixed.show_tree);
        { std::ofstream f(file); f << R"({"drawer_open":[1,2,3]})"; }
        check("prefs: a drawer_open that is not an object is ignored",
              core::load_prefs(file).drawer_open.empty());

        // A size can outlive the monitor it was stored on, and a window wider
        // than any display is one you cannot reach the edges of to fix.
        {
            std::ofstream f(file);
            f << R"({"win_width":99999,"win_height":-4,"desktop_tasks":true})";
        }
        const core::Prefs wild = core::load_prefs(file);
        check("prefs: an impossible window size falls back per field",
              wild.win_width == 940 && wild.win_height == 620 && wild.desktop_tasks);

        { std::ofstream f(file); f << "{ not json at all"; }
        const core::Prefs junk = core::load_prefs(file);
        check("prefs: an unparseable file loads defaults instead of throwing",
              junk.show_tree && !junk.show_drawer);

        { std::ofstream f(file); f << R"({"show_tree": "yes", "tree_width": 99999})"; }
        const core::Prefs weird = core::load_prefs(file);
        check("prefs: a wrong type and an absurd width fall back per field",
              weird.show_tree && weird.tree_width == 280);

        std::filesystem::remove_all(tmp);
    }

    // ── s009: the desktop projection, the half that is a pure function ──────
    // What reaches the desktop is decided here and written by Desktop.cpp, and
    // these checks are the reason that split exists: the writing side can only
    // be observed on a real session bus, so everything that can be a decision
    // instead of a side effect is one.
    {
        std::cout << "\n-- the desktop projection --\n";

        const std::int64_t now = 1789300000;   // a fixed instant; no clock here

        core::MemoryNodes m;
        const auto proj = m.create("", "Taxes");
        m.set_body(proj, "# Taxes\nThe folder on the desk, before the 15th.\n");
        m.set_status(proj, core::Status::Sequential);
        // The project carries the date; the steps inherit it. That is the
        // case worth testing -- a step with no date of its own still has a day.
        m.set_due(proj, core::day_end(now));

        const auto first = m.create(proj, "Gather the receipts");
        m.make_task(first, true);
        m.set_due(first, core::day_end(now));

        const auto second = m.create(proj, "Fill the form");
        m.make_task(second, true);                 // BLOCKED behind `first`

        const auto flagged = m.create("", "Call the accountant");
        m.make_task(flagged, true);
        m.set_due(flagged, now + 3600);
        m.set_flagged(flagged, true);

        const auto undated = m.create("", "Think about the shed");
        m.make_task(undated, true);

        const auto finished = m.create("", "Pay the water bill");
        m.make_task(finished, true);
        m.set_due(finished, core::day_end(now));
        m.set_done(finished, true);

        m.create("", "A plain note");

        core::TaskIndex idx;
        idx.rebuild(m);
        auto items = core::project(m, idx, now);

        check("projection: an undated todo does not reach the desktop",
              std::none_of(items.begin(), items.end(),
                           [&](const core::Projected& p) { return p.id == undated; }));
        check("projection: a done todo does not reach the desktop",
              std::none_of(items.begin(), items.end(),
                           [&](const core::Projected& p) { return p.id == finished; }));
        check("projection: a note does not reach the desktop", items.size() == 3);

        // The interesting one. `second` has no date of its own and is BLOCKED --
        // it projects anyway, on the date it inherits, because the calendar is
        // a day grid and availability is jot's question, not the desktop's.
        const core::Projected* blocked = nullptr;
        for (const auto& p : items) if (p.id == second) blocked = &p;
        check("projection: a blocked todo still reaches the desktop", blocked != nullptr);

        check("projection: the uid is stable and marks the row as ours",
              !items.empty() && items[0].uid == core::projection_uid(items[0].id) &&
              items[0].uid.rfind(core::projection_prefix(), 0) == 0);

        const core::Projected* allday = nullptr;
        const core::Projected* timed  = nullptr;
        for (const auto& p : items) {
            if (p.id == first)   allday = &p;
            if (p.id == flagged) timed  = &p;
        }
        check("projection: a bare date is recognised as all-day",
              allday && allday->all_day && allday->start == core::day_start(now));
        check("projection: a date with a time is not all-day, and starts at its time",
              timed && !timed->all_day && timed->start == now + 3600);
        check("projection: the flag rides along", timed && timed->flagged);

        // The WHY: the parent's prose, not its heading.
        check("projection: the description carries the parent's first prose line",
              allday &&
              allday->detail.find("The folder on the desk") != std::string::npos &&
              allday->detail.find("# Taxes") == std::string::npos);
        check("projection: availability is a WORD, not a filter",
              blocked && blocked->detail.rfind("Blocked", 0) == 0);

        // Overdue is relative to the instant it was asked at, and nothing else.
        const auto later = core::project(m, idx, now + 86400 * 2);
        check("projection: what is overdue depends only on `now`",
              !allday->overdue &&
              std::any_of(later.begin(), later.end(),
                          [&](const core::Projected& p) { return p.id == first && p.overdue; }));

        // ── the fingerprint ────────────────────────────────────────────────
        const std::string sig = core::projection_signature(items);
        check("projection: the same model gives the same fingerprint",
              sig == core::projection_signature(core::project(m, idx, now)));

        m.set_body(undated, "a thought about the shed that nobody will ever read");
        idx.update(m, undated);
        check("projection: editing a body the desktop cannot see changes nothing",
              sig == core::projection_signature(core::project(m, idx, now)));

        m.set_title(first, "Gather the receipts, all of them");
        idx.update(m, first);
        check("projection: renaming a projected todo DOES change the fingerprint",
              sig != core::projection_signature(core::project(m, idx, now)));
    }

    // ── s011: due notifications, the half that is a pure function ───────────
    // The same split as the projection above, one surface further on. The
    // interesting checks are the two ways this rule DIFFERS from that one:
    // availability filters here, and "already said" is remembered here.
    {
        std::cout << "\n-- due notifications --\n";

        const std::int64_t now = 1789300000;

        core::MemoryNodes m;
        const auto proj = m.create("", "Taxes");
        m.set_body(proj, "# Taxes\nThe folder on the desk, before the 15th.\n");
        m.set_status(proj, core::Status::Sequential);

        const auto first = m.create(proj, "Gather the receipts");
        m.make_task(first, true);
        m.set_due(first, now - 60);                 // due a minute ago -- AVAILABLE

        const auto second = m.create(proj, "Fill the form");
        m.make_task(second, true);
        m.set_due(second, now - 60);                // due, but BLOCKED behind `first`

        const auto later = m.create("", "Call the accountant");
        m.make_task(later, true);
        m.set_due(later, now + 3600);               // due in an hour -- not yet

        const auto deferred = m.create("", "Renew the permit");
        m.make_task(deferred, true);
        m.set_due(deferred, now - 60);
        m.set_defer(deferred, now + 86400);         // due, but DEFERRED

        const auto undated = m.create("", "Think about the shed");
        m.make_task(undated, true);

        m.create("", "A plain note");

        core::TaskIndex idx;
        idx.rebuild(m);

        auto r = core::due_announcements(m, idx, now, {});

        check("notify: a due available todo is announced",
              r.to_show.size() == 1 && r.to_show[0].id == first);
        // THE ASYMMETRY WITH THE PROJECTION, and the reason this file exists
        // separately from Projection.cpp. The calendar card shows both of
        // these, on purpose; an interruption about something you cannot act on
        // is what teaches a person to turn notifications off.
        // Identity, not counts: a count assertion passes for the wrong reason
        // the first time the fixture grows a node.
        const auto announced_ids = [](const core::AnnounceResult& a) {
            std::vector<core::NodeId> v;
            for (const auto& x : a.to_show) v.push_back(x.id);
            return v;
        };
        // NodeId IS a std::string, so one lambda covers both the id list and
        // the key list.
        const auto has = [](const std::vector<std::string>& v, const std::string& x) {
            return std::find(v.begin(), v.end(), x) != v.end();
        };
        const auto projects = [&](const core::NodeId& id) {
            const auto items = core::project(m, idx, now);
            return std::any_of(items.begin(), items.end(),
                               [&](const core::Projected& p) { return p.id == id; });
        };

        check("notify: a BLOCKED todo stays quiet where the projection shows it",
              projects(second) && !has(announced_ids(r), second));
        check("notify: a DEFERRED todo stays quiet too, and still projects",
              projects(deferred) && !has(announced_ids(r), deferred));
        check("notify: a deadline that has not arrived says nothing",
              !has(announced_ids(r), later));
        check("notify: an undated todo has no deadline to arrive",
              !has(announced_ids(r), undated) &&
              r.live == std::vector<std::string>{core::announce_key(first, now - 60)});

        // ── s015: KEEP IS NOT LIVE, and that is the milestone ──────────────
        // Nothing has been delivered yet, so nothing is in the announced set --
        // even though jot is about to speak about `first`. Before s015 this
        // vector held the key already, which is how a notification the daemon
        // dropped was recorded as said.
        check("notify: a key that was only SENT is not in the announced set",
              r.keep.empty() && r.live.size() == 1);

        // Said once, and then not again -- the whole difference between state
        // and an event. The announced set passed in is what DELIVERY produced.
        auto again = core::due_announcements(m, idx, now, r.live);
        check("notify: a DELIVERED todo is not announced twice",
              again.to_show.empty() && again.keep == r.live);

        // And the other half of the same rule: a deadline whose receipt never
        // came back is offered again. No retry machinery anywhere -- the tick
        // is the retry, because the key simply is not in the delivered set.
        auto retry = core::due_announcements(m, idx, now, {});
        check("notify: an UNDELIVERED todo is offered again next tick",
              retry.to_show.size() == 1 && retry.to_show[0].id == first &&
              retry.keep.empty());

        // Rescheduling is the most common edit a todo gets, and keying by id
        // alone would have made it silent.
        m.set_due(first, now - 30);
        idx.update(m, first);
        auto moved = core::due_announcements(m, idx, now, r.live);
        check("notify: moving the due date announces again",
              moved.to_show.size() == 1 && moved.to_show[0].id == first);
        check("notify: and the old key is pruned rather than kept forever",
              moved.keep.empty() && moved.live.size() == 1 &&
              moved.live[0] != r.live[0]);

        // Ticking it off empties the set, so re-opening it announces again --
        // which is correct: it became a thing to do again.
        m.set_done(first, true);
        idx.update(m, first);
        auto done = core::due_announcements(m, idx, now, moved.live);
        check("notify: a finished todo leaves the announced set",
              !has(done.keep, core::announce_key(first, now - 30)) &&
              !has(done.live, core::announce_key(first, now - 30)) &&
              !has(announced_ids(done), first));
        // `second` is no longer blocked once `first` is done, and it was already
        // due -- so the sequential project hands the notification on by itself,
        // which is the behaviour that makes this worth having at all.
        check("notify: finishing a step makes the NEXT one due and announced",
              done.live == std::vector<std::string>{core::announce_key(second, now - 60)} &&
              has(announced_ids(done), second));

        // Overdue is a DAY, not a second. A task due at 17:00 is not overdue at
        // 17:01, or every notification jot ever sends would be an overdue one.
        auto fresh = core::due_announcements(m, idx, now, {});
        check("notify: due a minute ago is not yet OVERDUE",
              fresh.to_show.size() == 1 && !fresh.to_show[0].overdue);
        auto old = core::due_announcements(m, idx, now + 86400 * 2, {});
        const auto* late = [&]() -> const core::Announcement* {
            for (const auto& a : old.to_show) if (a.id == second) return &a;
            return nullptr;
        }();
        check("notify: due two days ago IS overdue", late && late->overdue);
        // Two days on, the deferred one has come round by itself. Nothing had
        // to poke it -- availability is derived, so the clock alone is enough.
        check("notify: a defer date that has passed stops being a reason to be quiet",
              has(announced_ids(old), deferred));

        check("notify: the body carries the why, not the availability word",
              fresh.to_show[0].detail.find("Taxes") != std::string::npos &&
              fresh.to_show[0].detail.find("Available") == std::string::npos);
    }

    // ── s015: the outbox -- a send is not a delivery ────────────────────────
    // The ledger for the gap between asking the notification service and being
    // answered. It is pure and it is tested here because the thing it prevents
    // is INVISIBLE from the surface: a notification that was never shown, marked
    // as announced, and therefore never mentioned again. Nothing on screen would
    // say so -- the same argument as availability in core::Tasks.
    {
        std::cout << "\n-- notification receipts (outbox) --\n";

        core::Outbox box;
        const std::string a = "node-a@1789300000";
        const std::string b = "node-b@1789300000";

        check("outbox: an empty ledger has nothing in flight",
              box.size() == 0 && !box.in_flight(a) && box.tries(a) == 0);

        check("outbox: a first ask goes", box.begin(a) && box.in_flight(a));
        // The tick fires every sixty seconds. A daemon that has not answered
        // yet must not be asked again, or one deadline becomes four rows.
        check("outbox: a second ask while the first is in flight is refused",
              !box.begin(a) && box.size() == 1);

        // Delivered. The key leaves the ledger entirely -- from here the
        // ANNOUNCED SET is what keeps it quiet, and two records of the same
        // fact would be one too many.
        box.succeed(a);
        check("outbox: a delivered key leaves the ledger",
              box.size() == 0 && !box.in_flight(a));

        // Refused. Not in flight any more, so the next tick may ask again --
        // which is the entire retry mechanism: there isn't one.
        check("outbox: a refusal ends the flight and may be retried",
              box.begin(a) && box.fail(a) == core::Outbox::After::Retry &&
              !box.in_flight(a) && box.tries(a) == 1);

        // ... but not forever. A service that says no five times in five
        // minutes is not busy.
        for (int i = 1; i < core::kDeliveryTries - 1; ++i) {
            box.begin(a);
            check("outbox: still retrying while tries remain",
                  box.fail(a) == core::Outbox::After::Retry);
        }
        box.begin(a);
        check("outbox: the last try gives up rather than asking forever",
              box.fail(a) == core::Outbox::After::GiveUp);
        check("outbox: and giving up clears the key, because the caller now "
              "marks it announced",
              box.size() == 0 && box.tries(a) == 0);

        // Ticking a todo off mid-flight must not leave a count behind for a
        // rescheduled version of it to inherit. Pruned against the same LIVE
        // set the announced set is pruned against -- one definition of "still
        // a deadline", two consumers.
        box.begin(a);
        box.begin(b);
        box.fail(b);
        check("outbox: pruning drops what is no longer a deadline",
              box.size() == 2 && (box.prune({b}), box.size() == 1) &&
              !box.in_flight(a) && box.tries(b) == 1);
        check("outbox: and a key that survives the prune keeps its tries",
              box.tries(b) == 1);

        // A receipt for something never asked about -- the menu diagnostic, or
        // a stale reply after a prune. It must not manufacture an entry.
        core::Outbox fresh_box;
        check("outbox: failing an unknown key invents nothing",
              fresh_box.fail("who?") == core::Outbox::After::Retry &&
              fresh_box.size() == 0);
        fresh_box.succeed("who?");
        check("outbox: succeeding an unknown key is harmless too",
              fresh_box.size() == 0);
    }

    // ── first_prose_line -- one definition, two consumers (s009) ────────────
    {
        check("prose line: headings are skipped",
              core::first_prose_line("# Title\n\nthe real line\n") == "the real line");
        check("prose line: a body with nothing but headings has no prose line",
              core::first_prose_line("# One\n## Two\n").empty());
        check("prose line: an empty body is empty, not a crash",
              core::first_prose_line("").empty());
        const std::string wide(200, 'a');
        check("prose line: a long line is cut and marked",
              core::first_prose_line(wide).size() < wide.size() &&
              core::first_prose_line(wide).find("\u2026") != std::string::npos);
        // Codepoints, not bytes: a cut counted in bytes lands inside a
        // character and the result is not valid UTF-8.
        std::string dashes;
        for (int i = 0; i < 120; ++i) dashes += "\u2014";
        const auto cut = core::first_prose_line(dashes);
        check("prose line: the cut counts characters, not bytes",
              cut.size() % 3 == 0 && cut.size() < dashes.size());
    }

    // -- Lifecycle: what a close means, and what a quit means ---------------
    // The milestone's real work, and the reason it is a truth table rather than
    // a branch in the close handler: two of the three answers differ only in
    // whether unsaved notes are about to be lost, and the wrong one is SILENT.
    // Every row is asserted by IDENTITY -- which answer came back -- because a
    // count assertion is a test that passes for the wrong reason (s011 banked
    // that one the hard way).
    {
        using core::Lifecycle;
        using core::OnClose;
        using core::OnQuit;

        // The three plain readings of the X.
        check("close: with nothing resident and nothing unsaved, the X exits",
              core::on_close(Lifecycle{}) == OnClose::Exit);
        check("close: unsaved notes and no residency -- ask before losing them",
              core::on_close(Lifecycle{false, false, false, true}) == OnClose::AskFirst);
        check("close: residency on -- hide it, the process keeps the clock",
              core::on_close(Lifecycle{true, false, false, false}) == OnClose::StayResident);

        // THE INVERSION THIS MILESTONE EXISTS FOR. Residency outranks the
        // scratch prompt: nothing is being lost, so asking would be a lie, and
        // a dialog that cries wolf is one the user learns to dismiss before the
        // day it matters.
        check("close: residency does NOT prompt about scratch -- nothing is lost yet",
              core::on_close(Lifecycle{true, false, false, true}) == OnClose::StayResident);

        // ...and the other half of that trade: the ask has to land SOMEWHERE.
        check("quit: unsaved notes -- the quit is what asks now",
              core::on_quit(Lifecycle{true, false, false, true}) == OnQuit::AskFirst);
        check("quit: residency makes no difference to what Quit means",
              core::on_quit(Lifecycle{true, false, false, true}) ==
                  core::on_quit(Lifecycle{false, false, false, true}));
        check("quit: nothing unsaved -- go, without a dialog nobody needed",
              core::on_quit(Lifecycle{true, false, false, false}) == OnQuit::Exit);

        // A quit already under way drives the close that follows it. If
        // residency won here, the Quit item would hide the window instead of
        // destroying it and jot would refuse to stop -- from the one menu GNOME
        // offers for stopping it.
        check("close: a quit in progress beats residency, or Quit could never quit",
              core::on_close(Lifecycle{true, true, false, true}) == OnClose::Exit);
        check("quit: a quit in progress does not ask a second time",
              core::on_quit(Lifecycle{true, true, false, true}) == OnQuit::Exit);

        // The prompt's own continuation: answered once, not asked again.
        check("close: the answered prompt closes rather than re-opening itself",
              core::on_close(Lifecycle{false, true, true, true}) == OnClose::Exit &&
              core::on_close(Lifecycle{false, false, true, true}) == OnClose::Exit);
        check("quit: answered and forced goes straight out",
              core::on_quit(Lifecycle{false, false, true, true}) == OnQuit::Exit);
    }

    // -- Hotkey: the global capture shortcut ------------------------------------
    // Two jobs, and the second is the one with teeth. Judging a CHORD matters
    // because a global grab on a bare letter eats that letter everywhere on the
    // desktop, including in the dialog you would use to undo it. But the list
    // algebra matters more: `custom-keybindings` is gnome-settings-daemon's,
    // and it holds every shortcut the user made by hand. jot adding or removing
    // its own entry must leave all of those untouched, in order -- a bug there
    // costs somebody else's work, not jot's feature.
    {
        // Canonical form: the same chord, spelled four ways.
        check("hotkey: <Primary> is <Control>",
              core::accels_equal("<Primary>j", "<Control>j"));
        check("hotkey: case and key case do not make two shortcuts",
              core::accels_equal("<ctrl><SHIFT>N", "<Control><Shift>n"));
        check("hotkey: <Meta> and <Mod4> are Super",
              core::accels_equal("<Meta>space", "<Super>space"));
        check("hotkey: modifier ORDER does not make two shortcuts",
              core::accels_equal("<Shift><Control>n", "<Control><Shift>n"));
        check("hotkey: a named key keeps its case (F5 is not f5)",
              core::canonical_accel("<Control>F5") == "<Control>F5");
        check("hotkey: different chords are different",
              !core::accels_equal("<Control>j", "<Control><Alt>j"));
        // Nonsense never equals anything -- including other nonsense, which is
        // what stops a conflict scan reporting every unparseable dconf string
        // as a clash with every other one.
        check("hotkey: an empty accel matches nothing",
              !core::accels_equal("", "") && !core::accels_equal("<Control>", "<Control>"));
        check("hotkey: a bare modifier has no canonical form",
              core::canonical_accel("<Control>").empty());

        // The judgement. Empty objection == fit to be a global shortcut.
        check("hotkey: Ctrl+Alt+J is acceptable",
              core::hotkey_objection("<Control><Alt>j").empty());
        check("hotkey: Super+N is acceptable",
              core::hotkey_objection("<Super>n").empty());
        check("hotkey: a bare letter is REFUSED",
              !core::hotkey_objection("n").empty());
        check("hotkey: Shift alone does not qualify -- <Shift>n is the letter N",
              !core::hotkey_objection("<Shift>n").empty());
        check("hotkey: a function key stands alone (nothing else types F9)",
              core::hotkey_objection("F9").empty());
        check("hotkey: Escape is refused however it is dressed up",
              !core::hotkey_objection("<Control><Alt>Escape").empty());
        check("hotkey: Ctrl+Return is refused -- the desktop needs Return",
              !core::hotkey_objection("<Control>Return").empty());

        // The slot. The trailing slash is load-bearing: a relocatable GSettings
        // path without one is silently ignored by gnome-settings-daemon.
        const std::string mine = core::jot_slot_path();
        check("hotkey: the slot path ends in a slash",
              !mine.empty() && mine.back() == '/');
        check("hotkey: the slot path is under GNOME's custom-keybindings",
              mine.rfind(core::custom_keybindings_prefix(), 0) == 0);

        // ── the list algebra -- the part that touches someone else's data ──
        const std::vector<std::string> theirs = {
            "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom0/",
            "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom1/",
        };
        auto added = core::with_slot(theirs, mine);
        check("hotkey: adding ours keeps every stranger's entry, in order",
              added.size() == 3 && added[0] == theirs[0] && added[1] == theirs[1]);
        check("hotkey: ours is appended", added.back() == mine);
        check("hotkey: adding twice is idempotent",
              core::with_slot(added, mine).size() == 3);

        // A hand-edited or half-written list can hold ours twice; the fix is
        // silent and touches nothing else.
        std::vector<std::string> doubled = {theirs[0], mine, theirs[1], mine};
        auto fixed = core::with_slot(doubled, mine);
        check("hotkey: a duplicate of OURS is collapsed to one",
              fixed.size() == 3 && fixed[0] == theirs[0] && fixed[1] == mine &&
                  fixed[2] == theirs[1]);

        auto removed = core::without_slot(added, mine);
        check("hotkey: removing ours leaves the strangers exactly as they were",
              removed == theirs);
        check("hotkey: removing when we were never there changes nothing",
              core::without_slot(theirs, mine) == theirs);
        check("hotkey: has_slot sees ours and not theirs",
              core::has_slot(added, mine) && !core::has_slot(theirs, mine));

        // The command. `--capture` with NO text is the form that presents.
        check("hotkey: the command is --capture with nothing after it",
              core::capture_command("/home/s/jot/build/jot") ==
                  "/home/s/jot/build/jot --capture");
        // gnome-settings-daemon shell-parses the string, so a path with a space
        // in it must survive -- otherwise the key spawns the wrong thing and
        // fails at a keypress, with no terminal to say so.
        check("hotkey: a path with a space is quoted",
              core::capture_command("/home/s/my builds/jot") ==
                  "'/home/s/my builds/jot' --capture");
        check("hotkey: an apostrophe in the path survives quoting",
              core::capture_command("/home/o'neil/jot") ==
                  "'/home/o'\\''neil/jot' --capture");
        check("hotkey: our command is recognised wherever jot was built",
              core::looks_like_capture_command("/opt/jot/jot --capture") &&
                  core::looks_like_capture_command("'/home/s/my builds/jot' --capture"));
        check("hotkey: a stranger's command is not mistaken for ours",
              !core::looks_like_capture_command("/usr/bin/scanner --capture") &&
                  !core::looks_like_capture_command("/opt/jot/jot --help"));
    }

    std::cout << "-----------------------------------------------\n";

    // -- Recents: list ops + JSON round-trip with prune -----------------------
    {
        std::vector<std::string> l;
        core::recents_add(l, "/a");
        core::recents_add(l, "/b");
        core::recents_add(l, "/c");
        check("recents: newest first", l.size() == 3 && l.front() == "/c");
        core::recents_add(l, "/a");
        check("recents: re-add moves to front (no dup)",
              l.size() == 3 && l.front() == "/a");
        core::recents_add(l, "/x/");
        check("recents: trailing slash normalised",
              std::find(l.begin(), l.end(), "/x") != l.end() &&
              std::find(l.begin(), l.end(), "/x/") == l.end());
        std::vector<std::string> cap;
        for (int i = 0; i < 20; ++i)
            core::recents_add(cap, "/p" + std::to_string(i), 8);
        check("recents: trim to max (8)", cap.size() == 8 && cap.front() == "/p19");

        const std::string base =
            (std::filesystem::temp_directory_path() / "jot_selftest_recents").string();
        std::error_code ec;
        std::filesystem::remove_all(base, ec);
        const std::string live = base + "/live";
        std::filesystem::create_directories(live, ec);
        const std::string file = base + "/recent.json";

        std::vector<std::string> to_save = {live, base + "/GONE"};
        check("recents: save ok", core::save_recents(file, to_save));
        auto loaded = core::load_recents(file);
        check("recents: load prunes the missing path",
              loaded.size() == 1 && loaded.front() == live);
        check("recents: missing file -> empty",
              core::load_recents(base + "/nope.json").empty());
    }

    // -- TextMap: forward/inverse round-trip ----------------------------------
    {
        // A scene as a viewer flattens it:
        //   0 "The Cellar"     title  (non-prose)
        //   1 ""               gap    (non-prose)
        //   2 "She opened the" prose, para_start   <- para 1, wrap 1
        //   3 "old iron door." prose               <- para 1, wrap 2
        //   4 "Caf\u00e9 dust." prose, para_start  <- para 2 (UTF-8: é)
        std::vector<core::FlatLine> flat = {
            {"The Cellar",     false, false},
            {"",               false, false},
            {"She opened the", true,  true },
            {"old iron door.", true,  false},
            {"Caf\xC3\xA9 dust.", true, true },
        };
        roundtrip(flat, 2, 0, 2, 14, "single line");
        roundtrip(flat, 2, 4, 3, 8,  "multi-line (soft wrap)");
        roundtrip(flat, 2, 0, 4, 10, "cross-paragraph");
        roundtrip(flat, 4, 0, 4, 6,  "UTF-8 span (accented)");

        // Chrome is never landed on: a range covering all prose starts on line 2.
        std::vector<int> off;
        (void)core::visible_text(flat, off);
        core::LineCol s, e;
        core::map_range(flat, 0, 3, s, e);
        check("textmap: start snaps to prose, never chrome", s.line == 2);

        // Degenerate / empty inputs fail cleanly.
        check("textmap: empty range rejected", !core::map_range(flat, 5, 5, s, e));
        std::vector<core::FlatLine> no_prose = {{"Title", false, false}};
        check("textmap: no prose -> false", !core::map_range(no_prose, 0, 3, s, e));
    }
    // -- Shortcuts: the registry paradigm, exercised ---------------------------
    // A seed's claim isn't a fact until a consumer exercises it. The registry's
    // whole promise is that a key and its advertised row can't drift, because
    // both read one list -- so the guard that matters is pure and lives here:
    // no two chords may collide. (App wires from this same list; ShortcutsDialog
    // renders from it.)
    {
        namespace sc = jot::core;

        check("shortcuts: format_accel renders modifiers + key",
              sc::format_accel("<Ctrl><Shift>n") == "Ctrl+Shift+N",
              sc::format_accel("<Ctrl><Shift>n"));
        check("shortcuts: format_accel maps a named key to its glyph",
              sc::format_accel("<Ctrl>question") == "Ctrl+?",
              sc::format_accel("<Ctrl>question"));

        {
            sc::ShortcutSpec derived; derived.accels = {"<Ctrl>question", "<Ctrl>slash"};
            check("shortcuts: display_keys derives + joins accels",
                  derived.display_keys() == "Ctrl+?  /  Ctrl+/",
                  derived.display_keys());
            sc::ShortcutSpec doc; doc.keys = "Click a recent";
            check("shortcuts: display_keys prefers an explicit key string",
                  doc.display_keys() == "Click a recent");
        }

        check("shortcuts: registry is populated",
              !sc::shortcut_registry().empty(),
              std::to_string(sc::shortcut_registry().size()) + " specs");
        check("shortcuts: registry is accel-collision-free",
              sc::find_accel_collisions().empty(),
              sc::find_accel_collisions().empty()
                  ? "clean" : sc::find_accel_collisions().front());

        // Sections authored A-Z + contiguous: the dialog walks linearly and
        // starts a heading on change, so a stray out-of-order row would split a
        // section into two headings.
        {
            std::vector<std::string> secs;
            for (const auto& r : sc::shortcut_registry())
                if (secs.empty() || secs.back() != r.section) secs.push_back(r.section);
            std::vector<std::string> sorted = secs;
            std::sort(sorted.begin(), sorted.end());
            check("shortcuts: sections are contiguous and A-Z ordered", secs == sorted);
        }

        // The doc-only half of the paradigm: a gesture row documents without
        // binding (no accels), but must still show keys.
        {
            bool ok = true, saw_doc_only = false;
            for (const auto& r : sc::shortcut_registry())
                if (r.action.empty()) {
                    saw_doc_only = true;
                    if (!r.accels.empty() || r.keys.empty()) ok = false;
                }
            check("shortcuts: a doc-only row binds nothing but still displays keys",
                  ok && saw_doc_only);
        }
    }


    // -- Nodes: the model verbs behind the fixture surface ---------------------
    // The point of these is Notr's defect: a move must be one field write that
    // preserves identity and carries the subtree. Everything the UI can do to
    // the tree goes through these, so proving them headless proves the drag.
    {
        core::MemoryNodes m;
        int notifications = 0;
        m.on_changed([&](core::NodeSource::Change, const core::NodeId&) { ++notifications; });
        m.reset(core::fixture_nodes());

        check("nodes: fixtures load", m.count() == 11, std::to_string(m.count()));
        check("nodes: reset notifies once", notifications == 1);
        check("nodes: tree is a projection of parent links",
              m.children("").size() == 4 && m.children("n0003").size() == 2);

        // The move. n0004 (jot) carries two children; reparent it to Reference.
        const auto kids_before = m.children("n0004");
        check("nodes: move subtree accepted", m.move("n0004", "n0008"));
        const core::Node* moved = m.find("n0004");
        check("nodes: move preserves identity",
              moved != nullptr && moved->id == "n0004" && moved->parent_id == "n0008");
        check("nodes: children came along untouched", m.children("n0004") == kids_before);
        check("nodes: old parent lost exactly one child", m.children("n0003").size() == 1);

        // The cycle. Moving a node under its own descendant is the one move the
        // model must refuse; the surface relies on this rather than checking.
        std::string why;
        check("nodes: move into own subtree refused",
              !core::can_move(m, "n0004", "n0005", &why) && !why.empty(), why);
        check("nodes: move onto self refused", !m.move("n0004", "n0004"));
        check("nodes: is_descendant walks up the parent chain",
              core::is_descendant(m, "n0005", "n0008") &&
              !core::is_descendant(m, "n0008", "n0005"));

        // protect -- carried from Notr, and it has to hold against a parent
        // delete too, or it is decoration.
        check("nodes: protected node will not move", !m.move("n0009", ""));
        check("nodes: protected node will not delete", !m.remove("n0009"));
        check("nodes: delete vetoed by a protected descendant", !m.remove("n0008"));

        // create / edit / delete
        const core::NodeId fresh = m.create("n0001", "captured");
        check("nodes: create mints a new id under the parent",
              !fresh.empty() && m.find(fresh) && m.find(fresh)->parent_id == "n0001");
        check("nodes: set_body writes through", m.set_body(fresh, "- [ ] a task"));
        check("nodes: no-op write is refused (nothing to notify)",
              !m.set_body(fresh, "- [ ] a task"));
        const std::size_t before_delete = m.count();
        // n0003 kept only n0007 after the move above, so the cut is 2 nodes.
        check("nodes: delete takes the subtree", m.remove("n0003") &&
              m.count() == before_delete - 2 && !m.find("n0007"));

        // Sibling order is model state now: a drop between two rows names an
        // index, and these are the cases that gesture generates.
        {
            core::MemoryNodes o;
            o.reset(core::fixture_nodes());
            // n0003 Projects holds [n0004 jot, n0007 bindery].
            check("nodes: sibling_index reports position",
                  core::sibling_index(o, "n0007") == 1);
            check("nodes: move to an explicit index inserts there",
                  o.move("n0007", "n0003", 0) &&
                  o.children("n0003") == std::vector<core::NodeId>{"n0007", "n0004"});
            // The index a drop names is a PRE-removal position, so moving down
            // past one sibling is index 2, not 1. (The first draft of this test
            // said 1, and the model was right.)
            check("nodes: reorder within the same parent is a legal move",
                  o.move("n0007", "n0003", 2) &&
                  o.children("n0003") == std::vector<core::NodeId>{"n0004", "n0007"});
            check("nodes: dropping where it already is changes nothing",
                  !o.move("n0007", "n0003", 2) &&
                  o.children("n0003") == std::vector<core::NodeId>{"n0004", "n0007"});
            check("nodes: an index past the end appends rather than failing",
                  o.move("n0007", "n0008", 99) && o.children("n0008").back() == "n0007");
            check("nodes: index -1 appends",
                  o.move("n0007", "n0003", -1) && o.children("n0003").back() == "n0007");
        }

        check("nodes: dump renders every live node",
              core::dump(m).find("n0001") != std::string::npos &&
              core::dump(m).find("n0003") == std::string::npos);
    }

    // -- Nodes: the D6 instrument, model side ---------------------------------
    // A full rebuild is only cheap if the model's half is cheap. This measures
    // the walk the tree widget performs on every structural change; the widget
    // half is measured in the app, where there is a display.
    {
        core::MemoryNodes big;
        big.reset(core::stress_nodes(5000));
        check("nodes: stress set builds", big.count() == 5000);
        const auto t0 = std::chrono::steady_clock::now();
        std::size_t seen = 0;
        std::vector<core::NodeId> stack = big.children("");
        while (!stack.empty()) {
            const core::NodeId id = stack.back();
            stack.pop_back();
            ++seen;
            for (const auto& k : big.children(id)) stack.push_back(k);
        }
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t0).count();
        check("nodes: full 5000-node walk reaches every node", seen == 5000);
        check("nodes: full 5000-node walk is under 50ms", ms < 50.0,
              std::to_string(ms) + "ms");
    }


    // -- Markdown: the scan behind the body pane ------------------------------
    // D5 answered: Folio has no markdown editor to lift, so this is ours and it
    // gets the same treatment the node model got -- proven headless, before a
    // tag is applied to anything. Every check below is a question the editor
    // would otherwise be answering at runtime with no way to see it was wrong.
    {
        using core::Block;
        using core::Style;

        // A style's tag name is shared by the scan and the tag table. If these
        // ever diverge, styling silently stops happening, which is exactly the
        // drift the shortcut registry exists to prevent elsewhere.
        check("md: every style has a distinct tag name", [] {
            std::vector<std::string> names;
            for (auto s : core::all_styles()) names.push_back(core::tag_name(s));
            std::sort(names.begin(), names.end());
            return std::adjacent_find(names.begin(), names.end()) == names.end() &&
                   names.size() == core::all_styles().size();
        }());
        check("md: Mark is created last, so syntax stays visible inside styled runs",
              core::all_styles().back() == Style::Mark);

        // Counting helper: how many spans of a style cover a given text.
        const auto count_of = [](const core::Scan& sc, Style s) {
            return std::count_if(sc.spans.begin(), sc.spans.end(),
                                 [&](const core::Span& sp) { return sp.style == s; });
        };
        const auto text_of = [](const std::string& t, const core::Span& sp) {
            return t.substr(static_cast<std::size_t>(sp.begin),
                            static_cast<std::size_t>(sp.end - sp.begin));
        };
        const auto first_of = [&](const std::string& t, const core::Scan& sc, Style s) {
            for (const auto& sp : sc.spans)
                if (sp.style == s) return text_of(t, sp);
            return std::string{"<none>"};
        };

        // Block classification. One line each, because a block kind is a
        // property of the line and nothing else.
        {
            const std::string t =
                "# Title\n"
                "## Sub\n"
                "plain prose\n"
                "- a bullet\n"
                "- [ ] open task\n"
                "- [x] done task\n"
                "1. numbered\n"
                "> quoted\n"
                "---\n";
            const auto sc = core::scan(t);
            check("md: line index matches buffer line number",
                  sc.lines.size() == 10);   // 9 lines + the empty one after \n
            check("md: # is a heading at level 1",
                  sc.lines[0].block == Block::Heading && sc.lines[0].level == 1);
            check("md: ## is a heading at level 2",
                  sc.lines[1].block == Block::Heading && sc.lines[1].level == 2);
            check("md: unclaimed text is a paragraph",
                  sc.lines[2].block == Block::Paragraph);
            check("md: - is a bullet", sc.lines[3].block == Block::Bullet);
            check("md: - [ ] is a task, unchecked",
                  sc.lines[4].block == Block::Task && !sc.lines[4].checked);
            check("md: - [x] is a task, checked",
                  sc.lines[5].block == Block::Task && sc.lines[5].checked);
            check("md: 1. is a numbered item", sc.lines[6].block == Block::Numbered);
            check("md: > is a quote", sc.lines[7].block == Block::Quote);
            check("md: --- is a rule, not a bullet", sc.lines[8].block == Block::Rule);
            check("md: a checked task styles its text as done",
                  count_of(sc, Style::TaskDone) == 1);
        }

        // The checkbox range, which is what a click has to land in. Getting this
        // wrong means the box ticks the wrong note, or nothing at all.
        {
            const std::string t = "  - [x] buy milk";
            const auto sc = core::scan(t);
            check("md: a task reports its box range",
                  sc.lines[0].box_begin == 4 && sc.lines[0].box_end == 7);
            check("md: the box range is exactly the brackets",
                  t.substr(static_cast<std::size_t>(sc.lines[0].box_begin), 3) == "[x]");
            check("md: an indented task records its indent",
                  sc.lines[0].level == 2);
        }

        // Inline runs.
        {
            const std::string t = "a **bold** and *it* and `code` and ~~gone~~ here";
            const auto sc = core::scan(t);
            check("md: **bold** styles its content only",
                  first_of(t, sc, Style::Bold) == "bold");
            check("md: *italic* styles its content only",
                  first_of(t, sc, Style::Italic) == "it");
            check("md: `code` styles its content only",
                  first_of(t, sc, Style::Code) == "code");
            check("md: ~~strike~~ styles its content only",
                  first_of(t, sc, Style::Strike) == "gone");
            check("md: bold is not read as two italics",
                  count_of(sc, Style::Bold) == 1 && count_of(sc, Style::Italic) == 1);
        }

        // Links: the label is what the eye reads, the target is punctuation.
        {
            const std::string t = "see [the note](jot:n0004) for more";
            const auto sc = core::scan(t);
            check("md: a link styles its label",
                  first_of(t, sc, Style::Link) == "the note");
            check("md: a link's target is marked, not styled as text",
                  [&] {
                      for (const auto& sp : sc.spans)
                          if (sp.style == Style::Mark &&
                              text_of(t, sp) == "](jot:n0004)") return true;
                      return false;
                  }());
        }

        // The false positives that would make the pane annoying rather than
        // helpful. Each of these is a line a developer's notes file really
        // contains.
        {
            const auto sc1 = core::scan("call some_var_name in the loop");
            check("md: snake_case is not italic", count_of(sc1, Style::Italic) == 0);

            const auto sc2 = core::scan("a * lone star and one _ underscore");
            check("md: unpaired delimiters style nothing",
                  count_of(sc2, Style::Italic) == 0 && count_of(sc2, Style::Bold) == 0);

            const auto sc3 = core::scan("path C:\\\\temp\\\\thing");
            check("md: a stray backslash does not eat the line",
                  sc3.lines[0].block == Block::Paragraph);

            const auto sc4 = core::scan("2026 was a year");
            check("md: a bare number is not a numbered item",
                  sc4.lines[0].block == Block::Paragraph);

            const auto sc5 = core::scan("#hashtag not a heading");
            check("md: # without a space is not a heading",
                  sc5.lines[0].block == Block::Paragraph);
        }

        // Fences. The reason the scan is whole-buffer and not per-line: opening
        // a fence on one line changes what every line below it IS.
        {
            const std::string t =
                "intro\n"
                "```cpp\n"
                "auto **x = *p;\n"
                "# not a heading\n"
                "```\n"
                "outro **bold**\n";
            const auto sc = core::scan(t);
            check("md: the fence line is its own block",
                  sc.lines[1].block == Block::Fence && sc.lines[4].block == Block::Fence);
            check("md: lines inside a fence are code",
                  sc.lines[2].block == Block::Code && sc.lines[3].block == Block::Code);
            check("md: markdown inside a fence is inert",
                  count_of(sc, Style::Bold) == 1);   // only the one after the fence
            check("md: the fence closes, so text after it styles again",
                  sc.lines[5].block == Block::Paragraph);
        }

        // Byte offsets are what a string scanner produces; codepoint offsets are
        // what Gtk::TextBuffer wants. The conversion lives in core so a UTF-8
        // bug shows up HERE and not as "styling drifts right after an em dash".
        {
            const std::string t = "héllo — **wörld** ok";
            const auto sc = core::scan(t);
            check("md: codepoint offsets are shorter than byte offsets in UTF-8",
                  sc.lines[0].cp_end < sc.lines[0].end);
            check("md: a styled run's codepoint length matches its byte content",
                  [&] {
                      for (const auto& sp : sc.spans)
                          if (sp.style == Style::Bold)
                              return sp.cp_end - sp.cp_begin ==
                                     core::cp_len(t, sp.begin, sp.end) &&
                                     sp.cp_end - sp.cp_begin == 5;  // w-ö-r-l-d
                      return false;
                  }());
            check("md: multibyte content still styles the right text",
                  first_of(t, sc, Style::Bold) == "wörld");
        }
        {
            // Multi-line UTF-8: the one-pass conversion has to stay in step
            // across newlines, which is where an off-by-one would hide.
            const std::string t = "# Héading\nprose ünicode\n- [ ] tâche\n";
            const auto sc = core::scan(t);
            check("md: codepoint line starts stay in step across multibyte lines",
                  sc.lines[1].cp_begin == core::cp_len(t, 0, sc.lines[1].begin) &&
                  sc.lines[2].cp_begin == core::cp_len(t, 0, sc.lines[2].begin));
            check("md: a task box past multibyte text reports the right codepoints",
                  sc.lines[2].cp_box_begin == core::cp_len(t, 0, sc.lines[2].box_begin));
        }

        // The budget. A whole-buffer rescan runs on every idle after a
        // keystroke, so its cost is a typing-latency cost. This body is far
        // larger than a note will ever be; if it is fast here it is free there.
        {
            std::string big;
            big.reserve(400000);
            for (int i = 0; i < 4000; ++i) {
                big += "## Section " + std::to_string(i) + "\n";
                big += "some **bold** and `code` and a [link](jot:n0001) here\n";
                big += "- [ ] a task to do\n\n";
            }
            const auto t0 = std::chrono::steady_clock::now();
            const auto sc = core::scan(big);
            const double ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - t0).count();
            check("md: a 16000-line body scans completely",
                  sc.lines.size() == 16001);
            check("md: a 16000-line body scans in under 50ms", ms < 50.0,
                  std::to_string(ms) + "ms");
        }
    }

    // -- Lifecycle: what a close means, and what a quit means ---------------
    // The milestone's real work, and the reason it is a truth table rather than
    // a branch in the close handler: two of the three answers differ only in
    // whether unsaved notes are about to be lost, and the wrong one is SILENT.
    // Every row is asserted by IDENTITY -- which answer came back -- because a
    // count assertion is a test that passes for the wrong reason (s011 banked
    // that one the hard way).
    {
        using core::Lifecycle;
        using core::OnClose;
        using core::OnQuit;

        // The three plain readings of the X.
        check("close: with nothing resident and nothing unsaved, the X exits",
              core::on_close(Lifecycle{}) == OnClose::Exit);
        check("close: unsaved notes and no residency -- ask before losing them",
              core::on_close(Lifecycle{false, false, false, true}) == OnClose::AskFirst);
        check("close: residency on -- hide it, the process keeps the clock",
              core::on_close(Lifecycle{true, false, false, false}) == OnClose::StayResident);

        // THE INVERSION THIS MILESTONE EXISTS FOR. Residency outranks the
        // scratch prompt: nothing is being lost, so asking would be a lie, and
        // a dialog that cries wolf is one the user learns to dismiss before the
        // day it matters.
        check("close: residency does NOT prompt about scratch -- nothing is lost yet",
              core::on_close(Lifecycle{true, false, false, true}) == OnClose::StayResident);

        // ...and the other half of that trade: the ask has to land SOMEWHERE.
        check("quit: unsaved notes -- the quit is what asks now",
              core::on_quit(Lifecycle{true, false, false, true}) == OnQuit::AskFirst);
        check("quit: residency makes no difference to what Quit means",
              core::on_quit(Lifecycle{true, false, false, true}) ==
                  core::on_quit(Lifecycle{false, false, false, true}));
        check("quit: nothing unsaved -- go, without a dialog nobody needed",
              core::on_quit(Lifecycle{true, false, false, false}) == OnQuit::Exit);

        // A quit already under way drives the close that follows it. If
        // residency won here, the Quit item would hide the window instead of
        // destroying it and jot would refuse to stop -- from the one menu GNOME
        // offers for stopping it.
        check("close: a quit in progress beats residency, or Quit could never quit",
              core::on_close(Lifecycle{true, true, false, true}) == OnClose::Exit);
        check("quit: a quit in progress does not ask a second time",
              core::on_quit(Lifecycle{true, true, false, true}) == OnQuit::Exit);

        // The prompt's own continuation: answered once, not asked again.
        check("close: the answered prompt closes rather than re-opening itself",
              core::on_close(Lifecycle{false, true, true, true}) == OnClose::Exit &&
              core::on_close(Lifecycle{false, false, true, true}) == OnClose::Exit);
        check("quit: answered and forced goes straight out",
              core::on_quit(Lifecycle{false, false, true, true}) == OnQuit::Exit);
    }

    std::cout << "-----------------------------------------------\n";

    // -- Tasks: availability, the index, dates, and the jot.json round trip ---
    //
    // This is the section that matters most in the whole harness. Availability
    // is DERIVED, so a wrong answer never corrupts a file and never throws --
    // it just quietly does not show you a task, and a task you were not shown
    // is a task you did not do. There is no surface that would report that, so
    // the reporting happens here.
    {
        using core::Avail;
        using core::Filter;
        using core::Status;

        const std::int64_t now      = static_cast<std::int64_t>(std::time(nullptr));
        const std::int64_t tomorrow = now + 24 * 3600;
        const std::int64_t last_week = now - 7 * 24 * 3600;

        // ── the fields, and what "is a todo" means ──────────────────────────
        {
            core::MemoryNodes m;
            const auto a = m.create("", "a note");
            check("task: a fresh node is not a todo",
                  core::availability(m, a, now) == Avail::NotTask);

            check("task: make_task turns it into one", m.make_task(a, true));
            check("task: a plain todo with no dates is available",
                  core::availability(m, a, now) == Avail::Available);

            check("task: ticking it reports Done", m.set_done(a, true) &&
                  core::availability(m, a, now) == Avail::Done);
            m.set_done(a, false);

            // A no-op write must not dirty a note. The drawer sets every field
            // on every edit, so without this a note becomes "modified" just for
            // being looked at with the drawer open.
            const core::Node* n = m.find(a);
            const core::Task same = n->task;
            check("task: setting the same fields again is refused",
                  !m.set_task(a, same));

            m.set_due(a, tomorrow);
            m.set_flagged(a, true);
            m.set_status(a, Status::Sequential);
            check("task: un-making a todo clears its fields but keeps status",
                  m.make_task(a, false) &&
                  m.find(a)->task.due == 0 && !m.find(a)->task.flagged &&
                  m.find(a)->task.status == Status::Sequential);
        }

        // ── defer, and the way it inherits ──────────────────────────────────
        {
            core::MemoryNodes m;
            const auto p = m.create("", "project");
            const auto k = m.create(p, "step");
            m.make_task(k, true);

            m.set_defer(k, tomorrow);
            check("task: its own defer date hides it",
                  core::availability(m, k, now) == Avail::Deferred);
            m.set_defer(k, last_week);
            check("task: a defer date in the past does not hide it",
                  core::availability(m, k, now) == Avail::Available);

            // The parent is a NOTE and still defers its children: a plain note
            // is the natural project container, so dates on it have to count.
            m.set_defer(p, tomorrow);
            check("task: a parent's defer date hides the child",
                  core::availability(m, k, now) == Avail::Deferred);
            check("task: the later of the two defer dates wins",
                  core::effective_defer(m, k) == tomorrow);
        }

        // ── due inherits the other way ──────────────────────────────────────
        {
            core::MemoryNodes m;
            const auto p = m.create("", "project");
            const auto k = m.create(p, "step");
            m.make_task(k, true);
            m.set_due(p, now + 3 * 24 * 3600);
            check("task: a step inherits its project's due date",
                  core::effective_due(m, k) == now + 3 * 24 * 3600);
            m.set_due(k, tomorrow);
            check("task: the EARLIER due date wins", core::effective_due(m, k) == tomorrow);
            m.set_due(k, now + 9 * 24 * 3600);
            check("task: a step cannot be due later than its project",
                  core::effective_due(m, k) == now + 3 * 24 * 3600);
        }

        // ── Sequential vs Parallel: the engine ──────────────────────────────
        {
            core::MemoryNodes m;
            const auto p  = m.create("", "Taxes");
            const auto t1 = m.create(p, "gather receipts");
            const auto note = m.create(p, "last year's letter");   // a NOTE in the middle
            const auto t2 = m.create(p, "fill the form");
            const auto t3 = m.create(p, "post it");
            for (const auto& id : {t1, t2, t3}) m.make_task(id, true);

            m.set_status(p, Status::Parallel);
            check("task: a parallel parent makes every child available",
                  core::availability(m, t1, now) == Avail::Available &&
                  core::availability(m, t2, now) == Avail::Available &&
                  core::availability(m, t3, now) == Avail::Available);

            m.set_status(p, Status::Sequential);
            check("task: a sequential parent makes only the first one available",
                  core::availability(m, t1, now) == Avail::Available &&
                  core::availability(m, t2, now) == Avail::Blocked &&
                  core::availability(m, t3, now) == Avail::Blocked);
            check("task: the next action is the first incomplete task child",
                  core::next_action(m, p) == t1);

            // The note is not a step. If it blocked, the sequence would stop
            // dead at a row with nothing to tick.
            check("task: a note among the children is not a blocker",
                  core::availability(m, note, now) == Avail::NotTask);

            m.set_done(t1, true);
            check("task: ticking the first one unblocks exactly the next one",
                  core::availability(m, t2, now) == Avail::Available &&
                  core::availability(m, t3, now) == Avail::Blocked);
            check("task: the next action moves along with it",
                  core::next_action(m, p) == t2);

            // Sibling order IS the priority, and it has been model state since
            // s002. Dragging the last step above the current one makes it the
            // next action -- no priority field, and nothing new to build.
            m.move(t3, p, core::sibling_index(m, t2));
            check("task: dragging a step up the list makes it the next action",
                  core::next_action(m, p) == t3 &&
                  core::availability(m, t3, now) == Avail::Available &&
                  core::availability(m, t2, now) == Avail::Blocked);
        }

        // ── blocking reaches down the whole subtree ─────────────────────────
        {
            core::MemoryNodes m;
            const auto top  = m.create("", "House");
            const auto one  = m.create(top, "phase one");
            const auto two  = m.create(top, "phase two");
            const auto deep = m.create(two, "a step inside phase two");
            m.make_task(one, true);
            m.make_task(two, true);
            m.make_task(deep, true);
            m.set_status(top, Status::Sequential);
            check("task: a blocked branch blocks everything under it",
                  core::availability(m, deep, now) == Avail::Blocked);
            m.set_done(one, true);
            check("task: unblocking the branch reaches the whole subtree",
                  core::availability(m, deep, now) == Avail::Available);

            m.set_done(two, true);
            check("task: a finished parent task takes its children with it",
                  core::availability(m, deep, now) == Avail::Done);
        }

        // ── TaskIndex: membership, order, and the staleness check ───────────
        {
            core::MemoryNodes m;
            const auto a = m.create("", "alpha");
            const auto b = m.create("", "beta");
            const auto a1 = m.create(a, "alpha one");
            m.make_task(a1, true);
            m.make_task(b, true);

            core::TaskIndex ix;
            ix.rebuild(m);
            check("taskindex: it holds the todos and nothing else",
                  ix.count() == 2 && ix.holds(a1) && ix.holds(b) && !ix.holds(a));
            check("taskindex: document order, not creation order",
                  ix.tasks()[0] == a1 && ix.tasks()[1] == b);

            // The check that earns this class its keep, and the same one the
            // backlink index carries: the incremental path must land exactly
            // where a full rebuild lands. A disagreement here is a task that
            // silently stops appearing in Today.
            const auto a2 = m.create(a, "alpha two");
            m.make_task(a2, true);
            ix.update(m, a2);
            core::TaskIndex fresh;
            fresh.rebuild(m);
            check("taskindex: the incremental path agrees with a rebuild",
                  ix.tasks() == fresh.tasks());

            m.make_task(a2, false);
            ix.update(m, a2);
            check("taskindex: un-making a todo drops it", !ix.holds(a2) && ix.count() == 2);

            m.set_flagged(b, true);
            ix.update(m, b);
            check("taskindex: a field change is not a membership change",
                  ix.count() == 2 && ix.holds(b));

            ix.erase(b);
            check("taskindex: erase removes it from both the set and the order",
                  !ix.holds(b) && ix.tasks().size() == 1);
        }

        // ── the queries Today is made of ────────────────────────────────────
        {
            core::MemoryNodes m;
            const auto proj = m.create("", "Garden");
            const auto due_today   = m.create(proj, "water the beans");
            const auto flagged     = m.create(proj, "call the nursery");
            const auto later       = m.create(proj, "plant the bulbs");
            const auto overdue     = m.create("", "renew the tax disc");
            const auto deferred    = m.create("", "book the holiday");
            for (const auto& id : {due_today, flagged, later, overdue, deferred})
                m.make_task(id, true);
            m.set_due(due_today, core::day_end(now));
            m.set_flagged(flagged, true);
            m.set_due(later, now + 30 * 24 * 3600);
            m.set_due(overdue, last_week);
            m.set_defer(deferred, tomorrow);

            core::TaskIndex ix;
            ix.rebuild(m);
            auto has = [](const std::vector<core::NodeId>& v, const core::NodeId& id) {
                return std::find(v.begin(), v.end(), id) != v.end();
            };

            const auto today = ix.query(m, Filter::Today, now);
            check("query: Today holds what is due today and what is flagged",
                  has(today, due_today) && has(today, flagged));
            check("query: Today leaves out what is scheduled for later",
                  !has(today, later));
            check("query: Today leaves out what is deferred", !has(today, deferred));

            const auto od = ix.query(m, Filter::Overdue, now);
            check("query: Overdue holds the late one", has(od, overdue) && od.size() == 1);

            check("query: Scheduled is the future, not today",
                  has(ix.query(m, Filter::Scheduled, now), later) &&
                  !has(ix.query(m, Filter::Scheduled, now), due_today));

            check("query: Available leaves out the deferred one",
                  !has(ix.query(m, Filter::Available, now), deferred));

            // Blocked tasks stay out of Today; an overdue blocked one does not
            // stay out of Overdue. That asymmetry is deliberate.
            m.set_status(proj, Status::Sequential);
            m.set_due(later, last_week);
            check("query: Today drops what a sequence blocks",
                  !has(ix.query(m, Filter::Today, now), flagged));
            check("query: Overdue still shows a blocked late task",
                  has(ix.query(m, Filter::Overdue, now), later));

            m.set_done(due_today, true);
            check("query: Done is its own filter",
                  has(ix.query(m, Filter::Done, now), due_today) &&
                  !has(ix.query(m, Filter::Today, now), due_today));
        }

        // ── grouping by parent: the report OmniFocus cannot write ───────────
        {
            core::MemoryNodes m;
            const auto p1 = m.create("", "Taxes");
            const auto p2 = m.create("", "Garden");
            const auto t1 = m.create(p1, "gather receipts");
            const auto t2 = m.create(p2, "water the beans");
            const auto t3 = m.create(p1, "post it");
            const auto loose = m.create("", "ring the vet");
            for (const auto& id : {t1, t2, t3, loose}) m.make_task(id, true);
            m.set_status(p1, Status::Parallel);

            core::TaskIndex ix;
            ix.rebuild(m);
            const auto groups = core::group_by_parent(m, ix.query(m, Filter::Available, now));
            check("group: one group per parent, in first-appearance order",
                  groups.size() == 3 && groups[0].parent == p1 && groups[1].parent == p2);
            check("group: a parent's tasks stay in document order",
                  groups[0].tasks.size() == 2 &&
                  groups[0].tasks[0] == t1 && groups[0].tasks[1] == t3);
            check("group: a top-level todo groups under no parent -- unfiled is a state",
                  groups[2].parent.empty() && groups[2].tasks[0] == loose);
        }

        // ── dates: parse and format, adjacent, so they cannot skew ──────────
        {
            using core::DateKind;
            const std::int64_t d = core::parse_date("2026-09-14", DateKind::Due, now);
            const std::int64_t f = core::parse_date("2026-09-14", DateKind::Defer, now);
            check("date: a bare due date means the END of that day",
                  d != 0 && d == core::day_end(d));
            check("date: a bare defer date means the START of that day",
                  f != 0 && f == core::day_start(f));
            check("date: a bare date round-trips through format",
                  core::format_date(d) == "2026-09-14" &&
                  core::format_date(f) == "2026-09-14");
            const std::int64_t t = core::parse_date("2026-09-14 14:30", DateKind::Due, now);
            check("date: a typed clock time survives the round trip",
                  core::format_date(t) == "2026-09-14 14:30");
            check("date: today and tomorrow are a day apart",
                  core::parse_date("tomorrow", DateKind::Defer, now) -
                  core::parse_date("today", DateKind::Defer, now) == 24 * 3600);
            check("date: nothing typed means no date", core::parse_date("", DateKind::Due, now) == 0);
            check("date: an empty field is not an error", core::date_parses(""));
            check("date: a date that does not exist is refused, not normalised",
                  core::parse_date("2026-02-31", DateKind::Due, now) == 0 &&
                  !core::date_parses("2026-02-31"));
            check("date: garbage is refused", !core::date_parses("next thursday-ish"));
            check("date: zero formats as nothing at all", core::format_date(0).empty());
        }

        // ── the round trip through jot.json (D4: task state is structure) ───
        {
            const auto tmp = std::filesystem::temp_directory_path() /
                             ("jot_tasks_" + std::to_string(::getpid()));
            std::filesystem::create_directories(tmp);
            core::NodeId pid, kid;
            {
                core::Project p;
                check("tasks/disk: a jots folder opens", p.open(tmp.string()));
                pid = p.create("", "Taxes");
                kid = p.create(pid, "gather receipts");
                p.set_status(pid, Status::Sequential);
                p.make_task(kid, true);
                p.set_due(kid, core::parse_date("2026-12-01", core::DateKind::Due, now));
                p.set_flagged(kid, true);
                p.flush();
            }
            {
                core::Project p;
                p.open(tmp.string());
                const core::Node* k = p.find(kid);
                const core::Node* parent = p.find(pid);
                check("tasks/disk: the task fields come back",
                      k && k->task.is_task && k->task.flagged && !k->task.done &&
                      core::format_date(k->task.due) == "2026-12-01");
                check("tasks/disk: a parent's status comes back",
                      parent && parent->task.status == Status::Sequential);
                check("tasks/disk: a note is still a note",
                      parent && !parent->task.is_task);
            }
            // The note FILE is untouched by any of it -- task state is
            // structure, and structure lives in jot.json.
            {
                std::ifstream f(tmp / "notes" / (kid + ".md"));
                std::string text((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
                check("tasks/disk: the note file carries no task state",
                      text.find("due") == std::string::npos &&
                      text.find("task") == std::string::npos);
            }
            // A version 1 folder -- the shape every jots folder had before
            // s007 -- must open as a folder of notes, not fail to open.
            {
                std::ofstream f(tmp / "jot.json");
                f << R"({"format":"jot-jots folder","version":1,"nodes":[)"
                  << R"({"id":")" << kid << R"(","parent":"","title":"old note"}]})";
            }
            {
                core::Project p;
                check("tasks/disk: a version 1 jots folder still opens", p.open(tmp.string()));
                const core::Node* n = p.find(kid);
                check("tasks/disk: a version 1 node is a note with no task state",
                      n && !n->task.is_task && n->task.due == 0 &&
                      n->task.status == Status::None);
            }
            std::filesystem::remove_all(tmp);
        }
    }

    // -- Lifecycle: what a close means, and what a quit means ---------------
    // The milestone's real work, and the reason it is a truth table rather than
    // a branch in the close handler: two of the three answers differ only in
    // whether unsaved notes are about to be lost, and the wrong one is SILENT.
    // Every row is asserted by IDENTITY -- which answer came back -- because a
    // count assertion is a test that passes for the wrong reason (s011 banked
    // that one the hard way).
    {
        using core::Lifecycle;
        using core::OnClose;
        using core::OnQuit;

        // The three plain readings of the X.
        check("close: with nothing resident and nothing unsaved, the X exits",
              core::on_close(Lifecycle{}) == OnClose::Exit);
        check("close: unsaved notes and no residency -- ask before losing them",
              core::on_close(Lifecycle{false, false, false, true}) == OnClose::AskFirst);
        check("close: residency on -- hide it, the process keeps the clock",
              core::on_close(Lifecycle{true, false, false, false}) == OnClose::StayResident);

        // THE INVERSION THIS MILESTONE EXISTS FOR. Residency outranks the
        // scratch prompt: nothing is being lost, so asking would be a lie, and
        // a dialog that cries wolf is one the user learns to dismiss before the
        // day it matters.
        check("close: residency does NOT prompt about scratch -- nothing is lost yet",
              core::on_close(Lifecycle{true, false, false, true}) == OnClose::StayResident);

        // ...and the other half of that trade: the ask has to land SOMEWHERE.
        check("quit: unsaved notes -- the quit is what asks now",
              core::on_quit(Lifecycle{true, false, false, true}) == OnQuit::AskFirst);
        check("quit: residency makes no difference to what Quit means",
              core::on_quit(Lifecycle{true, false, false, true}) ==
                  core::on_quit(Lifecycle{false, false, false, true}));
        check("quit: nothing unsaved -- go, without a dialog nobody needed",
              core::on_quit(Lifecycle{true, false, false, false}) == OnQuit::Exit);

        // A quit already under way drives the close that follows it. If
        // residency won here, the Quit item would hide the window instead of
        // destroying it and jot would refuse to stop -- from the one menu GNOME
        // offers for stopping it.
        check("close: a quit in progress beats residency, or Quit could never quit",
              core::on_close(Lifecycle{true, true, false, true}) == OnClose::Exit);
        check("quit: a quit in progress does not ask a second time",
              core::on_quit(Lifecycle{true, true, false, true}) == OnQuit::Exit);

        // The prompt's own continuation: answered once, not asked again.
        check("close: the answered prompt closes rather than re-opening itself",
              core::on_close(Lifecycle{false, true, true, true}) == OnClose::Exit &&
              core::on_close(Lifecycle{false, false, true, true}) == OnClose::Exit);
        check("quit: answered and forced goes straight out",
              core::on_quit(Lifecycle{false, false, true, true}) == OnQuit::Exit);
    }

    std::cout << "-----------------------------------------------\n";

    // -- Capture: text in, a note out, and nothing lost ----------------------
    // The capture box and `jot --capture` both come through core::capture, so
    // what a captured line BECOMES is decided once. The property that matters
    // is that the title and the body between them always hold the whole of what
    // was typed: a capture box that silently ate the end of a sentence would be
    // worse than no capture box.
    {
        std::string t, b;

        core::capture_split("ring the vet", t, b);
        check("capture: a short line is the whole title", t == "ring the vet" && b.empty());

        core::capture_split("   ring the vet  ", t, b);
        check("capture: surrounding whitespace is not part of the note",
              t == "ring the vet" && b.empty());

        core::capture_split("ring the vet\nabout Tess's booster\nand the worming tablets",
                            t, b);
        check("capture: the first line titles it",  t == "ring the vet");
        check("capture: the rest becomes the body",
              b == "about Tess's booster\nand the worming tablets");

        const std::string longish =
            "ring the vet about the booster and the worming tablets and also ask "
            "whether the limp is anything to worry about";
        core::capture_split(longish, t, b);
        check("capture: a long line is cut for the title", t.size() < longish.size());
        check("capture: the cut lands on a word boundary",
              t.find("\u2026") != std::string::npos &&
              longish.compare(0, t.size() - 3, t.substr(0, t.size() - 3)) == 0);
        check("capture: NOTHING is lost -- the body keeps the whole line", b == longish);

        // No spaces to cut at: a pasted url or an identifier. The hard cut is
        // the honest answer and the body still has all of it.
        const std::string nospace(200, 'x');
        core::capture_split(nospace, t, b);
        check("capture: a line with no spaces still cuts, and still keeps everything",
              t.size() < nospace.size() && b == nospace);

        core::capture_split("   \n\t ", t, b);
        check("capture: nothing typed is not a note", t.empty() && b.empty());

        // Through the model: top-level, and nothing else moved.
        {
            core::MemoryNodes m;
            const auto existing = m.create("", "a note I am reading");
            const auto id = core::capture(m, "ring the vet\nabout the booster");
            check("capture: it lands at the TOP LEVEL -- unfiled is a state",
                  !id.empty() && m.find(id) && m.find(id)->parent_id.empty());
            check("capture: title and body both arrive",
                  m.find(id)->title == "ring the vet" &&
                  m.find(id)->body == "about the booster");
            check("capture: it is a note, not a todo", !m.find(id)->task.is_task);
            check("capture: the note that was already there is untouched",
                  m.find(existing) && m.find(existing)->title == "a note I am reading");
            check("capture: an empty capture creates nothing",
                  core::capture(m, "   ").empty() && m.count() == 2);
        }
    }

    // -- Lifecycle: what a close means, and what a quit means ---------------
    // The milestone's real work, and the reason it is a truth table rather than
    // a branch in the close handler: two of the three answers differ only in
    // whether unsaved notes are about to be lost, and the wrong one is SILENT.
    // Every row is asserted by IDENTITY -- which answer came back -- because a
    // count assertion is a test that passes for the wrong reason (s011 banked
    // that one the hard way).
    {
        using core::Lifecycle;
        using core::OnClose;
        using core::OnQuit;

        // The three plain readings of the X.
        check("close: with nothing resident and nothing unsaved, the X exits",
              core::on_close(Lifecycle{}) == OnClose::Exit);
        check("close: unsaved notes and no residency -- ask before losing them",
              core::on_close(Lifecycle{false, false, false, true}) == OnClose::AskFirst);
        check("close: residency on -- hide it, the process keeps the clock",
              core::on_close(Lifecycle{true, false, false, false}) == OnClose::StayResident);

        // THE INVERSION THIS MILESTONE EXISTS FOR. Residency outranks the
        // scratch prompt: nothing is being lost, so asking would be a lie, and
        // a dialog that cries wolf is one the user learns to dismiss before the
        // day it matters.
        check("close: residency does NOT prompt about scratch -- nothing is lost yet",
              core::on_close(Lifecycle{true, false, false, true}) == OnClose::StayResident);

        // ...and the other half of that trade: the ask has to land SOMEWHERE.
        check("quit: unsaved notes -- the quit is what asks now",
              core::on_quit(Lifecycle{true, false, false, true}) == OnQuit::AskFirst);
        check("quit: residency makes no difference to what Quit means",
              core::on_quit(Lifecycle{true, false, false, true}) ==
                  core::on_quit(Lifecycle{false, false, false, true}));
        check("quit: nothing unsaved -- go, without a dialog nobody needed",
              core::on_quit(Lifecycle{true, false, false, false}) == OnQuit::Exit);

        // A quit already under way drives the close that follows it. If
        // residency won here, the Quit item would hide the window instead of
        // destroying it and jot would refuse to stop -- from the one menu GNOME
        // offers for stopping it.
        check("close: a quit in progress beats residency, or Quit could never quit",
              core::on_close(Lifecycle{true, true, false, true}) == OnClose::Exit);
        check("quit: a quit in progress does not ask a second time",
              core::on_quit(Lifecycle{true, true, false, true}) == OnQuit::Exit);

        // The prompt's own continuation: answered once, not asked again.
        check("close: the answered prompt closes rather than re-opening itself",
              core::on_close(Lifecycle{false, true, true, true}) == OnClose::Exit &&
              core::on_close(Lifecycle{false, false, true, true}) == OnClose::Exit);
        check("quit: answered and forced goes straight out",
              core::on_quit(Lifecycle{false, false, true, true}) == OnQuit::Exit);
    }

    // -- Pending: the cold-capture spool -------------------------------------
    // The spool file is the ONLY copy of what was typed until a drain lands it
    // in a jots folder, so every check here is about NOT LOSING IT: the pump
    // round-trips, a file we did not write is still read as a thought, the
    // order is the order it was thought in, and a second capture in the same
    // second does not overwrite the first.
    {
        const std::string dir =
            (std::filesystem::temp_directory_path() / "jot_selftest_pending").string();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);

        // The pump, adjacent so a write cannot skew from its read.
        {
            core::Pending p;
            check("pending: round-trips a one-line capture",
                  core::decode_pending(core::encode_pending("buy milk", 1700000000), p) &&
                      p.text == "buy milk" && p.captured == 1700000000);

            core::Pending m;
            const std::string multi = "ring the vet\nabout the booster\n\nand the food";
            check("pending: a multi-line thought survives byte-for-byte",
                  core::decode_pending(core::encode_pending(multi, 42), m) && m.text == multi);

            // A capture containing our own fence. It is markdown -- a horizontal
            // rule in a captured note is not exotic -- and the parser must not
            // end the header on it or the note comes back truncated.
            core::Pending f;
            const std::string fenced = "before\n---\nafter";
            check("pending: a capture containing --- is not cut at it",
                  core::decode_pending(core::encode_pending(fenced, 7), f) && f.text == fenced);
        }

        // TOLERANCE. A file somebody echoed into the folder by hand has said
        // something; a spool that ignores what it does not recognise is a spool
        // that loses notes.
        {
            core::Pending bare;
            check("pending: a file with no front matter is read as a bare thought",
                  core::decode_pending("just a line\n", bare) && bare.text == "just a line" &&
                      bare.captured == 0);
            core::Pending empty;
            check("pending: an empty file says nothing and is not a capture",
                  !core::decode_pending("", empty));
            core::Pending unterminated;
            check("pending: an unterminated header is not a header",
                  core::decode_pending("---\ncaptured: 5\nno close fence", unterminated) &&
                      unterminated.text.find("no close fence") != std::string::npos);
        }

        // The spool itself: write, read back, remove.
        {
            check("pending: reading a folder that was never captured into is empty, not an error",
                  core::read_pending(dir).empty());

            check("pending: a cold capture reaches disk",
                  core::write_pending(dir, "second thought", 2000, "pid1"));
            check("pending: and so does one taken earlier",
                  core::write_pending(dir, "first thought", 1000, "pid2"));

            // NEVER CLOBBER. Same second, same uniquifier -- the first capture
            // must still be there afterwards.
            check("pending: a second capture in the same second does not overwrite the first",
                  core::write_pending(dir, "same second A", 3000, "pid3") &&
                      core::write_pending(dir, "same second B", 3000, "pid3"));

            auto waiting = core::read_pending(dir);
            check("pending: everything written is waiting", waiting.size() == 4,
                  std::to_string(waiting.size()));
            check("pending: OLDEST FIRST -- notes land in the order they were thought of",
                  waiting.size() == 4 && waiting[0].text == "first thought" &&
                      waiting[1].text == "second thought");

            // A half-written file is not a thought yet.
            std::ofstream(std::filesystem::path(dir) / "9999-x.md.part") << "---\n";
            check("pending: a write still in flight (.part) is left alone",
                  core::read_pending(dir).size() == 4);

            // The delete is a SEPARATE call from the read, on purpose: nothing
            // goes until its note exists somewhere that outlives the process.
            check("pending: reading does not consume the spool",
                  core::read_pending(dir).size() == 4);
            check("pending: removing one takes exactly one",
                  core::remove_pending(waiting[0]) && core::read_pending(dir).size() == 3);
            check("pending: removing the same file twice is false, not a crash",
                  !core::remove_pending(waiting[0]));
        }

        // One definition of the subpath, so App and Shell cannot disagree.
        check("pending: the spool is <data>/jot/pending",
              core::pending_dir("/home/s/.local/share") == "/home/s/.local/share/jot/pending");

        std::filesystem::remove_all(dir, ec);
    }

    std::cout << "-----------------------------------------------\n";
    std::cout << g_pass << " pass / " << g_fail << " fail\n";
    return g_fail == 0 ? 0 : 1;
}
