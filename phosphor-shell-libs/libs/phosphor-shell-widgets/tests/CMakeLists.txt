# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# QtQuickTest harness for the Phosphor.Widgets atoms. The cases live in
# tst_*.qml; tst_widgets.cpp is the QUICK_TEST_MAIN entry point. The test
# links the static Phosphor.Widgets and Phosphor.Theme QML modules (both
# the module libraries and their plugin registrars) so the QML engine the
# runner spins up can resolve `import Phosphor.Widgets` / `Phosphor.Theme`.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickTest)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
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
add_executable(test_phosphor_shell_widgets tst_widgets.cpp)

target_compile_features(test_phosphor_shell_widgets PRIVATE cxx_std_20)

target_link_libraries(test_phosphor_shell_widgets
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickTest
        # Static QML modules ship both a library and a plugin registrar.
        # The registrar publishes the module to QQmlEngine at runtime;
        # without it the `import` fails. Link the widgets module + its
        # plugin, plus the theme module + its plugin (the atoms import
        # Phosphor.Theme).
        PhosphorShellWidgetsQml
        PhosphorShellWidgetsQmlplugin
        PhosphorThemeQml
        PhosphorThemeQmlplugin
)

add_test(
    NAME test_phosphor_shell_widgets
    COMMAND test_phosphor_shell_widgets -input ${CMAKE_CURRENT_SOURCE_DIR}
)
phosphor_apply_test_isolation(test_phosphor_shell_widgets)
set_property(TEST test_phosphor_shell_widgets APPEND PROPERTY LABELS "phosphorshellwidgets")
# Headless run: the QtQuickTest runner spins up a QQuickView, so it needs
# a platform plugin. offscreen keeps it display-server-free on CI.
phosphor_append_test_environment(test_phosphor_shell_widgets "QT_QPA_PLATFORM=offscreen")
