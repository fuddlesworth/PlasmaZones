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
    # LABELS so `ctest -L` can select this lib the way it can every sibling;
    # TIMEOUT because this is a QtQuickTest binary that spins a QML engine,
    # matching the other Quick-backed tests in the tree.
    LABELS "phosphorshellbar"
    TIMEOUT 30
)

# Same private-bus isolation as libs/phosphor-shell/tests. Nothing in the
# bar's QML touches D-Bus today, but this is a QtQuickTest that instantiates
# real QML, and the moment a bar file imports a Phosphor.Service.* type it
# would start reading the developer's session bus with no diff to signal it.
# EXISTS-guarded explicit path, not a bare `include(PhosphorTestIsolation)`:
# a bare module name resolves only through the root project's
# CMAKE_MODULE_PATH, so a standalone configure of this library (which the
# repo supports — each lib is meant to stay extractable via `git subtree
# split`) would hard-fail at generate time. Same shape as the
# PhosphorLibTesting include in the sibling libraries.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PhosphorTestIsolation.cmake")
    include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PhosphorTestIsolation.cmake)
    phosphor_apply_test_isolation(test_phosphor_shell_bar)
endif()
