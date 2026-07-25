# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# QtQuickTest harness for the Phosphor.Bar framework. Cases live in
# tst_*.qml; tst_bar.cpp is the QUICK_TEST_MAIN entry point. The test
# links the bar module + the modules it imports (Phosphor.Widgets,
# Phosphor.Theme) and their plugin registrars so the QML engine can
# resolve every import.
#
# The Slot cases drive the registry-agnostic seam with a fake provider,
# so no Phosphor.Shell / Phosphor.Service.* runtime registration is needed
# (BarHost and the real widgets are never instantiated here). One case
# asserts a chip colour, which is the guard against the module's qmldir
# shadowing the Phosphor.Theme singleton.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickShapes QuickTest)

add_executable(test_phosphor_shell_bar tst_bar.cpp)

target_compile_features(test_phosphor_shell_bar PRIVATE cxx_std_20)

target_link_libraries(test_phosphor_shell_bar
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickShapes
        Qt6::QuickTest
        # The bar module + its plugin, plus the modules it imports and
        # their plugin registrars (Phosphor.Widgets pulls Phosphor.Theme).
        PhosphorShellBarQml
        PhosphorShellBarQmlplugin
        PhosphorShellWidgetsQml
        PhosphorShellWidgetsQmlplugin
        PhosphorThemeQml
        PhosphorThemeQmlplugin
)

add_test(
    NAME test_phosphor_shell_bar
    COMMAND test_phosphor_shell_bar -input ${CMAKE_CURRENT_SOURCE_DIR}
)

set_tests_properties(test_phosphor_shell_bar PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
