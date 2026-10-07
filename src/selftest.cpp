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

#include <cmath>
#include "core/Format.hpp"
#include <functional>
#include "core/Links.hpp"
#include "core/Markdown.hpp"
#include "core/Nodes.hpp"
#include "core/Lifecycle.hpp"
#include "core/Prefs.hpp"
#include "core/Recents.hpp"
#include "core/Project.hpp"
#include "core/Shortcuts.hpp"
#include "core/Notify.hpp"
#include "core/NoticeAction.hpp"
#include "core/RowLook.hpp"
#include "core/Packet.hpp"
#include "core/Undo.hpp"
#include "core/Selection.hpp"
#include "core/Hotkey.hpp"
#include "core/Enclosures.hpp"
#include "core/Pending.hpp"
#include "core/Projection.hpp"
#include "core/Tasks.hpp"
#include "core/TextMap.hpp"
#include "core/Render.hpp"
#include "core/Import.hpp"
#include "core/Inbox.hpp"
#include "core/Filing.hpp"
#include "core/CheatSheet.hpp"
#include "core/Tags.hpp"
#include "core/Review.hpp"
#include "core/Search.hpp"
#include "core/Forecast.hpp"
#include "core/Gather.hpp"
#include "core/Nudge.hpp"
#include "core/Zoom.hpp"
#include "core/DoneWhen.hpp"
#include "core/Deadline.hpp"

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
        check("prefs: a dropped file is COPIED until asked to link (s019)",
              !d.drop_links && !core::Prefs{}.drop_links);
        check("prefs: a first run gets a window size, not a zero",
              d.win_width == 940 && d.win_height == 620 && !d.win_maximized);

        core::Prefs p;
        p.show_tree = false;
        p.show_drawer = true;
        p.tree_width = 333;
        p.note_width = 777;
        p.desktop_tasks = true;
        p.background = true;
        p.drop_links = true;
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
                  r.background == p.background && r.drop_links == p.drop_links &&
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

        // s029: Move to...'s Recent, per jots folder, survives a restart.
        {
            core::Prefs mp;
            mp.move_recent["/a/Work.jots"] = {"id3", "id1"};
            mp.move_recent["/b/Home.jots"] = {"id9"};
            core::save_prefs(file, mp);
            const core::Prefs mr = core::load_prefs(file);
            check("prefs: move recents round-trip, per folder, in order",
                  mr.move_recent.size() == 2 &&
                      mr.move_recent.at("/a/Work.jots") == std::vector<std::string>{"id3", "id1"} &&
                      mr.move_recent.at("/b/Home.jots") == std::vector<std::string>{"id9"});
            check("prefs: a first run has no move recents", d.move_recent.empty());
            { std::ofstream f(file); f << R"({"move_recent":{"/a":["x",7,"y"],"/b":"no"},"show_tree":false})"; }
            const core::Prefs mm = core::load_prefs(file);
            check("prefs: a wrong-typed move recent drops alone",
                  mm.move_recent.size() == 1 &&
                      mm.move_recent.at("/a") == std::vector<std::string>{"x", "y"} && !mm.show_tree);
        }

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

    // ── s041: notification actions -- a verb and a key ──────────────────────
    // The buttons are one app action carrying "verb:amount:key". The danger is
    // a write from the tray into a model that has moved since the notice went
    // out, so the checks that matter are the REFUSALS: a stale key acts on
    // nothing. And Snooze must be about the notice, never the todo.
    {
        std::cout << "\n-- notification actions --\n";
        using core::NoticeAct;
        using core::NoticeVerb;
        using core::TargetState;

        const std::int64_t now = 1789300000;
        core::MemoryNodes m;
        m.set_clock([&] { return now; });
        const auto t = m.create("", "Pay the water bill");
        m.make_task(t, true);
        m.set_due(t, now - 60);
        const auto rep = m.create("", "Water the ferns");
        m.make_task(rep, true);
        m.set_due(rep, now - 60);
        m.set_repeat(rep, core::Repeat{1, core::RepeatUnit::Week});
        const auto note = m.create("", "Just a note");
        core::TaskIndex idx;
        idx.rebuild(m);

        const std::string key  = core::announce_key(t, now - 60);
        const std::string rkey = core::announce_key(rep, now - 60);

        // ── the wire format ───────────────────────────────────────────────
        const NoticeAct snooze{NoticeVerb::Snooze, 60, key};
        const auto back = core::decode_notice_act(core::encode_notice_act(snooze));
        check("notice: an act round-trips through its text", back && *back == snooze,
              core::encode_notice_act(snooze));
        check("notice: the text is readable in a log",
              core::encode_notice_act({NoticeVerb::Done, 0, key}) == "done:0:" + key);
        check("notice: the key's node is the part before the last @",
              core::key_node(key) == t && core::key_node("no-at-sign").empty());
        check("notice: junk does not decode",
              !core::decode_notice_act("") && !core::decode_notice_act("done") &&
              !core::decode_notice_act("fly:1:" + key) &&
              !core::decode_notice_act("snooze:x:" + key) &&
              !core::decode_notice_act("done:0:noatsign") &&
              !core::decode_notice_act("done:0:@123"));
        check("notice: a snooze of zero minutes is refused (it would ring at once)",
              !core::decode_notice_act("snooze:0:" + key) &&
              !core::decode_notice_act("remind:-1:" + key));

        // ── the buttons ───────────────────────────────────────────────────
        const auto r0 = core::due_announcements(m, idx, now, {});
        const core::Announcement* ann = nullptr;
        for (const auto& a : r0.to_show) if (a.id == t) ann = &a;
        const auto btns = ann ? core::todo_notice_buttons(*ann) : std::vector<core::NoticeButton>{};
        check("notice: a due todo carries three buttons, done first",
              btns.size() == 3 && btns[0].act.verb == NoticeVerb::Done &&
              btns[1].act.verb == NoticeVerb::Snooze && btns[2].act.verb == NoticeVerb::Remind);
        check("notice: every button names THIS occurrence",
              btns.size() == 3 && btns[0].act.key == key && btns[1].act.key == key &&
              btns[2].act.key == key);

        // ── the target check ──────────────────────────────────────────────
        check("notice: a fresh key is current",
              core::check_notice_target(m, key) == TargetState::Current);
        check("notice: a key for no node is Gone",
              core::check_notice_target(m, "nope@1") == TargetState::Gone);
        check("notice: a key for a plain note is NotTodo",
              core::check_notice_target(m, core::announce_key(note, 5)) == TargetState::NotTodo);
        m.set_due(t, now - 30);
        check("notice: a rescheduled todo's old key is Moved",
              core::check_notice_target(m, key) == TargetState::Moved);
        m.set_due(t, now - 60);
        // THE ONE THAT MATTERS: a repeating todo ticked from its notice rolls on
        // to next week. The same button pressed again must not tick next week.
        m.set_done(rep, true);
        check("notice: after a repeat rolls on, the old notice is Moved -- not next week's",
              !m.find(rep)->task.done &&
              core::check_notice_target(m, rkey) == TargetState::Moved);
        m.set_done(t, true);
        check("notice: a ticked todo is AlreadyDone",
              core::check_notice_target(m, key) == TargetState::AlreadyDone &&
              !core::target_state_words(TargetState::AlreadyDone).empty() &&
              core::target_state_words(TargetState::Current).empty());
        m.set_done(t, false);
        idx.rebuild(m);

        // ── snooze: the notice, not the todo ──────────────────────────────
        check("notice: Snooze 60 is an hour on",
              core::notice_until({NoticeVerb::Snooze, 60, key}, now) == now + 3600);
        {
            // Remind 1 = the same clock time tomorrow, even across a DST night.
            // Stand in a real zone on the night clocks go back (US, 2026-11-01).
            const char* old_tz = std::getenv("TZ");
            const std::string saved = old_tz ? old_tz : "";
            setenv("TZ", "America/Chicago", 1);
            tzset();
            std::tm lt{};
            lt.tm_year = 2026 - 1900; lt.tm_mon = 9; lt.tm_mday = 31;
            lt.tm_hour = 9; lt.tm_min = 15; lt.tm_isdst = -1;
            const std::int64_t sat = std::mktime(&lt);
            const std::int64_t sun = core::notice_until({NoticeVerb::Remind, 1, key}, sat);
            std::time_t st = static_cast<std::time_t>(sun);
            std::tm got{};
            localtime_r(&st, &got);
            check("notice: Remind 1 across the DST night is 9:15 the next day",
                  got.tm_mday == 1 && got.tm_mon == 10 && got.tm_hour == 9 && got.tm_min == 15 &&
                      sun - sat == 25 * 3600,
                  std::to_string(sun - sat));
            if (old_tz) setenv("TZ", saved.c_str(), 1); else unsetenv("TZ");
            tzset();
        }

        std::vector<std::string> announced{key};
        std::vector<core::Snoozed> ledger;
        const core::Task before = m.find(t)->task;
        core::park(ledger, announced, key, now + 3600);
        check("notice: parking takes the key out of the announced set",
              announced.empty() && ledger.size() == 1 && core::is_parked(ledger, key, now));
        check("notice: ...and touches nothing on the todo",
              m.find(t)->task.due == before.due && m.find(t)->task.defer == before.defer &&
              !m.find(t)->task.done);
        core::park(ledger, announced, key, now + 7200);
        check("notice: parking again replaces, never stacks",
              ledger.size() == 1 && ledger[0].until == now + 7200);

        auto asleep = core::due_announcements(m, idx, now + 60, announced, ledger);
        const auto shows = [](const core::AnnounceResult& r, const core::NodeId& id) {
            return std::any_of(r.to_show.begin(), r.to_show.end(),
                               [&](const core::Announcement& a) { return a.id == id; });
        };
        const auto in = [](const std::vector<std::string>& v, const std::string& x) {
            return std::find(v.begin(), v.end(), x) != v.end();
        };
        check("notice: a parked key is still LIVE but not shown",
              !shows(asleep, t) && in(asleep.live, key) && !in(asleep.keep, key));
        check("notice: pruning keeps a park that has not woken",
              !core::prune_parked(ledger, asleep.live, now + 60) && ledger.size() == 1);
        auto awake = core::due_announcements(m, idx, now + 7200, announced, ledger);
        check("notice: when the time comes it is said again", shows(awake, t));
        check("notice: ...and the woken park leaves the ledger",
              core::prune_parked(ledger, awake.live, now + 7200) && ledger.empty());

        core::park(ledger, announced, key, now + 3600);
        m.set_done(t, true);
        idx.rebuild(m);
        auto gone = core::due_announcements(m, idx, now + 60, announced, ledger);
        check("notice: ticking a snoozed todo drops its park (not a deadline any more)",
              core::prune_parked(ledger, gone.live, now + 60) && ledger.empty());
    }

    // ── s042: the card's look -- one state, one chip, decided in core ───────
    {
        std::cout << "\n-- card look (s042) --\n";
        const char* old_tz = std::getenv("TZ");
        const std::string saved = old_tz ? old_tz : "";
        setenv("TZ", "America/Chicago", 1);
        tzset();
        const auto at = [](int y, int mo, int d, int h, int mi, int sec) {
            std::tm t{};
            t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
            t.tm_hour = h; t.tm_min = mi; t.tm_sec = sec; t.tm_isdst = -1;
            return static_cast<std::int64_t>(std::mktime(&t));
        };
        const std::int64_t now = at(2026, 10, 30, 10, 0, 0);   // a Friday

        check("look: due later today shows the time", core::short_due(at(2026,10,30,17,0,0), now) == "Today 17:00");
        check("look: a bare date today shows no time", core::short_due(at(2026,10,30,23,59,59), now) == "Today");
        check("look: passed earlier today", core::short_due(at(2026,10,30,9,0,0), now) == "Overdue 09:00");
        check("look: tomorrow", core::short_due(at(2026,10,31,23,59,59), now) == "Tomorrow");
        // Across the fall-back night (Nov 1): still calendar days, not 86400s.
        check("look: within the week is the weekday, across DST",
              core::short_due(at(2026,11,3,23,59,59), now) == "Tue", core::short_due(at(2026,11,3,23,59,59), now));
        check("look: further out is the date", core::short_due(at(2026,11,12,23,59,59), now) == "Nov 12");
        check("look: another year says so", core::short_due(at(2027,1,4,23,59,59), now) == "Jan 4 2027");
        check("look: a past day counts days late",
              core::short_due(at(2026,10,27,23,59,59), now) == "3d late" &&
              core::short_due(at(2026,10,29,8,0,0), now) == "1d late");
        check("look: a day late across DST is still days",
              core::short_due(at(2026,10,30,9,0,0), at(2026,11,2,9,0,0)) == "3d late",
              core::short_due(at(2026,10,30,9,0,0), at(2026,11,2,9,0,0)));
        check("look: undated has no chip", core::short_due(0, now).empty());
        check("look: 22:00 tonight vs 08:00 tomorrow is Tomorrow, not Today",
              core::short_due(at(2026,10,31,8,0,0), at(2026,10,30,22,0,0)) == "Tomorrow 08:00");

        core::MemoryNodes m;
        const auto proj = m.create("", "Taxes");
        m.set_status(proj, core::Status::Sequential);
        const auto a = m.create(proj, "Gather receipts");
        const auto b = m.create(proj, "Fill the form");
        const auto late = m.create("", "Pay the bill");
        const auto today = m.create("", "Call the vet");
        const auto flag = m.create("", "Read the lease");
        const auto def = m.create("", "Renew permit");
        const auto done = m.create("", "Mail it");
        for (const auto& id : {a, b, late, today, flag, def, done}) m.make_task(id, true);
        m.set_due(late, at(2026,10,28,23,59,59));
        m.set_due(today, at(2026,10,30,23,59,59));
        m.set_flagged(flag, true);
        m.set_defer(def, at(2026,11,5,0,0,0));
        m.set_done(done, true);
        m.set_estimate(a, 45);
        m.set_repeat(a, core::Repeat{1, core::RepeatUnit::Week});
        const auto st = [&](const core::NodeId& id) { return core::row_look(m, *m.find(id), now).state; };
        check("look: available", st(a) == core::RowState::Available);
        check("look: blocked behind a step is Waiting", st(b) == core::RowState::Waiting);
        check("look: overdue", st(late) == core::RowState::Overdue);
        check("look: due today", st(today) == core::RowState::DueToday);
        check("look: flagged", st(flag) == core::RowState::Flagged);
        check("look: deferred", st(def) == core::RowState::Deferred);
        check("look: done", st(done) == core::RowState::Done);
        m.set_flagged(late, true);
        check("look: overdue beats flagged", st(late) == core::RowState::Overdue);
        const auto la = core::row_look(m, *m.find(a), now);
        check("look: the quiet line's parts",
              la.project == "Taxes" && la.estimate == "45m" && la.repeat == "every week" &&
              core::row_look(m, *m.find(late), now).project.empty());
        m.set_due(proj, at(2026,11,3,23,59,59));
        const auto inh = core::row_look(m, *m.find(a), now);
        check("look: a parent's due is shown and marked inherited",
              inh.due == "Tue" && inh.due_inherited);
        {
            // s043: the header's sum. late + flag + estimates.
            const auto v = core::summarize(m, {a, b, late, today, flag, def, done}, now);
            check("summary: counts what is not done", v.todo == 6, std::to_string(v.todo));
            check("summary: late and due today", v.late == 1 && v.today == 1);
            check("summary: minutes add up; the rest are counted, not guessed",
                  v.minutes == 45 && v.unsized == 5);
            check("summary: the line",
                  core::summary_text(v) == "6 to do  \u00b7  1 late  \u00b7  1 due today  \u00b7  ~45m + 5 not sized",
                  core::summary_text(v));
            check("summary: an empty view says so",
                  core::summary_text(core::summarize(m, {done}, now)) == "Nothing to do");
            m.set_estimate(b, 75);
            check("summary: sums past an hour",
                  core::summary_text(core::summarize(m, {a, b}, now)) == "2 to do  \u00b7  ~2h");
        }
        check("look: every state has a CSS word",
              std::string(core::row_state_word(core::RowState::DueToday)) == "today" &&
              std::string(core::row_state_word(core::RowState::OnHold)) == "hold");

        if (old_tz) setenv("TZ", saved.c_str(), 1); else unsetenv("TZ");
        tzset();
    }

    // ── s044: the packet -- required items, in by a file or a tick ─────────
    {
        std::cout << "\n-- packet (s044) --\n";
        const std::string body =
            "# Taxes 2026\n"
            "For the accountant, by March.\n\n"
            "- [ ] W-2 (employer) [w2.pdf](attachments/w2.pdf)\n"
            "- [ ] 1099-INT (bank)\n"
            "- [x] Property tax receipt\n"
            "- [ ] Mortgage 1098 ![scan](attachments/1098-scan.png) [p2](attachments/1098-p2.pdf)\n"
            "- [ ] Charity letter [letter](file:///home/scott/Documents/charity.pdf)\n"
            "- [ ] HSA 5498\n"
            "- not an item\n";
        const auto st = core::packet_state(body);
        check("packet: every checkbox line is an item, nothing else", st.total == 6,
              std::to_string(st.total));
        check("packet: a file on the line puts it in", st.items[0].in() && st.items[0].files.size() == 1 &&
              st.items[0].files[0] == "w2.pdf");
        check("packet: the file is lifted out of the label",
              st.items[0].label == "W-2 (employer)", st.items[0].label);
        check("packet: no file, no tick -- missing", !st.items[1].in());
        check("packet: a tick puts it in (the paper copy)", st.items[2].in() && st.items[2].files.empty());
        check("packet: two files on one line, image or not",
              st.items[3].files.size() == 2 && st.items[3].label == "Mortgage 1098", st.items[3].label);
        check("packet: a linked file counts too",
              st.items[4].in() && st.items[4].label == "Charity letter");
        check("packet: the count", st.in == 4 && !st.complete());
        check("packet: the line names what is missing",
              core::packet_line(st) == "4 of 6 in  ·  missing: 1099-INT (bank), HSA 5498",
              core::packet_line(st));
        check("packet: the count-only line (Note details)",
              core::packet_count(st) == "4 of 6 in  \u00b7  2 missing" &&
              core::packet_count(core::packet_state("- [x] a\n")) == "All 1 in",
              core::packet_count(st));
        check("packet: missing in order", st.missing() == std::vector<std::string>{"1099-INT (bank)", "HSA 5498"});
        const auto all = core::packet_state("- [x] a\n- [ ] b [f](attachments/f.pdf)\n");
        check("packet: all in", all.complete() && core::packet_line(all) == "All 2 in");
        const auto none = core::packet_state("just words\n");
        check("packet: no items yet says how to start",
              none.total == 0 && !none.complete() &&
              core::packet_line(none).find("checkbox") != std::string::npos);
        const auto many = core::packet_state("- [ ] a\n- [ ] b\n- [ ] c\n- [ ] d\n- [ ] e\n");
        check("packet: a long missing list is cut",
              core::packet_line(many) == "0 of 5 in  ·  missing: a, b, c and 2 more",
              core::packet_line(many));
        check("packet: a web link is not a file",
              !core::packet_state("- [ ] form [irs](https://irs.gov/w9)\n").items[0].in());

        core::MemoryNodes m;
        const auto t = m.create("", "Taxes");
        check("packet: the mark sets, and a no-op is refused",
              m.set_packet(t, true) && m.find(t)->packet && !m.set_packet(t, true));

        namespace fs = std::filesystem;
        const std::string dir = (fs::temp_directory_path() / "jot_selftest_packet").string();
        std::error_code ec;
        fs::remove_all(dir, ec);
        core::NodeId a, b;
        {
            core::Project v;
            v.open(dir);
            a = v.create("", "Taxes 2026");
            b = v.create("", "plain");
            v.set_packet(a, true);
            v.flush();
        }
        {
            core::Project v;
            v.open(dir);
            check("packet/jots: the mark survives a reopen, only where it was set",
                  v.find(a) && v.find(a)->packet && v.find(b) && !v.find(b)->packet);
        }
        fs::remove_all(dir, ec);
    }

    // ── s051: Gather for sending ───────────────────────────────────────────
    {
        std::cout << "\n-- gather for sending (s051) --\n";
        namespace fs = std::filesystem;
        check("gather: names are safe on any desktop",
              core::safe_file_name("W-2: employer/copy?") == "W-2 employer copy" &&
                  core::safe_file_name(" .hidden. ") == "hidden" && core::safe_file_name("") == "untitled" &&
                  core::safe_file_name("a\tb  <c>|d") == "a b c d",
              core::safe_file_name("W-2: employer/copy?"));
        {
            std::string lng;
            for (int i = 0; i < 40; ++i) lng += "é";   // 80 bytes of two-byte letters, then one more
            lng += "xé";
            const std::string cut = core::safe_file_name(lng);
            check("gather: a long name is cut on a character boundary",
                  cut.size() <= 80 && (static_cast<unsigned char>(cut.back()) & 0xC0) != 0xC0 &&
                      cut.size() % 2 == 0, std::to_string(cut.size()));
        }
        check("gather: crc32 of the check string", core::crc32("123456789") == 0xCBF43926u);

        const fs::path root = fs::temp_directory_path() / "jot_selftest_gather";
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "attachments", ec);
        fs::create_directories(root / "out", ec);
        fs::create_directories(root / "elsewhere", ec);
        const auto put = [](const fs::path& p, const std::string& bytes) {
            std::ofstream o(p, std::ios::binary);
            o << bytes;
        };
        const auto get = [](const fs::path& p) {
            std::ifstream i(p, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(i)), std::istreambuf_iterator<char>());
        };
        put(root / "attachments" / "w2-2026.pdf", "%PDF w2");
        put(root / "attachments" / "hsa-a.pdf", "%PDF hsa one");
        put(root / "attachments" / "pasted-20261006-101010.jpg", std::string("\xff\xd8 jpeg\0bytes", 13));
        put(root / "elsewhere" / "Bank Statement.PDF", "%PDF bank");
        core::AttachStore store;
        store.dir = (root / "attachments").string();
        const std::string linked = core::file_uri((root / "elsewhere" / "Bank Statement.PDF").string());
        const std::string body =
            "Send to Pat by March.\n"
            "- [ ] W-2 (employer) [w2](attachments/w2-2026.pdf)\n"
            "- [x] Property tax receipt\n"
            "- [ ] HSA: 5498 [a](attachments/hsa-a.pdf) ![b](attachments/pasted-20261006-101010.jpg)\n"
            "- [ ] 1099-INT (bank) [s](" + linked + ")\n";
        const auto st = core::packet_state(body);
        const std::int64_t when = 1'791'300'000;   // a fixed moment
        const auto plan = core::gather_plan("Taxes 2026", st, store, when);
        std::vector<std::string> names;
        for (const auto& f : plan.files) names.push_back(f.as);
        check("gather: every file under its ITEM's name, numbered by place",
              names == std::vector<std::string>{"01 W-2 (employer).pdf", "03 HSA 5498 - 1.pdf",
                                                "03 HSA 5498 - 2.jpg", "04 1099-INT (bank).PDF"},
              names.size() > 1 ? names[1] : std::string("?"));
        check("gather: a complete packet has no problems, one paper item",
              plan.problems.empty() && plan.paper == 1, std::to_string(plan.problems.size()));
        check("gather: the folder is the packet's name and the day",
              plan.name.rfind("Taxes 2026 - 20", 0) == 0 && plan.name.size() == 23, plan.name);
        check("gather: Contents names each item, says where the paper one is, and what a file was",
              plan.contents.find("02  Property tax receipt\n      paper copy") != std::string::npos &&
                  plan.contents.find("01 W-2 (employer).pdf   (was w2-2026.pdf)") != std::string::npos &&
                  plan.contents.find("4 items, 4 files.") != std::string::npos,
              plan.contents);

        // Refusals: not all in; a file gone.
        const auto half = core::packet_state("- [ ] W-2 [w2](attachments/w2-2026.pdf)\n- [ ] 1099\n");
        const auto hp = core::gather_plan("Half", half, store, when);
        std::string made, err;
        check("gather: an incomplete packet is refused and makes nothing",
              !hp.problems.empty() && !core::gather_to_folder(hp, (root / "out").string(), made, err) &&
                  fs::is_empty(root / "out"), err);
        const auto gone = core::gather_plan("Gone", core::packet_state("- [ ] x [x](attachments/nope.pdf)\n"),
                                            store, when);
        check("gather: a file that is not there is a problem by name",
              gone.problems.size() == 1 && gone.problems[0] == "x: nope.pdf is not there", 
              gone.problems.empty() ? "" : gone.problems[0]);

        // Into a folder.
        const bool ok = core::gather_to_folder(plan, (root / "out").string(), made, err);
        const fs::path dir = made;
        check("gather/folder: made, with Contents and every file, bytes intact",
              ok && fs::is_directory(dir) && dir.filename() == plan.name &&
                  get(dir / core::kContentsName) == plan.contents &&
                  get(dir / "01 W-2 (employer).pdf") == "%PDF w2" &&
                  get(dir / "03 HSA 5498 - 2.jpg") == std::string("\xff\xd8 jpeg\0bytes", 13) &&
                  get(dir / "04 1099-INT (bank).PDF") == "%PDF bank",
              err);
        std::string made2;
        check("gather/folder: again the same day -> a second folder, (2), no .part left",
              core::gather_to_folder(plan, (root / "out").string(), made2, err) &&
                  fs::path(made2).filename() == plan.name + " (2)" &&
                  std::distance(fs::directory_iterator(root / "out"), fs::directory_iterator{}) == 2,
              made2);
        check("gather/folder: the attachments are untouched",
              get(root / "attachments" / "w2-2026.pdf") == "%PDF w2" &&
                  fs::exists(root / "elsewhere" / "Bank Statement.PDF"));

        // Into a zip -- read back by walking its central directory.
        const std::string zp = (root / "out" / "packet.zip").string();
        check("gather/zip: written", core::gather_to_zip(plan, zp, err) && !fs::exists(zp + ".jot-tmp"), err);
        {
            const std::string z = get(zp);
            const auto u16 = [&](std::size_t at) {
                return static_cast<unsigned>(static_cast<unsigned char>(z[at])) |
                       static_cast<unsigned>(static_cast<unsigned char>(z[at + 1])) << 8;
            };
            const auto u32 = [&](std::size_t at) {
                return static_cast<std::uint32_t>(u16(at)) | static_cast<std::uint32_t>(u16(at + 2)) << 16;
            };
            const std::size_t eocd = z.size() >= 22 ? z.size() - 22 : 0;
            bool good = z.size() > 22 && u32(eocd) == 0x06054b50u && u16(eocd + 10) == plan.files.size() + 1;
            std::vector<std::string> got;
            std::size_t at = good ? u32(eocd + 16) : 0;
            for (unsigned i = 0; good && i < u16(eocd + 10); ++i) {
                good = u32(at) == 0x02014b50u && u16(at + 10) == 0 && (u16(at + 8) & 0x800);
                const std::uint32_t crc = u32(at + 16), size = u32(at + 20), local = u32(at + 42);
                const unsigned nl = u16(at + 28);
                const std::string name = z.substr(at + 46, nl);
                const std::size_t data = local + 30 + u16(local + 26) + u16(local + 28);
                good = good && u32(local) == 0x04034b50u && core::crc32(z.substr(data, size)) == crc;
                if (name == plan.name + "/01 W-2 (employer).pdf") good = good && z.substr(data, size) == "%PDF w2";
                got.push_back(name);
                at += 46 + nl + u16(at + 30) + u16(at + 32);
            }
            check("gather/zip: a stored zip whose CRCs and bytes check out",
                  good && got.size() == 5 && got[0] == plan.name + "/" + core::kContentsName &&
                      got[4] == plan.name + "/04 1099-INT (bank).PDF",
                  got.empty() ? "unreadable" : got.back());
        }

        // The stamp: a field, through every door.
        core::MemoryNodes m;
        const auto t = m.create("", "Taxes");
        check("gather/stamp: sets, and a no-op is refused",
              m.set_sent(t, when, zp) && m.find(t)->sent == when && m.find(t)->sent_to == zp &&
                  !m.set_sent(t, when, zp) && m.set_sent(t, 0, "") && m.find(t)->sent == 0);
        check("gather/stamp: the line says when and what",
              core::sent_line(when, zp).rfind("Sent ", 0) == 0 &&
                  core::sent_line(when, zp).find("·  packet.zip") != std::string::npos &&
                  core::sent_line(0, zp).empty(),
              core::sent_line(when, zp));
        const std::string jd = (root / "jots").string();
        core::NodeId a, b;
        {
            core::Project v;
            v.open(jd);
            a = v.create("", "Taxes 2026");
            b = v.create("", "plain");
            v.set_packet(a, true);
            v.set_sent(a, when, zp);
            v.flush();
        }
        {
            core::Project v;
            v.open(jd);
            check("gather/jots: the stamp survives a reopen, only where it was set",
                  v.find(a) && v.find(a)->sent == when && v.find(a)->sent_to == zp && v.find(b) &&
                      v.find(b)->sent == 0 && v.find(b)->sent_to.empty());
        }
        fs::remove_all(root, ec);
    }

    // ── s052: packet nudges ────────────────────────────────────────────────
    {
        std::cout << "\n-- packet nudges (s052) --\n";
        namespace fs = std::filesystem;
        const auto at = [](int y, int mo, int d, int h) {
            std::tm tm{};
            tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d; tm.tm_hour = h; tm.tm_isdst = -1;
            return static_cast<std::int64_t>(std::mktime(&tm));
        };
        const std::int64_t morning = at(2026, 10, 7, 10), early = at(2026, 10, 7, 8);
        check("nudge: local days count up by one across a day",
              core::local_day(at(2026, 10, 8, 10)) == core::local_day(morning) + 1 &&
                  core::local_day(at(2026, 10, 7, 23)) == core::local_day(at(2026, 10, 7, 0)));

        core::MemoryNodes m;
        const auto home = m.create("", "Home");
        const auto taxes = m.create(home, "Taxes");     // nested: found anyway
        m.set_body(taxes, "- [ ] a [f](attachments/f.pdf)\n- [ ] b\n- [ ] c\n");
        m.set_packet(taxes, true);
        check("nudge: off by default", !core::nudges_now(*m.find(taxes), morning));
        m.set_nudge(taxes, 7);
        check("nudge: a packet set to nudge, with something missing, does",
              core::nudges_now(*m.find(taxes), morning));

        const auto r = core::packet_nudges(m, morning, {});
        const std::string key = core::nudge_key(taxes, core::local_day(morning) / 7);
        check("nudge: after 9:00 it is shown, keyed by the period",
              r.to_show.size() == 1 && r.to_show[0].key == key && r.live == std::vector<std::string>{key} &&
                  r.keep.empty(), r.to_show.empty() ? "none" : r.to_show[0].key);
        check("nudge: the words",
              !r.to_show.empty() && r.to_show[0].summary == "Taxes: 1 of 3 in" &&
                  r.to_show[0].detail == "Missing: b, c", r.to_show.empty() ? "" : r.to_show[0].detail);
        const auto r8 = core::packet_nudges(m, early, {});
        check("nudge: before 9:00 it is live but quiet", r8.to_show.empty() && r8.live.size() == 1);
        const auto rk = core::packet_nudges(m, morning, {key});
        check("nudge: once delivered, quiet for the rest of the period",
              rk.to_show.empty() && rk.keep == std::vector<std::string>{key});
        check("nudge: the next period is a new key -- it says again",
              core::packet_nudges(m, morning + 7 * 86400, {key}).to_show.size() == 1);
        check("nudge: parked (In 3 days) stays quiet until then",
              core::packet_nudges(m, morning, {}, {{key, morning + 3 * 86400}}).to_show.empty());
        {
            // Every day + In 3 days: tomorrow is a NEW period, and must stay quiet.
            m.set_nudge(taxes, 1);
            const std::string k1 = core::nudge_key(taxes, core::local_day(morning));
            const std::vector<core::Snoozed> park{{k1, morning + 3 * 86400}};
            const auto tmw = core::packet_nudges(m, morning + 86400, {}, park);
            const auto fri = core::packet_nudges(m, morning + 3 * 86400 + 60, {}, park);
            check("nudge: a park holds the packet across periods, and stays live till it wakes",
                  tmw.to_show.empty() && tmw.live == std::vector<std::string>{k1} && fri.to_show.size() == 1 &&
                      fri.to_show[0].key != k1, tmw.live.empty() ? "pruned" : tmw.live[0]);
            m.set_nudge(taxes, 7);
        }
        {
            // The due half is handed the same announced set and drops a nudge
            // key from ITS keep -- the union is what keeps it.
            core::TaskIndex idx;
            idx.rebuild(m);
            const auto due = core::due_announcements(m, idx, morning, {key});
            check("nudge: the due half does not keep a nudge key; the nudge half does",
                  std::find(due.keep.begin(), due.keep.end(), key) == due.keep.end() &&
                      core::packet_nudges(m, morning, {key}).keep.size() == 1);
        }

        // When it stops.
        {
            core::MemoryNodes q;
            const auto p = q.create("", "P");
            q.set_body(p, "- [ ] a\n");
            q.set_packet(p, true);
            q.set_nudge(p, 1);
            const bool on = core::nudges_now(*q.find(p), morning);
            q.set_sent(p, morning, "/x.zip");
            const bool sent = core::nudges_now(*q.find(p), morning);
            q.set_sent(p, 0, "");
            q.set_body(p, "- [x] a\n");
            const bool all = core::nudges_now(*q.find(p), morning);
            q.set_body(p, "- [ ] a\n");
            q.make_task(p, true);
            q.set_defer(p, morning + 86400);
            const bool deferred = core::nudges_now(*q.find(p), morning);
            q.set_defer(p, 0);
            q.set_done(p, true);
            const bool done = core::nudges_now(*q.find(p), morning);
            check("nudge: stops when sent, all in, deferred or done",
                  on && !sent && !all && !deferred && !done);
        }

        // Buttons and the act.
        const auto st2 = core::packet_state(m.find(taxes)->body);
        check("nudge: two missing -> In 3 days only (Open is the Shell's)",
              core::nudge_buttons(key, st2).size() == 1 &&
                  core::nudge_buttons(key, st2)[0].act.verb == core::NoticeVerb::Remind &&
                  core::nudge_buttons(key, st2)[0].act.amount == 3);
        const auto st1 = core::packet_state("- [x] a\n- [ ] b\n");
        const auto b1 = core::nudge_buttons(key, st1);
        check("nudge: one missing -> I've got it, carrying its line",
              b1.size() == 2 && b1[1].act.verb == core::NoticeVerb::Got && b1[1].act.amount == 1);
        const auto got = core::decode_notice_act(core::encode_notice_act(b1[1].act));
        check("nudge: the act survives the wire; its key names the packet",
              got && got->verb == core::NoticeVerb::Got && got->amount == 1 && got->key == key &&
                  core::key_node(key) == taxes && core::is_nudge_key(key) &&
                  !core::decode_notice_act("got:-1:" + key));
        check("nudge: the target is checked now",
              core::check_nudge_target(m, key) == core::NudgeState::Current &&
                  core::check_nudge_target(m, core::nudge_key("nope", 1)) == core::NudgeState::Gone);
        m.set_body(taxes, "- [x] a\n- [x] b\n- [x] c\n");
        check("nudge: all in since -> nothing to do, and said",
              core::check_nudge_target(m, key) == core::NudgeState::AllIn &&
                  core::nudge_state_words(core::NudgeState::AllIn) == "everything is in now");
        check("nudge: I've got it ticks that line and only that",
              core::tick_line("x\n- [ ] a\n- [ ] b\n", 2) == "x\n- [ ] a\n- [x] b\n" &&
                  core::tick_line("- [x] a\n", 0).empty() && core::tick_line("plain\n", 0).empty() &&
                  core::tick_line("- [ ] a\n", 7).empty());
        check("nudge: the choices read",
              core::nudge_every_text(0) == "Never" && core::nudge_every_text(7) == "Every week" &&
                  core::nudge_every_text(3) == "Every 3 days" && core::nudge_choices().front() == 0);
        check("nudge: set refuses a no-op", !m.set_nudge(taxes, 7) && m.set_nudge(taxes, 0));

        const fs::path jd = fs::temp_directory_path() / "jot_selftest_nudge";
        std::error_code ec;
        fs::remove_all(jd, ec);
        core::NodeId a, b;
        {
            core::Project v;
            v.open(jd.string());
            a = v.create("", "Taxes");
            b = v.create("", "plain");
            v.set_packet(a, true);
            v.set_nudge(a, 14);
            v.flush();
        }
        {
            core::Project v;
            v.open(jd.string());
            check("nudge/jots: the cadence survives a reopen, only where it was set",
                  v.find(a) && v.find(a)->nudge == 14 && v.find(b) && v.find(b)->nudge == 0);
        }
        fs::remove_all(jd, ec);
    }

    // ── s053: the packet comes back next year ─────────────────────────────
    {
        std::cout << "\n-- packet again (s053) --\n";
        check("again: the year moves on; no year -> (next)",
              core::next_title("Taxes 2026") == "Taxes 2027" &&
                  core::next_title("2025 taxes for Pat") == "2026 taxes for Pat" &&
                  core::next_title("Room 12345") == "Room 12345 (next)" &&
                  core::next_title("Passport renewal") == "Passport renewal (next)" &&
                  core::next_title("FY2026 - 1099") == "FY2027 - 1099",
              core::next_title("FY2026 - 1099"));
        const std::string body =
            "For Pat.\n"
            "- [ ] W-2 (employer) [w2](attachments/w2.pdf)\n"
            "- [x] Property tax receipt\n"
            "- [ ] HSA ![a](attachments/a.png) [b](file:///home/s/b.pdf) [form](https://irs.gov/x)\n"
            "\n"
            "Last time: [Taxes 2025](jot:abc) — sent 1 Mar 2025.\n";
        const std::string fresh = core::fresh_body(body);
        check("again: items unticked, files off their lines, web links and words kept, old footer gone",
              fresh == "For Pat.\n- [ ] W-2 (employer)\n- [ ] Property tax receipt\n"
                       "- [ ] HSA [form](https://irs.gov/x)\n",
              fresh);
        const auto fs0 = core::packet_state(fresh);
        check("again: the fresh packet has every item and none in", fs0.total == 3 && fs0.in == 0);

        core::MemoryNodes m;
        const auto home = m.create("", "Home");
        const auto taxes = m.create(home, "Taxes 2026");
        const auto after = m.create(home, "After");
        m.set_body(taxes, body);
        m.set_packet(taxes, true);
        m.set_nudge(taxes, 7);
        m.make_task(taxes, true);
        const std::int64_t due = 1'776'200'000;   // mid-April 2026
        m.set_due(taxes, due);
        m.set_done(taxes, true);
        m.set_sent(taxes, 1'791'300'000, "/x/Taxes.zip");
        core::NodeId n2;
        {
            core::Journal j;
            core::UndoSource u{j, [&] { return &m; }};
            {
                core::Gesture g(u, "Do it again");
                n2 = core::packet_again(u, taxes);
            }
            const core::Node* c = m.find(n2);
            const auto kids = m.children(home);
            check("again: a copy, right below, as one step",
                  c && kids.size() == 3 && kids[1] == n2 && kids[2] == after && j.size() == 1 &&
                      j.undo_label() == "Do it again", std::to_string(j.size()));
            check("again: next year's title, a packet, same nudge, not sent",
                  c && c->title == "Taxes 2027" && c->packet && c->nudge == 7 && c->sent == 0 &&
                      c->sent_to.empty());
            check("again: a todo again -- not done, due one year on",
                  c && c->task.is_task && !c->task.done && c->task.finished == 0 &&
                      c->task.due > due + 364 * 86400 && c->task.due < due + 367 * 86400);
            check("again: it links back to the record",
                  c && c->body.find("Last time: [Taxes 2026](jot:" + taxes + ") — sent ") != std::string::npos &&
                      c->body.rfind(fresh, 0) == 0, c ? c->body : "");
            const core::Node* o = m.find(taxes);
            check("again: the old one is untouched -- files, ticks, stamp, done",
                  o && o->body == body && o->sent != 0 && o->task.done && o->title == "Taxes 2026");
            j.undo(m);
            check("again: one Ctrl+Z takes the copy away", !m.find(n2) && m.children(home).size() == 2);
        }
        check("again: a plain note is refused", core::packet_again(m, after).empty());
    }

    // ── s054: done-when ────────────────────────────────────────────────────
    {
        std::cout << "\n-- done-when (s054) --\n";
        namespace fs = std::filesystem;
        core::MemoryNodes m;
        const auto job = m.create("", "Kitchen");
        m.make_task(job, true);
        m.set_body(job, "- [x] tiles\n- [ ] grout [g](attachments/g.pdf)\n- [ ] sealant\n");
        check("done-when: just the tick by default -- met, nothing counted",
              core::done_state(m, job).met() && core::done_count(core::done_state(m, job)).empty());
        m.set_done_when(job, core::DoneWhen::Items);
        const auto st = core::done_state(m, job);
        check("done-when: Items counts the checkbox lines, a file or a tick is in",
              st.in == 2 && st.total == 3 && !st.met() && core::done_count(st) == "2 of 3 in" &&
                  st.missing.size() == 1 && st.missing[0] == "sealant",
              core::done_count(st));

        const auto proj = m.create("", "Move");
        const auto s1 = m.create(proj, "pack");
        const auto s2 = m.create(proj, "van");
        const auto s3 = m.create(proj, "keys");
        m.create(proj, "a plain note under it");
        for (const auto& k : {s1, s2, s3}) m.make_task(k, true);
        m.set_done(s1, true);
        core::set_project_state(m, s3, core::ProjectState::Dropped);
        m.set_done_when(proj, core::DoneWhen::Steps);
        const auto ps = core::done_state(m, proj);
        check("done-when: Steps counts the todos under it; dropped is dealt with; notes ignored",
              ps.total == 3 && ps.in == 2 && core::done_count(ps) == "2 of 3 steps" &&
                  ps.missing.size() == 1 && ps.missing[0] == "van",
              core::done_count(ps));

        const auto pk = m.create("", "Taxes");
        m.set_body(pk, "- [ ] W-2\n");
        m.set_packet(pk, true);
        check("done-when: a packet is Items whatever its setting says",
              core::effective_done_when(*m.find(pk)) == core::DoneWhen::Items &&
                  core::done_state(m, pk).total == 1);
        const auto empty = m.create("", "Empty");
        m.set_done_when(empty, core::DoneWhen::Items);
        check("done-when: no items yet is met (nothing to nag about)",
              core::done_state(m, empty).met() && !core::done_state(m, empty).counted());

        core::Task a, b;
        a.is_task = true; b = a; b.done = true;
        core::Task c, d; d.project = core::ProjectState::Completed;
        core::Task e, f; f.project = core::ProjectState::Dropped;
        check("done-when: a tick and a Complete finish; Drop and an untick do not",
              core::finishes(a, b) && core::finishes(c, d) && !core::finishes(e, f) && !core::finishes(b, a));

        // the question's words
        const auto one = core::done_ask(m, {job}, false);
        check("done-when: one -- the count and what is missing",
              one.message == "“Kitchen” is 2 of 3 in" && one.detail == "Missing: sealant." &&
                  one.button == "Tick Anyway",
              one.message + " / " + one.detail);
        const auto two = core::done_ask(m, {job, proj, empty}, true);
        check("done-when: many -- a line each, met ones left out; Complete Anyway",
              two.message == "2 aren’t finished yet" &&
                  two.detail == "“Kitchen” — 2 of 3 in\n“Move” — 2 of 3 steps" &&
                  two.button == "Complete Anyway",
              two.message + " / " + two.detail);
        check("done-when: all met -- no question", core::done_ask(m, {empty}, false).message.empty());

        // the gate, through the one door
        core::Journal j;
        core::UndoSource u(j, [&] { return &m; });
        std::vector<core::NodeId> asked;
        std::function<void()> again;
        u.set_gate([&](const core::NodeId& id, std::function<void()> retry) {
            asked.push_back(id);
            again = std::move(retry);
        });
        const bool refused = !u.set_done(job, true);
        check("gate: ticking an unmet done-when is refused and asked about, nothing written",
              refused && asked.size() == 1 && asked[0] == job && !m.find(job)->task.done && !j.can_undo());
        again();
        check("gate: Tick anyway -- done, one undo step", m.find(job)->task.done && j.can_undo() &&
                                                            j.undo_label() == "Tick");
        j.undo(m);
        check("gate: undone -- not done again", !m.find(job)->task.done);
        asked.clear();
        m.set_body(job, "- [x] tiles\n- [ ] grout [g](attachments/g.pdf)\n- [x] sealant\n");
        check("gate: met -- ticks without asking", u.set_done(job, true) && asked.empty() && m.find(job)->task.done);
        check("gate: unticking never asks", u.set_done(job, false) && asked.empty());
        check("gate: Complete on an unmet Steps project asks",
              !core::set_project_state(u, proj, core::ProjectState::Completed) && asked.size() == 1);
        asked.clear();
        {
            core::UndoSource::Pass pass(u);
            check("gate: a Pass goes through (a notification's Mark done)",
                  core::set_project_state(u, proj, core::ProjectState::Completed) && asked.empty());
        }
        check("gate: Drop never asks",
              core::set_project_state(u, proj, core::ProjectState::Dropped) && asked.empty());
        const auto plain = m.create("", "plain todo");
        m.make_task(plain, true);
        check("gate: a todo with no done-when ticks as before", u.set_done(plain, true) && asked.empty());

        m.make_task(job, false);
        check("done-when: kept when the note stops being a todo",
              m.find(job)->task.done_when == core::DoneWhen::Items);
        check("done-when: Undo names the change",
              core::task_label(core::Task{}, [] { core::Task t; t.done_when = core::DoneWhen::Steps; return t; }()) ==
                  "Done when");

        // the card / row's count
        const auto look = core::row_look(m, *m.find(s2), 0);
        m.make_task(proj, true);
        m.set_done(proj, false);
        core::set_project_state(m, proj, core::ProjectState::Active);
        const auto plook = core::row_look(m, *m.find(proj), 0);
        check("look: the card carries the count while unmet; a step with no rule has none",
              plook.done_when == "2 of 3 steps" && !plook.done_met && look.done_when.empty(),
              plook.done_when);

        const fs::path jd = fs::temp_directory_path() / "jot_selftest_donewhen.jots";
        std::error_code ec;
        fs::remove_all(jd, ec);
        core::NodeId x, y, z;
        {
            core::Project v;
            v.open(jd.string());
            x = v.create("", "Items");
            y = v.create("", "Steps");
            z = v.create("", "plain");
            v.set_done_when(x, core::DoneWhen::Items);
            v.set_done_when(y, core::DoneWhen::Steps);
            v.flush();
        }
        {
            core::Project v;
            v.open(jd.string());
            check("done-when/jots: the rule survives a reopen, only where it was set",
                  v.find(x) && v.find(x)->task.done_when == core::DoneWhen::Items && v.find(y) &&
                      v.find(y)->task.done_when == core::DoneWhen::Steps && v.find(z) &&
                      v.find(z)->task.done_when == core::DoneWhen::Tick);
        }
        fs::remove_all(jd, ec);
    }

    // ── s055: deadline ─────────────────────────────────────────────────────
    {
        std::cout << "\n-- deadline (s055) --\n";
        namespace fs = std::filesystem;
        // Stand at a fixed local noon so the day counts do not depend on today.
        std::tm t0{};
        t0.tm_year = 2026 - 1900; t0.tm_mon = 9; t0.tm_mday = 7; t0.tm_hour = 12; t0.tm_isdst = -1;
        const std::int64_t now = static_cast<std::int64_t>(std::mktime(&t0));
        const auto day = [&](int n) {   // 17:00 on the day n days from `now`
            std::tm t = t0;
            t.tm_mday += n; t.tm_hour = 17; t.tm_isdst = -1;
            return static_cast<std::int64_t>(std::mktime(&t));
        };

        core::MemoryNodes m;
        const auto mv = m.create("", "Move house");
        m.make_task(mv, true);
        const auto pack  = m.create(mv, "pack");
        const auto van   = m.create(mv, "book the van");
        const auto keys  = m.create(mv, "hand back keys");
        const auto clean = m.create(mv, "clean");
        m.create(mv, "a plain note under it");
        for (const auto& k : {pack, van, keys, clean}) m.make_task(k, true);
        m.set_done(pack, true);
        m.set_estimate(van, 30);
        m.set_estimate(clean, 90);
        m.set_due(mv, day(9));
        m.set_deadline(mv, true);

        auto s = core::deadline_state(m, mv, now);
        check("deadline: what is left -- the todos under it not done, their estimates, the unsized",
              s.steps == 3 && s.minutes == 120 && s.unsized == 1, std::to_string(s.steps));
        check("deadline: need = a day a step (or an hour), plus one spare", s.need == 4,
              std::to_string(s.need));
        check("deadline: 9 days out, needs 4 -- on track",
              s.pace == core::Pace::OnTrack && s.days_left == 9, core::pace_word(s.pace));
        check("deadline: the runway's words",
              core::runway_text(s) == "3 steps  \u00b7  ~2h (1 not sized)  \u00b7  9 days out",
              core::runway_text(s));
        const std::string on = core::pace_text(s, now);
        check("deadline: on track says when to start by and how much a day",
              on.rfind("On track \u2014 start by ", 0) == 0 &&
                  on.find("about 15m a day") != std::string::npos, on);
        m.set_due(mv, day(70));
        check("deadline: more than two weeks out -- the pace is per week",
              core::pace_text(core::deadline_state(m, mv, now), now).ends_with("about 15m a week"),
              core::pace_text(core::deadline_state(m, mv, now), now));
        m.set_due(mv, day(9));
        check("deadline: start by is the due day less what it needs",
              s.start_by == core::day_start(day(5)));
        check("deadline: on track pulls nothing", core::deadline_pulls(m, now).empty());
        check("deadline: the tree's mark -- 9d", core::deadline_mark(s) == "\u23f3 9d", core::deadline_mark(s));

        m.set_due(mv, day(4));
        s = core::deadline_state(m, mv, now);
        const auto pulls = core::deadline_pulls(m, now);
        check("deadline: 4 days left, needs 4 -- running short", s.pace == core::Pace::Short,
              core::pace_word(s.pace));
        check("deadline: the next step that can start is pulled forward",
              pulls.size() == 1 && pulls[0].deadline == mv && pulls[0].step == van);
        check("deadline: short says which step is on Today",
              core::pace_text(s, now) == "Running short \u2014 \u201cbook the van\u201d is on Today",
              core::pace_text(s, now));

        m.set_status(mv, core::Status::Sequential);
        m.set_defer(van, day(2));
        s = core::deadline_state(m, mv, now);
        check("deadline: nothing can start (the first step is deferred, in sequence) -- no pull",
              s.pace == core::Pace::Short && s.next.empty() && core::deadline_pulls(m, now).empty() &&
                  core::pace_text(s, now) == "Running short \u2014 nothing can start yet",
              core::pace_text(s, now));
        m.set_defer(van, 0);
        m.set_status(mv, core::Status::None);

        m.set_due(mv, day(-1));
        s = core::deadline_state(m, mv, now);
        check("deadline: past the due -- late, still pulls",
              s.pace == core::Pace::Late && s.days_left == -1 && core::deadline_pulls(m, now).size() == 1 &&
                  core::runway_text(s).ends_with("1 day late"),
              core::runway_text(s));

        for (const auto& k : {van, keys, clean}) m.set_done(k, true);
        s = core::deadline_state(m, mv, now);
        check("deadline: every step done -- ready, the work itself is next, no pull",
              s.pace == core::Pace::Ready && s.next == mv && core::deadline_pulls(m, now).empty() &&
                  core::pace_text(s, now) == "Everything is done \u2014 tick it.");
        m.set_done(mv, true);
        s = core::deadline_state(m, mv, now);
        check("deadline: ticked -- finished, nothing to say",
              s.pace == core::Pace::Finished && core::runway_text(s).empty());

        // one todo, no steps: it is its own step, sized by its own estimate
        const auto rep = m.create("", "Write the report");
        m.make_task(rep, true);
        m.set_estimate(rep, 180);
        m.set_due(rep, day(3));
        m.set_deadline(rep, true);
        s = core::deadline_state(m, rep, now);
        check("deadline: a lone todo is its own step; 3h needs 4 days -- short, pulls itself",
              s.steps == 1 && s.need == 4 && s.pace == core::Pace::Short && s.next == rep &&
                  core::deadline_pulls(m, now).size() == 1);

        // a packet: the missing items are the steps
        const auto tax = m.create("", "Taxes");
        m.make_task(tax, true);
        m.set_body(tax, "- [x] W-2\n- [ ] 1099-INT\n- [ ] HSA\n");
        m.set_packet(tax, true);
        m.set_due(tax, day(10));
        m.set_deadline(tax, true);
        s = core::deadline_state(m, tax, now);
        check("deadline: a packet's missing items are its steps",
              s.steps == 2 && s.unsized == 2 && s.need == 3 && s.pace == core::Pace::OnTrack);

        const auto nod = m.create("", "Someday");
        m.make_task(nod, true);
        m.set_deadline(nod, true);
        s = core::deadline_state(m, nod, now);
        check("deadline: no due date -- says to give it one",
              s.pace == core::Pace::NoDate &&
                  core::pace_text(s, now) == "Give it a due date and jot works back from it.");

        const auto plain = m.create("", "Not marked");
        m.make_task(plain, true);
        m.set_due(plain, day(1));
        check("deadline: an unmarked todo is never pulled",
              core::deadline_pulls(m, now).size() == 1);   // only the report

        const auto look = core::row_look(m, *m.find(rep), now);
        check("look: a deadline's card carries the runway and the pace",
              look.runway == "1 step  \u00b7  ~3h  \u00b7  3 days out" && look.pace == "short", look.runway);
        check("look: no runway on a todo that is not a deadline",
              core::row_look(m, *m.find(plain), now).runway.empty());

        m.make_task(rep, false);
        check("deadline: kept when the note stops being a todo", m.find(rep)->task.deadline);
        check("deadline: Undo names the change",
              core::task_label(core::Task{}, [] { core::Task t; t.deadline = true; return t; }()) == "Deadline" &&
                  core::task_label([] { core::Task t; t.deadline = true; return t; }(), core::Task{}) ==
                      "Not a deadline");

        // through the one door: one step, one undo
        core::Journal j;
        core::UndoSource u(j, [&] { return &m; });
        check("deadline: set through the door -- one step, undone whole",
              u.set_deadline(plain, true) && m.find(plain)->task.deadline && j.undo_label() == "Deadline");
        j.undo(m);
        check("deadline: undone", !m.find(plain)->task.deadline);

        const fs::path jd = fs::temp_directory_path() / "jot_selftest_deadline.jots";
        std::error_code ec;
        fs::remove_all(jd, ec);
        core::NodeId x, y;
        {
            core::Project v;
            v.open(jd.string());
            x = v.create("", "Deadline");
            y = v.create("", "plain");
            v.set_deadline(x, true);
            v.flush();
        }
        {
            core::Project v;
            v.open(jd.string());
            check("deadline/jots: the mark survives a reopen, only where it was set",
                  v.find(x) && v.find(x)->task.deadline && v.find(y) && !v.find(y)->task.deadline);
        }
        fs::remove_all(jd, ec);
    }

    // ── s053b: zoom ────────────────────────────────────────────────────────
    {
        std::cout << "\n-- zoom (s053b) --\n";
        check("zoom: in steps up, out steps down, and back lands where it began",
              core::zoom_in(100) == 110 && core::zoom_out(100) == 90 &&
                  core::zoom_out(core::zoom_in(125)) == 125 && core::zoom_in(core::zoom_out(70)) == 80);
        check("zoom: the ends hold", core::zoom_in(300) == 300 && core::zoom_out(70) == 70);
        check("zoom: junk lands on a step",
              core::zoom_clamp(0) == 100 && core::zoom_clamp(-5) == 100 && core::zoom_clamp(123) == 125 &&
                  core::zoom_clamp(9999) == 300 && core::zoom_text(150) == "150%");
        namespace fs = std::filesystem;
        const std::string f = (fs::temp_directory_path() / "jot_selftest_zoom.json").string();
        core::Prefs p;
        p.zoom = 175;
        core::save_prefs(f, p);
        const bool kept = core::load_prefs(f).zoom == 175;
        { std::ofstream o(f); o << "{\"zoom\": 133}"; }
        const bool snapped = core::load_prefs(f).zoom == 125;
        check("zoom pref: round trip, and an odd value snaps to a step", kept && snapped);
        std::error_code ec;
        fs::remove(f, ec);
    }

    // ── s045: undo in the navigator ────────────────────────────────────────
    {
        std::cout << "\n-- navigator undo (s045) --\n";
        const auto kids = [](const core::NodeSource& m, const core::NodeId& p) {
            std::string out;
            for (const auto& k : m.children(p)) out += m.find(k)->title + ",";
            return out;
        };

        // The bug found on the way in: remove() rebuilt sibling order from
        // storage order, quietly undoing earlier moves among the survivors.
        {
            core::MemoryNodes m;
            const auto a = m.create("", "a");
            const auto b = m.create("", "b");
            const auto c = m.create("", "c");
            m.move(c, "", 0);
            check("order: a move is seen", kids(m, "") == "c,a,b,", kids(m, ""));
            m.remove(b);
            check("order: deleting another note keeps the move", kids(m, "") == "c,a,", kids(m, ""));
            (void)a;
        }

        // s046: after a delete the selection lands beside it, so the keys
        // (and the next Ctrl+Z) stay in the tree. Next, else previous, else
        // the parent; a lone root leaves nothing.
        {
            core::MemoryNodes m;
            const auto p  = m.create("", "p");
            const auto k1 = m.create(p, "k1");
            const auto k2 = m.create(p, "k2");
            const auto k3 = m.create(p, "k3");
            const auto solo = m.create(k2, "solo");
            check("neighbour: the next sibling", core::neighbour_after_delete(m, k1) == k2);
            check("neighbour: the last one takes the one before", core::neighbour_after_delete(m, k3) == k2);
            check("neighbour: an only child gives the parent", core::neighbour_after_delete(m, solo) == k2);
            check("neighbour: a lone root leaves nothing", core::neighbour_after_delete(m, p).empty());
            m.move(k3, p, 0);   // order is m_kids', not storage's
            check("neighbour: follows a reorder", core::neighbour_after_delete(m, k3) == k1);
            check("neighbour: an unknown id leaves nothing", core::neighbour_after_delete(m, "nope").empty());
        }

        core::MemoryNodes m;
        core::Journal j;
        const auto taxes = m.create("", "Taxes");
        const auto w2 = m.create(taxes, "W-2");
        const auto form = m.create(taxes, "Form");
        m.set_body(w2, "the employer copy");
        m.make_task(form, true);
        m.set_due(form, 1'800'000'000);
        const auto home = m.create("", "Home");

        // delete a subtree
        check("undo: a delete is a step",
              j.run(m, "Delete Taxes", {taxes}, true, [&] { m.remove(taxes); return std::vector<core::NodeId>{}; }));
        check("undo: ...and it went", !m.find(taxes) && !m.find(w2) && kids(m, "") == "Home,");
        check("undo: the label", j.undo_label() == "Delete Taxes");
        j.undo(m);
        check("undo: the subtree is back under the same ids, in its place",
              m.find(taxes) && m.find(w2) && m.find(form) && kids(m, "") == "Taxes,Home," &&
                  kids(m, taxes) == "W-2,Form,", kids(m, "") + " / " + kids(m, taxes));
        check("undo: with its text and its todo record",
              m.find(w2)->body == "the employer copy" && m.find(form)->task.is_task &&
                  m.find(form)->task.due == 1'800'000'000);
        check("undo: redo is offered", j.can_redo() && j.redo_label() == "Delete Taxes");
        j.redo(m);
        check("redo: gone again", !m.find(taxes) && !m.find(form));
        j.undo(m);

        // move, and its exact place back
        j.run(m, "Move", {w2}, true, [&] { m.move(w2, home, -1); return std::vector<core::NodeId>{}; });
        check("move: done", kids(m, home) == "W-2," && kids(m, taxes) == "Form,");
        j.undo(m);
        check("move: undone to its old place (first, not last)", kids(m, taxes) == "W-2,Form,",
              kids(m, taxes));
        // reorder within one parent, both directions
        j.run(m, "Down", {w2}, false, [&] { m.move(w2, taxes, 2); return std::vector<core::NodeId>{}; });
        check("reorder: moved down", kids(m, taxes) == "Form,W-2,", kids(m, taxes));
        j.undo(m);
        check("reorder: undone", kids(m, taxes) == "W-2,Form,", kids(m, taxes));
        j.redo(m);
        check("reorder: redone", kids(m, taxes) == "Form,W-2,", kids(m, taxes));
        j.undo(m);

        // create
        core::NodeId made;
        j.run(m, "New note", {}, false, [&] { made = m.create(taxes, "1099"); return std::vector<core::NodeId>{made}; });
        check("create: made", m.find(made) && kids(m, taxes) == "W-2,Form,1099,");
        j.undo(m);
        check("create: undone", !m.find(made) && kids(m, taxes) == "W-2,Form,");
        j.redo(m);
        check("create: redone under the same id", m.find(made) && m.find(made)->title == "1099");
        j.undo(m);

        // fields: tick, title
        j.run(m, "Tick", {form}, false, [&] { m.set_done(form, true); return std::vector<core::NodeId>{}; });
        j.run(m, "Rename", {w2}, false, [&] { m.set_title(w2, "W2 form"); return std::vector<core::NodeId>{}; });
        j.undo(m);
        check("fields: rename undone", m.find(w2)->title == "W-2");
        j.undo(m);
        check("fields: tick undone", !m.find(form)->task.done);

        // undoing an old step leaves later, unrelated edits alone
        j.run(m, "Rename", {w2}, false, [&] { m.set_title(w2, "W2"); return std::vector<core::NodeId>{}; });
        m.set_body(w2, "typed after the rename");
        j.undo(m);
        check("undo touches only what its step changed",
              m.find(w2)->title == "W-2" && m.find(w2)->body == "typed after the rename");
        check("undo: the focus is the node the step was about", j.last_focus() == w2);

        // a step that changed nothing is not kept
        const auto n0 = j.size();
        check("nothing changed, no step",
              !j.run(m, "Move onto itself", {w2}, true, [&] { m.move(w2, taxes, 0); return std::vector<core::NodeId>{}; }) &&
                  j.size() == n0);
        // a new step forgets the redo branch
        j.run(m, "Rename", {w2}, false, [&] { m.set_title(w2, "x"); return std::vector<core::NodeId>{}; });
        check("a new step drops the redo branch", !j.can_redo());
        j.clear();
        check("clear", !j.can_undo() && !j.can_redo() && j.size() == 0);

        // the disk: an undone delete brings the body FILE back
        namespace fs = std::filesystem;
        const std::string dir = (fs::temp_directory_path() / "jot_selftest_undo").string();
        std::error_code ec;
        fs::remove_all(dir, ec);
        core::NodeId a;
        {
            core::Project v;
            v.open(dir);
            a = v.create("", "Keep me");
            v.set_body(a, "words that must survive");
            v.flush();
            core::Journal jj;
            jj.run(v, "Delete", {a}, true, [&] { v.remove(a); return std::vector<core::NodeId>{}; });
            v.flush();
            jj.undo(v);
            v.flush();
        }
        {
            core::Project v;
            v.open(dir);
            check("undo/jots: the undeleted note and its text survive a reopen",
                  v.find(a) && v.find(a)->body == "words that must survive");
        }
        fs::remove_all(dir, ec);
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


    // -- Tags as contexts (s035): a filter over inline #tags --------------------
    // The body is the source; these are queries. Pinned: case folds, nesting
    // rolls up (and only downward), no inheritance, and a todo lands in exactly
    // one of available / waiting / finished.
    {
        const std::int64_t now = 1'800'000'000;
        check("tags: the key folds case and drops a trailing slash",
              core::tag_key("Home/Garden/") == "home/garden" && core::tag_key("ERRANDS") == "errands");
        check("tags: nested is under its parent",
              core::tag_under("home/garden", "home") && core::tag_under("home", "home"));
        check("tags: a longer word is not nested",
              !core::tag_under("homework", "home") && !core::tag_under("home", "home/garden"));
        check("tags: an empty filter matches nothing", !core::tag_under("home", ""));

        // s036: the search over the chips. Folded like a key, matched anywhere,
        // Enter picks the exact key or the lone match and nothing else.
        check("tags search: the query folds case, spaces and leading #s",
              core::tag_query("  ##HoMe/G ") == "home/g" && core::tag_query("") == "");
        check("tags search: matches anywhere in the key",
              core::tag_matches("home", "ho") && core::tag_matches("phone", "ho") &&
              core::tag_matches("home/garden", "gard") && !core::tag_matches("errands", "ho"));
        check("tags search: an empty query matches every chip",
              core::tag_matches("errands", "") && core::tag_matches("", ""));
        {
            std::vector<core::TagInfo> L(4);
            L[0].key = "errands"; L[1].key = "home"; L[2].key = "home/garden"; L[3].key = "phone";
            check("tags search: Enter picks the lone match",
                  core::tag_pick(L, "err") == "errands" && core::tag_pick(L, "#GARD") == "home/garden");
            check("tags search: Enter picks the exact key over the others it also matches",
                  core::tag_pick(L, "home") == "home" && core::tag_pick(L, "Home ") == "home");
            check("tags search: several, none or empty picks nothing",
                  core::tag_pick(L, "ho").empty() && core::tag_pick(L, "zzz").empty() &&
                  core::tag_pick(L, "  #").empty() && core::tag_pick({}, "home").empty());
        }

        core::MemoryNodes m;
        const auto proj = m.create("", "Saturday");
        m.set_body(proj, "Things for the weekend #Errands");
        m.set_status(proj, core::Status::Sequential);
        const auto a = m.create(proj, "bank");
        const auto b = m.create(proj, "hardware store");
        for (const auto& id : {a, b}) m.make_task(id, true);
        m.set_body(a, "deposit #errands #errands");          // twice: one tag
        m.set_body(b, "screws #errands #home/garden");
        const auto c = m.create("", "seeds");
        m.make_task(c, true);
        m.set_body(c, "#home/garden later");
        m.set_defer(c, now + 86400 * 3);
        const auto d = m.create("", "milk");
        m.make_task(d, true);
        m.set_body(d, "#errands");
        m.set_done(d, true);
        const auto e = m.create("", "plain note");
        m.set_body(e, "about the `#notatag` and #Home");
        const auto f = m.create("", "homework");
        m.set_body(f, "#homework");

        check("tags: a node's keys are deduped, folded, in text order", [&] {
            const auto k = core::node_tag_keys(*m.find(b));
            return k.size() == 2 && k[0] == "errands" && k[1] == "home/garden";
        }());
        check("tags: a hash in a code span is not a key",
              core::node_tag_keys(*m.find(e)) == std::vector<std::string>{"home"});

        const auto list = core::tag_list(m, now);
        auto info = [&](const std::string& k) -> const core::TagInfo* {
            for (const auto& t : list) if (t.key == k) return &t;
            return nullptr;
        };
        check("tags: the list is sorted, parents before their nests", [&] {
            std::vector<std::string> ks;
            for (const auto& t : list) ks.push_back(t.key);
            return ks == std::vector<std::string>{"errands", "home", "home/garden", "homework"};
        }());
        const auto* er = info("errands");
        check("tags: the name is the first spelling in tree order",
              er && er->name == "Errands");
        check("tags: counts are distinct nodes -- one note, two todos open, one available",
              er && er->notes == 1 && er->open == 2 && er->available == 1,
              er ? std::to_string(er->notes) + "/" + std::to_string(er->open) + "/" +
                       std::to_string(er->available) : "missing");
        const auto* ho = info("home");
        check("tags: a parent rolls up its nests", ho && ho->written && ho->notes == 1 && ho->open == 2);
        const auto* hg = info("home/garden");
        check("tags: a nest has depth 1", hg && hg->depth == 1 && hg->open == 2 && hg->available == 0);

        auto mem = core::tag_members(m, "errands", now);
        check("tags: available is the sequence's first step",
              mem.available == std::vector<core::NodeId>{a});
        check("tags: the second step waits", mem.waiting == std::vector<core::NodeId>{b});
        check("tags: the project note is listed as a note", mem.notes == std::vector<core::NodeId>{proj});
        check("tags: a done todo is counted, not listed", mem.finished == 1);
        check("tags: the filter folds case too",
              core::tag_members(m, "ERRANDS", now).available.size() == 1);

        mem = core::tag_members(m, "home", now);
        check("tags: #home shows #home/garden's nodes, in tree order",
              mem.waiting == std::vector<core::NodeId>{b, c} &&
                  mem.notes == std::vector<core::NodeId>{e});
        check("tags: #home does not take #homework", [&] {
            for (const auto& id : mem.notes) if (id == f) return false;
            return true;
        }());
        mem = core::tag_members(m, "home/garden", now);
        check("tags: #home/garden does not show plain #home", mem.notes.empty());

        check("tags: no inheritance -- the project's tag is not its steps'", [&] {
            m.set_body(a, "deposit");
            const auto k = core::tag_members(m, "errands", now);
            return k.available.empty() && k.notes == std::vector<core::NodeId>{proj};
        }());
        check("tags: why a todo waits",
              core::waiting_reason(m, b, now).rfind("Blocked", 0) == 0 &&
                  core::waiting_reason(m, c, now).rfind("Deferred until ", 0) == 0 &&
                  core::waiting_reason(m, a, now).empty());

        core::MemoryNodes empty;
        check("tags: an empty folder has no tags", core::tag_list(empty, now).empty());
    }


    // -- The tag line (s035b): tags edited in Note details, kept as text -------
    {
        auto apply = [](std::string body, const core::FmtEdit& ed) {
            // Codepoint offsets; these bodies are ASCII except where noted.
            auto byte_at = [&](int cp) {
                std::size_t i = 0; int c = 0;
                while (i < body.size() && c < cp) {
                    ++i;
                    while (i < body.size() && (static_cast<unsigned char>(body[i]) & 0xC0) == 0x80) ++i;
                    ++c;
                }
                return i;
            };
            const auto b = byte_at(ed.cp_begin), e = byte_at(ed.cp_end);
            return body.substr(0, b) + ed.text + body.substr(e);
        };
        check("tagline: a last line of only tags is the tag line",
              core::find_tag_line("Buy screws\n\n#errands #home/garden").found &&
                  core::find_tag_line("Buy screws\n\n#errands #home/garden\n\n").names.size() == 2);
        check("tagline: a sentence that mentions a tag is not",
              !core::find_tag_line("Go to the shop #errands").found);
        check("tagline: a heading is not", !core::find_tag_line("x\n\n# errands").found);
        check("tagline: a tag inside code is not", !core::find_tag_line("x\n\n`#errands`").found);

        check("tagline: clean drops the hash and spaces",
              core::clean_tag_name("  #Home garden ") == "Home-garden");
        check("tagline: clean refuses a digit start and junk",
              core::clean_tag_name("#1st").empty() && core::clean_tag_name("a b!c").empty() &&
                  core::clean_tag_name("   ").empty());
        check("tagline: clean keeps nesting", core::clean_tag_name("home/garden/") == "home/garden");

        std::string body = "Buy screws";
        auto ed = core::tag_add_edit(body, "errands");
        body = apply(body, ed);
        check("tagline: the first add makes the line after a blank one",
              ed.ok && body == "Buy screws\n\n#errands", body);
        ed = core::tag_add_edit(body, "#home/garden");
        body = apply(body, ed);
        check("tagline: the next add joins the line", ed.ok && body == "Buy screws\n\n#errands #home/garden", body);
        check("tagline: a tag already carried (any case) is not added again",
              !core::tag_add_edit(body, "Errands").ok);
        check("tagline: a tag in the text counts as carried",
              !core::tag_add_edit("see #idea here", "idea").ok);
        check("tagline: an add to a body ending in a newline makes one blank line",
              apply("a\n", core::tag_add_edit("a\n", "x")) == "a\n\n#x");
        check("tagline: an add to an empty body is just the line",
              apply("", core::tag_add_edit("", "x")) == "#x" &&
                  apply("\n\n", core::tag_add_edit("\n\n", "x")) == "#x");
        check("tagline: an add puts the cursor after the new tag",
              core::tag_add_edit("ab", "x").sel_begin == 6);
        check("tagline: a list's last item keeps its own line",
              apply("- [ ] one", core::tag_add_edit("- [ ] one", "x")) == "- [ ] one\n\n#x");

        ed = core::tag_remove_edit(body, "ERRANDS");
        body = apply(body, ed);
        check("tagline: remove takes one tag, any case", ed.ok && body == "Buy screws\n\n#home/garden", body);
        ed = core::tag_remove_edit(body, "home/garden");
        body = apply(body, ed);
        check("tagline: removing the last one takes the line and the blank before it",
              ed.ok && body == "Buy screws", body);
        check("tagline: a tag in the text is not removable from here",
              !core::tag_remove_edit("see #idea here", "idea").ok);
        check("tagline: a note of only a tag line empties",
              apply("#a", core::tag_remove_edit("#a", "a")).empty());
        check("tagline: offsets are codepoints",
              apply("café", core::tag_add_edit("café", "x")) == "café\n\n#x");

        // And the Tags view reads the line like any other text.
        core::MemoryNodes m;
        const auto n = m.create("", "n");
        m.set_body(n, "Buy screws\n\n#errands");
        m.make_task(n, true);
        m.set_done(n, true);
        const auto mem = core::tag_members(m, "errands", 1'800'000'000);
        check("tagline: a done todo is in the Done list", mem.done == std::vector<core::NodeId>{n} &&
                                                              mem.finished == 1);
    }


    // -- s037: Review (road item 8) -------------------------------------------
    // A project wants a look every week (or its own interval); Mark Reviewed
    // starts the clock again and does not touch `modified`. What counts as a
    // project here is a todo DIRECTLY under it, or a container setting --
    // so a folder of folders does not ask for a weekly look.
    {
        std::tm tm{};
        tm.tm_year = 2026 - 1900; tm.tm_mon = 9; tm.tm_mday = 5; tm.tm_hour = 12; tm.tm_isdst = -1;
        const std::int64_t mon = static_cast<std::int64_t>(std::mktime(&tm));   // Mon 2026-10-05 12:00
        const std::int64_t day = 24 * 3600;
        std::int64_t clock = mon;
        core::MemoryNodes m;
        m.set_clock([&clock]() { return clock; });

        const auto home    = m.create("", "Home");          // a folder: holds a project, no todo
        const auto kitchen = m.create(home, "Kitchen");     // a project: a todo under it
        const auto paint   = m.create(kitchen, "Paint");
        const auto tiles   = m.create(kitchen, "Tiles");
        m.make_task(paint, true);
        m.make_task(tiles, true);
        const auto recipes = m.create("", "Recipes");       // reference notes only
        m.create(recipes, "Soup");
        const auto garden  = m.create("", "Garden");        // said something as a container
        m.set_status(garden, core::Status::Parallel);
        const auto old     = m.create("", "Old plan");
        const auto oldstep = m.create(old, "Step");
        m.make_task(oldstep, true);
        const auto inner   = m.create(old, "Inner");
        m.make_task(m.create(inner, "Deep"), true);

        check("review/project: a todo directly under it makes a project",
              core::is_project(m, kitchen) && core::is_project(m, old) && core::is_project(m, inner));
        check("review/project: a folder of projects is not one; reference notes are not",
              !core::is_project(m, home) && !core::is_project(m, recipes));
        check("review/project: a Children order alone makes one",
              core::is_project(m, garden));
        check("review/default: no interval reads as every week",
              core::review_every(m.find(kitchen)->task).every == 1 &&
                  core::review_every(m.find(kitchen)->task).unit == core::RepeatUnit::Week);

        check("review/due: a project made today is not due today",
              core::review_list(m, clock).empty());
        check("review/when: made Monday, the next look is in 7 days",
              core::review_when(*m.find(kitchen), clock) == "Next review Mon 12 Oct",
              core::review_when(*m.find(kitchen), clock));

        clock = mon + 7 * day;
        auto due = core::review_list(m, clock);
        check("review/due: a week on, every reviewable project is due, in document order",
              due == std::vector<core::NodeId>{kitchen, garden, old, inner});
        check("review/when: due today says so",
              core::review_when(*m.find(kitchen), clock) == "Review today");

        // Drop the old plan: it and the project inside it leave the list.
        core::set_project_state(m, old, core::ProjectState::Dropped);
        check("review/state: a dropped project, and one inside it, are not reviewed",
              core::review_list(m, clock) == std::vector<core::NodeId>{kitchen, garden});
        core::set_project_state(m, garden, core::ProjectState::OnHold);
        check("review/state: an on-hold project IS reviewed -- that is what a review is for",
              core::review_list(m, clock) == std::vector<core::NodeId>{kitchen, garden});

        const std::int64_t mod_before = m.find(kitchen)->modified;
        clock = mon + 7 * day + 3600;
        check("review/mark: Mark Reviewed writes", core::mark_reviewed(m, kitchen, clock));
        check("review/mark: it does not change `modified` -- a look is not an edit",
              m.find(kitchen)->modified == mod_before);
        check("review/mark: it leaves the list",
              core::review_list(m, clock) == std::vector<core::NodeId>{garden});
        check("review/when: the clock starts again from the look",
              core::review_when(*m.find(kitchen), clock) == "Next review Mon 19 Oct",
              core::review_when(*m.find(kitchen), clock));
        check("review/when: reviewed_when names the day",
              core::reviewed_when(*m.find(kitchen)) == "Reviewed 2026-10-12" &&
                  core::reviewed_when(*m.find(garden)) == "Never reviewed");

        // Its own interval. A week stores nothing (the default).
        core::Repeat every;
        core::repeat_parse("every 2 days", every);
        check("review/every: an interval is stored", core::set_review_every(m, garden, every) &&
                                                         m.find(garden)->task.review.every == 2);
        core::mark_reviewed(m, garden, clock);
        check("review/every: two days later it is due again",
              core::review_list(m, clock + day).empty() &&
                  core::review_list(m, clock + 2 * day) == std::vector<core::NodeId>{garden});
        check("review/when: in N days, tomorrow",
              core::review_when(*m.find(garden), clock + 0) == "Next review in 2 days" &&
                  core::review_when(*m.find(garden), clock + day) == "Next review tomorrow",
              core::review_when(*m.find(garden), clock));
        check("review/when: overdue says since when",
              core::review_when(*m.find(garden), clock + 3 * day) == "Review due since yesterday" &&
                  core::review_when(*m.find(garden), clock + 4 * day) == "Review due since Wednesday",
              core::review_when(*m.find(garden), clock + 3 * day));
        core::repeat_parse("weekly", every);
        check("review/every: weekly is the default, so it stores nothing",
              core::set_review_every(m, garden, every) && !m.find(garden)->task.review.on());

        check("review/next_up: the soonest not-yet-due project",
              core::next_up(m, clock) == garden || core::next_up(m, clock) == kitchen);

        // Un-todo a todo-project: the review fields stay with the container.
        const auto tp = m.create("", "Todo project");
        m.make_task(tp, true);
        m.make_task(m.create(tp, "a step"), true);
        core::mark_reviewed(m, tp, clock);
        m.make_task(tp, false);
        check("review/untodo: the last look survives un-tasking",
              m.find(tp)->task.reviewed == clock);

        // By state, and counts.
        const auto by = core::projects_by_state(m);
        check("review/by-state: each project under the strongest state in its chain",
              by.active == std::vector<core::NodeId>{kitchen, tp} &&
                  by.on_hold == std::vector<core::NodeId>{garden} &&
                  by.dropped == std::vector<core::NodeId>{old, inner} &&
                  by.completed.empty());
        {
            // A state alone makes one (a held note-project with only notes under
            // it); inside a COMPLETED project, a project lists as Completed.
            core::MemoryNodes q;
            const auto idea = q.create("", "Someday idea");
            q.create(idea, "a thought");
            check("review/project: a non-Active state alone makes one",
                  !core::is_project(q, idea) &&
                      core::set_project_state(q, idea, core::ProjectState::OnHold) &&
                      core::is_project(q, idea));
            const auto done = q.create("", "Done thing");
            const auto sub  = q.create(done, "Sub");
            q.make_task(q.create(done, "x"), true);
            q.make_task(q.create(sub, "y"), true);
            core::set_project_state(q, done, core::ProjectState::Completed);
            const auto qb = core::projects_by_state(q);
            check("review/by-state: inside a completed project is Completed",
                  qb.completed == std::vector<core::NodeId>{done, sub} && qb.active.empty());
        }
        m.set_done(paint, true);
        const auto c = core::project_counts(m, kitchen, clock);
        check("review/counts: left, available, total, next",
              c.left == 1 && c.available == 1 && c.total == 2 && c.next == tiles);

        // Round trip through a jots folder.
        const std::string dir =
            (std::filesystem::temp_directory_path() / "jot_selftest_review").string();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        core::NodeId pid;
        {
            core::Project v;
            v.open(dir);
            pid = v.create("", "P");
            v.make_task(v.create(pid, "s"), true);
            core::repeat_parse("every 3 weeks", every);
            core::set_review_every(v, pid, every);
            core::mark_reviewed(v, pid, mon);
            v.flush();
        }
        {
            core::Project v;
            v.open(dir);
            const core::Node* p = v.find(pid);
            check("review/persist: interval and last look round-trip",
                  p && p->task.review.every == 3 && p->task.review.unit == core::RepeatUnit::Week &&
                      p->task.reviewed == mon);
        }
        std::filesystem::remove_all(dir, ec);
    }


    // -- s037b: a project said on purpose ---------------------------------------
    {
        core::MemoryNodes m;
        const auto ref  = m.create("", "Recipes");           // info notes only
        m.create(ref, "Soup");
        const auto misc = m.create("", "Misc");              // holds a todo
        const auto t    = m.create(misc, "a todo");
        m.make_task(t, true);
        check("mark/auto: the rule as before",
              !core::is_project(m, ref) && core::is_project(m, misc));
        check("mark/on: a reference project of notes is a project",
              core::set_project_mark(m, ref, core::ProjectMark::On) && core::is_project(m, ref) &&
                  !core::inferred_project(m, ref));
        check("mark/off: a folder holding todos is never one",
              core::set_project_mark(m, misc, core::ProjectMark::Off) && !core::is_project(m, misc));
        check("mark/off: and it leaves Review and the lists",
              core::review_list(m, m.find(misc)->created + 30 * 86400) ==
                      std::vector<core::NodeId>{ref} &&
                  core::projects_by_state(m).active == std::vector<core::NodeId>{ref});

        // A project's own dates and flag pass down; a todo-project said On keeps
        // them when it stops being a todo.
        const auto p  = m.create("", "Trip");
        core::set_project_mark(m, p, core::ProjectMark::On);
        const auto st = m.create(p, "book hotel");
        m.make_task(st, true);
        m.set_due(p, 2000000000);
        m.set_flagged(p, true);
        check("mark/dates: a non-todo project's due passes down",
              core::effective_due(m, st) == 2000000000);
        check("mark/flag: its flag passes down", core::effective_flagged(m, st));
        core::TaskIndex ix;
        ix.rebuild(m);
        const auto fl = ix.query(m, core::Filter::Flagged, 1900000000);
        check("mark/flag: the step shows in Flagged",
              std::find(fl.begin(), fl.end(), st) != fl.end());
        m.make_task(p, true);
        m.make_task(p, false);
        check("mark/untodo: a project said On keeps its due and flag",
              m.find(p)->task.due == 2000000000 && m.find(p)->task.flagged);

        const std::string dir =
            (std::filesystem::temp_directory_path() / "jot_selftest_mark").string();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        core::NodeId a, b;
        {
            core::Project v;
            v.open(dir);
            a = v.create("", "on");
            b = v.create("", "off");
            core::set_project_mark(v, a, core::ProjectMark::On);
            core::set_project_mark(v, b, core::ProjectMark::Off);
            v.flush();
        }
        {
            core::Project v;
            v.open(dir);
            check("mark/persist: On and Off round-trip",
                  v.find(a)->task.mark == core::ProjectMark::On &&
                      v.find(b)->task.mark == core::ProjectMark::Off);
        }
        std::filesystem::remove_all(dir, ec);
    }


    // -- s038: search across every note ---------------------------------------
    {
        core::MemoryNodes m;
        const auto shed  = m.create("", "Paint the shed");
        m.set_body(shed, "Buy exterior paint.\nThe café wants Sage Green, two coats.\n#errands");
        const auto sub   = m.create(shed, "Brushes");
        m.set_body(sub, "Wide brush for the shed walls.");
        const auto other = m.create("", "Groceries");
        m.set_body(other, "milk, eggs, paint thinner? no -- #errands");
        const auto green = m.create("", "Green tea notes");

        check("search/words: lower-cased, split on spaces",
              core::search_words("  Sage  GREEN ") == std::vector<std::string>{"sage", "green"});
        check("search/empty: nothing to look for finds nothing",
              core::search(m, "   ").empty());
        auto h = core::search(m, "paint");
        check("search/rank: name hits first, then body hits in tree order",
              h.size() == 2 && h[0].id == shed && h[0].in_name && h[1].id == other && !h[1].in_name);
        h = core::search(m, "green sage");
        check("search/all words: every word must be in the note, any order",
              h.size() == 1 && h[0].id == shed);
        check("search/snippet: the line it matched on",
              h.size() == 1 && h[0].snippet == "The café wants Sage Green, two coats.", h.empty() ? "" : h[0].snippet);
        // "The café wants " is 15 codepoints (é is two bytes, one codepoint).
        check("search/offset: codepoints, not bytes, to the first match",
              h.size() == 1 && h[0].cp_start == 20 + 15 && h[0].cp_len == 4,
              h.empty() ? "" : std::to_string(h[0].cp_start));
        h = core::search(m, "#errands");
        check("search/tags: a tag is a word", h.size() == 2);
        h = core::search(m, "green");
        check("search/name only: a name hit with nothing in the body has no snippet",
              h.size() == 2 && h[0].id == green && h[0].snippet.empty() && h[0].cp_start == -1 &&
                  h[1].id == shed);
        check("search/nothing: no match is an empty list", core::search(m, "zebra").empty());
        check("search/count and cap",
              core::search_count(m, "e") == 4 && core::search(m, "e", 2).size() == 2);
        std::string longline(300, 'x');
        longline.replace(200, 6, "needle");
        m.set_body(other, longline);
        h = core::search(m, "needle");
        check("search/snippet: a long line is cut around the hit",
              h.size() == 1 && h[0].snippet.find("needle") != std::string::npos &&
                  h[0].snippet.size() < 120 && h[0].snippet.rfind("…", 0) == 0);
    }

    // -- s038b: the query language and perspectives -------------------------
    {
        const std::int64_t now = 1789300000;   // a fixed instant
        core::MemoryNodes m;
        const auto shop = m.create("", "Shopping");
        m.set_body(shop, "Things to pick up.");
        const auto milk = m.create(shop, "Milk");
        m.make_task(milk, true);
        m.set_body(milk, "#errands #store");
        m.set_due(milk, core::day_end(now));                    // due today
        const auto tyre = m.create(shop, "Tyre pressure");
        m.make_task(tyre, true);
        m.set_body(tyre, "#car");
        m.set_due(tyre, now + 3 * 86400);                       // within the week
        const auto late = m.create("", "Return the drill");
        m.make_task(late, true);
        m.set_body(late, "#errands");
        m.set_due(late, now - 86400);                           // overdue
        m.set_flagged(late, true);
        const auto later = m.create("", "Paint the shed");
        m.make_task(later, true);
        m.set_body(later, "#home/garden someday");
        m.set_defer(later, now + 10 * 86400);                   // waiting
        const auto ticked = m.create("", "Post the letter");
        m.make_task(ticked, true);
        m.set_body(ticked, "#errands");
        m.set_due(ticked, core::day_end(now));
        m.set_done(ticked, true);
        const auto idea = m.create("", "Garden ideas");
        m.set_body(idea, "\n  Raised beds by the fence.\n#home");
        m.set_inbox(idea, true);

        auto ids = [&](const std::string& q) {
            std::vector<core::NodeId> out;
            for (const auto& h : core::search(m, q, 200, now)) out.push_back(h.id);
            return out;
        };
        using V = std::vector<core::NodeId>;

        check("query/tag: a tag is the tag, not the word (#errands)",
              ids("#errands") == V{milk, late, ticked});
        check("query/tag: nested and typed-so-far both count (#home, #gard -> no; #home/g)",
              ids("#home") == V{later, idea} && ids("#home/g") == V{later});
        check("query/and: two tags, both", ids("#errands #store") == V{milk});
        check("query/or: either tag", ids("#store or #car") == V{milk, tyre});
        check("query/or binds only its neighbours: (#car or #store) and is:flagged",
              ids("#car or #store is:flagged").empty() &&
                  ids("#car or #errands is:flagged") == V{late});
        check("query/not: a minus excludes", ids("#errands -is:done") == V{milk, late});
        check("query/is:remaining leaves out done", ids("is:remaining") == V{milk, tyre, late, later});
        check("query/is:available", ids("is:available") == V{milk, tyre, late});
        check("query/is:waiting: deferred", ids("is:waiting") == V{later});
        check("query/is:done", ids("is:done") == V{ticked});
        check("query/is:note and is:inbox", ids("is:note") == V{shop, idea} && ids("is:inbox") == V{idea});
        check("query/is:flagged", ids("is:flagged") == V{late});
        check("query/due:overdue", ids("due:overdue") == V{late});
        check("query/due:today is due BY today (late included, done not)",
              ids("due:today") == V{milk, late});
        check("query/due:week", ids("due:week") == V{milk, tyre, late});
        check("query/due:none: remaining todos with no date", ids("due:none") == V{later});
        check("query/project: inferred from the todos under it", ids("is:project") == V{shop});
        check("query/unknown is: is a word", ids("is:foo").empty() && !core::parse_query("is:foo").has_filters());
        check("query/words still work with filters", ids("paint is:waiting") == V{later});
        check("query/words alone: no filters", !core::parse_query("paint shed").has_filters() &&
                                                    core::parse_query("paint or shed").has_filters());
        {
            auto h = core::search(m, "is:available", 200, now);
            check("query/status line: a filter-only todo says where it stands",
                  !h.empty() && h[0].id == milk && h[0].snippet == "Available · due today" &&
                      h[0].cp_start == -1 && !h[0].in_name,
                  h.empty() ? "" : h[0].snippet);
            h = core::search(m, "is:inbox", 200, now);
            check("query/status line: a note shows its first line of text",
                  h.size() == 1 && h[0].snippet == "Raised beds by the fence.", h.empty() ? "" : h[0].snippet);
            h = core::search(m, "#car", 200, now);
            check("query/tag: the line it is on, the tag selected",
                  h.size() == 1 && h[0].snippet == "#car" && h[0].cp_start == 0 && h[0].cp_len == 4);
        }
        check("query/active", core::query_active("is:done") && !core::query_active("  ") &&
                                  !core::query_active("or"));   // `or` alone joins nothing
        check("query/describe",
              core::describe_query("#errands or #car is:available due:week -#someday") ==
                  "#errands or #car · available todos · due within a week · not #someday",
              core::describe_query("#errands or #car is:available due:week -#someday"));
        check("query/describe: plain words say nothing extra", core::describe_query("paint shed").empty());

        // The menu's view over the text, and its writers.
        const std::string q = "#errands  is:available due:today";
        check("query/menu reads", core::query_show(q) == "available" && core::query_due(q) == "today" &&
                                      !core::query_flagged(q) && core::query_show("#a").empty());
        check("query/menu writes show: replaces, keeps the rest",
              core::query_set_show(q, "waiting") == "#errands due:today is:waiting");
        check("query/menu writes show: anything removes it",
              core::query_set_show(q, "") == "#errands due:today");
        check("query/menu writes due", core::query_set_due(q, "week") == "#errands is:available due:week");
        check("query/menu flag on / off",
              core::query_set_flagged(q, true) == "#errands is:available due:today is:flagged" &&
                  core::query_set_flagged("is:flagged #a", false) == "#a" &&
                  core::query_flagged("x is:flagged"));
        check("query/menu leaves or-ed and negated tokens alone",
              core::query_set_show("is:done or is:dropped -is:inbox", "available") ==
                  "is:done or is:dropped -is:inbox is:available" &&
                  core::query_show("is:done or is:dropped").empty());

        // Perspectives.
        std::vector<core::Perspective> ps;
        check("perspective/save refuses an empty name or query",
              !core::perspective_save(ps, "  ", "#a") && !core::perspective_save(ps, "Errands", "   ") &&
                  ps.empty());
        core::perspective_save(ps, "Errands", "#errands   is:available");
        core::perspective_save(ps, "car", "#car");
        core::perspective_save(ps, "  Away  ", "#errands or #car");
        check("perspective/sorted by name, case ignored, spacing tidied",
              ps.size() == 3 && ps[0].name == "Away" && ps[1].name == "car" && ps[2].name == "Errands" &&
                  ps[2].query == "#errands is:available");
        core::perspective_save(ps, "ERRANDS", "#errands");
        check("perspective/same name replaces", ps.size() == 3 && ps[2].name == "ERRANDS" &&
                                                    ps[2].query == "#errands");
        const core::Perspective* hit = core::perspective_for(ps, " #CAR ");
        check("perspective/for: a query that reads the same is the same", hit && hit->name == "car");
        check("perspective/named", core::perspective_named(ps, "away") &&
                                       !core::perspective_named(ps, "nope"));
        check("perspective/remove", core::perspective_remove(ps, "Car") && ps.size() == 2 &&
                                        !core::perspective_remove(ps, "car"));

        const std::string file =
            (std::filesystem::temp_directory_path() / "jot_selftest_persp.json").string();
        core::Prefs pp;
        pp.perspectives = ps;
        core::save_prefs(file, pp);
        const core::Prefs back = core::load_prefs(file);
        check("perspective/prefs round-trip", back.perspectives == ps);
        check("perspective/first run has none", core::Prefs{}.perspectives.empty());
        std::filesystem::remove(file);
    }

    // -- s039: the Forecast ---------------------------------------------------
    {
        const std::int64_t now = core::day_start(1789300000) + 10 * 3600;   // 10:00 on a day
        const std::int64_t today = core::day_start(now);
        core::MemoryNodes m;
        const auto proj = m.create("", "Move house");
        m.set_due(proj, core::day_end(today + 2 * 86400 + 3600));        // day 2
        const auto pack = m.create(proj, "Pack");                          // inherits day 2
        m.make_task(pack, true);
        const auto early = m.create("", "Call at 3");
        m.make_task(early, true);
        m.set_due(early, today + 15 * 3600);                              // later today
        const auto late = m.create("", "Late one");
        m.make_task(late, true);
        m.set_due(late, today + 8 * 3600);                                // 08:00 today: overdue
        const auto starts = m.create("", "Plant bulbs");
        m.make_task(starts, true);
        m.set_defer(starts, today + 4 * 86400 + 3600);                    // starts day 4
        m.set_due(starts, today + 4 * 86400 + 12 * 3600);                 // and due that day
        const auto far = m.create("", "Renew passport");
        m.make_task(far, true);
        m.set_due(far, today + 20 * 86400 + 3600);                        // later
        const auto done = m.create("", "Done already");
        m.make_task(done, true);
        m.set_due(done, today + 15 * 3600);
        m.set_done(done, true);
        const auto passed = m.create("", "Deferred to yesterday");
        m.make_task(passed, true);
        m.set_defer(passed, today - 86400);

        core::TaskIndex idx;
        idx.rebuild(m);
        const auto f = core::forecast(m, idx, now, 7);
        using V = std::vector<core::NodeId>;
        check("forecast/seven days, today first, empty ones kept",
              f.days.size() == 7 && f.days[0].day == today && f.days[3].due.empty());
        check("forecast/today: due later today, not the overdue one, not the done one",
              f.days[0].due == V{early});
        check("forecast/inherited due: a step lands on its project's day", f.days[2].due == V{pack});
        check("forecast/starts: a future defer lands on its day",
              f.days[4].starts == V{starts} && f.days[4].due == V{starts});
        check("forecast/count: one todo due and starting the same day is one",
              f.days[4].count() == 1 && f.days[2].count() == 1);
        check("forecast/a passed defer is nothing to forecast",
              std::none_of(f.days.begin(), f.days.end(), [&](const core::ForecastDay& d) {
                  return std::find(d.starts.begin(), d.starts.end(), passed) != d.starts.end();
              }));
        check("forecast/later: only days with something, by day",
              f.later.size() == 1 && f.later[0].due == V{far} &&
                  f.later[0].day == core::day_start(today + 20 * 86400 + 3600));

        // DST: the night clocks go back (US, 2026-11-01) is 25 hours. A fixed
        // 86400 step would land every later bucket at 23:00 the day before.
        const char* old_tz = std::getenv("TZ");
        const std::string saved = old_tz ? old_tz : "";
        setenv("TZ", "America/Chicago", 1);
        tzset();
        std::tm tm{};
        tm.tm_year = 2026 - 1900; tm.tm_mon = 9; tm.tm_mday = 31; tm.tm_hour = 12; tm.tm_isdst = -1;
        const std::int64_t oct31 = static_cast<std::int64_t>(std::mktime(&tm));
        core::MemoryNodes e;
        core::TaskIndex ei;
        ei.rebuild(e);
        const auto fd = core::forecast(e, ei, oct31, 4);
        bool midnights = fd.days.size() == 4;
        for (const auto& d : fd.days) {
            std::time_t t = static_cast<std::time_t>(d.day);
            std::tm lt{};
            localtime_r(&t, &lt);
            midnights = midnights && lt.tm_hour == 0 && lt.tm_min == 0;
        }
        check("forecast/DST: every day starts at local midnight across the 25-hour night", midnights);
        check("forecast/DST: the fall-back day is 25 hours long",
              fd.days.size() == 4 && fd.days[2].day - fd.days[1].day == 25 * 3600);
        if (old_tz) setenv("TZ", saved.c_str(), 1); else unsetenv("TZ");
        tzset();
    }

    // -- s040: time estimates ---------------------------------------------------
    {
        check("estimate/parse: minutes, bare and named",
              core::parse_estimate("15") == 15 && core::parse_estimate("15m") == 15 &&
                  core::parse_estimate(" 15 min ") == 15 && core::parse_estimate("90 minutes") == 90);
        check("estimate/parse: hours and mixed",
              core::parse_estimate("1h") == 60 && core::parse_estimate("1h30") == 90 &&
                  core::parse_estimate("1h 30m") == 90 && core::parse_estimate("1.5h") == 90 &&
                  core::parse_estimate("2 hours") == 120);
        check("estimate/parse: empty is none, junk is -1",
              core::parse_estimate("") == 0 && core::parse_estimate("soon") == -1 &&
                  core::parse_estimate("1h2h") == -1 && core::parse_estimate("5 days") == -1);
        check("estimate/format", core::format_estimate(0).empty() && core::format_estimate(15) == "15m" &&
                                     core::format_estimate(60) == "1h" &&
                                     core::format_estimate(90) == "1h 30m");
        check("estimate/round trip through the text",
              core::parse_estimate(core::format_estimate(135)) == 135);

        const std::int64_t now = 1789300000;
        core::MemoryNodes m;
        const auto quick = m.create("", "Reply to Ann");
        m.make_task(quick, true);
        m.set_estimate(quick, 10);
        const auto hour = m.create("", "Mow the lawn");
        m.make_task(hour, true);
        m.set_estimate(hour, 60);
        const auto bare = m.create("", "Think about it");
        m.make_task(bare, true);
        const auto done = m.create("", "Done quick");
        m.make_task(done, true);
        m.set_estimate(done, 5);
        m.set_done(done, true);
        check("estimate/set refuses a negative", !m.set_estimate(quick, -5) && m.find(quick)->task.estimate == 10);
        m.make_task(hour, false);
        check("estimate/un-making a todo clears it", m.find(hour)->task.estimate == 0);
        m.make_task(hour, true);
        m.set_estimate(hour, 60);

        auto ids = [&](const std::string& q) {
            std::vector<core::NodeId> out;
            for (const auto& h : core::search(m, q, 200, now)) out.push_back(h.id);
            return out;
        };
        using V = std::vector<core::NodeId>;
        check("query/est: at most that long, done left out", ids("est:30") == V{quick});
        check("query/est: hours read too", ids("est:1h") == V{quick, hour});
        check("query/est:none: remaining todos with no estimate", ids("est:none") == V{bare});
        check("query/est: junk is a word", !core::parse_query("est:soon").has_filters());
        check("query/est: the line shows it",
              !core::search(m, "est:30", 200, now).empty() &&
                  core::search(m, "est:30", 200, now)[0].snippet == "Available · 10m",
              core::search(m, "est:30", 200, now).empty() ? "" : core::search(m, "est:30", 200, now)[0].snippet);
        check("query/est: describe", core::describe_query("is:available est:30") ==
                                         "Available todos · 30m or less");
        check("query/est: menu reads and writes",
              core::query_est("a est:1h") == "60" && core::query_est("est:none") == "none" &&
                  core::query_set_est("is:available est:1h", "30") == "is:available est:30" &&
                  core::query_set_est("est:30 x", "") == "x");

        const std::string dir =
            (std::filesystem::temp_directory_path() / "jot_selftest_estimate").string();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        core::NodeId a, b;
        {
            core::Project v;
            v.open(dir);
            a = v.create("", "timed");
            v.make_task(a, true);
            v.set_estimate(a, 45);
            b = v.create("", "untimed");
            v.make_task(b, true);
            v.flush();
        }
        {
            core::Project v;
            v.open(dir);
            check("estimate/persist: round-trips, absent stays none",
                  v.find(a)->task.estimate == 45 && v.find(b)->task.estimate == 0);
        }
        std::filesystem::remove_all(dir, ec);
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

    // -- Render: the reading view (s021) -------------------------------------
    {
        auto has = [](const core::Rendered& r, core::Style st, const std::string& txt) {
            for (const auto& s : r.spans)
                if (s.style == st && r.text.substr(static_cast<std::size_t>(s.begin),
                                                   static_cast<std::size_t>(s.end - s.begin)) == txt)
                    return true;
            return false;
        };
        auto r = core::render("## Plan\nsome **bold** and _it_ and `code`");
        check("render: marks gone", r.text == "Plan\nsome bold and it and code", r.text);
        check("render: styles survive onto the right text",
              has(r, core::Style::H2, "Plan") && has(r, core::Style::Bold, "bold") &&
                  has(r, core::Style::Italic, "it") && has(r, core::Style::Code, "code"));
        bool no_mark = true;
        for (const auto& s : r.spans) no_mark = no_mark && s.style != core::Style::Mark;
        check("render: no Mark span survives", no_mark);

        r = core::render("- milk\n  - eggs\n1. one\n- [ ] call\n- [x] done");
        check("render: bullets, nesting, numbers kept, boxes",
              r.text == "• milk\n  • eggs\n1. one\n☐ call\n☑ done", r.text);
        check("render: two boxes on the right source lines",
              r.boxes.size() == 2 && r.boxes[0].line == 3 && !r.boxes[0].checked &&
                  r.boxes[1].line == 4 && r.boxes[1].checked &&
                  core::source_cp(r, r.boxes[0].cp) == 23);
        check("render: a done task's text is struck", has(r, core::Style::TaskDone, "done"));

        r = core::render("see [the site](https://x.org) and [[no]] #inbox");
        check("render: a link keeps its label and target",
              r.text == "see the site and [[no]] #inbox" && r.links.size() == 1 &&
                  r.links[0].target == "https://x.org" &&
                  r.text.substr(4, 8) == "the site" && r.links[0].cp_begin == 4 &&
                  r.links[0].cp_end == 12, r.text);
        check("render: a tag keeps its hash", has(r, core::Style::Tag, "#inbox"));

        r = core::render("a\n```\nint x;\n```\nb\n---\n![Pic](attachments/p.png) after");
        const std::string fffc = core::kAnchorChar;
        check("render: a fenced block, a rule and an image are each one anchor",
              r.text == "a\n" + fffc + "\nb\n" + fffc + "\n" + fffc + " after", r.text);
        check("render: anchors in order, with kind and target",
              r.anchors.size() == 3 && r.anchors[0].kind == core::RenderAnchor::Kind::Code &&
                  r.anchors[1].kind == core::RenderAnchor::Kind::Rule &&
                  r.anchors[2].kind == core::RenderAnchor::Kind::Image &&
                  r.anchors[2].target == "attachments/p.png" && r.anchors[2].label == "Pic" &&
                  r.anchors[0].cp == 2 && r.anchors[1].cp == 6 && r.anchors[2].cp == 8);
        check("render/code: the bubble carries the code, as written, and no lang",
              r.anchors[0].code == "int x;" && r.anchors[0].lang.empty(), r.anchors[0].code);
        r = core::render("```cpp \nint main() {\n    return 0;\n}\n```\ntail");
        check("render/code: several lines, indentation kept, the lang trimmed",
              r.anchors.size() == 1 && r.anchors[0].code == "int main() {\n    return 0;\n}" &&
                  r.anchors[0].lang == "cpp" && r.text == fffc + "\ntail", r.text);
        r = core::render("x\n```\nnever closed\n**not bold**");
        check("render/code: an unclosed fence runs to the end, marks and all",
              r.anchors.size() == 1 && r.anchors[0].code == "never closed\n**not bold**" &&
                  r.text == "x\n" + fffc, r.text);
        r = core::render("```\n```\nafter");
        check("render/code: an empty block is still a bubble",
              r.anchors.size() == 1 && r.anchors[0].code.empty() && r.text == fffc + "\nafter",
              r.text);

        // The map: every kept codepoint round-trips; UTF-8 does not drift it.
        const std::string src = "# Café **naïve** — [x](y)\n- été";
        r = core::render(src);
        check("render/map: one entry per codepoint plus the end",
              static_cast<int>(r.src_cp.size()) == core::cp_len(r.text, 0, static_cast<int>(r.text.size())) + 1);
        bool mono = true;
        for (std::size_t i = 1; i < r.src_cp.size(); ++i) mono = mono && r.src_cp[i] >= r.src_cp[i - 1];
        check("render/map: non-decreasing", mono);
        // Exact for the first codepoint from each source position; a glyph's
        // second codepoint shares its source with the first, so it maps back
        // to the first (never past itself).
        bool rt = true;
        for (int i = 0; i + 1 < static_cast<int>(r.src_cp.size()); ++i) {
            const int back = core::rendered_cp(r, core::source_cp(r, i));
            const bool first = i == 0 || r.src_cp[static_cast<std::size_t>(i) - 1] !=
                                             r.src_cp[static_cast<std::size_t>(i)];
            rt = rt && (first ? back == i : back < i);
        }
        check("render/map: rendered -> source -> rendered round-trips", rt);
        check("render/map: 'naïve' maps into the bold, past the stars",
              core::source_cp(r, 5) == 9 && core::rendered_cp(r, 7) == 5);
        check("render/map: the end maps to the end",
              core::source_cp(r, 1 << 20) == core::cp_len(src, 0, static_cast<int>(src.size())));
        check("render: empty body", core::render("").text.empty() &&
                                        core::render("").src_cp.size() == 1);
    }

    // -- Live Preview: which marks hide, what is drawn (s022, s023) ---------
    {
        // What the eye sees: the source minus the hidden runs (ASCII bodies,
        // so a codepoint is a byte).
        auto seen = [](const std::string& src, int rf, int rl) {
            const auto v = core::live_view(core::scan(src), src, rf, rl);
            std::string out;
            int at = 0;
            const int n = static_cast<int>(src.size());
            for (const auto& h : v.hidden) {
                const int b = std::min(h.begin, n), e = std::min(h.end, n);
                out += src.substr(static_cast<std::size_t>(at), static_cast<std::size_t>(b - at));
                at = e;
            }
            return out + src.substr(static_cast<std::size_t>(at));
        };
        using K = core::LiveDeco::Kind;
        auto count = [](const core::LiveView& v, K k) {
            int c = 0;
            for (const auto& d : v.decos) c += d.kind == k;
            return c;
        };
        const std::string src = "## Plan\nsome **bold** and [a link](jot:n1)\n> said\ntail";
        check("live: every mark hidden when the cursor is elsewhere",
              seen(src, 99, 99) == "Plan\nsome bold and a link\nsaid\ntail", seen(src, 99, 99));
        check("live: the cursor's line keeps its marks",
              seen(src, 1, 1) == "Plan\nsome **bold** and [a link](jot:n1)\nsaid\ntail",
              seen(src, 1, 1));
        check("live: a selection's lines all keep theirs", seen(src, 0, 2) == src, seen(src, 0, 2));

        const std::string lst = "- milk\n  - *eggs*\n1. one\n- [ ] call `bob`\n- [x] done\nx";
        auto lv = core::live_view(core::scan(lst), lst, 5, 5);
        check("live: bullets and boxes hidden (drawn instead), numbers kept, inline marks hidden",
              seen(lst, 5, 5) == "milk\n  eggs\n1. one\ncall bob\ndone\nx", seen(lst, 5, 5));
        check("live: two bullets, two boxes (one ticked), four hanging lines",
              count(lv, K::Bullet) == 2 && count(lv, K::Box) == 2 && lv.hang_lines.size() == 4 &&
                  lv.decos[3].kind == K::Box && !lv.decos[2].checked && lv.decos[3].checked);
        check("live: a bullet's glyph sits where its content starts",
              lv.decos[1].kind == K::Bullet && lv.decos[1].cp == 11);
        check("live: the cursor's list line keeps its mark and draws nothing",
              seen(lst, 0, 0).rfind("- milk\n", 0) == 0 &&
                  count(core::live_view(core::scan(lst), lst, 0, 0), K::Bullet) == 1);

        const std::string blk = "a\n```cpp\nint **x**;\ny();\n```\n---\n![sun](a.png)\nsee ![i](b.png) here\nz";
        auto bv = core::live_view(core::scan(blk), blk, 0, 0);
        check("live: fences take no room, code stays, the rule is drawn, a lone image is the editor's",
              seen(blk, 0, 0) == "a\nint **x**;\ny();\n\n![sun](a.png)\nsee ![i](b.png) here\nz",
              seen(blk, 0, 0));
        bool code_ok = false, img_ok = false;
        for (const auto& d : bv.decos) {
            if (d.kind == K::Code) code_ok = d.line == 2 && d.code == "int **x**;\ny();" && d.lang == "cpp";
            if (d.kind == K::Image) img_ok = d.line == 6 && d.target == "a.png" && d.label == "sun";
        }
        check("live: one code deco (text between the fences, the lang), one rule, ONE image (not the inline one)",
              code_ok && img_ok && count(bv, K::Code) == 1 && count(bv, K::Rule) == 1 &&
                  count(bv, K::Image) == 1);
        check("live: the cursor anywhere in a block reveals the WHOLE block",
              seen(blk, 3, 3).find("```cpp\nint **x**;\ny();\n```\n") != std::string::npos &&
                  count(core::live_view(core::scan(blk), blk, 3, 3), K::Code) == 0);
        const std::string tail = "```\ncode\n```";
        check("live: a block closing on the last line hides the newline before its fence",
              seen(tail, 99, 99) == "code", seen(tail, 99, 99));
        const std::string open_only = "```sh\nls\nmore";
        check("live: an unclosed block still hides its opening fence",
              seen(open_only, 99, 99) == "ls\nmore", seen(open_only, 99, 99));

        const std::string u = "# \xC3\xA9t\xC3\xA9 **\xC3\xA0**\nx";
        const auto h = core::live_view(core::scan(u), u, 1, 1).hidden;
        check("live: ranges are codepoints, not bytes",
              h.size() == 3 && h[0].begin == 0 && h[0].end == 2 && h[1].begin == 6 &&
                  h[1].end == 8 && h[2].begin == 9 && h[2].end == 11);
        const auto e = core::live_view(core::scan(""), "", -1, -1);
        check("live: empty body hides and draws nothing", e.hidden.empty() && e.decos.empty());

        // -- s026: reveal per RUN, not per line ---------------------------
        // The cursor is one codepoint (sb == se) or a selection [sb, se].
        auto seen_at = [](const std::string& src, int sb, int se) {
            const auto sc = core::scan(src);
            int line = 0;
            for (int k = 0; k < sb && k < static_cast<int>(src.size()); ++k) line += src[static_cast<std::size_t>(k)] == '\n';
            int line_e = line;
            for (int k = sb; k < se && k < static_cast<int>(src.size()); ++k) line_e += src[static_cast<std::size_t>(k)] == '\n';
            const auto v = core::live_view(sc, src, line, line_e, sb, se);
            std::string out;
            int at = 0;
            const int n = static_cast<int>(src.size());
            for (const auto& h : v.hidden) {
                const int b = std::min(h.begin, n), en = std::min(h.end, n);
                out += src.substr(static_cast<std::size_t>(at), static_cast<std::size_t>(b - at));
                at = en;
            }
            return out + src.substr(static_cast<std::size_t>(at));
        };
        // "## Plan\n" is 8; `**bold**` is [13,21); `[a link](jot:n1)` is [26,42).
        const auto rs = core::scan(src);
        check("runs: the scanner records each inline construct whole",
              rs.runs.size() == 2 && rs.runs[0].cp_begin == 13 && rs.runs[0].cp_end == 21 &&
                  rs.runs[1].cp_begin == 26 && rs.runs[1].cp_end == 42 && rs.runs[1].line == 1);
        check("live/run: the cursor on a line but in no run reveals none of its inline marks",
              seen_at(src, 9, 9) == "Plan\nsome bold and a link\nsaid\ntail", seen_at(src, 9, 9));
        check("live/run: the cursor in the bold word reveals the bold word only",
              seen_at(src, 16, 16) == "Plan\nsome **bold** and a link\nsaid\ntail", seen_at(src, 16, 16));
        check("live/run: touching either edge counts (arrowing up to it; just finished typing it)",
              seen_at(src, 13, 13) == seen_at(src, 16, 16) && seen_at(src, 21, 21) == seen_at(src, 16, 16));
        check("live/run: the cursor in the link reveals the link only",
              seen_at(src, 30, 30) == "Plan\nsome bold and [a link](jot:n1)\nsaid\ntail", seen_at(src, 30, 30));
        check("live/run: a selection reveals every run it touches",
              seen_at(src, 16, 30) == "Plan\nsome **bold** and [a link](jot:n1)\nsaid\ntail", seen_at(src, 16, 30));
        check("live/run: line-level marks still follow the line (heading, quote)",
              seen_at(src, 3, 3) == "## Plan\nsome bold and a link\nsaid\ntail" &&
                  seen_at(src, 45, 45) == "Plan\nsome bold and a link\n> said\ntail",
              seen_at(src, 3, 3) + " | " + seen_at(src, 45, 45));
        check("live/run: no cursor given -> the s022 line rule",
              seen(src, 1, 1) == "Plan\nsome **bold** and [a link](jot:n1)\nsaid\ntail");

        const std::string mix = "- a \\* `x` ~~s~~ *i* __b__";
        const auto ms = core::scan(mix);
        check("runs: escape, code, strike, italic, bold -- five, in order, disjoint",
              ms.runs.size() == 5 && ms.runs[0].cp_begin == 4 && ms.runs[0].cp_end == 6 &&
                  [&] {
                      for (std::size_t k = 1; k < ms.runs.size(); ++k)
                          if (ms.runs[k].cp_begin < ms.runs[k - 1].cp_end) return false;
                      return true;
                  }());
        check("live/run: a list line's runs follow the cursor (s027: its bullet stays drawn)",
              seen_at(mix, 8, 8) == "a * `x` s i b", seen_at(mix, 8, 8));

        // -- s027: the cursor's bullet / task line keeps its DRAWN mark ---
        const std::string bl = "- milk\n  - [ ] eggs\n1. one\nx";
        check("live/mark: the cursor in a bullet's words -- the bullet stays drawn",
              seen_at(bl, 4, 4) == "milk\n  eggs\n1. one\nx", seen_at(bl, 4, 4));
        check("live/mark: at the words' first character it is still the words",
              seen_at(bl, 2, 2) == "milk\n  eggs\n1. one\nx", seen_at(bl, 2, 2));
        check("live/mark: the cursor AT the mark (line start, inside it) shows `- `",
              seen_at(bl, 0, 0) == "- milk\n  eggs\n1. one\nx" && seen_at(bl, 1, 1) == seen_at(bl, 0, 0),
              seen_at(bl, 0, 0) + " | " + seen_at(bl, 1, 1));
        check("live/mark: a task's `- [ ] ` shows only at the mark; the indent counts as at it",
              seen_at(bl, 16, 16) == "milk\n  eggs\n1. one\nx" &&
                  seen_at(bl, 7, 7) == "milk\n  - [ ] eggs\n1. one\nx" &&
                  seen_at(bl, 12, 12) == seen_at(bl, 7, 7),
              seen_at(bl, 16, 16) + " | " + seen_at(bl, 7, 7));
        {
            const auto v_in = core::live_view(core::scan(bl), bl, 0, 0, 4, 4);
            const auto v_at = core::live_view(core::scan(bl), bl, 0, 0, 0, 0);
            check("live/mark: drawn (bullet + hang) in the words, not drawn at the mark",
                  count(v_in, K::Bullet) == 1 && v_in.hang_lines.size() == 2 &&
                      count(v_at, K::Bullet) == 0 && v_at.hang_lines.size() == 1);
        }
        check("live/mark: a selection over the mark shows it",
              seen_at(bl, 0, 4) == "- milk\n  eggs\n1. one\nx", seen_at(bl, 0, 4));
        check("live/mark: a number is content either way",
              seen_at(bl, 20, 20) == "milk\n  eggs\n1. one\nx", seen_at(bl, 20, 20));
        {
            const auto bs = core::scan(bl);
            check("touched_marks: the lines whose mark the cursor is at",
                  core::touched_marks(bs, 0, 0, 0, 0) == std::vector<int>{0} &&
                      core::touched_marks(bs, 0, 0, 3, 3).empty() &&
                      core::touched_marks(bs, 0, 1, 1, 9) == std::vector<int>({0, 1}) &&
                      core::touched_marks(bs, 2, 2, 22, 22).empty());
        }

        const auto tr = core::touched_runs(rs, 16, 16);
        const auto none = core::touched_runs(rs, 9, 9);
        check("touched_runs: [lo, hi) of what the cursor touches; empty is lo == hi",
              tr.first == 0 && tr.second == 1 && none.first == none.second &&
                  core::touched_runs(rs, 16, 30) == std::make_pair(0, 2) &&
                  core::touched_runs(rs, -1, -1).first == core::touched_runs(rs, -1, -1).second);

        const std::string adj = "**a***b* z";
        check("live/run: the cursor between two touching runs reveals both",
              seen_at(adj, 5, 5) == "**a***b* z" && seen_at(adj, 9, 9) == "ab z",
              seen_at(adj, 5, 5) + " | " + seen_at(adj, 9, 9));
        const auto us = core::scan(u);
        check("runs: codepoints, not bytes", us.runs.size() == 1 && us.runs[0].cp_begin == 6 &&
                                                 us.runs[0].cp_end == 11);
    }

    // -- Format: the format bar's verbs (s024) --------------------------------
    // Written as marked-up strings: `|` is the cursor, `{..}` the selection,
    // before and after. The markers are read out, the verb runs, and the
    // result is written back with the new selection marked the same way.
    {
        auto cp2b = [](const std::string& s, int cp) {
            std::size_t i = 0;
            for (int k = 0; k < cp && i < s.size(); ++k) {
                const auto c = static_cast<unsigned char>(s[i]);
                i += c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
            }
            return i;
        };
        auto run_with = [&](std::string in, const std::function<core::FmtEdit(const std::string&, int, int)>& verb) {
            int a = -1, b = -1;
            if (auto p = in.find('|'); p != std::string::npos) {
                in.erase(p, 1);
                a = b = core::cp_len(in, 0, static_cast<int>(p));
            } else {
                auto p0 = in.find('{');
                in.erase(p0, 1);
                auto p1 = in.find('}');
                in.erase(p1, 1);
                a = core::cp_len(in, 0, static_cast<int>(p0));
                b = core::cp_len(in, 0, static_cast<int>(p1));
            }
            const auto e = verb(in, a, b);
            if (!e.ok) return std::string("(no edit)");
            std::string out = core::apply(in, e);
            if (e.sel_begin == e.sel_end) {
                out.insert(cp2b(out, e.sel_begin), "|");
            } else {
                out.insert(cp2b(out, e.sel_end), "}");
                out.insert(cp2b(out, e.sel_begin), "{");
            }
            return out;
        };
        auto run = [&](const std::string& in, core::Fmt f) {
            return run_with(in, [f](const std::string& t, int a, int b) { return core::format(t, a, b, f); });
        };
        auto is = [&](const std::string& what, const std::string& in, core::Fmt f,
                      const std::string& want) {
            const std::string got = run(in, f);
            check("format: " + what, got == want, got);
        };
        using F = core::Fmt;
        // inline
        is("bold wraps a selection, the words stay selected",
           "a {big} dog", F::Bold, "a **{big}** dog");
        is("bold again unwraps it", "a **{big}** dog", F::Bold, "a {big} dog");
        is("bold unwraps when the marks were selected too", "a {**big**} dog", F::Bold, "a {big} dog");
        is("the cursor in a word bolds the word and stays put", "a bi|g dog", F::Bold, "a **bi|g** dog");
        is("the cursor at a word's end stays inside the close mark", "a big| dog", F::Bold, "a **big|** dog");
        is("the cursor in a bold word unbolds it", "a **bi|g** dog", F::Bold, "a bi|g dog");
        is("the cursor on nothing inserts a pair", "a | dog", F::Bold, "a **|** dog");
        is("a second press takes the empty pair away", "a **|** dog", F::Bold, "a | dog");
        is("italic is one star", "a {big} dog", F::Italic, "a *{big}* dog");
        is("italic does NOT unwrap bold", "a **{big}** dog", F::Italic, "a ***{big}*** dog");
        is("italic unwraps bold-italic to bold", "a ***{big}*** dog", F::Italic, "a **{big}** dog");
        is("bold unwraps bold-italic to italic", "a ***{big}*** dog", F::Bold, "a *{big}* dog");
        is("bold does NOT unwrap italic", "a *{big}* dog", F::Bold, "a ***{big}*** dog");
        is("strike and code", "{x}", F::Strike, "~~{x}~~");
        is("code wraps", "run {ls -l} now", F::Code, "run `{ls -l}` now");
        is("spaces stay outside the marks", "a{ big }dog", F::Bold, "a **{big}** dog");
        is("a selection over two lines wraps each line",
           "{one\ntwo} three", F::Bold, "**{one**\n**two}** three");
        is("and unwraps each line", "**{one**\n**two}** three", F::Bold, "{one\ntwo} three");
        is("a mixed selection wraps only what is bare",
           "{**one**\ntwo}", F::Bold, "**{one**\n**two}**");
        is("codepoints, not bytes", "caf\xC3\xA9 {na\xC3\xAFve}", F::Italic,
           "caf\xC3\xA9 *{na\xC3\xAFve}*");
        is("an em dash is not part of the word", "one\xE2\x80\x94tw|o", F::Bold,
           "one\xE2\x80\x94**tw|o**");
        // link
        is("link: nothing selected -> []() with the cursor in the label", "see |", F::Link, "see [|]()");
        is("link: words become the label, the cursor waits for the address",
           "see {the docs}", F::Link, "see [the docs](|)");
        is("link: an address becomes the target, the cursor waits for a label",
           "see {https://x.org}", F::Link, "see [|](https://x.org)");
        is("link: not across lines", "{a\nb}", F::Link, "(no edit)");
        // lines
        is("bullet on a line", "bu|y milk", F::Bullet, "- bu|y milk");
        is("bullet again makes it plain", "- bu|y milk", F::Bullet, "bu|y milk");
        is("bullet on a blank line starts a list", "|", F::Bullet, "- |");
        is("bullet over lines, the blank one skipped",
           "{a\n\nb}", F::Bullet, "{- a\n\n- b}");
        is("bullet keeps nesting", "  |x", F::Bullet, "  - |x");
        is("task on a bullet converts it", "- bu|y", F::Task, "- [ ] bu|y");
        is("task on tasks makes them plain, ticked or not",
           "{- [ ] a\n- [x] b}", F::Task, "{a\nb}");
        is("task on a mix keeps a tick", "{- [x] a\nb}", F::Task, "{- [x] a\n- [ ] b}");
        is("numbered numbers the lines", "{a\nb\nc}", F::Numbered, "{1. a\n2. b\n3. c}");
        is("numbered again makes them plain", "{1. a\n2. b}", F::Numbered, "{a\nb}");
        is("H2 on a paragraph", "Ti|tle", F::H2, "## Ti|tle");
        is("H1 on an H2 changes the level", "## Ti|tle", F::H1, "# Ti|tle");
        is("H2 on an H2 makes it plain", "## Ti|tle", F::H2, "Ti|tle");
        is("a heading drops a list mark", "- Ti|tle", F::H3, "### Ti|tle");
        is("plain strips everything", "{## A\n- [ ] b\n> c}", F::Plain, "{A\nb\nc}");
        is("quote adds and removes", "{a\nb}", F::Quote, "{> a\n> b}");
        is("quote off", "{> a\n> b}", F::Quote, "{a\nb}");
        is("a bullet inside a quote keeps the quote", "> |x", F::Bullet, "> - |x");
        is("a cursor inside the old mark lands at the words", "#|# A", F::H1, "# |A");
        is("a selection ending at the next line's start leaves that line alone",
           "{a\n}b", F::Bullet, "{- a\n}b");
        // blocks
        is("code block fences the lines", "x\n{a\nb}\ny", F::CodeBlock, "x\n```\n{a\nb}\n```\ny");
        is("code block on a blank line opens an empty block", "|", F::CodeBlock, "```\n|\n```");
        is("code block inside a fence removes it", "x\n```\na|\n```\ny", F::CodeBlock, "x\n{a}\ny");
        is("code block from the fence line removes it too", "```|\na\n```", F::CodeBlock, "{a}");
        is("code block outside any fence adds one", "```\na\n```\n|b", F::CodeBlock,
           "```\na\n```\n```\n{b}\n```");

        // -- s025: Enter and Tab in a list ------------------------------------
        auto en = [&](const std::string& what, const std::string& in, const std::string& want) {
            const std::string got = run_with(in, [](const std::string& t, int a, int b) {
                return core::enter(t, a, b);
            });
            check("enter: " + what, got == want, got);
        };
        auto tab = [&](const std::string& what, const std::string& in, bool out, const std::string& want) {
            const std::string got = run_with(in, [out](const std::string& t, int a, int b) {
                return core::indent(t, a, b, out);
            });
            check(std::string(out ? "shift+tab: " : "tab: ") + what, got == want, got);
        };
        en("a bullet continues", "- milk|", "- milk\n- |");
        en("the bullet's own mark is kept", "* milk|", "* milk\n* |");
        en("a task continues unticked", "- [x] milk|", "- [x] milk\n- [ ] |");
        en("a number counts on, keeping its )", "9) nine|", "9) nine\n10) |");
        en("nesting is kept", "  - kid|", "  - kid\n  - |");
        en("mid-item, the rest goes down with it", "- buy |milk", "- buy\n- |milk");
        en("a quote continues", "> said|", "> said\n> |");
        en("a list in a quote continues both", "> - a|", "> - a\n> - |");
        en("an empty top-level item ends the list", "- a\n- |", "- a\n|");
        en("an empty task too", "- a\n- [ ] |", "- a\n|");
        en("an empty nested item steps out a level", "- a\n  - |", "- a\n- |");
        en("an empty item in a quote keeps the quote", "> - |", "> |");
        en("an empty quote line ends the quote", "> a\n> |", "> a\n|");
        en("a plain line is the text view's", "plain|", "(no edit)");
        en("a heading is the text view's", "# Head|", "(no edit)");
        en("the cursor inside the mark is the text view's", "-| a", "(no edit)");
        en("a selection is the text view's", "- {a}", "(no edit)");
        en("inside a code block is the text view's", "```\n- a|\n```", "(no edit)");
        tab("nests under the item above, lined up with its words", "- a\n- |b", false, "- a\n  - |b");
        tab("under a number, by the number's width (s027: and restarts at 1)", "1. a\n2. |b", false,
            "1. a\n   1. |b");
        tab("under a task, as under its bullet", "- [ ] a\n- [ ] |b", false, "- [ ] a\n  - [ ] |b");
        tab("the first child can't go deeper (nothing changes)", "- a\n  - |b", false, "- a\n  - |b");
        tab("the first item with nothing above still nests", "- |a", false, "  - |a");
        tab("a selected sub-list moves together", "- a\n{- b\n  - c}", false, "- a\n{  - b\n    - c}");
        tab("a plain line is the text view's", "pla|in", false, "(no edit)");
        tab("out to the parent's level", "- a\n  - |b", true, "- a\n- |b");
        tab("out from deep", "- a\n  - b\n    - |c", true, "- a\n  - b\n  - |c");
        tab("out at the top does nothing, but is ours", "- |a", true, "- |a");
        tab("the cursor in the indent lands at the mark", "- a\n | - b", true, "- a\n|- b");

        // -- s027: numbered lists renumber -----------------------------------
        en("a number in the middle: the rest count on", "1. a|\n2. b\n3. c", "1. a\n2. |\n3. b\n4. c");
        en("a list keeps the number it starts at", "5. a|\n6. b", "5. a\n6. |\n7. b");
        en("wider numbers carry the cursor", "9. a|\n10. b", "9. a\n10. |\n11. b");
        en("Enter-Enter ends the item; the rest close the gap", "1. a\n2. |\n3. b", "1. a\n|\n2. b");
        en("an empty nested item steps out and counts on at its new level",
           "1. a\n   1. b\n   2. |\n2. c", "1. a\n   1. b\n2. |\n3. c");
        en("in a quote too", "> 1. a|\n> 2. b", "> 1. a\n> 2. |\n> 3. b");
        tab("nesting restarts at 1, the rest close up", "1. a\n2. |b\n3. c", false, "1. a\n   1. |b\n2. c");
        tab("nesting under existing children counts on", "1. a\n   1. x\n2. |b", false,
            "1. a\n   1. x\n   2. |b");
        tab("out: counts on at the parent's level, the rest after it", "1. a\n   1. b\n   2. |c\n2. d", true,
            "1. a\n   1. b\n2. |c\n3. d");
        tab("a moved sub-list keeps its own count", "1. a\n{2. b\n   1. x}\n3. c", false,
            "1. a\n{   1. b\n      1. x}\n2. c");
        is("numbered under a list counts on", "1. a\n{b\nc}", F::Numbered, "1. a\n{2. b\n3. c}");
        is("a bullet in a numbered list splits it; the rest keep their numbers",
           "1. a\n2. |b\n3. c", F::Bullet, "1. a\n- |b\n3. c");
        auto rn = [&](const std::string& what, const std::string& in, const std::string& want) {
            const std::string got = run_with(in, [](const std::string& t, int a, int b) {
                return core::renumber(t, a, b);
            });
            check("renumber: " + what, got == want, got);
        };
        rn("a deleted item closes the gap", "1. a\n|3. c\n4. d", "1. a\n|2. c\n3. d");
        rn("from the end of the item above too", "1. a|\n3. c", "1. a|\n2. c");
        rn("a blank line inside a list does not end it", "1. a\n\n|3. b", "1. a\n\n|2. b");
        rn("nested runs count on their own", "1. a\n   3. x\n|   5. y\n4. b",
           "1. a\n   3. x\n|   4. y\n2. b");
        rn("a paragraph ends the list", "1. a\n\npara\n|3. x", "(no edit)");
        rn("already right: nothing to do", "1. a\n|2. b", "(no edit)");
        rn("code is never a list", "```\n1. a\n|3. b\n```", "(no edit)");
        auto bk = [&](const std::string& what, const std::string& in, const std::string& want) {
            const std::string got = run_with(in, [](const std::string& t, int a, int b) {
                return core::backspace(t, a, b);
            });
            check("backspace: " + what, got == want, got);
        };
        bk("at a bullet's words the mark goes", "- |milk", "|milk");
        bk("a nested task keeps its indent", "a\n  - [ ] |x", "a\n  |x");
        bk("an empty bullet", "- |", "|");
        bk("in a quote the quote stays", "> - |a", "> |a");
        bk("in the words it is the text view's", "- m|ilk", "(no edit)");
        bk("a number is visible: the text view's", "1. |a", "(no edit)");
        bk("a selection is the text view's", "- {m}ilk", "(no edit)");
    }

    // -- Import: a markdown file becomes a note (s021b) ----------------------
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path root = fs::temp_directory_path() / "jot_selftest_import";
        fs::remove_all(root, ec);
        fs::create_directories(root / "img", ec);
        std::ofstream(root / "img" / "sun.png", std::ios::binary) << "PNG";
        std::ofstream(root / "img" / "a b.png", std::ios::binary) << "PNG";

        check("import/name: md, markdown, txt; not pdf",
              core::is_markdown_filename("/x/A.MD") && core::is_markdown_filename("n.markdown") &&
                  core::is_markdown_filename("t.txt") && !core::is_markdown_filename("r.pdf"));
        check("import/title: the first H1, closing hashes dropped",
              core::import_title("intro\n## sub\n# Real Title #\n", "/x/f.md") == "Real Title");
        check("import/title: no H1 -> the filename's stem",
              core::import_title("## only h2\ntext", "/x/My Notes.md") == "My Notes");

        const fs::path md = root / "plan.md";
        std::ofstream(md, std::ios::binary)
            << "\xEF\xBB\xBF# Plan\r\n![s](img/sun.png) ![sp](img/a%20b.png) ![gone](img/no.png)\r\n"
               "![w](https://x.org/p.png) [doc](img/sun.png)\r\n";
        core::ImportedNote note;
        std::string err;
        const bool ok = core::import_markdown(md.string(), note, err);
        check("import/file: read, BOM and CR gone, titled", ok && note.title == "Plan" &&
                                                                note.body.rfind("# Plan\n", 0) == 0 &&
                                                                note.body.find('\r') == std::string::npos,
              err);
        const std::string sun = core::file_uri((root / "img" / "sun.png").string());
        const std::string ab  = core::file_uri((root / "img" / "a b.png").string());
        check("import/pictures: relative images that exist become file:// links",
              note.pictures == 2 && note.body.find("![s](" + sun + ")") != std::string::npos &&
                  note.body.find("![sp](" + ab + ")") != std::string::npos, note.body);
        check("import/pictures: a missing one, a web one, and a plain link are left alone",
              note.body.find("![gone](img/no.png)") != std::string::npos &&
                  note.body.find("![w](https://x.org/p.png)") != std::string::npos &&
                  note.body.find("[doc](img/sun.png)") != std::string::npos, note.body);
        {
            auto rr = core::render(note.body);
            int imgs = 0;
            for (const auto& a : rr.anchors) imgs += a.kind == core::RenderAnchor::Kind::Image;
            check("import/pictures: the rewritten body still renders all four images", imgs == 4);
        }
        std::ofstream(root / "bad.md", std::ios::binary) << "ok \xC3\x28 bad";
        check("import/refuse: not UTF-8", !core::import_markdown((root / "bad.md").string(), note, err) &&
                                              !err.empty());
        check("import/refuse: a folder", !core::import_markdown(root.string(), note, err) && !err.empty());
        check("import/refuse: a missing file",
              !core::import_markdown((root / "nope.md").string(), note, err));

        // s021c: a whole folder.
        const fs::path vault = root / "My Vault";
        fs::create_directories(vault / "Recipes" / "Soups", ec);
        fs::create_directories(vault / "empty" / "deeper", ec);
        fs::create_directories(vault / ".obsidian", ec);
        std::ofstream(vault / "b note.md") << "# B";
        std::ofstream(vault / "A note.MD") << "a";
        std::ofstream(vault / "photo.png") << "PNG";
        std::ofstream(vault / ".obsidian" / "hidden.md") << "x";
        std::ofstream(vault / "Recipes" / "bread.md") << "bread";
        std::ofstream(vault / "Recipes" / "Soups" / "leek.markdown") << "leek";
        std::ofstream(vault / "empty" / "deeper" / "notes.pdf") << "%PDF";
        fs::create_directory_symlink(vault, vault / "loop", ec);
        core::ImportItem plan;
        bool trunc = false;
        const bool planned = core::plan_folder_import((vault / "").string(), plan, 2000, &trunc);
        check("import/folder: the folder is the parent, named after it",
              planned && plan.folder && plan.title == "My Vault" && !trunc);
        check("import/folder: folders first, then files, case-insensitive; hidden, "
              "non-markdown, empty folders and a symlink loop left out",
              plan.children.size() == 3 && plan.children[0].folder &&
                  plan.children[0].title == "Recipes" && plan.children[1].title == "A note" &&
                  plan.children[2].title == "b note");
        check("import/folder: nesting kept",
              plan.children[0].children.size() == 2 && plan.children[0].children[0].folder &&
                  plan.children[0].children[0].title == "Soups" &&
                  plan.children[0].children[0].children.size() == 1 &&
                  plan.children[0].children[1].title == "bread");
        check("import/folder: four files in all", core::count_files(plan) == 4);
        core::ImportItem small;
        check("import/folder: the limit stops it and says so",
              core::plan_folder_import(vault.string(), small, 2, &trunc) && trunc &&
                  core::count_files(small) == 2);
        check("import/folder: a folder with no markdown is refused",
              !core::plan_folder_import((vault / "empty").string(), small));
        check("import/folder: a file is not a folder",
              !core::plan_folder_import((vault / "b note.md").string(), small));
        fs::remove_all(root, ec);
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

        // s016c. The control that must PASS first: the chord that caused the
        // bug is recognised in all three spellings, so a clean registry means
        // the detector found nothing rather than that it cannot see.
        check("shortcuts: Ctrl+Delete is recognised as a text-editing chord",
              sc::steals_text_editing("<Ctrl>Delete") &&
                  sc::steals_text_editing("<Control>delete") &&
                  sc::steals_text_editing("<Primary>Delete"));
        check("shortcuts: an ordinary app chord is not",
              !sc::steals_text_editing("<Ctrl>n") && !sc::steals_text_editing("F2"));
        check("shortcuts: no app-wide shortcut takes a key away from a text box",
              sc::find_text_editing_steals().empty(),
              sc::find_text_editing_steals().empty()
                  ? "clean" : sc::find_text_editing_steals().front());
        {
            bool bound = false;
            for (const auto& r : sc::shortcut_registry())
                if (r.action == "win.delete-note" && !r.accels.empty()) bound = true;
            check("shortcuts: delete-note has no app-wide key (the tree binds Delete)",
                  !bound);
        }
        {
            // s018 (Scott): the function row is not dependable on a MacBook
            // under Asahi, so no action may be reachable ONLY by an F-key --
            // and the first accel (the one a menu shows) is the twin.
            auto fkey = [](const std::string& a) {
                return a.size() >= 2 && a[0] == 'F' && std::isdigit(static_cast<unsigned char>(a[1]));
            };
            std::string bad;
            for (const auto& r : sc::shortcut_registry()) {
                if (r.action.empty() || r.accels.empty()) continue;
                const bool any_f = std::any_of(r.accels.begin(), r.accels.end(), fkey);
                if (any_f && fkey(r.accels.front())) bad += r.action + " ";
            }
            check("shortcuts: every F-key has a letter-row twin, listed first", bad.empty(),
                  bad.empty() ? "clean" : bad);
        }

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

    // -- Project status (s031) --------------------------------------------------
    // On hold, Dropped and Completed are statements about a CONTAINER that its
    // contents obey. The failure this guards is the silent one: a todo still in
    // Today after you paused its project is noise you learn to ignore; a todo
    // missing from Today because a far ancestor was dropped by mistake is a task
    // you do not do. Both directions are pinned here.
    {
        using core::Avail;
        using core::ProjectState;
        const std::int64_t now = 1'800'000'000;
        core::MemoryNodes m;
        const auto area = m.create("", "Home");
        const auto proj = m.create(area, "Kitchen");          // a NOTE as the project
        const auto t1 = m.create(proj, "measure the wall");
        const auto t2 = m.create(proj, "order the shelf");
        for (const auto& id : {t1, t2}) m.make_task(id, true);
        m.set_due(t2, now - 3600);                            // overdue
        m.set_flagged(t1, true);
        core::TaskIndex ix;
        ix.rebuild(m);
        auto has = [](const std::vector<core::NodeId>& v, const core::NodeId& id) {
            return std::find(v.begin(), v.end(), id) != v.end();
        };

        check("project: a new note is Active",
              core::project_state(*m.find(proj)) == ProjectState::Active);
        check("project: an active project's todos are available",
              core::availability(m, t1, now) == Avail::Available &&
                  core::availability(m, t2, now) == Avail::Available);

        // On hold.
        check("project: On hold is written",
              core::set_project_state(m, proj, ProjectState::OnHold));
        check("project: ...its todos are On hold",
              core::availability(m, t1, now) == Avail::OnHold &&
                  core::availability(m, t2, now) == Avail::OnHold);
        check("project: ...and leave Available and Today",
              !has(ix.query(m, core::Filter::Available, now), t1) &&
                  !has(ix.query(m, core::Filter::Today, now), t1));
        check("project: ...but late is late (Overdue) and a flag still shows",
              has(ix.query(m, core::Filter::Overdue, now), t2) &&
                  has(ix.query(m, core::Filter::Flagged, now), t1));
        check("project: stopped_by names the held project", core::stopped_by(m, t1) == proj);
        check("project: Active is no reason", core::stopped_by(m, area).empty());
        {
            int writes = 0;
            m.on_changed([&](core::NodeSource::Change, const core::NodeId&) { ++writes; });
            core::set_project_state(m, proj, ProjectState::OnHold);
            check("project: setting the state it already has writes nothing", writes == 0,
                  std::to_string(writes));
            m.on_changed(nullptr);
        }

        // Dropped, and precedence: a dropped project inside a held area.
        core::set_project_state(m, area, ProjectState::OnHold);
        core::set_project_state(m, proj, ProjectState::Dropped);
        check("project: dropped inside held reads Dropped",
              core::availability(m, t1, now) == Avail::Dropped);
        check("project: Dropped leaves Overdue and Flagged too",
              !has(ix.query(m, core::Filter::Overdue, now), t2) &&
                  !has(ix.query(m, core::Filter::Flagged, now), t1));
        check("project: stopped_by names the NEAREST stop", core::stopped_by(m, t1) == proj);
        {
            const auto pj = core::project(m, ix, now);
            bool t2_there = false;
            for (const auto& p : pj) t2_there = t2_there || p.id == t2;
            check("project: a dropped todo leaves the desktop calendar", !t2_there);
        }
        core::set_project_state(m, proj, ProjectState::Active);   // area still held
        {
            const auto pj = core::project(m, ix, now);
            bool t2_there = false;
            for (const auto& p : pj) t2_there = t2_there || p.id == t2;
            check("project: a held todo stays on the calendar (a date is a fact)", t2_there);
        }
        check("project: held from two levels up still holds",
              core::availability(m, t1, now) == Avail::OnHold && core::stopped_by(m, t1) == area);
        core::set_project_state(m, area, ProjectState::Active);

        // Completed, on a note: everything inside counts as done.
        core::set_project_state(m, proj, ProjectState::Completed);
        check("project: a completed note's todos are Done",
              core::availability(m, t1, now) == Avail::Done &&
                  has(ix.query(m, core::Filter::Done, now), t2));
        check("project: ...and not Overdue", !has(ix.query(m, core::Filter::Overdue, now), t2));

        // Back to Active: everything returns, nothing was lost.
        core::set_project_state(m, proj, ProjectState::Active);
        check("project: Active brings every todo back, dates and flags intact",
              core::availability(m, t1, now) == Avail::Available && m.find(t1)->task.flagged &&
                  m.find(t2)->task.due == now - 3600);

        // Completed on a TODO is its tick -- one way to be finished.
        const auto tp = m.create("", "Paint the hall");
        const auto tk = m.create(tp, "buy paint");
        m.make_task(tp, true);
        m.make_task(tk, true);
        core::set_project_state(m, tp, ProjectState::Completed);
        check("project: Completed on a todo ticks it; the stored word stays Active",
              m.find(tp)->task.done && m.find(tp)->task.project == ProjectState::Active);
        check("project: ...it reads Completed",
              core::project_state(*m.find(tp)) == ProjectState::Completed);
        check("project: ...its step is Done", core::availability(m, tk, now) == Avail::Done);
        core::set_project_state(m, tp, ProjectState::OnHold);
        check("project: another state on a ticked todo unticks it",
              !m.find(tp)->task.done &&
                  core::project_state(*m.find(tp)) == ProjectState::OnHold &&
                  core::availability(m, tk, now) == Avail::OnHold);
        check("project: a todo on hold is itself On hold",
              core::availability(m, tp, now) == Avail::OnHold);

        // make_task keeps the container's word; a completed note made a todo
        // becomes a ticked todo, not a todo with two finishes.
        m.make_task(tp, false);
        check("project: un-making a todo keeps its project state",
              m.find(tp)->task.project == ProjectState::OnHold && !m.find(tp)->task.is_task);
        core::set_project_state(m, tp, ProjectState::Completed);
        m.make_task(tp, true);
        check("project: a completed note made a todo is a ticked todo",
              m.find(tp)->task.done && m.find(tp)->task.project == ProjectState::Active);

        // Protected: refused, like every other task edit on a locked note.
        const auto locked = m.create("", "locked");
        m.set_protect(locked, true);
        check("project: a protected note's state is refused",
              !core::set_project_state(m, locked, ProjectState::Dropped) &&
                  m.find(locked)->task.project == ProjectState::Active);

        // Single actions: every item available, nothing comes first.
        const auto errands = m.create("", "Errands");
        const auto e1 = m.create(errands, "post office");
        const auto e2 = m.create(errands, "hardware store");
        m.make_task(e1, true);
        m.make_task(e2, true);
        m.set_status(errands, core::Status::SingleActions);
        check("project: a single-action list makes every item available",
              core::availability(m, e1, now) == Avail::Available &&
                  core::availability(m, e2, now) == Avail::Available);

        check("project: the words", std::string(core::project_state_name(ProjectState::OnHold)) ==
                                            "On hold" &&
                                        std::string(core::avail_name(Avail::Dropped)) == "Dropped");
    }

    // The words survive the disk, and only non-defaults are written.
    {
        namespace fs = std::filesystem;
        const std::string dir = (fs::temp_directory_path() / "jot_selftest_project").string();
        std::error_code ec;
        fs::remove_all(dir, ec);
        core::NodeId held, dropped, done_note, list, plain;
        {
            core::Project v;
            v.open(dir);
            held = v.create("", "held");
            dropped = v.create("", "dropped");
            done_note = v.create("", "finished");
            list = v.create("", "errands");
            plain = v.create("", "plain");
            core::set_project_state(v, held, core::ProjectState::OnHold);
            core::set_project_state(v, dropped, core::ProjectState::Dropped);
            core::set_project_state(v, done_note, core::ProjectState::Completed);
            v.set_status(list, core::Status::SingleActions);
            v.flush();
        }
        std::ifstream f(fs::path(dir) / "jot.json");
        const std::string js((std::istreambuf_iterator<char>(f)), {});
        std::size_t n_keys = 0;
        for (std::size_t at = 0; (at = js.find("\"project\"", at)) != std::string::npos; ++at) ++n_keys;
        check("project/jots: only the three non-active nodes write the key", n_keys == 3, js);
        check("project/jots: written as words",
              js.find("\"on-hold\"") != std::string::npos &&
                  js.find("\"dropped\"") != std::string::npos &&
                  js.find("\"completed\"") != std::string::npos &&
                  js.find("\"single\"") != std::string::npos);
        {
            core::Project v;
            v.open(dir);
            check("project/jots: every state survives a reopen",
                  v.find(held)->task.project == core::ProjectState::OnHold &&
                      v.find(dropped)->task.project == core::ProjectState::Dropped &&
                      v.find(done_note)->task.project == core::ProjectState::Completed &&
                      v.find(list)->task.status == core::Status::SingleActions &&
                      v.find(plain)->task.project == core::ProjectState::Active);
        }
        fs::remove_all(dir, ec);
    }

    // -- Logbook (s032): when it was done ---------------------------------------
    // The stamp is written by the store on the way INTO a finished state, by
    // every road (tick, project menu), and cleared on the way out. The failure
    // this guards: a Logbook that shows a thing on the wrong day, or keeps a
    // thing you unticked.
    {
        using core::ProjectState;
        std::int64_t clock = 1'800'000'000;
        core::MemoryNodes m;
        m.set_clock([&clock] { return clock; });
        const auto home = m.create("", "Home");
        const auto a = m.create(home, "buy paint");
        const auto b = m.create(home, "sand the door");
        const auto proj = m.create("", "Kitchen");                // a NOTE project
        const auto k1 = m.create(proj, "measure");
        const auto old = m.create("", "ancient");
        for (const auto& id : {a, b, k1, old}) m.make_task(id, true);

        check("log: a fresh todo has no finish time", m.find(a)->task.finished == 0);
        m.set_done(a, true);
        check("log: ticking stamps the time", m.find(a)->task.finished == clock);
        clock += 60;
        m.set_flagged(a, true);
        check("log: another edit to a done todo keeps its time",
              m.find(a)->task.finished == clock - 60);
        m.set_done(a, false);
        check("log: unticking clears it", m.find(a)->task.finished == 0);
        m.set_done(a, true);
        check("log: re-ticking stamps the new time", m.find(a)->task.finished == clock);

        clock += 3600;
        core::set_project_state(m, proj, ProjectState::Completed);
        check("log: Completed on a note project stamps", m.find(proj)->task.finished == clock);
        clock += 60;
        core::set_project_state(m, proj, ProjectState::Dropped);
        check("log: Completed -> Dropped is a new event, a new time",
              m.find(proj)->task.finished == clock);
        core::set_project_state(m, proj, ProjectState::OnHold);
        check("log: On hold is not finished", m.find(proj)->task.finished == 0);
        core::set_project_state(m, proj, ProjectState::Completed);

        clock += 60;
        core::set_project_state(m, b, ProjectState::Completed);   // a todo: this is its tick
        check("log: Completed on a todo is its tick, stamped",
              m.find(b)->task.done && m.find(b)->task.finished == clock);
        m.make_task(b, false);
        check("log: un-making a done todo clears the time", m.find(b)->task.finished == 0);
        m.make_task(b, true);

        // An item done before s032: done, no time -- as a load hands it over.
        {
            std::vector<core::Node> all;
            for (const auto& id : {home, a, b, proj, k1, old}) all.push_back(*m.find(id));
            all.back().task.done     = true;
            all.back().task.finished = 0;
            m.reset(all);
        }

        const auto lb = core::logbook(m);
        auto pos = [&lb](const core::NodeId& id) {
            for (std::size_t i = 0; i < lb.size(); ++i) if (lb[i].id == id) return int(i);
            return -1;
        };
        check("log: done todo, completed project and the undated one are listed",
              lb.size() == 3 && pos(a) >= 0 && pos(proj) >= 0 && pos(old) >= 0,
              std::to_string(lb.size()));
        check("log: a todo inside a completed project is not listed twice", pos(k1) == -1);
        check("log: newest first, undated last",
              pos(proj) == 0 && pos(a) == 1 && pos(old) == 2);
        check("log: the project entry says Completed",
              !lb.empty() && lb[0].kind == ProjectState::Completed);

        const auto days = core::group_by_day(lb);
        check("log: two groups -- one day, then the undated tail",
              days.size() == 2 && days[0].entries.size() == 2 && days[1].day == 0,
              std::to_string(days.size()));

        const std::int64_t now = 1'800'000'000;
        check("log: day labels", core::day_label(core::day_start(now), now) == "Today" &&
                                     core::day_label(now - 24 * 3600, now) == "Yesterday" &&
                                     core::day_label(0, now) == "No date recorded",
              core::day_label(now - 24 * 3600, now));
        const std::string wk = core::day_label(now - 3 * 24 * 3600, now);
        const std::string far = core::day_label(now - 40 * 24 * 3600, now);
        check("log: a weekday within the week, a date beyond it",
              wk.find(' ') == std::string::npos && far.find(' ') != std::string::npos, wk + " / " + far);
        check("log: format_clock is HH:MM", core::format_clock(now).size() == 5 &&
                                               core::format_clock(0).empty());

        // Dropped keeps its own kind.
        core::set_project_state(m, proj, ProjectState::Dropped);
        const auto lb2 = core::logbook(m);
        check("log: a dropped project is listed as Dropped",
              !lb2.empty() && lb2[0].id == proj && lb2[0].kind == ProjectState::Dropped);
    }
    // The stamp survives the disk, and is written only when set.
    {
        namespace fs = std::filesystem;
        const std::string dir = (fs::temp_directory_path() / "jot_selftest_logbook").string();
        std::error_code ec;
        fs::remove_all(dir, ec);
        core::NodeId done_id, open_id;
        std::int64_t stamp = 0;
        {
            core::Project v;
            v.open(dir);
            done_id = v.create("", "done one");
            open_id = v.create("", "open one");
            v.make_task(done_id, true);
            v.make_task(open_id, true);
            v.set_done(done_id, true);
            stamp = v.find(done_id)->task.finished;
            v.flush();
        }
        std::ifstream f(fs::path(dir) / "jot.json");
        const std::string js((std::istreambuf_iterator<char>(f)), {});
        std::size_t n_keys = 0;
        for (std::size_t at = 0; (at = js.find("\"finished\"", at)) != std::string::npos; ++at) ++n_keys;
        check("log/jots: only the finished node writes the key", n_keys == 1 && stamp != 0, js);
        {
            core::Project v;
            v.open(dir);
            check("log/jots: the time survives a reopen",
                  v.find(done_id)->task.finished == stamp &&
                      v.find(open_id)->task.finished == 0);
            check("log/jots: the Logbook reads it back", core::logbook(v).size() == 1);
        }
        fs::remove_all(dir, ec);
    }

    // -- Repeat (s033): a todo that comes back ---------------------------------
    // The failure modes: a rule that reads back as something else, a month
    // that walks backwards (Jan 31 -> Feb 28 -> Mar 28), a DST night that
    // moves "end of day" by an hour, and a tick that finishes a repeating
    // todo for good instead of bringing it back.
    {
        using core::Repeat;
        using core::RepeatUnit;
        auto parsed = [](const std::string& t, int every, RepeatUnit u) {
            Repeat r;
            return core::repeat_parse(t, r) && r.every == every && (every == 0 || r.unit == u);
        };
        check("repeat: words", parsed("weekly", 1, RepeatUnit::Week) &&
                                   parsed("Daily", 1, RepeatUnit::Day) &&
                                   parsed("fortnightly", 2, RepeatUnit::Week) &&
                                   parsed("monthly", 1, RepeatUnit::Month) &&
                                   parsed("annually", 1, RepeatUnit::Year));
        check("repeat: every N units", parsed("every 2 weeks", 2, RepeatUnit::Week) &&
                                           parsed("  Every Other  Month ", 2, RepeatUnit::Month) &&
                                           parsed("3 days", 3, RepeatUnit::Day) &&
                                           parsed("every day", 1, RepeatUnit::Day) &&
                                           parsed("every 1 year", 1, RepeatUnit::Year));
        check("repeat: empty / none / never is off",
              parsed("", 0, RepeatUnit::Day) && parsed("none", 0, RepeatUnit::Day) &&
                  parsed("never", 0, RepeatUnit::Day));
        {
            Repeat r{3, RepeatUnit::Week, true};
            const Repeat before = r;
            check("repeat: nonsense is refused and changes nothing",
                  !core::repeat_parse("every 0 days", r) && !core::repeat_parse("every 2", r) &&
                      !core::repeat_parse("sometimes", r) && !core::repeat_parse("every 2x weeks", r) &&
                      r == before);
            Repeat d{0, RepeatUnit::Day, true};
            check("repeat: parsing keeps the from-done switch",
                  core::repeat_parse("weekly", d) && d.from_done);
        }
        {
            bool ok = true;
            std::string bad;
            for (const auto& r : {Repeat{1, RepeatUnit::Day}, Repeat{2, RepeatUnit::Week},
                                  Repeat{1, RepeatUnit::Month}, Repeat{5, RepeatUnit::Year}}) {
                Repeat back;
                if (!core::repeat_parse(core::repeat_text(r), back) || back != r) {
                    ok = false;
                    bad += core::repeat_text(r) + " ";
                }
            }
            check("repeat: text round-trips", ok && core::repeat_text(Repeat{}).empty(), bad);
        }

        // Calendar arithmetic, in a zone with DST so the night it changes is real.
        const char* old_tz = std::getenv("TZ");
        const std::string saved = old_tz ? old_tz : "";
        setenv("TZ", "America/Chicago", 1);
        tzset();
        auto at = [](int y, int mo, int d, int h, int mi, int sec) {
            std::tm tm{};
            tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d;
            tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = sec; tm.tm_isdst = -1;
            return static_cast<std::int64_t>(std::mktime(&tm));
        };
        const Repeat monthly{1, RepeatUnit::Month};
        const std::int64_t jan31 = at(2027, 1, 31, 23, 59, 59);
        check("repeat: Jan 31 + a month is Feb 28",
              core::repeat_add(jan31, monthly, 1) == at(2027, 2, 28, 23, 59, 59));
        check("repeat: ...and + two months is Mar 31, not Mar 28",
              core::repeat_add(jan31, monthly, 2) == at(2027, 3, 31, 23, 59, 59));
        check("repeat: Feb 29 + a year is Feb 28",
              core::repeat_add(at(2028, 2, 29, 9, 0, 0), Repeat{1, RepeatUnit::Year}) ==
                  at(2029, 2, 28, 9, 0, 0));
        check("repeat: a day across the DST night keeps end-of-day",
              core::repeat_add(at(2026, 10, 31, 23, 59, 59), Repeat{1, RepeatUnit::Day}) ==
                  at(2026, 11, 1, 23, 59, 59));
        {
            // Fixed: due ten days ago, weekly -> the first one after now (+14),
            // and the defer keeps its two days, start of day, across DST.
            const std::int64_t now = at(2026, 11, 3, 12, 0, 0);
            std::int64_t due = at(2026, 10, 24, 23, 59, 59);
            std::int64_t defer = at(2026, 10, 22, 0, 0, 0);
            core::repeat_next(Repeat{1, RepeatUnit::Week}, now, due, defer);
            check("repeat: fixed skips the missed one and lands after now",
                  due == at(2026, 11, 7, 23, 59, 59), core::format_date(due));
            check("repeat: the defer keeps its gap, at the start of a day",
                  defer == at(2026, 11, 5, 0, 0, 0), core::format_date(defer));
        }
        {
            // The defer on the far side of the DST night from the due: moved
            // by seconds it would land at 23:00 the day before.
            const std::int64_t now = at(2026, 11, 3, 12, 0, 0);
            std::int64_t due = at(2026, 11, 2, 23, 59, 59);
            std::int64_t defer = at(2026, 10, 30, 0, 0, 0);
            core::repeat_next(Repeat{1, RepeatUnit::Week}, now, due, defer);
            check("repeat: a defer across the DST night stays at the start of its day",
                  due == at(2026, 11, 9, 23, 59, 59) && defer == at(2026, 11, 6, 0, 0, 0),
                  core::format_date(defer));
        }
        {
            // From done: due in five days at 18:00, done today -> a week from today at 18:00.
            const std::int64_t now = at(2026, 10, 2, 15, 0, 0);
            std::int64_t due = at(2026, 10, 7, 18, 0, 0), defer = 0;
            core::repeat_next(Repeat{1, RepeatUnit::Week, true}, now, due, defer);
            check("repeat: from done counts from the day it was done",
                  due == at(2026, 10, 9, 18, 0, 0) && defer == 0, core::format_date(due));
            std::int64_t d0 = 0, f0 = 0;
            core::repeat_next(Repeat{1, RepeatUnit::Week}, now, d0, f0);
            check("repeat: no dates, nothing to move", d0 == 0 && f0 == 0);
        }

        // The roll, at the one door.
        {
            std::int64_t clock = at(2026, 10, 2, 15, 0, 0);
            core::MemoryNodes m;
            m.set_clock([&clock] { return clock; });
            const auto plants = m.create("", "water the plants");
            const auto plain = m.create("", "one-off");
            const auto rent = m.create("", "pay rent");
            const auto step = m.create(rent, "log in to the bank");
            for (const auto& id : {plants, plain, rent, step}) m.make_task(id, true);
            m.set_due(plants, at(2026, 10, 2, 23, 59, 59));
            m.set_repeat(plants, Repeat{1, RepeatUnit::Week});
            m.set_due(rent, at(2026, 10, 1, 23, 59, 59));
            m.set_repeat(rent, Repeat{1, RepeatUnit::Month});

            m.set_done(plants, true);
            const core::Node* p = m.find(plants);
            check("repeat/roll: ticking brings it back unticked", !p->task.done && p->task.finished == 0);
            check("repeat/roll: ...due a week on", p->task.due == at(2026, 10, 9, 23, 59, 59),
                  core::format_date(p->task.due));
            check("repeat/roll: ...and the occurrence is in the history",
                  m.history().size() == 1 && m.history()[0].id == plants &&
                      m.history()[0].title == "water the plants" && m.history()[0].when == clock);
            m.set_done(plain, true);
            check("repeat/roll: a todo with no rule finishes as before",
                  m.find(plain)->task.done && m.history().size() == 1);

            m.set_done(step, true);
            clock += 3600;
            core::set_project_state(m, rent, core::ProjectState::Completed);
            check("repeat/roll: Completed on a repeating project rolls it too",
                  !m.find(rent)->task.done && m.find(rent)->task.due == at(2026, 11, 1, 23, 59, 59) &&
                      m.history().size() == 2, core::format_date(m.find(rent)->task.due));
            check("repeat/roll: ...and its steps start over", !m.find(step)->task.done);

            const auto lb = core::logbook(m);
            std::size_t reps = 0;
            for (const auto& e : lb) if (e.repeat) ++reps;
            check("repeat/log: the Logbook lists both occurrences, newest first",
                  reps == 2 && lb.size() == 3 && lb[0].repeat && lb[0].title == "pay rent",
                  std::to_string(lb.size()));
            m.set_title(plants, "water the ferns");
            {
                std::string t;
                for (const auto& e : core::logbook(m)) if (e.repeat && e.id == plants) t = e.title;
                check("repeat/log: a record keeps the title it was done under",
                      t == "water the plants", t);
            }
            m.make_task(plants, false);
            check("repeat: un-making the todo drops its rule", !m.find(plants)->task.repeat.on());
        }

        // s033b: the quick picks beside the calendars.
        {
            using core::QuickDate;
            const std::int64_t fri = at(2026, 10, 2, 15, 0, 0);   // a Friday
            auto q = [](QuickDate k, std::int64_t now, std::int64_t due = 0) {
                return core::quick_date_text(k, now, due);
            };
            check("quick: today / tomorrow / next week",
                  q(QuickDate::Today, fri) == "2026-10-02" && q(QuickDate::Tomorrow, fri) == "2026-10-03" &&
                      q(QuickDate::NextWeek, fri) == "2026-10-09");
            check("quick: from a Friday, this weekend is Saturday and next Monday the 5th",
                  q(QuickDate::ThisWeekend, fri) == "2026-10-03" &&
                      q(QuickDate::NextMonday, fri) == "2026-10-05");
            check("quick: on a weekend, this weekend is today",
                  q(QuickDate::ThisWeekend, at(2026, 10, 3, 9, 0, 0)) == "2026-10-03" &&
                      q(QuickDate::ThisWeekend, at(2026, 10, 4, 9, 0, 0)) == "2026-10-04");
            check("quick: on a Monday, next Monday is a week on",
                  q(QuickDate::NextMonday, at(2026, 10, 5, 9, 0, 0)) == "2026-10-12" &&
                      q(QuickDate::NextMonday, at(2026, 10, 4, 9, 0, 0)) == "2026-10-05");
            check("quick: 2 days before due, and nothing without a due",
                  q(QuickDate::BeforeDue, fri, at(2026, 10, 9, 23, 59, 59)) == "2026-10-07" &&
                      q(QuickDate::BeforeDue, fri, 0).empty());
            check("quick: across the DST night, still the right day",
                  q(QuickDate::NextWeek, at(2026, 10, 30, 23, 30, 0)) == "2026-11-06" &&
                      q(QuickDate::Tomorrow, at(2026, 10, 31, 23, 30, 0)) == "2026-11-01");
        }

        if (saved.empty()) unsetenv("TZ"); else setenv("TZ", saved.c_str(), 1);
        tzset();
    }
    // The rule and the history survive the disk.
    {
        namespace fs = std::filesystem;
        const std::string dir = (fs::temp_directory_path() / "jot_selftest_repeat").string();
        std::error_code ec;
        fs::remove_all(dir, ec);
        core::NodeId a, b;
        {
            core::Project v;
            v.open(dir);
            a = v.create("", "weekly chore");
            b = v.create("", "plain");
            v.make_task(a, true);
            v.make_task(b, true);
            v.set_due(a, 1'800'000'000);
            v.set_repeat(a, core::Repeat{2, core::RepeatUnit::Week, true});
            v.set_done(a, true);
            v.flush();
        }
        std::ifstream f(fs::path(dir) / "jot.json");
        const std::string js((std::istreambuf_iterator<char>(f)), {});
        check("repeat/jots: written as words, only on the repeating node",
              js.find("\"every 2 weeks\"") != std::string::npos &&
                  js.find("\"repeat_from\": \"done\"") != std::string::npos &&
                  js.find("\"history\"") != std::string::npos, js);
        {
            core::Project v;
            v.open(dir);
            const auto& r = v.find(a)->task.repeat;
            check("repeat/jots: the rule survives a reopen",
                  r.every == 2 && r.unit == core::RepeatUnit::Week && r.from_done &&
                      !v.find(b)->task.repeat.on());
            check("repeat/jots: the history survives a reopen",
                  v.history().size() == 1 && v.history()[0].title == "weekly chore" &&
                      core::logbook(v).size() == 1);
        }
        fs::remove_all(dir, ec);
    }

    // -- Cheat sheet (s030): the running reference ------------------------------
    // "Every milestone adds its lines" is a rule; these make it a failing test.
    // A keyed verb with no line, a line naming a verb that does not exist, a key
    // written here by hand that the registry would say differently -- each one
    // is a sheet that lies to the person reading it.
    {
        const auto& cs = core::cheat_sheet();
        const auto& secs = core::cheat_sections();
        check("cheat: the sheet is populated", cs.size() >= 40, std::to_string(cs.size()) + " lines");

        {
            const auto miss = core::cheat_missing_actions();
            std::string all;
            for (const auto& a : miss) all += a + " ";
            check("cheat: every keyed verb has a line (Diagnostics aside)", miss.empty(), all);
        }
        {
            const auto unk = core::cheat_unknown_actions();
            std::string all;
            for (const auto& a : unk) all += a + " ";
            check("cheat: every action a line names is a registered verb", unk.empty(), all);
        }

        // Sections: each line's section is a known one, sections are contiguous
        // and come in cheat_sections() order, and none is empty.
        {
            bool known = true, ordered = true;
            int last = -1;
            std::vector<int> count(secs.size(), 0);
            for (const auto& l : cs) {
                const auto it = std::find(secs.begin(), secs.end(), l.section);
                if (it == secs.end()) { known = false; continue; }
                const int at = static_cast<int>(it - secs.begin());
                if (at < last) ordered = false;
                last = at;
                ++count[static_cast<std::size_t>(at)];
            }
            check("cheat: every line is in a known section", known);
            check("cheat: sections are contiguous and in reading order", ordered);
            check("cheat: no section is empty",
                  std::find(count.begin(), count.end(), 0) == count.end());
        }

        // Every line can be read: something in the left column, something it does.
        {
            bool ok = true;
            std::string bad;
            for (const auto& l : cs)
                if (l.display_how().empty() || l.what.empty()) { ok = false; bad = l.what; }
            check("cheat: every line has a how and a what", ok, bad);
        }

        // Keys come from the registry, not from this file: Move shows Ctrl+M
        // because the registry says so, and an action line never repeats a key
        // literally (a literal would be the copy that drifts).
        {
            const core::CheatLine* move = nullptr;
            bool literal_dup = false;
            for (const auto& l : cs) {
                if (l.action == "win.move-to") move = &l;
                if (!l.action.empty() && !l.keys.empty() &&
                    l.keys.find("Ctrl") != std::string::npos)
                    literal_dup = true;
            }
            check("cheat: Move's key is read from the registry",
                  move && move->display_how() == "Ctrl+M", move ? move->display_how() : "none");
            check("cheat: no action line also spells a key by hand", !literal_dup);
        }
        {
            core::CheatLine both{"Help", "win.import-md-folder", "Main menu", "x", ""};
            check("cheat: an unkeyed verb shows its literal how", both.display_how() == "Main menu",
                  both.display_how());
            core::CheatLine two{"Help", "win.move-to", "or here", "x", ""};
            check("cheat: a key plus a second way are joined",
                  two.display_how() == "Ctrl+M  or  or here", two.display_how());
        }

        // The sheet's own key: Ctrl+H (the letter twin, first) and F1, GNOME's help.
        {
            bool found = false;
            for (const auto& s2 : core::shortcut_registry())
                if (s2.action == "win.cheat-sheet" && s2.accels.size() == 2 &&
                    s2.accels[0] == "<Ctrl>h" && s2.accels[1] == "F1")
                    found = true;
            check("cheat: Ctrl+H / F1 open the cheat sheet", found);
            check("cheat: Ctrl+H takes nothing from a text box",
                  !core::steals_text_editing("<Ctrl>h"));
        }

        // The filter: every word must hit, case-blind, across how / what / where
        // / section; empty shows all.
        {
            const core::CheatLine* move = nullptr;
            for (const auto& l : cs)
                if (l.action == "win.move-to") move = &l;
            check("cheat: an empty query shows every line",
                  std::all_of(cs.begin(), cs.end(),
                              [](const core::CheatLine& l) { return core::cheat_matches(l, "  "); }));
            check("cheat: found by its key, any case", move && core::cheat_matches(*move, "ctrl+M"));
            check("cheat: found by its section and a word",
                  move && core::cheat_matches(*move, "INBOX  file"));
            check("cheat: found by its 'where' line", move && core::cheat_matches(*move, "recent"));
            check("cheat: every word must hit", move && !core::cheat_matches(*move, "inbox zebra"));
            int due = 0;
            for (const auto& l : cs)
                if (core::cheat_matches(l, "due")) ++due;
            check("cheat: 'due' finds more than one line", due >= 2, std::to_string(due));
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

        // s025c: jot --list NAME item...
        {
            core::MemoryNodes m;
            const auto proj = m.create("", "Projects");
            bool grew = true;
            const auto g = core::capture_list(m, "Groceries", {"milk", " eggs ", "sourdough bread"}, &grew);
            check("list: a new name makes a top-level note of tasks",
                  !g.empty() && !grew && m.find(g)->parent_id.empty() && m.find(g)->title == "Groceries" &&
                      m.find(g)->body == "- [ ] milk\n- [ ] eggs\n- [ ] sourdough bread\n",
                  m.find(g) ? m.find(g)->body : "(none)");
            const auto g2 = core::capture_list(m, "  groceries ", {"butter"}, &grew);
            check("list: the same name, any case, GROWS that note", g2 == g && grew &&
                      m.find(g)->body == "- [ ] milk\n- [ ] eggs\n- [ ] sourdough bread\n- [ ] butter\n");
            m.set_body(g, "Shop list");   // no trailing newline
            core::capture_list(m, "Groceries", {"jam"});
            check("list: added on a fresh line after text with no newline",
                  m.find(g)->body == "Shop list\n- [ ] jam\n", m.find(g)->body);
            const auto kid = m.create(proj, "Hardware");
            check("list: a note deep in the tree is found", core::capture_list(m, "hardware", {"nails"}) == kid);
            m.set_protect(kid, true);
            const auto k2 = core::capture_list(m, "Hardware", {"screws"}, &grew);
            check("list: a protected note is not written into -- a new one is made",
                  !k2.empty() && k2 != kid && !grew && m.find(kid)->body == "- [ ] nails\n");
            const std::size_t before = m.count();
            check("list: no items, blank items, or no name do nothing",
                  core::capture_list(m, "Groceries", {}).empty() &&
                      core::capture_list(m, "Groceries", {"  ", ""}).empty() &&
                      core::capture_list(m, "  ", {"x"}).empty() && m.count() == before);
            core::capture_list(m, "Two", {"line\nbreak"});
            check("list: a newline inside an item is a space",
                  m.find(core::find_list_note(m, "two"))->body == "- [ ] line break\n");
            core::Pending lp;
            check("list: the spool carries the list name and its items",
                  core::decode_pending(core::encode_pending("milk\neggs", 9, "Groceries"), lp) &&
                      lp.list == "Groceries" && lp.text == "milk\neggs" && lp.captured == 9);
            core::Pending cp;
            check("list: a plain capture has no list name",
                  core::decode_pending(core::encode_pending("milk", 9), cp) && cp.list.empty());

            // s025d: jot NAME -a words
            check("append: a line on a fresh line", core::append_text("Hello", "world") == "Hello\nworld\n");
            check("append: to an empty body", core::append_text("", " hi ") == "hi\n");
            check("append: after a task list, a blank line so it does not join the list",
                  core::append_text("- [ ] milk\n", "check the pantry") ==
                      "- [ ] milk\n\ncheck the pantry\n");
            check("append: after a numbered item too", core::append_text("1. a", "b") == "1. a\n\nb\n");
            check("append: after prose, no blank line", core::append_text("text\n", "more") == "text\nmore\n");
            check("append: nothing to add changes nothing", core::append_text("x\n", "   ") == "x\n");
            const auto ga = core::capture_append(m, "GROCERIES", "check the pantry first", &grew);
            check("append: grows the note of that name, any case",
                  ga == g && grew && m.find(g)->body == "Shop list\n- [ ] jam\n\ncheck the pantry first\n",
                  m.find(g)->body);
            const auto na = core::capture_append(m, "Vet", "they open at 8", &grew);
            check("append: a new name makes the note", !na.empty() && !grew &&
                      m.find(na)->title == "Vet" && m.find(na)->body == "they open at 8\n");
            core::Pending ap;
            check("append: the spool carries the note name",
                  core::decode_pending(core::encode_pending("they open at 8", 3, {}, "Vet"), ap) &&
                      ap.append == "Vet" && ap.list.empty() && ap.text == "they open at 8");
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

    // -- Inbox: the mark, the states, Clean Up (s028) --------------------------
    // s007 said "there is no Inbox". s028 overturned it: every capture road
    // marks what it MAKES, filing or ticking makes it PROCESSED without taking
    // it off the list, and only Clean Up clears the mark.
    {
        core::MemoryNodes m;
        const auto proj = m.create("", "Taxes");
        const auto plain = m.create("", "a note made by hand");
        check("inbox: a note made by hand is not in the Inbox", !m.find(plain)->inbox);

        const auto c1 = core::capture(m, "ring the vet");
        const auto c2 = core::capture(m, "file the 1099\nfrom the bank");
        check("inbox: a capture is marked", m.find(c1)->inbox && m.find(c2)->inbox);
        check("inbox: a capture is Waiting",
              core::inbox_state(*m.find(c1)) == core::InboxState::Waiting);

        bool grew = false;
        const auto gl = core::capture_list(m, "Groceries", {"milk"}, &grew);
        check("inbox: a NEW list note is marked", !grew && m.find(gl)->inbox);
        m.set_inbox(gl, false);
        core::capture_list(m, "Groceries", {"eggs"}, &grew);
        check("inbox: GROWING a list does not re-mark it", grew && !m.find(gl)->inbox);
        const auto ga = core::capture_append(m, "Ideas", "a thought", &grew);
        check("inbox: a NEW append note is marked", !grew && m.find(ga)->inbox);
        m.set_inbox(ga, false);

        // Order: tree order, so a later capture is below an earlier one.
        auto mem = core::inbox_members(m);
        check("inbox: members in tree order",
              mem.size() == 2 && mem[0] == c1 && mem[1] == c2);

        // Filing: a move makes it Filed -- and it STAYS in the Inbox.
        check("inbox: file it under a project", m.move(c2, proj));
        check("inbox: a filed capture is Filed, not gone",
              m.find(c2)->inbox &&
                  core::inbox_state(*m.find(c2)) == core::InboxState::Filed);
        mem = core::inbox_members(m);
        check("inbox: a filed capture is still a member (until Clean Up)",
              mem.size() == 2 && std::find(mem.begin(), mem.end(), c2) != mem.end());

        // Done: a todo ticked done is processed even at the top level.
        const auto c3 = core::capture(m, "buy stamps");
        m.make_task(c3, true);
        check("inbox: an open todo at the top level is Waiting",
              core::inbox_state(*m.find(c3)) == core::InboxState::Waiting);
        m.set_done(c3, true);
        check("inbox: a done todo is Done",
              core::inbox_state(*m.find(c3)) == core::InboxState::Done);
        m.move(c3, proj);
        check("inbox: done beats filed", core::inbox_state(*m.find(c3)) == core::InboxState::Done);

        auto counts = core::inbox_counts(m);
        check("inbox: counts -- one waiting, two ready",
              counts.waiting == 1 && counts.ready == 2,
              std::to_string(counts.waiting) + "/" + std::to_string(counts.ready));

        // Clean Up: clears the processed, leaves the waiting, moves nothing.
        const auto before_parent = m.find(c2)->parent_id;
        const auto before_body = m.find(c2)->body;
        int notes = 0;
        m.on_changed([&](core::NodeSource::Change w, const core::NodeId&) {
            if (w == core::NodeSource::Change::Flags) ++notes;
        });
        check("inbox: Clean Up takes the two processed", core::clean_up(m) == 2);
        check("inbox: ...and says so through the model (Flags)", notes == 2);
        check("inbox: the waiting one stays", m.find(c1)->inbox);
        check("inbox: the cleaned ones are unmarked", !m.find(c2)->inbox && !m.find(c3)->inbox);
        check("inbox: Clean Up moved and edited nothing",
              m.find(c2)->parent_id == before_parent && m.find(c2)->body == before_body);
        check("inbox: a second Clean Up is a no-op", core::clean_up(m) == 0);
        counts = core::inbox_counts(m);
        check("inbox: counts after -- one waiting, none ready",
              counts.waiting == 1 && counts.ready == 0);

        // The hand-cleared case: Keep at the top level.
        check("inbox: set_inbox off is a write", m.set_inbox(c1, false));
        check("inbox: set_inbox to the same value is a no-op", !m.set_inbox(c1, false));
        check("inbox: empty now", core::inbox_members(m).empty());

        // A protected note can still be marked and cleared.
        m.set_protect(plain, true);
        check("inbox: protection does not refuse the mark",
              m.set_inbox(plain, true) && m.set_inbox(plain, false));

        // Ages.
        const std::int64_t now = 1'800'000'000;
        check("inbox: age -- just now", core::age_phrase(now - 5, now) == "just now");
        check("inbox: age -- minutes", core::age_phrase(now - 4 * 60, now) == "4 min ago");
        check("inbox: age -- hours", core::age_phrase(now - 3 * 3600, now) == "3 h ago");
        check("inbox: age -- a day", core::age_phrase(now - 30 * 3600, now) == "1 day ago");
        check("inbox: age -- days", core::age_phrase(now - 5 * 86400, now) == "5 days ago");
        check("inbox: age -- old is a date",
              core::age_phrase(now - 40 * 86400, now) == core::format_date(now - 40 * 86400));
        check("inbox: age -- a clock gone backwards is just now",
              core::age_phrase(now + 500, now) == "just now");
        check("inbox: age -- no time is no phrase", core::age_phrase(0, now).empty());
    }

    // -- Move to... : where a note can go (s029) ------------------------------
    // The picker offers only what can_move allows, names each place by its
    // path, ranks for what was typed, and remembers where you filed lately.
    {
        core::MemoryNodes m;
        const auto work  = m.create("", "Work");
        const auto taxes = m.create(work, "Taxes");
        const auto y2025 = m.create(taxes, "2025");
        const auto home  = m.create("", "Home");
        const auto notes = m.create(home, "Notes");
        const auto wnote = m.create(work, "Notes");
        const auto cap   = core::capture(m, "file the 1099");

        auto ids = [](const std::vector<core::MoveTarget>& v) {
            std::vector<core::NodeId> out;
            for (const auto& t : v) out.push_back(t.id);
            return out;
        };
        auto has = [&](const std::vector<core::MoveTarget>& v, const core::NodeId& id) {
            const auto i = ids(v);
            return std::find(i.begin(), i.end(), id) != i.end();
        };

        // A top-level capture: everything but itself; no "Top level" (it is there).
        auto t = core::move_targets(m, cap, "");
        check("move: a top-level note is not offered the top level", !has(t, ""));
        check("move: a note is not offered itself", !has(t, cap));
        check("move: every other note is a place, in tree order",
              ids(t) == std::vector<core::NodeId>{work, taxes, y2025, wnote, home, notes},
              std::to_string(t.size()));
        check("move: a place is named by its path",
              t[2].title == "2025" && t[2].path == "Work › Taxes" && t[0].path.empty());
        check("move: node_path agrees", core::node_path(m, y2025) == "Work › Taxes");

        // A nested note: the top level first, never its own subtree, never
        // where it already is.
        t = core::move_targets(m, taxes, "");
        check("move: a nested note is offered the top level, first",
              !t.empty() && t[0].id.empty() && t[0].title == core::kTopLevel);
        check("move: never into its own subtree", !has(t, y2025) && !has(t, taxes));
        check("move: not where it already is", !has(t, work));
        for (const auto& x : t)
            check("move: every offer passes can_move (" + x.title + ")",
                  core::can_move(m, taxes, x.id));

        // Ranking: starts-with, then contains, then path.
        const auto tax2 = m.create(home, "Property tax");
        t = core::move_targets(m, cap, "tax");
        check("move: query -- prefix first, then contains, then path",
              ids(t) == std::vector<core::NodeId>{taxes, tax2, y2025}, std::to_string(t.size()));
        t = core::move_targets(m, cap, "  NOTES ");
        check("move: query is trimmed and case-blind",
              ids(t) == std::vector<core::NodeId>{wnote, notes});
        check("move: two \"Notes\" told apart by path",
              t.size() == 2 && t[0].path == "Work" && t[1].path == "Home");
        check("move: nothing matches -> empty", core::move_targets(m, cap, "zebra").empty());
        t = core::move_targets(m, taxes, "top");
        check("move: \"top\" finds the top level", !t.empty() && t[0].id.empty());

        // A protected note goes nowhere; an unknown one neither.
        m.set_protect(cap, true);
        check("move: a protected note has no places", core::move_targets(m, cap, "").empty());
        m.set_protect(cap, false);
        check("move: an unknown note has no places", core::move_targets(m, "nope", "").empty());

        // An untitled note is still a place, and says so.
        const auto blank = m.create("", "");
        t = core::move_targets(m, cap, "untitled");
        check("move: an untitled place reads Untitled",
              t.size() == 1 && t[0].id == blank && t[0].title == "Untitled");

        // Recent: most recent first, deduplicated, capped, top level not kept.
        std::vector<core::NodeId> r;
        core::remember_target(r, taxes);
        core::remember_target(r, home);
        core::remember_target(r, taxes);
        core::remember_target(r, "");
        check("move: recent -- newest first, no repeats, no top level",
              r == std::vector<core::NodeId>{taxes, home});
        for (int i = 0; i < 9; ++i) core::remember_target(r, "x" + std::to_string(i), 5);
        check("move: recent -- capped", r.size() == 5 && r[0] == "x8");

        // Recent places are filtered for THIS note: gone, itself, its parent,
        // its own subtree.
        r = {y2025, taxes, work, home, "gone"};
        auto rt = core::recent_targets(m, taxes, r);
        check("move: recent -- only legal places for this note",
              ids(rt) == std::vector<core::NodeId>{home}, std::to_string(rt.size()));
        rt = core::recent_targets(m, cap, r);
        check("move: recent -- carries the path",
              rt.size() == 4 && rt[0].id == y2025 && rt[0].path == "Work › Taxes");

        // The whole road: a capture filed by the picker reads Filed on the Inbox.
        t = core::move_targets(m, cap, "taxes");
        check("move: file the capture", !t.empty() && m.move(cap, t[0].id, -1));
        check("move: ...and the Inbox says Filed",
              m.find(cap)->parent_id == taxes &&
                  core::inbox_state(*m.find(cap)) == core::InboxState::Filed);
        check("move: ...appended at the end of its new home",
              m.children(taxes).back() == cap);
    }

    // The picker's key is a real chord, and nobody else's.
    {
        const auto& reg = core::shortcut_registry();
        bool found = false;
        for (const auto& s2 : reg)
            if (s2.action == "win.move-to" && !s2.accels.empty() && s2.accels[0] == "<Ctrl>m")
                found = true;
        check("move: Ctrl+M is win.move-to", found);
    }

    // The mark survives the disk, and only marked nodes carry the key.
    {
        namespace fs = std::filesystem;
        const std::string dir = (fs::temp_directory_path() / "jot_selftest_inbox").string();
        std::error_code ec;
        fs::remove_all(dir, ec);
        core::NodeId cap, hand;
        {
            core::Project v;
            v.open(dir);
            hand = v.create("", "by hand");
            cap = core::capture(v, "captured");
            v.flush();
        }
        std::ifstream f(fs::path(dir) / "jot.json");
        const std::string js((std::istreambuf_iterator<char>(f)), {});
        std::size_t n_keys = 0;
        for (std::size_t at = 0; (at = js.find("\"inbox\"", at)) != std::string::npos; ++at) ++n_keys;
        check("inbox/jots: only the marked node writes the key", n_keys == 1, js);
        {
            core::Project v;
            v.open(dir);
            check("inbox/jots: the mark survives a reopen",
                  v.find(cap) && v.find(cap)->inbox && v.find(hand) && !v.find(hand)->inbox);
            v.set_inbox(cap, false);
        }
        {
            core::Project v;
            v.open(dir);
            check("inbox/jots: clearing it is written at once",
                  v.find(cap) && !v.find(cap)->inbox);
        }
        {
            // Adopt (scratch -> folder) carries the mark.
            core::MemoryNodes scratch;
            core::capture(scratch, "scribbled before a folder existed");
            const std::string dir2 = dir + "_adopt";
            fs::remove_all(dir2, ec);
            core::Project v;
            v.open(dir2);
            v.adopt(scratch);
            const auto roots = v.children("");
            check("inbox/adopt: the scratch capture is still in the Inbox",
                  roots.size() == 1 && v.find(roots[0])->inbox);
            fs::remove_all(dir2, ec);
        }
        fs::remove_all(dir, ec);
    }

    // ── enclosures (s016b) ──────────────────────────────────────────────────
    // Images as the first enclosure kind. The list is DERIVED from the body;
    // the files and the metadata are carried by adopt. Every check here is a
    // way an image could be lost or pointed at wrongly without the screen
    // saying so.
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path root = fs::temp_directory_path() / "jot_selftest_enclosures";
        fs::remove_all(root, ec);
        fs::create_directories(root / "src", ec);

        check("enclosure: slug keeps the name, lowers it, dashes the rest",
              core::slug_filename("My Photo (1).JPG") == "my-photo-1.jpg",
              core::slug_filename("My Photo (1).JPG"));
        check("enclosure: slug takes only the basename of a path",
              core::slug_filename("/home/s/Pictures/Cat.png") == "cat.png");
        check("enclosure: an unsluggable stem becomes 'image'",
              core::slug_filename("\xe7\x8c\xab.png") == "image.png",
              core::slug_filename("\xe7\x8c\xab.png"));
        check("enclosure: a dotfile is a name, not an extension",
              core::slug_filename(".hidden") == "hidden");
        check("enclosure: a pasted image is named by the moment",
              core::paste_filename(0).rfind("pasted-", 0) == 0 &&
                  core::paste_filename(0).size() == std::string("pasted-19700101-000000.png").size());
        check("enclosure: image by extension, case-insensitive",
              core::is_image_filename("a.PNG") && core::is_image_filename("b.jpeg") &&
                  !core::is_image_filename("c.pdf") && !core::is_image_filename("png"));
        check("enclosure: attachment_name reads our prefix",
              core::attachment_name("attachments/a.png") == "a.png");
        check("enclosure: attachment_name refuses escape and subfolders",
              core::attachment_name("attachments/../x.png").empty() &&
                  core::attachment_name("attachments/a/b.png").empty() &&
                  core::attachment_name("https://x/a.png").empty() &&
                  core::attachment_name("attachments/").empty());
        check("enclosure: the reference drops brackets from the label",
              core::image_markdown("a [b]", "a.png") == "![a b](attachments/a.png)");

        // ingest, and unique names
        const fs::path srcf = root / "src" / "Holiday Snap.PNG";
        std::ofstream(srcf, std::ios::binary) << "not really a png";
        core::AttachStore st{(root / "scratch").string(), {}};
        std::string err;
        const std::string n1 = core::ingest_file(st, srcf.string(), 1000, err);
        const std::string n2 = core::ingest_file(st, srcf.string(), 1001, err);
        check("enclosure: ingest copies under the slugged name", n1 == "holiday-snap.png", n1 + err);
        check("enclosure: a second ingest of the same name gets -2", n2 == "holiday-snap-2.png", n2);
        check("enclosure: the source is untouched", fs::exists(srcf, ec));
        check("enclosure: metadata records source, size and time",
              st.metas.count(n1) && st.metas[n1].source == srcf.string() &&
                  st.metas[n1].size == 16 && st.metas[n1].added == 1000 &&
                  st.metas[n1].mode == "embedded");
        const std::string n3 = core::ingest_bytes(st, "PNGBYTES", "pasted-20260101-000000.png",
                                                  "clipboard", 1002, err);
        check("enclosure: bytes ingest writes the file", n3 == "pasted-20260101-000000.png" &&
                  fs::file_size(fs::path(st.dir) / n3, ec) == 8, n3 + err);
        check("enclosure: ingest of a missing file fails with a reason",
              core::ingest_file(st, (root / "nope.png").string(), 1, err).empty() && !err.empty());

        // s018 -- any file, not just an image. Its own store, so the counts
        // the image tests below rely on are not disturbed.
        {
            check("file enclosure: an image is referenced with a bang",
                  core::enclosure_markdown("Cat", "cat.png") == "![Cat](attachments/cat.png)");
            check("file enclosure: any other file is a plain link",
                  core::enclosure_markdown("Q3 [final]", "q3-final.pdf") ==
                      "[Q3 final](attachments/q3-final.pdf)",
                  core::enclosure_markdown("Q3 [final]", "q3-final.pdf"));
            check("file enclosure: an unsluggable stem becomes 'file', not 'image'",
                  core::slug_filename("\xe7\x8c\xab.PDF") == "file.pdf",
                  core::slug_filename("\xe7\x8c\xab.PDF"));
            const fs::path pdf = root / "src" / "Q3 Report.PDF";
            std::ofstream(pdf, std::ios::binary) << "%PDF-1.7 not really";
            core::AttachStore fst{(root / "files").string(), {}};
            const std::string fn = core::ingest_file(fst, pdf.string(), 2000, err);
            check("file enclosure: a PDF ingests like an image did", fn == "q3-report.pdf" &&
                      fst.metas.count(fn) && fst.metas[fn].size == 19, fn + err);
            check("file enclosure: a folder is refused, with a reason",
                  core::ingest_file(fst, (root / "src").string(), 1, err).empty() &&
                      err.find("folder") != std::string::npos, err);
            const std::string fb = "See " + core::enclosure_markdown("Q3 Report", fn) +
                                   " and ![p](attachments/q3-report.pdf)\n";
            const auto fl = core::enclosures(fb, fst);
            check("file enclosure: a plain link is listed, present, joined",
                  fl.size() == 1 && fl[0].name == fn && fl[0].present && fl[0].has_meta &&
                      fl[0].refs == 2 && fl[0].label == "Q3 Report",
                  std::to_string(fl.size()));
            std::string fb2 = fb;
            check("file enclosure: a rename rewrites plain links and bang links alike",
                  core::rename_references(fb2, fn, "q3-report-2.pdf") == 2 &&
                      fb2.find("[Q3 Report](attachments/q3-report-2.pdf)") != std::string::npos,
                  fb2);
            const std::string out = (root / "out.pdf").string();
            check("file enclosure: Save a Copy is byte-exact for any file",
                  core::copy_out(fst, fn, out, err) && fs::file_size(out, ec) == 19, err);
        }

        // the derived list
        const std::string body =
            "# Trip\n![Holiday Snap](attachments/holiday-snap.png)\n"
            "again ![again](attachments/holiday-snap.png) and [a link](attachments/gone.png)\n"
            "![web](https://example.com/x.png) `![code](attachments/code.png)`\n";
        const auto encl = core::enclosures(body, st);
        check("enclosure: one entry per FILE, first-mention order",
              encl.size() == 2 && encl[0].name == "holiday-snap.png" && encl[1].name == "gone.png",
              std::to_string(encl.size()));
        check("enclosure: repeated references are counted, not listed",
              !encl.empty() && encl[0].refs == 2 && encl[0].line == 1);
        check("enclosure: present and joined with its metadata",
              !encl.empty() && encl[0].present && encl[0].has_meta && encl[0].meta.size == 16);
        check("enclosure: a reference with no file is listed and says so",
              encl.size() == 2 && !encl[1].present && !encl[1].has_meta);
        check("enclosure: web images and code spans are not enclosures",
              std::none_of(encl.begin(), encl.end(), [](const core::Enclosure& e) {
                  return e.name == "code.png";
              }));

        std::string b2 = body;
        check("enclosure: rename rewrites every reference to that file and only it",
              core::rename_references(b2, "holiday-snap.png", "holiday-snap-9.png") == 2 &&
                  b2.find("attachments/holiday-snap.png") == std::string::npos &&
                  b2.find("attachments/gone.png") != std::string::npos &&
                  b2.find("`![code](attachments/code.png)`") != std::string::npos);

        // jot.json pump
        const auto back = core::decode_metas(core::encode_metas(st.metas));
        check("enclosure: metadata round-trips through its JSON",
              back.size() == st.metas.size() && back.at(n1).source == srcf.string() &&
                  back.at(n3).source == "clipboard" && back.at(n1).added == 1000);
        check("enclosure: decode refuses a key that escapes the folder",
              core::decode_metas(R"({"../x.png":{"size":1}})").empty());

        // adopt: the scratch buffer's images go home, collisions renamed in
        // the bodies before they are written.
        {
            core::MemoryNodes mem;
            const auto a = mem.create("", "Trip");
            mem.set_body(a, "![Holiday Snap](attachments/holiday-snap.png)\n"
                            "![p](attachments/pasted-20260101-000000.png)\n"
                            "![lost](attachments/never-was.png)\n");
            const fs::path dest = root / "Dest.jots";
            fs::create_directories(dest / "attachments", ec);
            std::ofstream(dest / "attachments" / "holiday-snap.png") << "someone else's";

            core::Project p;
            check("enclosure/adopt: opens", p.open(dest.string()));
            check("enclosure/adopt: adopts the note", p.adopt(mem, &st, /*move=*/true) == 1);
            const auto roots = p.children("");
            const core::Node* nd = roots.empty() ? nullptr : p.find(roots.front());
            check("enclosure/adopt: a colliding name is renamed IN THE BODY",
                  nd && nd->body.find("attachments/holiday-snap-2.png") != std::string::npos &&
                      nd->body.find("attachments/holiday-snap.png)") == std::string::npos,
                  nd ? nd->body : "");
            check("enclosure/adopt: the file arrived under that name",
                  fs::file_size(dest / "attachments" / "holiday-snap-2.png", ec) == 16);
            check("enclosure/adopt: what was already there is untouched",
                  fs::file_size(dest / "attachments" / "holiday-snap.png", ec) == 14);
            check("enclosure/adopt: a move leaves the scratch copy gone",
                  !fs::exists(fs::path(st.dir) / "holiday-snap.png", ec) &&
                      !fs::exists(fs::path(st.dir) / n3, ec));
            check("enclosure/adopt: an UNREFERENCED scratch file stays where it was",
                  fs::exists(fs::path(st.dir) / n2, ec));
            check("enclosure/adopt: a missing file stays referenced (Missing, not dropped)",
                  nd && nd->body.find("attachments/never-was.png") != std::string::npos);
            check("enclosure/adopt: metadata carried under the NEW name",
                  p.attach().metas.count("holiday-snap-2.png") &&
                      p.attach().metas.at("holiday-snap-2.png").source == srcf.string() &&
                      p.attach().metas.count(n3));
            p.flush();
            const std::string disk = [&] {
                std::ifstream in(dest / "jot.json");
                return std::string(std::istreambuf_iterator<char>(in), {});
            }();
            check("enclosure/adopt: jot.json carries the enclosures object (v3)",
                  disk.find("\"enclosures\"") != std::string::npos &&
                      disk.find("\"version\": 3") != std::string::npos);
            core::Project q;
            check("enclosure/adopt: metadata survives a reopen",
                  q.open(dest.string()) && q.attach().metas.count("holiday-snap-2.png") &&
                      q.attach().metas.at("holiday-snap-2.png").size == 16);
        }
        {
            // Save As from a jots folder COPIES: the old folder keeps its images.
            core::Project src;
            src.open((root / "Dest.jots").string());
            core::Project cp;
            cp.open((root / "Copy.jots").string());
            check("enclosure/adopt: save-as adopts", cp.adopt(src, &src.attach(), false) == 1);
            check("enclosure/adopt: save-as COPIES -- both folders have the image",
                  fs::exists(root / "Copy.jots" / "attachments" / "holiday-snap-2.png", ec) &&
                      fs::exists(root / "Dest.jots" / "attachments" / "holiday-snap-2.png", ec));
        }
        {
            // A folder with no enclosures writes no enclosures key.
            core::Project p;
            p.open((root / "Plain.jots").string());
            p.create("", "x");
            p.flush();
            std::ifstream in(root / "Plain.jots" / "jot.json");
            const std::string disk((std::istreambuf_iterator<char>(in)), {});
            check("enclosure: no enclosures, no key -- a plain folder's jot.json is unchanged",
                  disk.find("enclosures") == std::string::npos);
        }
        {
            // s017: out. Save a copy, and the path guard.
            core::AttachStore o{(root / "out_src").string(), {}};
            fs::create_directories(o.dir, ec);
            std::ofstream(fs::path(o.dir) / "a.png", std::ios::binary) << "AAAA";
            std::string e2;
            check("enclosure/out: a path for a real name",
                  core::enclosure_path(o, "a.png") == (fs::path(o.dir) / "a.png").string());
            check("enclosure/out: no path for a name that escapes",
                  core::enclosure_path(o, "../x.png").empty() &&
                      core::enclosure_path(o, "a/b.png").empty() &&
                      core::enclosure_path(core::AttachStore{}, "a.png").empty());
            const fs::path dest = root / "exported.png";
            check("enclosure/out: copy_out writes the bytes",
                  core::copy_out(o, "a.png", dest.string(), e2) &&
                      fs::file_size(dest, ec) == 4, e2);
            std::ofstream(dest, std::ios::binary | std::ios::trunc) << "old-and-longer";
            check("enclosure/out: copy_out overwrites (the dialog asked)",
                  core::copy_out(o, "a.png", dest.string(), e2) && fs::file_size(dest, ec) == 4);
            check("enclosure/out: no temp file left behind",
                  !fs::exists(dest.string() + ".jot-tmp", ec));
            check("enclosure/out: copying onto itself is refused and harmless",
                  !core::copy_out(o, "a.png", (fs::path(o.dir) / "a.png").string(), e2) &&
                      fs::file_size(fs::path(o.dir) / "a.png", ec) == 4);
            check("enclosure/out: a missing enclosure says so",
                  !core::copy_out(o, "gone.png", dest.string(), e2) && !e2.empty());
        }
        {
            // s019: LINKED enclosures -- a file jot points at where it lives.
            check("linked/uri: a space and parens are encoded",
                  core::file_uri("/home/s/My Docs/q3 (final).pdf") ==
                      "file:///home/s/My%20Docs/q3%20%28final%29.pdf");
            check("linked/uri: a relative path has no uri", core::file_uri("a/b.pdf").empty());
            check("linked/path: decodes back",
                  core::linked_path("file:///home/s/My%20Docs/q3%20%28final%29.pdf") ==
                      "/home/s/My Docs/q3 (final).pdf");
            check("linked/path: localhost form accepted",
                  core::linked_path("file://localhost/tmp/x.txt") == "/tmp/x.txt");
            check("linked/path: http, attachments, a host, a NUL, a bad escape are refused",
                  core::linked_path("https://x/y.pdf").empty() &&
                      core::linked_path("attachments/a.png").empty() &&
                      core::linked_path("file://server/x").empty() &&
                      core::linked_path("file:///a%00b").empty() &&
                      core::linked_path("file:///a%2").empty() &&
                      core::linked_path("file:///a%zzb").empty());
            check("linked/key: two spellings of one path are one key",
                  core::linked_key("file:///tmp/a b.pdf") == core::linked_key("file:///tmp/a%20b.pdf") &&
                      core::is_linked_key(core::linked_key("file:///tmp/a b.pdf")) &&
                      !core::is_linked_key("file:///tmp/a b.pdf") && !core::is_linked_key("a.png"));
            check("linked/markdown: a plain link for a file, a bang for an image",
                  core::enclosure_markdown("Q3", "file:///d/q3.pdf") == "[Q3](file:///d/q3.pdf)" &&
                      core::enclosure_markdown("Pic", "file:///d/p.PNG") == "![Pic](file:///d/p.PNG)");

            const fs::path elsewhere = root / "elsewhere dir";
            fs::create_directories(elsewhere, ec);
            const fs::path pdf = elsewhere / "q3 report.pdf";
            std::ofstream(pdf, std::ios::binary) << "%PDF-linked";
            core::AttachStore st{(root / "lk.jots" / "attachments").string(), {}};
            std::string e2;
            check("linked/link: a folder is refused",
                  core::link_file(st, elsewhere.string(), 1, e2).empty() && !e2.empty());
            const std::string key = core::link_file(st, pdf.string(), 100, e2);
            check("linked/link: returns the uri and records size, mtime, mode",
                  key == core::file_uri(pdf.string()) && st.metas.count(key) &&
                      st.metas[key].mode == "linked" && st.metas[key].size == 11 &&
                      st.metas[key].mtime > 0, e2);
            check("linked/link: nothing is copied, the store dir is not even made",
                  !fs::exists(st.dir, ec));

            std::string body = "see " + core::enclosure_markdown("Q3 report", key) +
                               "\nand again [again](" + key + ")\nweb [w](https://x.org/a.pdf)\n";
            auto list = core::enclosures(body, st);
            check("linked/list: one row, two refs, linked, OK",
                  list.size() == 1 && list[0].linked && list[0].refs == 2 &&
                      list[0].status == core::EnclosureStatus::Ok &&
                      list[0].path == pdf.string() && list[0].display() == "q3 report.pdf" &&
                      list[0].has_meta);
            check("linked/path: enclosure_path resolves a linked key",
                  core::enclosure_path(st, key) == pdf.string());

            // Modified: size changes.
            std::ofstream(pdf, std::ios::binary | std::ios::trunc) << "%PDF-linked-v2";
            list = core::enclosures(body, st);
            check("linked/status: a changed file reads Modified",
                  list.size() == 1 && list[0].status == core::EnclosureStatus::Modified);
            check("linked/accept: accept_change restamps", core::accept_change(st, key, e2), e2);
            list = core::enclosures(body, st);
            check("linked/accept: and it reads OK again",
                  list.size() == 1 && list[0].status == core::EnclosureStatus::Ok);

            // Typed by hand, no record: OK, not Modified.
            core::AttachStore bare{st.dir, {}};
            list = core::enclosures("[t](" + key + ")", bare);
            check("linked/status: a typed link with no record reads OK",
                  list.size() == 1 && list[0].status == core::EnclosureStatus::Ok && !list[0].has_meta);

            // Missing, then Relink.
            const fs::path moved = elsewhere / "Q3 moved.pdf";
            fs::rename(pdf, moved, ec);
            list = core::enclosures(body, st);
            check("linked/status: a moved file reads Missing",
                  list.size() == 1 && list[0].status == core::EnclosureStatus::Missing && !list[0].present);
            const std::int64_t first_added = st.metas[key].added;
            const std::string nk = core::relink(st, key, moved.string(), 999, e2);
            check("linked/relink: a new key, added kept, old meta left alone",
                  !nk.empty() && nk != key && st.metas.count(nk) &&
                      st.metas[nk].added == first_added && st.metas.count(key), e2);
            const int n = core::retarget_references(body, key, nk);
            check("linked/relink: both references rewritten, web link untouched",
                  n == 2 && body.find(key) == std::string::npos &&
                      body.find("[Q3 report](" + nk + ")") != std::string::npos &&
                      body.find("[again](" + nk + ")") != std::string::npos &&
                      body.find("https://x.org/a.pdf") != std::string::npos, body);
            list = core::enclosures(body, st);
            check("linked/relink: the row reads OK at the new place",
                  list.size() == 1 && list[0].status == core::EnclosureStatus::Ok &&
                      list[0].path == moved.string());
            check("linked/relink: a folder is refused",
                  core::relink(st, nk, elsewhere.string(), 1, e2).empty());

            // Mixed with embedded; retarget works on an attachment name too.
            std::string mixed = "![a](attachments/a.png) [b](" + nk + ")";
            check("linked/list: embedded and linked together, in order",
                  core::enclosures(mixed, st).size() == 2 &&
                      !core::enclosures(mixed, st)[0].linked && core::enclosures(mixed, st)[1].linked);
            check("linked/retarget: an embedded reference can be pointed at a linked key",
                  core::retarget_references(mixed, "a.png", nk) == 1 &&
                      mixed.find("![a](" + nk + ")") != std::string::npos, mixed);
            check("linked/refs: linked_references lists keys once",
                  core::linked_references(mixed).size() == 1 &&
                      core::linked_references(mixed)[0] == nk);

            // jot.json round trip carries mode and mtime; a uri key survives decode.
            const auto back = core::decode_metas(core::encode_metas(st.metas));
            check("linked/json: a uri key and its mtime survive the round trip",
                  back.count(nk) && back.at(nk).mode == "linked" &&
                      back.at(nk).mtime == st.metas[nk].mtime);
            check("linked/json: a non-canonical uri key is dropped on decode",
                  core::decode_metas("{\"file:///a b\":{\"mode\":\"linked\"}}").empty());

            // Adopt: the scratch store's linked meta follows the note.
            core::Project scratch_src;
            scratch_src.open((root / "lk_src.jots").string());
            const core::NodeId lid = scratch_src.create("", "linked note");
            scratch_src.set_body(lid, "[b](" + nk + ")");
            scratch_src.record_enclosure(nk, st.metas[nk]);
            scratch_src.flush();
            core::Project lk_dst;
            lk_dst.open((root / "lk_dst.jots").string());
            const int adopted_n = lk_dst.adopt(scratch_src, &scratch_src.attach(), true);
            check("linked/adopt: the metadata comes along, the file does not move",
                  adopted_n >= 1 && lk_dst.attach().metas.count(nk) && fs::exists(moved, ec),
                  "adopted " + std::to_string(adopted_n) + ", meta " +
                      std::to_string(lk_dst.attach().metas.count(nk)));

            // s020: CONVERT -- Embed a Copy and Link Instead.
            std::string cb = "x [Q3](" + nk + ") y\n![a](attachments/a.png)\n";
            const std::string en = core::embed_copy(st, nk, 2000, e2);
            check("convert/embed: a linked file is copied in under a slug, source recorded",
                  en == "q3-moved.pdf" && fs::exists(fs::path(st.dir) / en, ec) &&
                      st.metas.count(en) && st.metas[en].mode == "embedded" &&
                      st.metas[en].source == moved.string() && st.metas[en].size == 14, e2);
            check("convert/embed: the linked file is left where it is, its meta too",
                  fs::exists(moved, ec) && st.metas.count(nk));
            check("convert/embed: retarget makes the note carry the copy",
                  core::retarget_references(cb, nk, en) == 1 &&
                      cb.find("[Q3](attachments/q3-moved.pdf)") != std::string::npos, cb);
            {
                auto cl = core::enclosures(cb, st);
                check("convert/embed: the row is embedded and OK",
                      cl.size() == 2 && !cl[0].linked && cl[0].present && cl[0].name == en);
            }
            check("convert/embed: an attachment name is refused",
                  core::embed_copy(st, "a.png", 1, e2).empty() && !e2.empty());
            check("convert/embed: a missing linked file is refused",
                  core::embed_copy(st, key, 1, e2).empty() && !e2.empty());

            check("convert/original: an embed from Files knows its original",
                  core::original_of(st, en) == moved.string());
            st.metas["pasted.png"].source = "clipboard";
            check("convert/original: a paste, a linked key, an unknown name have none",
                  core::original_of(st, "pasted.png").empty() && core::original_of(st, nk).empty() &&
                      core::original_of(st, "nobody.png").empty());

            const std::string back_k = core::link_instead(st, en, moved.string(), 3000, e2);
            check("convert/link: the embed's original becomes the key",
                  back_k == nk && st.metas[nk].mode == "linked" && st.metas[nk].size == 14, e2);
            check("convert/link: the embedded copy stays in attachments/",
                  fs::exists(fs::path(st.dir) / en, ec) && st.metas.count(en));
            check("convert/link: retarget makes the note point at the original again",
                  core::retarget_references(cb, en, back_k) == 1 &&
                      cb.find("[Q3](" + nk + ")") != std::string::npos &&
                      cb.find("![a](attachments/a.png)") != std::string::npos, cb);
            check("convert/link: a linked key is refused, a folder is refused",
                  core::link_instead(st, nk, moved.string(), 1, e2).empty() &&
                      core::link_instead(st, en, elsewhere.string(), 1, e2).empty());
            {
                // An image keeps its bang both ways.
                std::string ib = "![Pic](attachments/p.png)";
                core::retarget_references(ib, "p.png", "file:///d/p.png");
                check("convert/image: the bang survives a round trip",
                      ib == "![Pic](file:///d/p.png)" &&
                          core::retarget_references(ib, "file:///d/p.png", "p.png") == 1 &&
                          ib == "![Pic](attachments/p.png)", ib);
            }
        }
        fs::remove_all(root, ec);
    }

    // ── s046b: undo coverage -- gestures, merging, the one door, the gate ──
    std::cout << "\n-- undo coverage (s046b) --\n";
    {
        namespace fs = std::filesystem;
        // A whole picture of a store: every field undo is responsible for, in
        // tree order, so "undo put it back" is one string comparison.
        const auto state = [](const core::NodeSource& s) {
            std::string out;
            std::function<void(const core::NodeId&, int)> walk = [&](const core::NodeId& p, int d) {
                for (const auto& id : s.children(p)) {
                    const core::Node* n = s.find(id);
                    if (!n) continue;
                    const core::Task& t = n->task;
                    out += std::string(static_cast<std::size_t>(d) * 2, ' ') + id + " [" + n->title + "] {" +
                           n->body + "} p" + std::to_string(n->protect) + " i" + std::to_string(n->inbox) +
                           " k" + std::to_string(n->packet) + " s" + std::to_string(n->sent) + n->sent_to + " n" + std::to_string(n->nudge) +
                           " t" + std::to_string(t.is_task) +
                           std::to_string(t.done) + std::to_string(t.flagged) + " due" +
                           std::to_string(t.due) + " def" + std::to_string(t.defer) + " st" +
                           std::to_string(static_cast<int>(t.status)) + " ps" +
                           std::to_string(static_cast<int>(t.project)) + " m" +
                           std::to_string(static_cast<int>(t.mark)) + " rep" +
                           std::to_string(t.repeat.every) + " rev" + std::to_string(t.review.every) +
                           "/" + std::to_string(t.reviewed) + " est" + std::to_string(t.estimate) + "\n";
                    walk(id, d + 1);
                }
            };
            walk("", 0);
            return out;
        };
        // A fresh fixture behind a door: Taxes{W-2, Form(todo, due)}, Home, Inbox note.
        struct Rig {
            core::MemoryNodes m;
            core::Journal j;
            core::UndoSource u{j, [this] { return &m; }};
            core::NodeId taxes, w2, form, home, loose;
            Rig() {
                taxes = m.create("", "Taxes");
                w2 = m.create(taxes, "W-2");
                form = m.create(taxes, "Form");
                m.set_body(w2, "the employer copy");
                m.make_task(form, true);
                m.set_due(form, 1'800'000'000);
                home = m.create("", "Home");
                loose = m.create("", "loose thought");
                m.set_inbox(loose, true);
            }
        };

        // The table: every write a surface can make through the door. Each
        // must be ONE step, undo must give back the exact picture, redo the
        // exact after. A write added to NodeSource must be overridden by
        // UndoSource (pure virtual) -- and then belongs in this table.
        struct Verb {
            const char* name;
            std::function<void(Rig&)> run;
            const char* label;   // what the Undo menu would say ("" = don't check)
        };
        const std::vector<Verb> verbs = {
            {"create", [](Rig& r) { r.u.create(r.taxes, "new"); }, "New note"},
            {"set_title", [](Rig& r) { r.u.set_title(r.w2, "W-2 (2026)"); }, "Rename"},
            {"set_body", [](Rig& r) { r.u.set_body(r.home, "keys"); }, "Edit text"},
            {"set_protect", [](Rig& r) { r.u.set_protect(r.home, true); }, "Protect"},
            {"set_inbox", [](Rig& r) { r.u.set_inbox(r.loose, false); }, "Out of the Inbox"},
            {"set_packet", [](Rig& r) { r.u.set_packet(r.taxes, true); }, "Packet"},
            {"set_nudge", [](Rig& r) { r.u.set_nudge(r.taxes, 7); }, "Nudge"},
            {"set_sent", [](Rig& r) { r.u.set_sent(r.taxes, 1'800'000'000, "/tmp/Taxes.zip"); }, "Sent"},
            {"move", [](Rig& r) { r.u.move(r.home, r.taxes, 0); }, "Move"},
            {"remove", [](Rig& r) { r.u.remove(r.taxes); }, "Delete “Taxes”"},
            {"restore", [](Rig& r) { core::Node n; n.id = "back"; n.title = "Back"; r.u.restore(n, 0); }, "Restore"},
            {"set_done", [](Rig& r) { r.u.set_done(r.form, true); }, "Tick"},
            {"set_flagged", [](Rig& r) { r.u.set_flagged(r.form, true); }, "Flag"},
            {"set_due", [](Rig& r) { r.u.set_due(r.form, 1'900'000'000); }, "Due date"},
            {"set_defer", [](Rig& r) { r.u.set_defer(r.form, 1'700'000'000); }, "Defer date"},
            {"set_estimate", [](Rig& r) { r.u.set_estimate(r.form, 30); }, "Estimate"},
            {"set_repeat", [](Rig& r) { core::Repeat p; p.every = 1; r.u.set_repeat(r.form, p); }, "Repeat"},
            {"set_status", [](Rig& r) { r.u.set_status(r.taxes, core::Status::Sequential); }, "Children"},
            {"make_task", [](Rig& r) { r.u.make_task(r.home, true); }, "Make a todo"},
            {"un-make_task", [](Rig& r) { r.u.make_task(r.form, false); }, "Not a todo"},
            {"set_project_state", [](Rig& r) { core::set_project_state(r.u, r.taxes, core::ProjectState::OnHold); }, "Project status"},
            {"set_project_mark", [](Rig& r) { core::set_project_mark(r.u, r.home, core::ProjectMark::On); }, "Is a project"},
            {"mark_reviewed", [](Rig& r) { core::mark_reviewed(r.u, r.taxes, 1'800'000'000); }, "Mark reviewed"},
            {"set_review_every", [](Rig& r) { core::Repeat w; w.every = 2; w.unit = core::RepeatUnit::Week;
                                              core::set_review_every(r.u, r.taxes, w); }, "Review interval"},
            {"capture", [](Rig& r) { core::capture(r.u, "call the dentist\nabout Tuesday"); }, "Capture"},
            {"capture_list (new)", [](Rig& r) { core::capture_list(r.u, "Groceries", {"milk", "eggs"}); }, "List"},
            {"capture_list (grow)", [](Rig& r) { core::capture_list(r.u, "Home", {"bulbs"}); }, "List"},
            {"capture_append", [](Rig& r) { core::capture_append(r.u, "Home", "check the pantry"); }, "Append"},
            {"clean_up", [](Rig& r) { core::clean_up(r.u); }, "Clean Up"},
        };
        for (const auto& v : verbs) {
            Rig r;
            // Some verbs need a raw setup first (a project said on purpose, a
            // filed capture); that is part of "before", not of the step.
            const std::string name = v.name;
            if (name == "mark_reviewed" || name == "set_review_every")
                core::set_project_mark(r.m, r.taxes, core::ProjectMark::On);
            if (name == "clean_up") r.m.move(r.loose, r.home, -1);
            const std::size_t had = r.j.size();
            const std::string pre = state(r.m);
            v.run(r);
            const std::string after = state(r.m);
            const std::string label = r.j.undo_label();
            check("door: " + name + " is one step", r.j.size() == had + 1,
                  std::to_string(r.j.size() - had) + " step(s)");
            check("door: " + name + " changed something", after != pre);
            r.j.undo(r.m);
            const std::string undone = state(r.m);
            r.j.redo(r.m);
            const std::string redone = state(r.m);
            check("door: " + name + " -- undo gives back the exact picture", undone == pre,
                  "\nwant:\n" + pre + "got:\n" + undone);
            check("door: " + name + " -- redo gives the exact after", redone == after,
                  "\nwant:\n" + after + "got:\n" + redone);
            if (*v.label)
                check("door: " + name + " is called \u201c" + v.label + "\u201d", label == v.label, label);
        }

        // Reads pass straight through; a no-op write is not a step.
        {
            Rig r;
            check("door: reads are the store's", r.u.count() == r.m.count() &&
                                                 r.u.find(r.w2) == r.m.find(r.w2) &&
                                                 r.u.children(r.taxes) == r.m.children(r.taxes));
            r.u.set_title(r.w2, "W-2");
            r.u.set_due(r.form, 1'800'000'000);
            const std::size_t n = r.j.size();
            r.u.move(r.taxes, r.w2, -1);     // into its own child: refused
            r.u.set_inbox(r.home, false);    // already off
            check("door: a refused or no-op write is not a step", r.j.size() == n);
        }

        // A gesture: several writes, one Ctrl+Z; nested ones fold; the outer
        // label wins; an empty gesture keeps nothing.
        {
            Rig r;
            const std::string pre = state(r.m);
            {
                core::Gesture g(r.u, "New project");
                const auto p = r.u.create("", "Kitchen");
                core::set_project_mark(r.u, p, core::ProjectMark::On);
                core::as_step(r.u, "inner", {p}, false, [&] { r.u.set_body(p, "tiles"); });
            }
            check("gesture: three writes and a nested step are ONE step", r.j.size() == 1);
            check("gesture: the outer label names it", r.j.undo_label() == "New project", r.j.undo_label());
            r.j.undo(r.m);
            check("gesture: one undo takes all of it back", state(r.m) == pre, state(r.m));
            r.j.redo(r.m);
            check("gesture: one redo brings all of it", r.m.count() == 6);
            { core::Gesture g(r.u, "nothing"); }
            check("gesture: an empty gesture is not a step", r.j.size() == 1 && r.j.undo_label() == "New project");
            bool ran = false;
            core::MemoryNodes plain;
            check("as_step: on a store with no door it just runs",
                  !core::as_step(plain, "x", {}, false, [&] { ran = true; }) && ran);
        }

        // Merging: typing a name is one step per field, not per keystroke.
        {
            Rig r;
            double clock = 100.0;
            r.j.set_clock([&] { return clock; });
            r.u.set_title(r.home, "H");
            clock += 0.3; r.u.set_title(r.home, "Ho");
            clock += 0.3; r.u.set_title(r.home, "Hous");
            clock += 0.3; r.u.set_title(r.home, "House");
            check("merge: four keystrokes inside the window are one step", r.j.size() == 1);
            r.j.undo(r.m);
            check("merge: undo goes back to the name before the typing", r.m.find(r.home)->title == "Home");
            r.j.redo(r.m);
            check("merge: redo gives the last keystroke", r.m.find(r.home)->title == "House");
            clock += 5.0; r.u.set_title(r.home, "Houses");
            check("merge: a pause starts a new step", r.j.size() == 2);
            clock += 0.2; r.u.set_title(r.w2, "W");
            check("merge: another field (another note) is its own step", r.j.size() == 3);
            r.j.undo(r.m);
            clock += 0.1; r.u.set_title(r.w2, "W2");
            check("merge: after an undo nothing folds into the undone step", r.j.size() == 3 &&
                                                                               r.j.undo_label() == "Rename");
            clock += 0.1; r.u.set_due(r.form, 1);
            clock += 0.1; r.u.set_due(r.form, 2);
            check("merge: only steps that ask to merge fold (dates do not)", r.j.size() == 5);
        }

        // The faults the s045 audit asked about.
        {
            Rig r;
            r.u.set_title(r.w2, "W-2 form");
            r.m.set_body(r.w2, "typed after the rename");   // the editor writes raw
            r.j.undo(r.m);
            check("fault: undo a rename keeps text typed since",
                  r.m.find(r.w2)->title == "W-2" && r.m.find(r.w2)->body == "typed after the rename");
            const auto made = r.u.create(r.home, "");
            r.u.set_title(made, "Named");
            r.j.undo(r.m);
            r.j.undo(r.m);
            check("fault: create then rename, both undone -> gone", !r.m.find(made));
            r.j.redo(r.m);
            r.j.redo(r.m);
            check("fault: ...both redone -> back under its id, named",
                  r.m.find(made) && r.m.find(made)->title == "Named" &&
                      r.m.find(made)->parent_id == r.home);
            // A tick on a card is the same tick as the tree's.
            r.u.set_done(r.form, true);
            check("fault: a card's tick (set_done through the door) is a step called Tick",
                  r.j.undo_label() == "Tick" && r.m.find(r.form)->task.done);
            r.j.undo(r.m);
            check("fault: ...and it unticks", !r.m.find(r.form)->task.done &&
                                              r.m.find(r.form)->task.finished == 0);
            // The door asks for the store at every call: a folder swap needs
            // no re-pointing, and no step crosses it.
            core::MemoryNodes other;
            core::NodeSource* cur = &r.m;
            core::Journal j2;
            core::UndoSource u2(j2, [&] { return cur; });
            u2.set_title(r.home, "A");
            cur = &other;
            const auto o = u2.create("", "elsewhere");
            check("door: a store swap forgets the old store's steps",
                  j2.size() == 1 && other.find(o) && j2.undo_label() == "New note");
            cur = nullptr;
            check("door: no store -> writes refused, reads empty",
                  u2.create("", "x").empty() && !u2.set_title(o, "y") && u2.count() == 0 &&
                      u2.history().empty());
        }

        // task_label names the first field that moved.
        {
            core::Task a, b;
            b = a; b.is_task = true;
            check("label: is_task first", core::task_label(a, b) == "Make a todo");
            a.is_task = true; b = a; b.done = true; b.due = 5;
            check("label: done before due", core::task_label(a, b) == "Tick");
            b = a; b.finished = 9;
            check("label: an unnamed field is Edit todo", core::task_label(a, b) == "Edit todo");
        }

        // ── the gate's other half: no write goes around the door ────────────
        {
            const auto f = [](const std::string& t) { return core::raw_writes("x.cpp", t).size(); };
            check("gate: a write on the real store is found", f("    m_store->set_due(id, 5);\n") == 1);
            check("gate: the real store handed to a core writer is found",
                  f("core::clean_up(*m_store);\n") == 1 && f("core::set_project_state( * m_store, id, s);") == 1);
            check("gate: the real store handed to a pane is found", f("m_today->set_source(m_store.get());") == 1);
            check("gate: a write through the door is not", f("m_undo.set_due(id, 5);\ncore::clean_up(m_undo);\n") == 0);
            check("gate: a read on the real store is not", f("const auto* n = m_store->find(id);\nm_store->children(\"\");") == 0);
            check("gate: a raw write with a reason is allowed",
                  f("m_store->set_body(id, b);   // raw: files moved on disk\n") == 0 &&
                      f("// raw: the editor's body\nm_editor->set_source(m_store.get());\n") == 0);
            check("gate: the finding names file and line",
                  core::raw_writes("Shell.cpp", "a\nm_store->remove(id);\n") ==
                      std::vector<std::string>{"Shell.cpp:2: m_store->remove(id);"});
#ifdef JOT_SOURCE_DIR
            std::error_code ec;
            std::vector<std::string> found;
            int scanned = 0;
            for (const auto& e : fs::directory_iterator(fs::path(JOT_SOURCE_DIR) / "src", ec)) {
                if (e.path().extension() != ".cpp" || e.path().filename() == "selftest.cpp") continue;
                std::ifstream in(e.path());
                const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                ++scanned;
                for (auto& s : core::raw_writes(e.path().filename().string(), text)) found.push_back(s);
            }
            std::string list;
            for (const auto& s : found) list += "\n    " + s;
            check("gate: the scan read the UI sources", scanned >= 20, std::to_string(scanned) + " file(s)");
            check("gate: every model write in the app goes through the door (UndoSource)", found.empty(), list);
#else
            check("gate: JOT_SOURCE_DIR is defined (the scan has sources to read)", false);
#endif
        }
    }

    // ── s047: acting on several notes ──────────────────────────────────────
    std::cout << "\n-- selection (s047) --\n";
    {
        core::MemoryNodes m;
        const auto a  = m.create("", "A");
        const auto a1 = m.create(a, "A1");
        const auto a2 = m.create(a, "A2");
        const auto b  = m.create("", "B");
        const auto c  = m.create("", "C");
        const auto c1 = m.create(c, "C1");
        using V = std::vector<core::NodeId>;
        check("order: document order, whatever order they were picked in",
              core::in_document_order(m, {c1, b, a2, a}) == V{a, a2, b, c1});
        check("order: unknown ids dropped, duplicates once",
              core::in_document_order(m, {b, "nope", b}) == V{b});
        check("roots: a note under a selected one goes with it",
              core::selection_roots(m, {a2, a, c1}) == V{a, c1});
        check("roots: siblings are all roots", core::selection_roots(m, {a1, a2}) == V{a1, a2});
        check("roots: nothing selected, nothing", core::selection_roots(m, {}).empty());
        check("neighbour: after the last root", core::neighbour_after_delete_all(m, {a, b}) == c);
        check("neighbour: skips a selected sibling", core::neighbour_after_delete_all(m, {b, c, a1}) == a);
        check("neighbour: before the first when nothing after", core::neighbour_after_delete_all(m, {c}) == b);
        check("neighbour: the parent when every sibling goes", core::neighbour_after_delete_all(m, {a1, a2}) == a);
        check("neighbour: everything going leaves nothing", core::neighbour_after_delete_all(m, {a, b, c}).empty());
        m.make_task(a1, true); m.make_task(a2, true); m.set_done(a2, true);
        const auto done = [](const core::Node& n) { return n.task.done; };
        check("toggle: one not done -> all go on", core::turn_on(m, {a1, a2}, done));
        m.set_done(a1, true);
        check("toggle: every one on -> all go off", !core::turn_on(m, {a1, a2}, done));

        // Through the door: a multi-delete is ONE step and comes back whole.
        core::Journal j;
        core::UndoSource u(j, [&] { return static_cast<core::NodeSource*>(&m); });
        const std::size_t had = m.count();
        const auto roots = core::selection_roots(m, {a, a1, c});
        core::as_step(u, "Delete 2 notes", roots, true, [&] { for (const auto& id : roots) u.remove(id); });
        check("multi: delete two subtrees is one step", j.size() == 1 && m.count() == had - 5);
        j.undo(m);
        check("multi: one undo brings both back, in order",
              m.count() == had && m.children("") == V{a, b, c} && m.children(a) == V{a1, a2} &&
                  m.find(a2)->task.done);
        core::as_step(u, "Flag 2 notes", {a1, a2}, false, [&] { u.set_flagged(a1, true); u.set_flagged(a2, true); });
        check("multi: a toggle over two is one step (the undone delete is dropped from redo)",
              j.size() == 1 && j.undo_label() == "Flag 2 notes");
        j.undo(m);
        check("multi: ...one undo unflags both", !m.find(a1)->task.flagged && !m.find(a2)->task.flagged);
    }

    // ── s048: the tree's cards ─────────────────────────────────────────────
    std::cout << "\n-- tree cards (s048) --\n";
    {
        check("compact due: the time goes on a later day", core::compact_due("Tomorrow 19:02") == "Tomorrow" &&
                                                            core::compact_due("Tue 9:00") == "Tue");
        check("compact due: kept where it is the news", core::compact_due("Today 17:00") == "Today 17:00" &&
                                                        core::compact_due("Overdue 17:00") == "Overdue 17:00");
        check("compact due: no time, nothing to drop", core::compact_due("Oct 12 2027") == "Oct 12 2027" &&
                                                        core::compact_due("2d late") == "2d late" &&
                                                        core::compact_due("Tomorrow") == "Tomorrow");
        core::MemoryNodes m;
        const auto p = m.create("", "P");
        const auto t1 = m.create(p, "a"), t2 = m.create(p, "b");
        m.create(p, "a note");
        const auto deep = m.create(t1, "deep");
        m.make_task(t1, true); m.make_task(t2, true); m.make_task(deep, true);
        m.set_done(t2, true);
        const auto sc = core::step_count(m, p);
        check("steps: direct todo children only, done counted", sc.total == 2 && sc.done == 1);
        check("steps: none -> total 0", core::step_count(m, deep).total == 0);
    }

    // ── s049: what several notes share, and a set on all of them ───────────
    std::cout << "\n-- common metadata (s049) --\n";
    {
        core::MemoryNodes m;
        const auto t1 = m.create("", "one"), t2 = m.create("", "two"), t3 = m.create("", "three");
        const auto note = m.create("", "plain");
        for (const auto& t : {t1, t2, t3}) m.make_task(t, true);
        m.set_due(t1, 1800000000); m.set_due(t2, 1800000000); m.set_due(t3, 1800000000);
        m.set_defer(t1, 1700000000);
        m.set_estimate(t1, 30); m.set_estimate(t2, 30); m.set_estimate(t3, 30);
        m.set_flagged(t2, true);
        m.set_body(t1, "a\n\n#car #home");
        m.set_body(t2, "b\n\n#car");
        m.set_body(t3, "c #car in text");
        using V = std::vector<core::NodeId>;
        auto c = core::common(m, {t1, t2, t3});
        check("common: three todos counted", c.count == 3 && c.todos == 3 && !c.is_task.mixed && c.is_task.value);
        check("common: the same due shows as itself", !c.due.mixed && c.due.value == 1800000000);
        check("common: one defer and two none -> mixed", c.defer.mixed);
        check("common: one flagged -> mixed", c.flagged.mixed);
        check("common: same estimate shows", !c.estimate.mixed && c.estimate.value == 30);
        check("common: nobody repeats -> shared 'never'", !c.repeat.mixed && !c.repeat.value.on());
        check("common: a tag all three carry (line or text) is shared",
              c.tags_all == std::vector<std::string>{"car"});
        check("common: a tag one carries is 'some', with its count",
              c.tags_some.size() == 1 && c.tags_some[0].first == "home" && c.tags_some[0].second == 1);
        auto c2 = core::common(m, {t1, note, "gone"});
        check("common: a plain note makes 'is a todo' mixed; unknown ids dropped",
              c2.count == 2 && c2.todos == 1 && c2.is_task.mixed);
        check("common: todo fields read over the todos only", !c2.due.mixed && !c2.defer.mixed &&
                                                              c2.defer.value == 1700000000);
        m.set_protect(t3, true);
        check("writable: protected left out", core::writable(m, {t1, t2, t3, note}, false) == V{t1, t2, note});
        check("writable: todos only", core::writable(m, {t1, note, t2}, true) == V{t1, t2});
        check("common: protected counted", core::common(m, {t1, t3}).locked == 1);
        m.set_protect(t3, false);
        check("label: one note, the verb alone", core::many_label("Due date", 1) == "Due date");
        check("label: several, counted", core::many_label("Due date", 3) == "Due date 3 notes");

        // Tags over many: only the notes it changes; a tag in text is not on the line.
        auto adds = core::tag_edits(m, {t1, t2, t3}, "home", true);
        check("tags: add goes to the ones without it", adds.size() == 2 && adds[0].first == t2 &&
                                                        adds[1].first == t3);
        auto rem = core::tag_edits(m, {t1, t2, t3}, "car", false);
        check("tags: remove takes it off the line; 'in text' is left (removed where written)",
              rem.size() == 2 && rem[0].first == t1 && rem[1].first == t2);

        // Through the door: a set over three is ONE step, and one undo puts each back as it was.
        core::Journal j;
        core::UndoSource u(j, [&] { return static_cast<core::NodeSource*>(&m); });
        {
            core::Gesture g(u, core::many_label("Defer date", 3));
            for (const auto& id : core::writable(u, {t1, t2, t3}, true)) u.set_defer(id, 1750000000);
        }
        check("set: defer on all three", m.find(t1)->task.defer == 1750000000 &&
                                         m.find(t2)->task.defer == 1750000000 &&
                                         m.find(t3)->task.defer == 1750000000);
        check("set: one step, named for the many", j.size() == 1 && j.undo_label() == "Defer date 3 notes");
        check("set: now shared", !core::common(m, {t1, t2, t3}).defer.mixed);
        j.undo(m);
        check("set: one undo -> each its own again (one had a defer, two none)",
              m.find(t1)->task.defer == 1700000000 && m.find(t2)->task.defer == 0 &&
                  m.find(t3)->task.defer == 0);
        {
            core::Gesture g(u, core::many_label("Add tag #home", 2));
            for (const auto& [id, ed] : core::tag_edits(u, {t1, t2, t3}, "home", true))
                u.set_body(id, core::apply(m.find(id)->body, ed));
        }
        auto c3 = core::common(m, {t1, t2, t3});
        check("tags: after add-to-all, shared", std::find(c3.tags_all.begin(), c3.tags_all.end(), "home") !=
                                                     c3.tags_all.end() && c3.tags_some.empty());
        // s049: undo / redo hand back what was selected when the step was made.
        {
            core::Journal j2;
            core::UndoSource u2(j2, [&] { return static_cast<core::NodeSource*>(&m); });
            std::vector<core::NodeId> sel{t1, t2};
            j2.set_selection_source([&] { return sel; });
            core::as_step(u2, "Flag 2 notes", {t1, t2}, false, [&] { u2.set_flagged(t1, true); u2.set_flagged(t2, true); });
            sel = {note};   // the user moved on
            j2.undo(m);
            check("selection: undo hands back the group selected at the time",
                  j2.last_selection() == V{t1, t2});
            j2.redo(m);
            check("selection: ...and so does redo", j2.last_selection() == V{t1, t2});
            core::Journal j3;   // no source: nothing to hand back
            core::UndoSource u3(j3, [&] { return static_cast<core::NodeSource*>(&m); });
            u3.set_flagged(t3, true);
            j3.undo(m);
            check("selection: no source said, none handed back", j3.last_selection().empty());
        }
        j.undo(m);
        check("tags: one undo -> only the first has it", core::common(m, {t1, t2, t3}).tags_some.size() == 1 &&
                                                         m.find(t2)->body == "b\n\n#car");
    }

    // ── s050: the accent from the portal ──────────────────────────────────
    std::cout << "\n-- accent (s050) --\n";
    {
        check("accent: GNOME blue in, hex out", core::accent_css(0.2078, 0.5176, 0.8941) == "#3584e4");
        check("accent: the corners", core::accent_css(0, 0, 0) == "#000000" && core::accent_css(1, 1, 1) == "#ffffff");
        check("accent: out of range is 'none set'", core::accent_css(-1, 0, 0).empty() &&
                                                     core::accent_css(0, 2, 0).empty());
        check("accent: NaN is 'none set'", core::accent_css(0, 0, std::nan("")).empty());
        // s050b: the preference
        check("accent pref: hex check", core::is_hex_colour("#9141ac") && !core::is_hex_colour("9141ac") &&
                                         !core::is_hex_colour("#9141a") && !core::is_hex_colour("#zz41ac"));
        check("accent pref: presets are GNOME's nine, all valid", core::accent_presets().size() == 9 &&
              std::all_of(core::accent_presets().begin(), core::accent_presets().end(),
                          [](const core::AccentPreset& p) { return core::is_hex_colour(p.hex); }));
        check("accent pref: names", core::accent_name("#9141AC") == "Purple" &&
                                    core::accent_name("#123456") == "Custom" && core::accent_name("").empty());
        {
            const auto dir = std::filesystem::temp_directory_path() / "jot_s050b_prefs";
            std::filesystem::create_directories(dir);
            const std::string file = (dir / "prefs.json").string();
            core::Prefs a;
            a.accent = "#9141ac";
            core::save_prefs(file, a);
            check("accent pref: round trip", core::load_prefs(file).accent == "#9141ac");
            a.accent = "";
            core::save_prefs(file, a);
            check("accent pref: '' (follow the desktop) round trips", core::load_prefs(file).accent.empty());
            { std::ofstream f(file); f << "{\"accent\": \"chartreuse\"}"; }
            check("accent pref: junk in the file -> follow the desktop", core::load_prefs(file).accent.empty());
            std::filesystem::remove_all(dir);
        }
    }

    std::cout << "-----------------------------------------------\n";
    std::cout << g_pass << " pass / " << g_fail << " fail\n";
    return g_fail == 0 ? 0 : 1;
}
