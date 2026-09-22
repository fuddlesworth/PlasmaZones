# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# QtQuickTest harness for the Phosphor.Polkit prompt. Cases live in
# tst_*.qml; tst_polkit.cpp is the QUICK_TEST_MAIN entry point. The test
# links the module + the modules it imports (Phosphor.Widgets,
# Phosphor.Theme) and their plugin registrars.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickShapes QuickTest)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
add_executable(test_phosphor_shell_polkit tst_polkit.cpp)

target_compile_features(test_phosphor_shell_polkit PRIVATE cxx_std_20)

target_link_libraries(test_phosphor_shell_polkit
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickShapes
        Qt6::QuickTest
        PhosphorShellPolkitQml
        PhosphorShellPolkitQmlplugin
        PhosphorShellWidgetsQml
        PhosphorShellWidgetsQmlplugin
        PhosphorThemeQml
        PhosphorThemeQmlplugin
)

add_test(
    NAME test_phosphor_shell_polkit
    COMMAND test_phosphor_shell_polkit -input ${CMAKE_CURRENT_SOURCE_DIR}
)
phosphor_apply_test_isolation(test_phosphor_shell_polkit)
set_property(TEST test_phosphor_shell_polkit APPEND PROPERTY LABELS "phosphorshellpolkit")
phosphor_append_test_environment(test_phosphor_shell_polkit "QT_QPA_PLATFORM=offscreen")
