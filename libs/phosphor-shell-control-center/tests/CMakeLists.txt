# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# QtQuickTest harness for the Phosphor.ControlCenter surface. Cases live
# in tst_*.qml; tst_controlcenter.cpp is the QUICK_TEST_MAIN entry point.
# The test links the control-center module + the modules it imports
# (Phosphor.Widgets, Phosphor.Theme) and their plugin registrars so the
# QML engine can resolve every import.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickTest)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
# Guarded so a standalone subtree configure of this library still works
# without the top-level module, matching the launcher's sibling tests.
if(EXISTS "${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake")
    include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
endif()
add_executable(test_phosphor_shell_control_center tst_controlcenter.cpp)

target_compile_features(test_phosphor_shell_control_center PRIVATE cxx_std_20)

target_link_libraries(test_phosphor_shell_control_center
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickTest
        # The control-center module + its plugin, plus the modules it
        # imports and their plugin registrars (Phosphor.Widgets pulls
        # Phosphor.Theme).
        PhosphorShellControlCenterQml
        PhosphorShellControlCenterQmlplugin
        PhosphorShellWidgetsQml
        PhosphorShellWidgetsQmlplugin
        PhosphorThemeQml
        PhosphorThemeQmlplugin
)

add_test(
    NAME test_phosphor_shell_control_center
    COMMAND test_phosphor_shell_control_center -input ${CMAKE_CURRENT_SOURCE_DIR}
)
set_property(TEST test_phosphor_shell_control_center APPEND PROPERTY LABELS "phosphorshellcontrolcenter")
# Same fallback the launcher's tests carry: under a standalone configure the
# helper is absent, and the offscreen platform still has to be set or the test
# tries to open a real window.
if(COMMAND phosphor_apply_test_isolation)
    phosphor_apply_test_isolation(test_phosphor_shell_control_center)
    phosphor_append_test_environment(test_phosphor_shell_control_center "QT_QPA_PLATFORM=offscreen")
else()
    set_property(TEST test_phosphor_shell_control_center APPEND PROPERTY ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
