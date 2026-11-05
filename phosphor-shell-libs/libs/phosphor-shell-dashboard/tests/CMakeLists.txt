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
# CMAKE_CURRENT_LIST_DIR, not CMAKE_SOURCE_DIR: the latter is the repo root
# in-tree but this LIBRARY's directory in a standalone configure, so the include
# looked for <tier>/libs/<lib>/cmake/PhosphorTestIsolation.cmake and hard-failed at
# generate time. The relative walk reaches the repo root from either. The EXISTS
# guard covers the remaining case, a genuine extracted `git subtree split` where
# the repo-root module is not in the subtree at all.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/PhosphorTestIsolation.cmake")
    include(${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/PhosphorTestIsolation.cmake)
else()
    # Stand-ins so the call sites below need no guard of their own.
    # phosphor_append_test_environment is reproduced EXACTLY, because it is
    # self-contained and because callers use it for things like
    # QT_QPA_PLATFORM=offscreen that the tests need to pass at all — a no-op
    # there would break them rather than merely un-isolate them. The isolation
    # helper degrades to nothing: its private D-Bus session and XDG sandbox need
    # module-level state that does not exist without the module.
    function(phosphor_apply_test_isolation _target)
    endfunction()
    function(phosphor_append_test_environment _test_name)
        set_property(TEST ${_test_name} APPEND PROPERTY ENVIRONMENT ${ARGN})
    endfunction()
endif()
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
