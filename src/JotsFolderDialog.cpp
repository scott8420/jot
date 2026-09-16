#include "JotsFolderDialog.hpp"
#include "Log.hpp"
#include "Registry.hpp"
#include "core/Project.hpp"

#include <gtkmm/box.h>
#include <gtkmm/enums.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/headerbar.h>
#include <giomm/asyncresult.h>
#include <giomm/file.h>
#include <glibmm/miscutils.h>

#include <filesystem>

// JotsFolderDialog.cpp -- naming a jots folder. See the header for why there
// is one of these rather than three.

namespace jot {
namespace {

std::string tilde(const std::string& path) {
    const std::string home = Glib::get_home_dir();
    if (!home.empty() && path.rfind(home + "/", 0) == 0) return "~" + path.substr(home.size());
    if (path == home) return "~";
    return path;
}

}  // namespace

JotsFolderDialog::JotsFolderDialog(Gtk::Window& parent, Mode mode,
                                   const std::string& location,
                                   const std::string& name,
                                   Done done)
    : m_mode(mode),
      m_location(location),
      m_done(std::move(done)),
      m_name("jotsfolder.name"),
      m_location_label("jotsfolder.location"),
      m_choose("jotsfolder.choose"),
      m_preview("jotsfolder.preview"),
      m_accept("jotsfolder.accept") {
    set_name("shell.jotsfolder");
    registry::add("shell.jotsfolder", this);

    set_modal(true);
    set_resizable(false);
    set_default_size(460, -1);
    set_transient_for(parent);
    // Held in a unique_ptr by Shell: the X must hide, not destruct.
    set_hide_on_close(true);

    auto* hb = Gtk::make_managed<Gtk::HeaderBar>();
    hb->set_show_title_buttons(true);
    set_titlebar(*hb);

    auto* page = Gtk::make_managed<widgets::Box>(
        widgets::unregistered, "shell.jotsfolder.page", Gtk::Orientation::VERTICAL, 12);
    page->set_margin(24);

    // The three occasions, differing only in what they say.
    std::string heading, blurb, accept_label;
    switch (mode) {
        case Mode::New:
            set_title("New jots");
            heading      = "What should these jots be called?";
            blurb        = "jot will create a folder for them. You can move or rename it "
                           "later without losing anything.";
            accept_label = "Create";
            break;
        case Mode::Save:
            set_title("Save your jots");
            heading      = "Where should these notes live?";
            blurb        = "The notes you have written are not on disk yet. Give them a "
                           "name and jot will create a folder and write them into it.";
            accept_label = "Save";
            break;
        case Mode::SaveAs:
            set_title("Save jots as");
            heading      = "Save a copy of these jots";
            blurb        = "jot will create a new folder, write every note into it, and "
                           "switch to it. The folder you are in now is left exactly as "
                           "it is.";
            accept_label = "Save as";
            break;
        case Mode::Rename:
            set_title("Rename jots");
            heading      = "Rename these jots";
            blurb        = "The folder keeps its place. Nothing inside it is rewritten \u2014 "
                           "notes are addressed by identity, not by path.";
            accept_label = "Rename";
            break;
    }

    auto* h = Gtk::make_managed<widgets::Label>(widgets::unregistered, "shell.jotsfolder.heading");
    h->set_markup("<span size='large' weight='bold'>" + heading + "</span>");
    h->set_xalign(0.0f);
    page->append(*h);

    auto* why = Gtk::make_managed<widgets::Label>(
        widgets::unregistered, "shell.jotsfolder.why", blurb);
    why->set_wrap(true);
    why->set_xalign(0.0f);
    why->set_max_width_chars(50);
    why->add_css_class("dim-label");
    page->append(*why);

    m_name.set_placeholder_text("Name");
    m_name.set_text(name);
    m_name.set_activates_default(false);
    m_name.set_margin_top(4);
    m_name.signal_changed().connect([this]() { revalidate(); });
    m_name.signal_activate().connect([this]() { accept(); });
    page->append(m_name);

    // Location row. In Rename mode it is shown and NOT editable: seeing where
    // the folder is answers "am I renaming the right one" without offering to
    // move it in the same gesture.
    auto* row = Gtk::make_managed<widgets::Box>(
        widgets::unregistered, "shell.jotsfolder.row", Gtk::Orientation::HORIZONTAL, 10);
    m_location_label.set_xalign(0.0f);
    m_location_label.set_hexpand(true);
    m_location_label.set_ellipsize(Pango::EllipsizeMode::MIDDLE);
    m_location_label.add_css_class("dim-label");
    row->append(m_location_label);
    m_choose.set_label("Choose\u2026");
    m_choose.signal_clicked().connect(sigc::mem_fun(*this, &JotsFolderDialog::choose_location));
    m_choose.set_visible(mode != Mode::Rename);
    row->append(m_choose);
    page->append(*row);

    // What will exist on disk when they press the button. The user types a
    // name; this is the only place the `.jots` suffix appears, and it appears
    // before they commit rather than after.
    m_preview.set_xalign(0.0f);
    m_preview.set_wrap(true);
    m_preview.set_max_width_chars(50);
    m_preview.set_margin_top(2);
    m_preview.add_css_class("dim-label");
    page->append(m_preview);

    auto* buttons = Gtk::make_managed<widgets::Box>(
        widgets::unregistered, "shell.jotsfolder.buttons", Gtk::Orientation::HORIZONTAL, 8);
    buttons->set_halign(Gtk::Align::END);
    buttons->set_margin_top(8);
    auto* cancel = Gtk::make_managed<widgets::Button>(
        widgets::unregistered, "shell.jotsfolder.cancel", "Cancel");
    cancel->signal_clicked().connect([this]() { set_visible(false); });
    buttons->append(*cancel);
    m_accept.set_label(accept_label);
    m_accept.add_css_class("suggested-action");
    m_accept.signal_clicked().connect(sigc::mem_fun(*this, &JotsFolderDialog::accept));
    buttons->append(m_accept);
    page->append(*buttons);

    set_child(*page);
    revalidate();
    m_name.grab_focus();
}

JotsFolderDialog::~JotsFolderDialog() { registry::remove(this); }

void JotsFolderDialog::choose_location() {
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Where should the folder go?");
    std::error_code ec;
    if (std::filesystem::is_directory(m_location, ec))
        dialog->set_initial_folder(Gio::File::create_for_path(m_location));
    dialog->select_folder(*this,
        [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto dir = dialog->select_folder_finish(result);
                if (!dir) return;
                if (const std::string p = dir->get_path(); !p.empty()) {
                    m_location = p;
                    revalidate();
                }
            } catch (const Glib::Error& e) {
                if (auto lg = log::get(log::Area::Shell))
                    lg->info("jotsfolder: chooser dismissed ({})", e.what());
            }
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// revalidate -- the preview and the button state, written in ONE place.
//
// Every reason the button can be off says so in the preview line. A greyed
// button with no explanation is the most annoying shape a dialog has, and the
// explanation costs one label.
// ─────────────────────────────────────────────────────────────────────────────
void JotsFolderDialog::revalidate() {
    m_location_label.set_text(tilde(m_location));
    m_location_label.set_tooltip_text(m_location);

    const std::string folder = core::jots_folder_name(m_name.get_text());
    if (folder.empty()) {
        m_preview.set_text(m_name.get_text().empty()
                               ? "Give it a name."
                               : "That name can\u2019t be used for a folder.");
        m_accept.set_sensitive(false);
        return;
    }

    const std::string target = (std::filesystem::path(m_location) / folder).string();
    std::error_code ec;
    if (std::filesystem::exists(target, ec)) {
        // Occupied is a refusal here rather than a merge. Opening someone
        // else's jots folder is what "Open jots…" is for, and doing it by
        // accident under a Create button is not a thing the user asked for.
        m_preview.set_text("There is already something at " + tilde(target) + ".");
        m_accept.set_sensitive(false);
        return;
    }
    m_preview.set_text("Will create " + tilde(target));
    m_accept.set_sensitive(true);
}

void JotsFolderDialog::accept() {
    const std::string folder = core::jots_folder_name(m_name.get_text());
    if (folder.empty()) return;
    const std::string target = (std::filesystem::path(m_location) / folder).string();
    std::error_code ec;
    if (std::filesystem::exists(target, ec)) return;   // revalidate already said so

    set_visible(false);
    if (m_done) m_done(target);
}

}  // namespace jot
