# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# QtQuickTest harness for the Phosphor.Notifications toast framework.
# Cases live in tst_*.qml; tst_notifications.cpp is the QUICK_TEST_MAIN
# entry point. The test links the notifications module + the modules it
# imports (Phosphor.Widgets, Phosphor.Theme) and their plugin registrars.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickShapes QuickTest)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
add_executable(test_phosphor_shell_notifications tst_notifications.cpp)

target_compile_features(test_phosphor_shell_notifications PRIVATE cxx_std_20)

target_link_libraries(test_phosphor_shell_notifications
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickShapes
        Qt6::QuickTest
        PhosphorShellNotificationsQml
        PhosphorShellNotificationsQmlplugin
        PhosphorShellWidgetsQml
        PhosphorShellWidgetsQmlplugin
        PhosphorThemeQml
        PhosphorThemeQmlplugin
)

add_test(
    NAME test_phosphor_shell_notifications
    COMMAND test_phosphor_shell_notifications -input ${CMAKE_CURRENT_SOURCE_DIR}
)
phosphor_apply_test_isolation(test_phosphor_shell_notifications)

phosphor_append_test_environment(test_phosphor_shell_notifications "QT_QPA_PLATFORM=offscreen")
