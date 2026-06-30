# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# QtQuickTest harness for the Phosphor.OSD framework. Cases live in
# tst_*.qml; tst_osd.cpp is the QUICK_TEST_MAIN entry point. The test
# links the OSD module + the modules it imports (Phosphor.Widgets,
# Phosphor.Theme) and their plugin registrars so the QML engine can
# resolve every import.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickShapes QuickTest)

add_executable(test_phosphor_shell_osd tst_osd.cpp)

target_compile_features(test_phosphor_shell_osd PRIVATE cxx_std_20)

target_link_libraries(test_phosphor_shell_osd
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickShapes
        Qt6::QuickTest
        # The OSD module + its plugin, plus the modules it imports and
        # their plugin registrars (Phosphor.Widgets pulls Phosphor.Theme).
        PhosphorShellOsdQml
        PhosphorShellOsdQmlplugin
        PhosphorShellWidgetsQml
        PhosphorShellWidgetsQmlplugin
        PhosphorThemeQml
        PhosphorThemeQmlplugin
)

add_test(
    NAME test_phosphor_shell_osd
    COMMAND test_phosphor_shell_osd -input ${CMAKE_CURRENT_SOURCE_DIR}
)

set_tests_properties(test_phosphor_shell_osd PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
