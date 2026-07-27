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
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
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
