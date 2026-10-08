#include "Shell.hpp"
#include "App.hpp"
#include "TimelinePane.hpp"   // s059
#include "DrawerPane.hpp"
#include "EditorPane.hpp"
#include "TreePane.hpp"
#include "TodayPane.hpp"
#include "InboxPane.hpp"
#include "TagsPane.hpp"
#include "ProjectsPane.hpp"
#include "SearchPane.hpp"
#include "core/Search.hpp"
#include "Log.hpp"
#include "Menus.hpp"

#include <glibmm/variant.h>

#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/headerbar.h>

#include <gdk/gdkkeysyms.h>
#include <gtkmm/enums.h>
#include <gtkmm/cssprovider.h>
#include <gdkmm/display.h>

// Shell_zones.cpp -- ZONES. Construction of the named regions: the window frame
// and header, the paned tree|note body, and the menu model.

namespace jot {

void Shell::build_shell() {  // zone: window + header + paned body
    set_title("jot");
    // The remembered size, or the first-run default that lives in Prefs. Set
    // BEFORE the window is realised, which is why it is here and not in the
    // later layout pass: set_default_size on a shown window is a resize the
    // user watches happen.
    set_default_size(m_prefs.win_width, m_prefs.win_height);
    if (m_prefs.win_maximized) maximize();

    auto header = Gtk::make_managed<Gtk::HeaderBar>();

    // Brand logo at the start: a frameless button whose icon is a symbolic
    // resource resolved from the compiled-in bundle (App registers it), so this
    // one button exercises the resource pipeline. Click opens the About window
    // (the dialog-lifetime exemplar).
    m_logo_button.set_icon_name("jot-app-logo-symbolic");
    m_logo_button.set_has_frame(false);
    m_logo_button.set_tooltip_text("About jot");
    m_logo_button.set_action_name("win.about");
    header->pack_start(m_logo_button);

    // s033c: the two pane toggles, together, before + (Scott).
    build_pane_toggles(*header);

    // Capture is the shortest path in the app -- it gets a button, not a menu
    // item. (ARCHITECTURE: "the front door is a blank note, not a tree.")
    m_new_button.set_icon_name("list-add-symbolic");
    m_new_button.set_tooltip_text("New note (Ctrl+N)");
    m_new_button.set_action_name("win.new-note");
    header->pack_start(m_new_button);

    build_capture_bar(*header);

    m_menu_button.set_icon_name("open-menu-symbolic");
    m_menu_button.set_tooltip_text("Main menu");
    m_menu_button.set_menu_model(build_menu());
    header->pack_end(m_menu_button);

    // The NOTE menu (s016c), beside the main one. The hamburger had grown to
    // ~30 items doing four jobs; the ten that act on the selected note moved
    // here, and it is the same model the tree's right-click shows (Menus.cpp).
    m_note_menu_button.set_icon_name("view-more-symbolic");
    m_note_menu_button.set_tooltip_text("This note");
    m_note_menu_button.set_menu_model(menus::note_menu(true));   // s053c: with View
    header->pack_end(m_note_menu_button);

    // s021 / s034: Source | Live | Reading. It stays at the right where the
    // eye was: it is about the note, not the panes.
    build_view_modes(*header);

    // The title is a BUTTON, because the jots folder's location is a thing you
    // need to see and act on, not decoration. update_jots_title() fills it.
    build_jots_title();
    header->set_title_widget(m_jots_button);

    set_titlebar(*header);

    // ── tree | note | drawer ────────────────────────────────────────────────
    // Nested Paned: paned_left(tree | paned_right(note | drawer)). Lifted from
    // Folio's sidebar|centre|inspector, which is already this shape.
    //
    // The flags are the part worth reading twice, and they are deliberately not
    // symmetric. Both SIDE panes hold their width and the NOTE absorbs every
    // resize, because the note is what you are looking at and a window drag
    // should make it bigger, not make the metadata column wider.
    // s059: the centre is a stack -- the note, or the timeline in its place.
    m_center.add(*m_editor, "note");
    m_center.add(*m_timeline, "timeline");
    m_center.set_transition_type(Gtk::StackTransitionType::CROSSFADE);
    m_center.set_transition_duration(120);
    m_paned_right.set_start_child(m_center);
    m_paned_right.set_end_child(*m_drawer);
    m_paned_right.set_resize_start_child(true);    // the note takes the slack
    m_paned_right.set_resize_end_child(false);     // the drawer holds its width
    m_paned_right.set_shrink_start_child(false);
    m_paned_right.set_shrink_end_child(false);

    build_left_pane();
    m_paned_left.set_start_child(m_left);
    m_paned_left.set_end_child(m_paned_right);
    m_paned_left.set_resize_start_child(false);    // the tree holds its width
    m_paned_left.set_resize_end_child(true);       // everything right of it resizes
    m_paned_left.set_shrink_start_child(false);
    m_paned_left.set_shrink_end_child(false);

    // ── live position tracking, GUARDED BY THE VISIBLE FLAG ─────────────────
    // That guard is the subtle bit, and it is the scar Folio earned. Hiding a
    // Paned child makes the widget report the COLLAPSED position; without the
    // guard, that value is written back as the user's preferred width and the
    // real one is gone for good -- not at the next launch, but immediately and
    // silently, so it looks like the pane forgot rather than like we overwrote.
    m_paned_left.property_position().signal_changed().connect([this]() {
        if (m_prefs.show_tree && !m_applying_layout)
            m_prefs.tree_width = m_paned_left.get_position();
    });
    m_paned_right.property_position().signal_changed().connect([this]() {
        if (m_prefs.show_drawer && !m_applying_layout)
            m_prefs.note_width = m_paned_right.get_position();
    });

    // s062e: a line over everything when this jot is older than its program
    // file (rebuilt since it started). Hidden until then.
    auto* outer = Gtk::make_managed<widgets::Box>("shell.outer", Gtk::Orientation::VERTICAL, 0);
    m_stale_bar = Gtk::make_managed<widgets::Box>("shell.stale_bar", Gtk::Orientation::HORIZONTAL, 12);
    m_stale_bar->add_css_class("jot-stale-bar");
    m_stale_bar->set_margin_start(12);
    m_stale_bar->set_margin_end(12);
    m_stale_bar->set_margin_top(6);
    m_stale_bar->set_margin_bottom(6);
    auto* msg = Gtk::make_managed<widgets::Label>("shell.stale_bar.label");
    msg->set_text("This jot is an older build \u2014 the program was rebuilt since it started.");
    msg->set_xalign(0.0f);
    msg->set_hexpand(true);
    m_stale_bar->append(*msg);
    auto* restart = Gtk::make_managed<widgets::Button>("shell.stale_bar.restart");
    restart->set_label("Restart");
    restart->add_css_class("suggested-action");
    restart->set_tooltip_text("Save, quit, and start the new build");
    restart->signal_clicked().connect([this]() {
        if (auto* app = dynamic_cast<App*>(Gio::Application::get_default().get()))
            app->restart_new_build();
    });
    m_stale_bar->append(*restart);
    m_stale_bar->set_visible(false);
    outer->append(*m_stale_bar);
    m_paned_left.set_vexpand(true);
    outer->append(m_paned_left);
    set_child(*outer);
    // Looked at whenever the window comes forward -- cheap (one readlink, one stat).
    property_is_active().signal_changed().connect([this]() {
        if (!is_active()) return;
        auto* app = dynamic_cast<App*>(Gio::Application::get_default().get());
        const bool old = app && app->stale();
        if (old && !m_stale_bar->get_visible())
            if (auto lg = log::get(log::Area::Shell)) lg->info("stale: this jot is older than its program file");
        m_stale_bar->set_visible(old);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// build_capture_bar -- the shortest path in the app, and it is a line you can
// already see.
//
// ARCHITECTURE has called jot a scribble pad since s001: capture first, file
// second. Seven milestones in, the shortest path to a new thought was still
// Ctrl+N and then finding where the note went. This is the fix.
//
// It is in the HEADER rather than in the note pane because the point is that it
// does not disturb what you are reading. It is ALWAYS VISIBLE rather than
// behind a button, because a box you have to summon costs a click and the
// premise is that capture costs nothing.
//
// Narrow on purpose. A wide box invites a paragraph; the box is for the
// sentence you would otherwise lose, and capture_split() keeps the whole of
// whatever arrives anyway.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::build_capture_bar(Gtk::HeaderBar& header) {  // zone: the capture line
    m_capture.set_placeholder_text("Capture a thought\u2026");
    m_capture.set_tooltip_text("Type and press Enter: a new unfiled note, "
                               "without leaving this one (Ctrl+Shift+Enter)");
    m_capture.set_width_chars(16);
    m_capture.set_max_width_chars(28);
    m_capture.set_hexpand(false);
    m_capture.signal_activate().connect(sigc::mem_fun(*this, &Shell::on_capture_activate));

    // The confirmation lives in the placeholder, so it occupies no space and
    // cannot be left behind: the next keystroke is what clears it.
    m_capture.signal_changed().connect([this]() {
        if (!m_capture_tell) return;
        m_capture_tell = false;
        m_capture.set_placeholder_text("Capture a thought\u2026");
    });

    // Escape gives the keyboard back to the note. Without it the capture line
    // is a place the cursor can get stuck in a window with no other exit but
    // the mouse.
    auto key = Gtk::EventControllerKey::create();
    key->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType) {
            if (keyval != GDK_KEY_Escape) return false;
            m_capture.set_text("");
            m_editor->focus_capture();
            return true;
        }, false);
    m_capture.add_controller(key);

    header.pack_start(m_capture);
}

// ─────────────────────────────────────────────────────────────────────────────
// build_left_pane -- Notes and Today, behind one switcher.
//
// The left pane was "the tree" for six milestones. It is now HOW YOU NAVIGATE,
// and the tree is one of two ways to do it. That framing is what keeps three
// panes at three: Today is not a fourth pane and not a window, it is the other
// view of the same nodes.
//
// A Stack rather than swapping the Paned's child: the tree keeps its scroll
// position, its collapse state and its selection while you are away in Today,
// so switching back does not cost you the place you were working in.
//
// The tabs are ToggleButtons driven by a stateful STRING action rather than a
// GtkStackSwitcher, for the reason the pane toggles are: the state has two
// consumers (these buttons and the View menu's radio items) and one place to
// read it from is what stops them disagreeing.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::build_left_pane() {  // zone: Notes | Today
    m_left_tabs.add_css_class("linked");
    m_left_tabs.set_margin(8);
    // s037: five tabs. The theme pads a button for a dialog (10 px a side);
    // at 5 px the row is ~80 px narrower, and the row's width is the side
    // pane's minimum -- the tabs, not the tree, decide how narrow it can go.
    if (auto display = Gdk::Display::get_default()) {
        auto css = Gtk::CssProvider::create();
        css->load_from_data(".jot-left-tab { padding-left: 5px; padding-right: 5px; }\n"
                            ".jot-tab-count { font-size: smaller; font-weight: 700; }\n");
        gtk_style_context_add_provider_for_display(display->gobj(), GTK_STYLE_PROVIDER(css->gobj()),
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    for (auto* t : {&m_tab_notes, &m_tab_inbox, &m_tab_today, &m_tab_tags, &m_tab_projects})
        t->add_css_class("jot-left-tab");

    // s048: icons, not words -- a Mac segmented control. The name is the
    // tooltip's first word; the Inbox keeps its count beside its tray.
    m_tab_notes.set_icon_name("jot-tab-notes-symbolic");
    m_tab_notes.set_hexpand(true);
    m_tab_notes.set_action_name("win.left-view");
    m_tab_notes.set_action_target_value(Glib::Variant<Glib::ustring>::create("notes"));
    m_tab_notes.set_tooltip_text("Notes \u2014 the note tree");
    m_left_tabs.append(m_tab_notes);

    // s028. Between Notes and Today: where a thought goes after it is
    // captured and before it is something to do. The label carries the
    // waiting count (queue_inbox_refresh writes it).
    {
        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
        box->set_halign(Gtk::Align::CENTER);
        auto* img = Gtk::make_managed<Gtk::Image>();
        img->set_from_icon_name("jot-tab-inbox-symbolic");
        box->append(*img);
        m_tab_inbox_count = Gtk::make_managed<Gtk::Label>();
        m_tab_inbox_count->add_css_class("jot-tab-count");
        m_tab_inbox_count->set_visible(false);
        box->append(*m_tab_inbox_count);
        m_tab_inbox.set_child(*box);
    }
    m_tab_inbox.set_hexpand(true);
    m_tab_inbox.set_action_name("win.left-view");
    m_tab_inbox.set_action_target_value(Glib::Variant<Glib::ustring>::create("inbox"));
    m_tab_inbox.set_tooltip_text("Inbox \u2014 what you captured and have not dealt with yet");
    m_left_tabs.append(m_tab_inbox);

    m_tab_today.set_icon_name("jot-tab-today-symbolic");
    m_tab_today.set_hexpand(true);
    m_tab_today.set_action_name("win.left-view");
    m_tab_today.set_action_target_value(Glib::Variant<Glib::ustring>::create("today"));
    m_tab_today.set_tooltip_text("Today \u2014 what is actually available to do");
    m_left_tabs.append(m_tab_today);

    // s035. Tags as contexts: pick a #tag, see what you can do there.
    m_tab_tags.set_icon_name("jot-tab-tags-symbolic");
    m_tab_tags.set_hexpand(true);
    m_tab_tags.set_action_name("win.left-view");
    m_tab_tags.set_action_target_value(Glib::Variant<Glib::ustring>::create("tags"));
    m_tab_tags.set_tooltip_text("Tags \u2014 pick a #tag: its todos and notes from all over (Ctrl+Shift+T)");
    m_left_tabs.append(m_tab_tags);

    // s037. Review and every project by state: one tab, two ways to look.
    m_tab_projects.set_icon_name("jot-tab-projects-symbolic");
    m_tab_projects.set_hexpand(true);
    m_tab_projects.set_action_name("win.left-view");
    m_tab_projects.set_action_target_value(Glib::Variant<Glib::ustring>::create("projects"));
    m_tab_projects.set_tooltip_text("Projects \u2014 review: the projects due a look -- and every project by state (Ctrl+Shift+P)");
    m_left_tabs.append(m_tab_projects);

    // s038. Find over the tree: while it has words, the results stand where
    // the tree stands (a Stack, so the tree keeps its scroll and folds).
    m_find.set_placeholder_text("Find in all notes");
    m_find.set_tooltip_text("Every word, anywhere in a note's name or text (Ctrl+F). "
                            "Also #tag, is:available, due:week, or, -word -- the menu beside "
                            "writes them. Enter opens the first; Esc goes back to the tree.");
    m_find.set_hexpand(true);
    m_find.signal_search_changed().connect([this]() {
        const std::string q = m_find.get_text().raw();
        m_search->set_query(q);
        m_notes_stack.set_visible_child(core::query_active(q) ? "results" : "tree");
        sync_find_actions();   // s038b: the menu shows what the text says
    });
    // s038b. The filter menu: Show / Due / Flagged write tokens into the field,
    // and the saved perspectives put theirs there. ONE control for one concept
    // -- the field is the filter, and this is how to write one without
    // learning the words (CANON: one concept, one widget).
    m_find_menu.set_icon_name("jot-filter-symbolic");
    m_find_menu.set_tooltip_text("Filter and perspectives (Ctrl+J)");
    m_find_menu.set_has_frame(false);
    m_find_menu.set_valign(Gtk::Align::CENTER);
    m_find_row.set_margin_start(8);
    m_find_row.set_margin_end(4);
    m_find_row.set_margin_bottom(6);
    m_find_row.append(m_find);
    m_find_row.append(m_find_menu);
    m_find.signal_activate().connect([this]() {
        if (!m_search->open_first()) m_find.error_bell();
    });
    m_find.signal_stop_search().connect([this]() {
        end_search();
        m_tree->grab_focus();
    });
    m_notes_stack.add(*m_tree, "tree");
    m_notes_stack.add(*m_search, "results");
    m_notes_stack.set_vexpand(true);
    m_notes_page.append(m_find_row);
    m_notes_page.append(m_notes_stack);
    m_left_stack.add(m_notes_page, "notes");
    m_left_stack.add(*m_inbox, "inbox");
    m_left_stack.add(*m_today, "today");
    m_left_stack.add(*m_tags,  "tags");    // s035
    m_left_stack.add(*m_projects, "projects");   // s037
    m_left_stack.set_vexpand(true);
    m_left_stack.set_transition_type(Gtk::StackTransitionType::CROSSFADE);
    m_left_stack.set_transition_duration(120);

    m_left.add_css_class("jot-side");   // s050: the source-list shade (Appearance.cpp)
    m_left.append(m_left_tabs);
    m_left.append(m_left_stack);
}

// ─────────────────────────────────────────────────────────────────────────────
// build_pane_toggles -- the focus mode, wearing two buttons.
//
// Both off leaves the note alone on screen, which IS the front door
// ARCHITECTURE describes. The toggles are not a side effect of having panes to
// hide; hiding them is the feature and the panes are what it hides.
//
// ToggleButtons rather than menu items because this is a thing you do while
// looking at a note and change your mind about, and a two-click round trip
// through a hamburger is not that. Each writes its own flag and then calls ONE
// apply_layout_state() -- two writers for one layout is how the two panes end
// up disagreeing about whose turn it is to be visible.
//
// Icon choice, since a missing icon renders as a broken image and that would be
// a bad way to find out: `sidebar-show-symbolic` is long-standing Adwaita, and
// `dialog-information-symbolic` is in the freedesktop spec itself. Neither is
// one of the recent additions.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::build_pane_toggles(Gtk::HeaderBar& header) {  // zone: the focus-mode toggles
    // s033c (Scott): one pair at the left, before +, instead of a sidebar
    // icon at one end and an info icon at the other. Each is a window outline
    // with the panel it shows filled in -- left filled for the side pane,
    // right filled for note details -- so the picture says which pane, and
    // pressed says it is showing. jot's own icons (resources/), drawn on
    // whole pixels so they are crisp at 16 px. Same actions as before, so
    // Ctrl+[ / Ctrl+] (and F9 / F10) are unchanged.
    auto* pair = Gtk::make_managed<widgets::Box>("shell.pane_toggles",
                                                 Gtk::Orientation::HORIZONTAL, 0);
    pair->add_css_class("linked");

    m_tree_toggle.set_icon_name("jot-pane-left-symbolic");
    m_tree_toggle.set_tooltip_text("Show the side pane (Ctrl+[ or F9)");
    m_tree_toggle.set_action_name("win.toggle-tree");
    pair->append(m_tree_toggle);

    m_drawer_toggle.set_icon_name("jot-pane-right-symbolic");
    m_drawer_toggle.set_tooltip_text("Show note details (Ctrl+] or F10)");
    m_drawer_toggle.set_action_name("win.toggle-drawer");
    pair->append(m_drawer_toggle);

    header.pack_start(*pair);
}

// ─────────────────────────────────────────────────────────────────────────────
// build_view_modes -- how the note looks, as one control (s034).
//
// Until s034 this was two things for one question: an eye in the header
// (Reading on/off) and a check item in the View menu (Live Preview on/off).
// Three views, two switches, and the one people use most -- Live -- was the one
// you could not see. CANON, "one concept, one widget -- and the widget shows
// the state": three joined buttons, exactly one pressed.
//
// The two bools stay the model (prefs, Ctrl+E, Ctrl+Shift+E, the menu checks
// all unchanged). The string action is a VIEW of them: apply_layout_state()
// derives it, on_view_mode() maps a click back onto them. Reading keeps the
// Live flag as it was, so Ctrl+E out of Reading lands in the editing view you
// came from, as it always has.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::build_view_modes(Gtk::HeaderBar& header) {  // zone: Source | Live | Reading
    auto* trio = Gtk::make_managed<widgets::Box>("shell.view_modes",
                                                 Gtk::Orientation::HORIZONTAL, 0);
    trio->add_css_class("linked");
    struct M { widgets::ToggleButton& b; const char* icon; const char* target; const char* tip; };
    for (const M& m : {M{m_mode_source,  "jot-view-source-symbolic",  "source",
                         "Source: every mark on screen"},
                       M{m_mode_live,    "jot-view-live-symbolic",    "live",
                         "Live Preview: marks show only where the cursor is (Ctrl+Shift+E)"},
                       M{m_mode_reading, "jot-view-reading-symbolic", "reading",
                         "Reading: the note without its marks (Ctrl+E)"}}) {
        m.b.set_icon_name(m.icon);
        m.b.set_tooltip_text(m.tip);
        m.b.set_action_name("win.view-mode");
        m.b.set_action_target_value(Glib::Variant<Glib::ustring>::create(m.target));
        trio->append(m.b);
    }
    // s059: the fourth -- the same control, because it answers the same
    // question (what the middle of the window shows). Pressed, the timeline
    // takes the note's place; any of the other three brings the note back.
    m_mode_timeline.set_icon_name("jot-view-timeline-symbolic");
    m_mode_timeline.set_tooltip_text("Timeline: your work along the days (Ctrl+Shift+L)");
    m_mode_timeline.set_action_name("win.view-mode");
    m_mode_timeline.set_action_target_value(Glib::Variant<Glib::ustring>::create("timeline"));
    m_mode_timeline.set_margin_start(6);
    auto* both = Gtk::make_managed<widgets::Box>(widgets::unregistered, "shell.view_modes_and_timeline",
                                                 Gtk::Orientation::HORIZONTAL, 0);
    both->append(*trio);
    both->append(m_mode_timeline);
    header.pack_end(*both);
}

// ─────────────────────────────────────────────────────────────────────────────
// build_jots_title -- the header title, as a menu button.
//
// Two lines: the folder, over the folder it is in. Neither alone is enough --
// "jots" says nothing (every jots folder could be called that) and a bare
// absolute path is unreadable at a glance in a headerbar. Together they answer
// "which jots am I in" and "where is that" without opening anything.
//
// The MENU is where the full absolute path lives, as a section label, with the
// three verbs a location actually has under it. The menu model is held and
// rebuilt in place by update_jots_title() rather than recreated, because the
// button holds a reference to it.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::build_jots_title() {  // zone: header title menu button
    auto* stack = Gtk::make_managed<widgets::Box>(
        widgets::unregistered, "shell.jots_title", Gtk::Orientation::VERTICAL);
    stack->set_valign(Gtk::Align::CENTER);

    m_jots_name.add_css_class("title-4");
    m_jots_name.set_ellipsize(Pango::EllipsizeMode::MIDDLE);
    m_jots_name.set_max_width_chars(28);
    stack->append(m_jots_name);

    m_jots_where.add_css_class("dim-label");
    m_jots_where.add_css_class("caption");
    m_jots_where.set_ellipsize(Pango::EllipsizeMode::MIDDLE);
    m_jots_where.set_max_width_chars(34);
    stack->append(m_jots_where);

    m_jots_menu = Gio::Menu::create();
    m_jots_button.set_child(*stack);
    m_jots_button.set_has_frame(false);
    m_jots_button.set_menu_model(m_jots_menu);
}

// ─────────────────────────────────────────────────────────────────────────────
// build_menu -- the MAIN menu, app-level things only (s016c).
//
// It had grown to about thirty items in seven sections, doing four jobs: note
// verbs, file verbs, view state, and settings. Scott's rule: a menu is only
// helpful if its items are concise and easy to find. So:
//
//   * note and todo verbs -> the note menu (the button beside this one, and the
//     tree's right-click), one shared model in Menus.cpp;
//   * the three settings -> gone from menus entirely; Preferences is their one
//     home, the same rule s016a applied to the Today footer;
//   * view state and diagnostics -> submenus, because they are looked for,
//     not read in passing.
//
// Every key still works; nothing here was the only way to reach anything.
// ─────────────────────────────────────────────────────────────────────────────
Glib::RefPtr<Gio::Menu> Shell::build_menu() {  // zone: hamburger model
    auto menu = Gio::Menu::create();

    auto file = Gio::Menu::create();
    // "New" and "Open" are the same operation on disk -- a jots folder is just
    // a folder -- but they are different intentions, and collapsing them into
    // one verb made starting a new set of jots the longer path. Two items.
    file->append("New jots\u2026", "win.new-jots");
    file->append("Open jots\u2026", "win.open-jots");
    m_recents_menu = Gio::Menu::create();
    file->append_submenu("Recent jots", m_recents_menu);
    file->append("Close jots", "win.close-jots");   // s062b
    // s021c: import is a jots-level verb -- it fills the folder you are in --
    // so it sits with Open, not only in the note menu (where the tree's
    // right-click still offers it).
    auto imp = Gio::Menu::create();
    imp->append("Import Markdown Files\u2026", "win.import-md");
    imp->append("Import Markdown Folder\u2026", "win.import-md-folder");
    file->append_section(imp);
    menu->append_section(file);

    // Save exists although nothing is unsaved for long (structure writes at
    // once, bodies on a timer), because an app with no save verb looks like one
    // that might lose your work. "Save", not "Save all" -- the plural described
    // the implementation, not what the user is doing.
    // s045: the navigator's undo. s046b: every model change, wherever it was
    // made (a card's tick, Note details, a capture) -- so no longer "in the
    // outline". The note body's Ctrl+Z is still the text's own.
    auto undo = Gio::Menu::create();
    undo->append("Undo", "win.undo-nav");
    undo->append("Redo", "win.redo-nav");
    menu->append_section(undo);

    auto save = Gio::Menu::create();
    save->append("Save", "win.save-all");
    save->append("Save as\u2026", "win.save-as");
    menu->append_section(save);

    // The two left-pane views as radio items on the one stateful action the tab
    // buttons use, and the two pane toggles as check items. F9 / F10 are the
    // fast road; this is where you find them.
    auto view = Gio::Menu::create();
    view->append("Notes", "win.left-view::notes");
    view->append("Inbox", "win.left-view::inbox");
    view->append("Today", "win.left-view::today");
    view->append("Tags", "win.left-view::tags");     // s035
    view->append("Projects", "win.left-view::projects");   // s037
    view->append("Clean Up Inbox", "win.clean-up");   // s028
    auto panes = Gio::Menu::create();
    panes->append("Side pane", "win.toggle-tree");
    panes->append("Note details", "win.toggle-drawer");
    view->append_section(panes);
    auto mode = Gio::Menu::create();
    mode->append("Reading view", "win.toggle-reading");   // s021
    mode->append("Live preview", "win.toggle-live");      // s022
    mode->append("Timeline", "win.timeline");             // s059
    mode->append("Glance of Today", "win.glance");        // s066
    view->append_section(mode);
    view->append_section(menus::zoom_menu());             // s053b

    // For finding out what jot thinks is true. Refresh-the-desktop lives here
    // now: it is a nudge for when the calendar looks stale, which is a
    // diagnosis, not a setting.
    auto diag = Gio::Menu::create();
    diag->append("Dump node tree", "win.dump-nodes");
    diag->append("Dump widget registry", "win.dump-registry");
    diag->append("Test notifications", "win.test-notify");
    diag->append("Refresh desktop calendar", "win.desktop-sync");

    auto sub = Gio::Menu::create();
    sub->append_submenu("View", view);
    sub->append_submenu("Diagnostics", diag);
    menu->append_section(sub);

    auto app = Gio::Menu::create();
    app->append("Preferences", "win.preferences");
    app->append("jot Help", "win.help");             // s067
    app->append("Cheat sheet", "win.cheat-sheet");   // s030
    app->append("Keyboard shortcuts", "win.shortcuts");
    app->append("About jot", "win.about");
    app->append("Quit", "win.quit");
    menu->append_section(app);
    return menu;
}

}  // namespace jot
