# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# QtQuickTest harness for Phosphor.Dashboard. Cases live in tst_*.qml;
# tst_dashboard.cpp is the QUICK_TEST_MAIN entry point. The test links
# the module + the modules it imports (Phosphor.Widgets, Phosphor.Theme)
# and their plugin registrars so the QML engine can resolve every import.
# No daemon and no PlacementMap singleton: the cases bind fakes.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickTest)

# Shared test isolation: a private D-Bus session (ShortcutCatalog opens a
# service watcher on construction, and nothing here may activate the
# INSTALLED daemon) plus a per-target XDG sandbox.
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
add_executable(test_phosphor_shell_dashboard tst_dashboard.cpp)

target_compile_features(test_phosphor_shell_dashboard PRIVATE cxx_std_20)

target_link_libraries(test_phosphor_shell_dashboard
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickTest
        PhosphorShellDashboardQml
        PhosphorShellDashboardQmlplugin
        PhosphorShellWidgetsQml
        PhosphorShellWidgetsQmlplugin
        PhosphorThemeQml
        PhosphorThemeQmlplugin
)

add_test(
    NAME test_phosphor_shell_dashboard
    COMMAND test_phosphor_shell_dashboard -input ${CMAKE_CURRENT_SOURCE_DIR}
)
phosphor_apply_test_isolation(test_phosphor_shell_dashboard)
set_property(TEST test_phosphor_shell_dashboard APPEND PROPERTY LABELS "phosphorshelldashboard")
phosphor_append_test_environment(test_phosphor_shell_dashboard "QT_QPA_PLATFORM=offscreen")
