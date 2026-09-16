#pragma once
#include "widgets/Widgets.hpp"

#include <gtkmm/window.h>

#include <functional>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// JotsFolderDialog -- naming a jots folder.
//
// ONE dialog, three occasions, because all three ask the same question and
// differ only in why they are asking:
//
//   New     -- "New jots…" from the menu. Name + location.
//   Save    -- the scratch buffer is being given a home on close. Name +
//              location, and the wording says what is about to happen to the
//              notes already on screen.
//   SaveAs  -- "Save as…". A jots folder IS already on disk; this writes a
//              SECOND one containing everything, and switches to it. Distinct
//              from Save because Save's wording ("these are not on disk yet")
//              is a lie when a folder is open, and a dialog that lies about
//              what it is about to do is how people lose a folder they meant
//              to keep.
//   Rename  -- the folder keeps its place and changes its name. Location is
//              shown but not editable, because a rename that silently moves is
//              how you end up with two jots folders and no memory of which is
//              which.
//
// Three dialogs would drift. The mode changes a heading, a button label and
// whether one row is sensitive; the naming rules, the live preview and the
// refusals are written once.
//
// **The preview is the point.** The user types a NAME; what appears on disk is
// `<name>.jots`, and the row underneath shows the whole path they are about to
// create. No suffix is ever typed, and nothing is silently rewritten: an
// unusable name (blank, a path separator) greys the button and says why,
// rather than being sanitised behind the user's back into a folder they did
// not ask for. core::jots_folder_name() owns that rule and is selftested.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot {

class JotsFolderDialog : public Gtk::Window {
public:
    enum class Mode { New, Save, SaveAs, Rename };

    // done(absolute_target_dir). Fired only on accept -- cancelling and closing
    // do nothing at all, which is what makes this safe to use as the "Save"
    // arm of a close prompt: if the dialog goes away, so does the close.
    using Done = std::function<void(const std::string&)>;

    JotsFolderDialog(Gtk::Window& parent, Mode mode,
                     const std::string& location,      // parent dir to create in
                     const std::string& name,          // pre-filled display name
                     Done done);
    ~JotsFolderDialog() override;

private:
    void choose_location();
    void revalidate();                 // preview + button state, from one place
    void accept();

    Mode        m_mode;
    std::string m_location;
    Done        m_done;

    widgets::Entry  m_name;
    widgets::Label  m_location_label;
    widgets::Button m_choose;
    widgets::Label  m_preview;
    widgets::Button m_accept;
};

}  // namespace jot
