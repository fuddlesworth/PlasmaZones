# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.10 REQUIRED COMPONENTS Test Qml Quick)

# D-Bus isolation, applied through the shared helper below.
# test_phosphorshell_workspaces reaches the session bus through
# VirtualDesktopManager (org.kde.KWin), so without it the test would read the
# developer's live compositor and behave differently than on CI. It keeps both
# branches and skips whichever did not apply, so under ctest it deterministically
# exercises the no-compositor contract while running the binary directly on a
# KWin session still performs the real integration check.

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
#
# EXISTS-guarded explicit path, not a bare `include(PhosphorTestIsolation)`:
# a bare module name resolves only through the root project's
# CMAKE_MODULE_PATH, so a standalone configure of this library (which the
# repo supports — each lib is meant to stay extractable via `git subtree
# split`) would hard-fail at generate time. Same shape as the
# PhosphorLibTesting include in the sibling libraries. Included ONCE, at the
# top, so every add_test below can isolate right where it registers; a second
# include-and-apply block would double-append the XDG environment.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PhosphorTestIsolation.cmake")
    include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PhosphorTestIsolation.cmake)
    set(_phosphorshell_have_isolation TRUE)
else()
    set(_phosphorshell_have_isolation FALSE)
endif()

# Isolation + per-test environment through ONE seam. With the helper present
# the environment goes through phosphor_append_test_environment — a bare
# `set_tests_properties(... ENVIRONMENT ...)` REPLACES the whole list and
# would silently drop the XDG sandbox the isolation call just installed. On a
# standalone configure (helper absent) the raw SET is the only writer, so it
# is safe there and keeps the offscreen QPA the tests rely on.
function(phosphorshell_isolate_test name env)
    if(_phosphorshell_have_isolation)
        phosphor_apply_test_isolation(${name})
        phosphor_append_test_environment(${name} "${env}")
    else()
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "${env}")
    endif()
endfunction()

# Smoke test for the security boundary in ShellLoader (path-traversal /
# absolute-path / dotfile rejection). Adding more PhosphorShell tests
# in this directory keeps them under one CTest target prefix.
add_executable(test_phosphorshell_shellloader test_shellloader.cpp)
target_link_libraries(test_phosphorshell_shellloader
    PRIVATE
        Qt6::Test
        PhosphorShell::PhosphorShell
)
add_test(NAME test_phosphorshell_shellloader COMMAND test_phosphorshell_shellloader)
phosphorshell_isolate_test(test_phosphorshell_shellloader "QT_QPA_PLATFORM=offscreen")
set_tests_properties(test_phosphorshell_shellloader PROPERTIES
    LABELS "phosphorshell"
)

# Toplevels facade, covering the `activeToplevel` accessor's no-compositor
# contract. Needs no QML: the accessor is pure C++ and the offscreen QPA
# gives the inert-manager state the assertions are about.
add_executable(test_phosphorshell_toplevels test_toplevels.cpp)
target_link_libraries(test_phosphorshell_toplevels
    PRIVATE
        Qt6::Test
        PhosphorShell::PhosphorShell
)
add_test(NAME test_phosphorshell_toplevels COMMAND test_phosphorshell_toplevels)
phosphorshell_isolate_test(test_phosphorshell_toplevels "QT_QPA_PLATFORM=offscreen")
set_tests_properties(test_phosphorshell_toplevels PROPERTIES
    LABELS "phosphorshell"
)

# PanelWindow::visibleBand — the input-region arithmetic. Pure and static,
# so it needs no Wayland surface and no QML.
add_executable(test_phosphorshell_panelwindow_inputregion test_panelwindow_inputregion.cpp)
target_link_libraries(test_phosphorshell_panelwindow_inputregion
    PRIVATE
        Qt6::Test
        PhosphorShell::PhosphorShell
)
add_test(NAME test_phosphorshell_panelwindow_inputregion COMMAND test_phosphorshell_panelwindow_inputregion)
phosphorshell_isolate_test(test_phosphorshell_panelwindow_inputregion "QT_QPA_PLATFORM=offscreen")
set_tests_properties(test_phosphorshell_panelwindow_inputregion PROPERTIES
    LABELS "phosphorshell"
)

# PanelWindow property setters: the clamps (thickness floor of 1, shadow /
# carve floor of 0) and the change-gated notifies. ShellEngine feeds
# thickness() straight into visibleBand, so a broken clamp bypasses the
# band math's tested degenerate-input guard.
add_executable(test_phosphorshell_panelwindow_setters test_panelwindow_setters.cpp)
target_link_libraries(test_phosphorshell_panelwindow_setters
    PRIVATE
        Qt6::Test
        PhosphorShell::PhosphorShell
)
add_test(NAME test_phosphorshell_panelwindow_setters COMMAND test_phosphorshell_panelwindow_setters)
phosphorshell_isolate_test(test_phosphorshell_panelwindow_setters "QT_QPA_PLATFORM=offscreen")
set_tests_properties(test_phosphorshell_panelwindow_setters PROPERTIES
    LABELS "phosphorshell"
)

# Workspaces singleton + its list model. Talks to the real session bus, so
# it exercises the compositor branch on a KWin desktop and the
# no-compositor branch on a headless runner, skipping whichever did not
# apply.
add_executable(test_phosphorshell_workspaces test_workspaces.cpp)
target_link_libraries(test_phosphorshell_workspaces
    PRIVATE
        Qt6::Test
        PhosphorShell::PhosphorShell
)
add_test(NAME test_phosphorshell_workspaces COMMAND test_phosphorshell_workspaces)
phosphorshell_isolate_test(test_phosphorshell_workspaces "QT_QPA_PLATFORM=offscreen")
# TIMEOUT set AFTER the isolation call, which only supplies its 120s default
# when no TIMEOUT is set yet — last write wins.
set_tests_properties(test_phosphorshell_workspaces PROPERTIES
    LABELS "phosphorshell"
    TIMEOUT 30
)

# PerScreenPanels: the per-screen panel instantiator. Registers its types
# into a test-local URI, so it needs neither a shell file nor a layer-shell
# compositor.
add_executable(test_phosphorshell_perscreenpanels test_perscreenpanels.cpp)
target_link_libraries(test_phosphorshell_perscreenpanels
    PRIVATE
        Qt6::Test
        Qt6::Qml
        Qt6::Quick
        PhosphorShell::PhosphorShell
)
add_test(NAME test_phosphorshell_perscreenpanels COMMAND test_phosphorshell_perscreenpanels)
# TWO screens, via the offscreen plugin's configfile option. The default
# offscreen platform reports exactly one, and with one output "kept the
# delegate's own screen binding" and "assigned the row's screen" are the same
# pointer — so anExplicitScreenBindingIsNotOverwritten could only ever skip.
# It runs for real with this. Load-bearing: routed through the isolation seam
# so nothing can later wipe it with a bare ENVIRONMENT SET.
phosphorshell_isolate_test(test_phosphorshell_perscreenpanels
    "QT_QPA_PLATFORM=offscreen:configfile=${CMAKE_CURRENT_SOURCE_DIR}/two-screens.json")
set_tests_properties(test_phosphorshell_perscreenpanels PROPERTIES
    LABELS "phosphorshell"
    TIMEOUT 30
)

# PerScreen QML helper (Phase 1.5). Loads PerScreen.qml directly from
# the source tree via QQmlComponent::setData with a path-imported
# directory, so the test doesn't depend on the Phosphor.Shell static
# plugin being importable from a unit-test binary.
# perscreen_fakemodel.h declares a Q_OBJECT subclass of QAbstractListModel.
# AUTOMOC needs it listed as a source so the moc'd vtable / staticMetaObject
# get generated and linked into this target.
add_executable(test_phosphorshell_perscreen
    test_perscreen.cpp
    perscreen_fakemodel.h
)
target_link_libraries(test_phosphorshell_perscreen
    PRIVATE
        Qt6::Test
        Qt6::Qml
        Qt6::Quick
        PhosphorShell::PhosphorShell
)
# Source-dir path to the QML directory holding PerScreen.qml. Embedded
# at compile time so the test doesn't need install or relative-path
# resolution from $CWD.
target_compile_definitions(test_phosphorshell_perscreen
    PRIVATE
        PERSCREEN_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../qml/Phosphor/Shell"
)
add_test(NAME test_phosphorshell_perscreen COMMAND test_phosphorshell_perscreen)
phosphorshell_isolate_test(test_phosphorshell_perscreen "QT_QPA_PLATFORM=offscreen")
set_tests_properties(test_phosphorshell_perscreen PROPERTIES
    LABELS "phosphorshell"
    TIMEOUT 30
)
