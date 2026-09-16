cmake_minimum_required(VERSION 3.20)
project(jot VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED gtkmm-4.0)
find_package(spdlog REQUIRED)

# ── libecal -- the desktop projection, and the FIRST dependency outside
# gtkmm/spdlog (s009). NOT REQUIRED, deliberately: without it the app builds
# whole and Desktop.cpp compiles its no-op half, so a machine with no
# evolution-data-server-devel loses one optional feature rather than the build.
# Fedora: evolution-data-server-devel. Debian/Ubuntu: libecal2.0-dev
# libedataserver1.2-dev.
# JOT_DESKTOP=OFF forces the no-libecal path even on a machine that HAS it.
# That is not a user setting -- it exists so both halves of the #ifdef in
# Desktop.cpp get compiled before a ship. s009 shipped a fallback branch that
# had never been compiled by anybody, because the sandbox had libecal and so
# only ever built the other half. A branch nothing compiles is a branch that is
# broken and does not know it.
option(JOT_DESKTOP "Build the desktop projection (needs libecal)" ON)

if(JOT_DESKTOP)
    pkg_check_modules(ECAL libecal-2.0 libedataserver-1.2)
endif()
if(ECAL_FOUND)
    message(STATUS "jot: desktop projection ON (libecal ${ECAL_libecal-2.0_VERSION})")
elseif(JOT_DESKTOP)
    message(WARNING
        "jot: desktop projection OFF -- libecal not found.\n"
        "     The app builds without it; 'Show dated todos on the desktop'\n"
        "     will be greyed out.\n"
        "     Fedora:        sudo dnf install evolution-data-server-devel\n"
        "     Debian/Ubuntu: sudo apt install libecal2.0-dev libedataserver1.2-dev")
else()
    message(STATUS "jot: desktop projection OFF (JOT_DESKTOP=OFF)")
endif()

# ── jot_core -- the GTK-free core (the model side of the one real seam) ──────
# No gtkmm on this library's link line, BY DESIGN: a stray UI include under
# core/ fails to compile, so the seam is enforced by the build graph and the
# core stays headless-testable. (docs/two-layer-seam.md.)
add_library(jot_core STATIC
    # The node model + the NodeSource seam the whole surface reads through.
    # GTK-free on purpose: the tree, the editor and drag-and-drop can only
    # reach the model through this interface (docs: CANON, fixture-through-seam).
    src/core/Nodes.cpp
    # The markdown scan behind the body pane (s004, D5). GTK-free so the whole
    # "what is this line and which bytes are syntax" question is answered under
    # test, leaving the editor only "apply a tag where the scan says to".
    src/core/Markdown.cpp
    src/core/Recents.cpp
    # The cold-capture spool. In core because the question it answers -- what is
    # a thought that was written down while the app was not running -- is a data
    # question, and because the encode/decode pair has to be under the selftest:
    # the spool file is the ONLY copy of what the user typed until a drain
    # succeeds, so a parse that quietly returns nothing would be a lost note.
    src/core/Pending.cpp
    # Prefs -- the layout state the drawer toggles write. A SECOND pump on the
    # Recents shape rather than a settings framework: four fields do not earn a
    # GSettings schema, and a schema would turn "run the build" into "install".
    src/core/Prefs.cpp
    src/core/Lifecycle.cpp
    # Who points at whom. The backlink half of requirement 8 cannot be computed
    # from the note in front of you, so it is an index -- and an index's real
    # failure is going STALE, which is only visible from under the selftest.
    src/core/Links.cpp
    # Persistence: a NodeSource over a jots folder (jot.json + notes/<uuid>.md).
    # Encode and decode adjacent, so a write cannot skew from its read.
    src/core/Project.cpp
    # The GTD engine: availability derived from (parent status, sibling order,
    # done, defer) and the index that finds the todos. Pure and GTK-free BECAUSE
    # a wrong availability answer is invisible from the surface -- a task wrongly
    # hidden is a task you do not do, and nothing on screen would say so.
    src/core/Tasks.cpp
    # WHAT the desktop is told, decided in core and nowhere else (s009). The
    # backend that writes it is Desktop.cpp, over in the app target with
    # libecal; this half is a pure function of the model and an instant, so it
    # is a selftest away from being wrong in silence on someone else's desktop.
    src/core/Projection.cpp
    src/core/Notify.cpp
    src/core/TextMap.cpp
    # The SHORTCUT-REGISTRY paradigm: one list, two consumers (App wires accels
    # from it, ShortcutsDialog renders from it) so a key and its advertised row
    # can't drift. GTK-free, so it lives in core and the selftest guards it
    # headless -- a pure test proves no two chords collide.
    src/core/Shortcuts.cpp
    # s014. WHAT a global capture shortcut is: which chords are fit to be grabbed
    # desktop-wide, and the list algebra for adding jot's slot to -- and taking
    # it out of -- gnome-settings-daemon's custom-keybindings WITHOUT disturbing
    # anyone else's entries. In core because that list belongs to another
    # program and to the user, so the operations on it have to be under test:
    # the expensive failure is not a hotkey that does not fire, it is somebody
    # else's shortcuts dropped on the way past.
    src/core/Hotkey.cpp
)
target_include_directories(jot_core PUBLIC include)
# Vendored nlohmann/json (header-only, MIT), PRIVATE -- an implementation detail
# of the pump, no consumer inherits the include (CANON: surprise-free build).
target_include_directories(jot_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# ── Headless selftest -- exercises the core with no display ────────────────────
add_executable(jot_selftest src/selftest.cpp)
target_link_libraries(jot_selftest PRIVATE jot_core)

# ── jot_desktop_probe -- the s009 instrument, built only with libecal ────────
# Writes a fixture projection into EDS through the real backend and then asks
# the list gnome-shell-calendar-server's own live query. No GTK: it links
# jot_core + Desktop.cpp + Log.cpp and nothing else, so it runs under
# dbus-run-session with no display. See tools/desktop_probe.cpp for how to run
# it against a throwaway data dir.
if(ECAL_FOUND)
    add_executable(jot_desktop_probe tools/desktop_probe.cpp src/Desktop.cpp src/Log.cpp)
    target_include_directories(jot_desktop_probe PRIVATE include ${ECAL_INCLUDE_DIRS})
    target_compile_definitions(jot_desktop_probe PRIVATE JOT_HAS_ECAL=1)
    target_link_libraries(jot_desktop_probe PRIVATE jot_core spdlog::spdlog ${ECAL_LIBRARIES})
    target_link_directories(jot_desktop_probe PRIVATE ${ECAL_LIBRARY_DIRS})
endif()

# ── App resources (gresource) -- the RESOURCE-PIPELINE paradigm ────────────────
# glib-compile-resources embeds resources/jot.gresource.xml into a C source;
# App::on_activate registers it and points the icon theme at the bundle so
# set_icon_name("jot-logo-symbolic") resolves out of the binary. The generated
# .c compiles as C (project enables C above) so jot_resources_get_resource()
# has C linkage, matching the extern "C" prototype in App.cpp. Adding an icon:
# drop the .svg in resources/icons/scalable/apps/, list it in the .xml, rebuild.
# (docs/resources-and-dialog-lifetime.md)
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
set(JOT_GRESOURCE_XML "${CMAKE_CURRENT_SOURCE_DIR}/resources/jot.gresource.xml")
set(JOT_RESOURCES_C   "${CMAKE_CURRENT_BINARY_DIR}/jot-resources.c")
file(GLOB JOT_RESOURCE_FILES
     "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/scalable/apps/*.svg")
add_custom_command(
    OUTPUT  "${JOT_RESOURCES_C}"
    COMMAND "${GLIB_COMPILE_RESOURCES}"
            --target "${JOT_RESOURCES_C}"
            --generate-source
            --c-name jot_resources
            jot.gresource.xml
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    DEPENDS "${JOT_GRESOURCE_XML}" ${JOT_RESOURCE_FILES}
    COMMENT "Compiling gresource bundle (jot-resources.c)"
    VERBATIM)

# ── jot -- the GTK app ───────────────────────────────────────────────────────
# The Shell is a SPLIT CLASS across TUs by category (see the index in
# include/Shell.hpp); all parts compile into the one jot::Shell.
add_executable(jot
    ${JOT_RESOURCES_C}
    src/main.cpp
    src/App.cpp
    # Follows the desktop's light/dark preference (GTK4 will not do it for us
    # and we do not link libadwaita). Needs no new dependency: the D-Bus and
    # GSettings calls are GIO, which gtkmm-4.0 already brings in.
    src/Appearance.cpp
    src/Shell.cpp
    # jot's own surfaces. Both read a core::NodeSource and nothing else; the
    # fixture store is swapped for the real one in Shell without touching them.
    src/TreePane.cpp
    src/TreePane_dnd.cpp
    src/EditorPane.cpp
    # The third pane. Reads the NodeSource and the link index, writes nothing --
    # every row is derived, so there is no state here to fall out of step.
    src/DrawerPane.cpp
    # The report. Groups by parent because the parent's note is the WHY -- the
    # one thing a task app with a scratch-field "note" cannot put anywhere.
    src/TodayPane.cpp
    src/Shell_zones.cpp
    src/Shell_bindings.cpp
    src/Shell_handlers.cpp
    src/Shell_helpers.cpp
    src/AboutWindow.cpp
    # Naming a jots folder: new, save-the-scratch-buffer, and rename. One
    # dialog, three occasions -- three would drift.
    # The only file in jot that includes libecal. Compiles either way: without
    # the dependency it is a complete no-op that reports itself unavailable.
    src/Desktop.cpp
    src/JotsFolderDialog.cpp
    src/ShortcutsDialog.cpp
    # s014: the preferences window, and the GSettings half of the capture hotkey
    # (core/Hotkey.cpp decides what to write; this performs it).
    src/PreferencesWindow.cpp
    src/Keybinding.cpp
    src/Registry.cpp
    src/Log.cpp
)
target_include_directories(jot PRIVATE include ${GTKMM_INCLUDE_DIRS})
target_link_libraries(jot     PRIVATE ${GTKMM_LIBRARIES} jot_core spdlog::spdlog)
target_link_directories(jot   PRIVATE ${GTKMM_LIBRARY_DIRS})
target_compile_options(jot    PRIVATE ${GTKMM_CFLAGS_OTHER})

if(ECAL_FOUND)
    target_compile_definitions(jot PRIVATE JOT_HAS_ECAL=1)
    target_include_directories(jot PRIVATE ${ECAL_INCLUDE_DIRS})
    target_link_libraries(jot      PRIVATE ${ECAL_LIBRARIES})
    target_link_directories(jot    PRIVATE ${ECAL_LIBRARY_DIRS})
endif()
