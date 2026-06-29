# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# QtQuickTest harness for the Phosphor.Notifications toast framework.
# Cases live in tst_*.qml; tst_notifications.cpp is the QUICK_TEST_MAIN
# entry point. The test links the notifications module + the modules it
# imports (Phosphor.Widgets, Phosphor.Theme) and their plugin registrars.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickShapes QuickTest)

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

set_tests_properties(test_phosphor_shell_notifications PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
