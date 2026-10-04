#include "PerspectiveDialog.hpp"
#include "Registry.hpp"

#include <gtkmm/box.h>
#include <gtkmm/enums.h>
#include <gtkmm/headerbar.h>
#include <glibmm/markup.h>

// PerspectiveDialog.cpp -- see the header.

namespace jot {

namespace {
std::string trimmed(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
}
}  // namespace

PerspectiveDialog::PerspectiveDialog(Gtk::Window& parent, const std::string& query,
                                     const std::string& reads, const std::string& suggested,
                                     Taken taken, Done done)
    : m_taken(std::move(taken)),
      m_done(std::move(done)),
      m_heading("persp.heading"),
      m_name("persp.name"),
      m_query("persp.query"),
      m_save("persp.save") {
    set_name("shell.perspective");
    registry::add("shell.perspective", this);

    set_title("Save Perspective");
    set_modal(true);
    set_transient_for(parent);
    set_default_size(380, -1);
    set_resizable(false);

    auto* hb = Gtk::make_managed<Gtk::HeaderBar>();
    hb->set_show_title_buttons(false);
    auto* cancel = Gtk::make_managed<widgets::Button>(widgets::unregistered, "persp.cancel");
    cancel->set_label("Cancel");
    cancel->signal_clicked().connect([this]() { close(); });
    hb->pack_start(*cancel);
    m_save.set_label("Save");
    m_save.add_css_class("suggested-action");
    m_save.signal_clicked().connect([this]() { accept(); });
    hb->pack_end(m_save);
    set_titlebar(*hb);

    auto* page = Gtk::make_managed<widgets::Box>(widgets::unregistered, "persp.page",
                                                 Gtk::Orientation::VERTICAL, 8);
    page->set_margin(18);

    m_heading.set_markup("<b>Name this view</b>");
    m_heading.set_xalign(0.0f);
    page->append(m_heading);

    m_name.set_placeholder_text("Errands, This week, Waiting on…");
    m_name.set_text(suggested);
    m_name.signal_changed().connect([this]() { update(); });
    m_name.signal_activate().connect([this]() { accept(); });
    page->append(m_name);

    std::string what = "<tt>" + Glib::Markup::escape_text(query) + "</tt>";
    if (!reads.empty()) what += "\n" + Glib::Markup::escape_text(reads);
    m_query.set_markup(what);
    m_query.set_xalign(0.0f);
    m_query.set_wrap(true);
    m_query.set_selectable(true);
    m_query.add_css_class("dim-label");
    m_query.set_margin_top(4);
    page->append(m_query);

    set_child(*page);
    update();
    m_name.grab_focus();
    m_name.select_region(0, -1);
}

PerspectiveDialog::~PerspectiveDialog() { registry::remove(this); }

void PerspectiveDialog::update() {
    const std::string n = trimmed(m_name.get_text().raw());
    m_save.set_sensitive(!n.empty());
    m_save.set_label(!n.empty() && m_taken && m_taken(n) ? "Replace" : "Save");
}

void PerspectiveDialog::accept() {
    const std::string n = trimmed(m_name.get_text().raw());
    if (n.empty()) {
        m_name.error_bell();
        return;
    }
    if (m_done) m_done(n);
    close();
}

}  // namespace jot
