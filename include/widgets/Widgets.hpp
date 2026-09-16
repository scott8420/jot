#pragma once
#include "widgets/Named.hpp"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/entry.h>
#include <gtkmm/expander.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/paned.h>
#include <gtkmm/popovermenu.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stack.h>
#include <gtkmm/stacksidebar.h>
#include <gtkmm/separator.h>
#include <gtkmm/textview.h>
#include <gtkmm/togglebutton.h>

// The wrapper aliases. Widen on demand, not for symmetry (CANON). Each line is
// the whole cost of making a GTK widget name-mandatory in jot's app code.
namespace jot::widgets {

using Box            = Named<Gtk::Box>;
using Button         = Named<Gtk::Button>;
using CheckButton    = Named<Gtk::CheckButton>;
using Entry          = Named<Gtk::Entry>;
using Expander       = Named<Gtk::Expander>;
using Image          = Named<Gtk::Image>;
using Label          = Named<Gtk::Label>;
using ListBox        = Named<Gtk::ListBox>;
using MenuButton     = Named<Gtk::MenuButton>;
using Paned          = Named<Gtk::Paned>;
using PopoverMenu    = Named<Gtk::PopoverMenu>;
using ScrolledWindow = Named<Gtk::ScrolledWindow>;
using Separator      = Named<Gtk::Separator>;
using Stack          = Named<Gtk::Stack>;
using StackSidebar   = Named<Gtk::StackSidebar>;
using TextView       = Named<Gtk::TextView>;
using ToggleButton   = Named<Gtk::ToggleButton>;

}  // namespace jot::widgets
