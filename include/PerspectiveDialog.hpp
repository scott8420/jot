#pragma once
#include "widgets/Widgets.hpp"

#include <gtkmm/window.h>

#include <functional>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// PerspectiveDialog -- "Save as Perspective..." (s038b).
//
// One field: the name. Under it, what is being saved -- the query, and how
// jot reads it -- so the name is given to something you can see. A name that
// is already taken says so on the button ("Replace"), because saving over a
// perspective is a choice and should not be a surprise.
//
// IT DECIDES NOTHING: the Shell saves (core::perspective_save) when Done fires.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot {

class PerspectiveDialog : public Gtk::Window {
public:
    using Done  = std::function<void(const std::string& name)>;
    using Taken = std::function<bool(const std::string& name)>;

    PerspectiveDialog(Gtk::Window& parent, const std::string& query, const std::string& reads,
                      const std::string& suggested, Taken taken, Done done);
    ~PerspectiveDialog() override;

private:
    void update();
    void accept();

    Taken m_taken;
    Done  m_done;
    widgets::Label  m_heading;
    widgets::Entry  m_name;
    widgets::Label  m_query;
    widgets::Button m_save;
};

}  // namespace jot
