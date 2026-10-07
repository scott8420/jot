#pragma once
#include "core/Glance.hpp"
#include "core/Nodes.hpp"
#include "core/Tasks.hpp"
#include "widgets/Widgets.hpp"

#include <gtkmm/window.h>
#include <sigc++/signal.h>

#include <string>

namespace jot {

// GlanceWindow -- s066: core::glance()'s GTK consumer, and the way it leaves.
//
// Scott: "a 'Glance of Today' view that is the day's activities. A quick
// listing that could be emailed to my iphone as a note or calendar event."
// It is a CARD to take away, so it is its own small window (Ctrl+Shift+G,
// View › Glance of Today), not one more view in the side pane: what you see is
// what goes out. Three ways out, in the title bar:
//
//   Copy        plain text AND html on the clipboard -- Keep takes the text,
//               iCloud Notes / Gmail's compose keep the headings and bullets
//   Email       the mail app (or Gmail, as the browser's mail handler) with
//               the Glance as the message, To: the address in the foot
//   Save .ics   an iCalendar file of the day -- Google Calendar › Import, or
//               attach it and the iPhone's Mail offers "Add to Calendar"
//
// s066b: a mail link cannot carry an attachment (Scott: "a way of attaching
// directly the ics file in an email would be helpful"), so the foot holds the
// .ics as a CHIP you drag onto the mail's compose -- Gmail in the browser, or
// any mail app -- the way a file is dragged from Files. Click it: Files shows
// it. jot never touches the mail account.
//
// DECIDES NOTHING: every line, and every byte that leaves, is core/Glance's,
// under the selftest. Clicking a line emits signal_goto, as Today's rows do.
// Hide-on-close singleton like the cheat sheet; the Shell refreshes it on any
// model change and on the minute, while it is showing.
class GlanceWindow : public Gtk::Window {
public:
    GlanceWindow();
    ~GlanceWindow() override;

    void set_source(const core::NodeSource* src, const core::TaskIndex* tasks);
    void set_mail_to(const std::string& to);
    void set_save_dir(const std::string& dir) { m_save_dir = dir; }
    void show(Gtk::Window& parent);
    void refresh();   // no-op while hidden

    const core::Glance& current() const { return m_glance; }

    sigc::signal<void(core::NodeId)>& signal_goto() { return m_sig_goto; }
    // The address or the save folder changed -- the Shell keeps them in prefs.
    sigc::signal<void(std::string, std::string)>& signal_prefs() { return m_sig_prefs; }

private:
    void build();
    void fill();
    void on_copy();
    void on_email();
    void on_save();
    void say(const std::string& text);
    // s066b: the .ics as a FILE you can drag into a mail -- written fresh to
    // jot's cache each time a drag starts (or the chip is clicked), so it is
    // always the Glance on screen. Returns the path, "" on failure.
    std::string write_drag_file();

    const core::NodeSource* m_src   = nullptr;
    const core::TaskIndex*  m_tasks = nullptr;
    core::Glance            m_glance;
    std::string             m_save_dir;
    std::string             m_last_sig;   // what was drawn -- a minute tick that changes nothing redraws nothing

    widgets::Label          m_day;
    widgets::Label          m_summary;
    widgets::ScrolledWindow m_scroll;
    widgets::Box            m_column;
    widgets::Entry          m_to;
    widgets::Label          m_status;
    widgets::Button         m_copy, m_email, m_save;
    widgets::Box            m_chip;        // s066b: drag me into the mail
    widgets::Label          m_chip_name;
    bool                    m_dropped = false;

    sigc::signal<void(core::NodeId)>             m_sig_goto;
    sigc::signal<void(std::string, std::string)> m_sig_prefs;
};

}  // namespace jot
