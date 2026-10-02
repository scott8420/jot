#include "CheatSheetWindow.hpp"
#include "core/CheatSheet.hpp"
#include "Log.hpp"
#include "Registry.hpp"

#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/headerbar.h>
#include <gdk/gdkkeysyms.h>

// CheatSheetWindow.cpp -- draws core::cheat_sheet(). See the header.

namespace jot {

namespace {

// The left column's label. Its width is ONE number whatever it is asked.
//
// A wrapping label asked "how wide for THIS height?" answers with its one-line
// width, and a horizontal GtkBox asks exactly that when it allocates (it
// measures each child for the row's height). So a two-line command reported
// min = natural = 200 to a plain measure() and was still given 264 px -- seen
// under Xvfb and pinned by a width trace in s030. A size request, a SizeGroup
// and max_width_chars all lose to that question; answering it does not.
class KeyLabel : public widgets::Label {
public:
    KeyLabel(const std::string& text, int width)
        : widgets::Label(widgets::unregistered, "cheat.how", text), m_width(width) {}

protected:
    void measure_vfunc(Gtk::Orientation o, int for_size, int& min, int& nat, int& min_base,
                       int& nat_base) const override {
        if (o == Gtk::Orientation::HORIZONTAL) {
            min = nat = m_width;
            min_base = nat_base = -1;
            return;
        }
        // Height: always for the one width we will be given.
        (void)for_size;
        widgets::Label::measure_vfunc(o, m_width, min, nat, min_base, nat_base);
    }

private:
    int m_width;
};

}  // namespace

CheatSheetWindow::CheatSheetWindow()
    : m_search("cheat.search"),
      m_scroll("cheat.scroll"),
      m_column("cheat.column", Gtk::Orientation::VERTICAL, 0),
      m_footer("cheat.footer") {
    set_name("shell.cheatsheet");
    registry::add("shell.cheatsheet", this);

    set_title("Cheat Sheet");
    set_modal(false);
    set_resizable(true);
    set_default_size(640, 720);
    set_hide_on_close(true);   // built once by the Shell, re-presented

    auto* hb = Gtk::make_managed<Gtk::HeaderBar>();
    hb->set_show_title_buttons(true);
    set_titlebar(*hb);

    auto* page = Gtk::make_managed<widgets::Box>(
        widgets::unregistered, "cheat.page", Gtk::Orientation::VERTICAL, 8);
    page->set_margin(14);

    m_search.set_placeholder_text("Type to find a line — “inbox”, “due”, “ctrl+m”");
    m_search.set_icon_from_icon_name("system-search-symbolic", Gtk::Entry::IconPosition::PRIMARY);
    m_search.signal_changed().connect([this]() { filter(); });
    page->append(m_search);

    m_column.set_margin_start(4);
    m_column.set_margin_end(12);
    m_column.set_margin_bottom(12);
    m_scroll.set_child(m_column);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_vexpand(true);
    page->append(m_scroll);

    m_footer.set_xalign(0.0f);
    m_footer.set_wrap(true);
    m_footer.add_css_class("dim-label");
    m_footer.set_visible(false);
    page->append(m_footer);

    // Escape: first empties the search, then closes. A reader who typed a
    // filter and pressed Escape wants the whole sheet back, not the window gone.
    auto esc = Gtk::EventControllerKey::create();
    esc->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    esc->signal_key_pressed().connect(
        [this](guint key, guint, Gdk::ModifierType) -> bool {
            if (key != GDK_KEY_Escape) return false;
            if (!m_search.get_text().empty()) m_search.set_text("");
            else set_visible(false);
            return true;
        },
        false);
    add_controller(esc);

    build();
    filter();
    set_child(*page);
}

CheatSheetWindow::~CheatSheetWindow() { registry::remove(this); }

void CheatSheetWindow::show(Gtk::Window& parent) {
    set_transient_for(parent);
    m_search.set_text("");   // a fresh look each time; filter() runs off the change
    m_scroll.get_vadjustment()->set_value(0.0);
    present();
    m_search.grab_focus();
}

void CheatSheetWindow::build() {
    const auto& sections = core::cheat_sections();
    const auto& lines = core::cheat_sheet();

    for (std::size_t s = 0; s < sections.size(); ++s) {
        auto* head = Gtk::make_managed<widgets::Box>(
            widgets::unregistered, "cheat.section", Gtk::Orientation::VERTICAL, 2);
        head->set_margin_top(s == 0 ? 4 : 16);
        head->set_margin_bottom(4);
        auto* title = Gtk::make_managed<widgets::Label>(widgets::unregistered,
                                                        "cheat.section.title", sections[s]);
        title->set_xalign(0.0f);
        title->add_css_class("heading");
        head->append(*title);
        head->append(*Gtk::make_managed<widgets::Separator>(
            widgets::unregistered, "cheat.section.rule", Gtk::Orientation::HORIZONTAL));
        m_column.append(*head);
        m_headings.push_back(head);

        for (std::size_t i = 0; i < lines.size(); ++i) {
            const auto& l = lines[i];
            if (l.section != sections[s]) continue;

            auto* row = Gtk::make_managed<widgets::Box>(
                widgets::unregistered, "cheat.row", Gtk::Orientation::HORIZONTAL, 16);
            row->set_margin_top(3);
            row->set_margin_bottom(3);
            row->set_margin_start(6);

            auto* how = Gtk::make_managed<KeyLabel>(l.display_how(), kHowWidth);
            how->set_xalign(0.0f);
            how->set_valign(Gtk::Align::START);
            how->set_wrap(true);
            how->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
            how->set_selectable(true);   // a command line is something you copy
            how->add_css_class("monospace");
            row->append(*how);

            auto* text = Gtk::make_managed<widgets::Box>(
                widgets::unregistered, "cheat.text", Gtk::Orientation::VERTICAL, 1);
            text->set_hexpand(true);
            auto* what = Gtk::make_managed<widgets::Label>(widgets::unregistered, "cheat.what",
                                                           l.what);
            what->set_xalign(0.0f);
            what->set_wrap(true);
            text->append(*what);
            if (!l.where.empty()) {
                auto* where = Gtk::make_managed<widgets::Label>(widgets::unregistered,
                                                                "cheat.where", l.where);
                where->set_xalign(0.0f);
                where->set_wrap(true);
                where->add_css_class("dim-label");
                where->add_css_class("caption");
                text->append(*where);
            }
            row->append(*text);

            m_column.append(*row);
            m_rows.push_back({row, i, s});
        }
    }
}

void CheatSheetWindow::filter() {
    const std::string q = m_search.get_text();
    const auto& lines = core::cheat_sheet();
    std::vector<int> per(m_headings.size(), 0);
    m_visible = 0;
    for (const auto& r : m_rows) {
        const bool on = core::cheat_matches(lines[r.line], q);
        r.widget->set_visible(on);
        if (on) {
            ++per[r.section];
            ++m_visible;
        }
    }
    for (std::size_t s = 0; s < m_headings.size(); ++s) m_headings[s]->set_visible(per[s] > 0);

    if (m_visible == 0) {
        m_footer.set_text("Nothing matches “" + q + "”. Fewer letters, or another word?");
        m_footer.set_visible(true);
    } else {
        m_footer.set_visible(false);
    }
    if (auto lg = log::get(log::Area::Shell))
        lg->debug("cheat sheet: '{}' -> {} lines", q, m_visible);
}

}  // namespace jot
