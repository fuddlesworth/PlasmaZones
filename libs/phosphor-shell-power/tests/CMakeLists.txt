# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test Qml Quick)

# Resolves every type the module is supposed to publish. A QML file dropped
# from QML_FILES or renamed still compiles and links cleanly and fails only at
# runtime, as "PowerMenu is not a type", with the menu simply never appearing.
# Nothing but a test catches that, which is the same rationale as the bar's
# tst_bar.
#
# A wrong QT_RESOURCE_ALIAS is NOT caught here, and deliberately is not worth
# a test: qt_add_qml_module GENERATES the qmldir from the alias, so the two can
# never disagree and the failure mode is unreachable. (Mutation-tested: an alias
# of BogusAlias.qml rewrites the qmldir to match and everything still loads.)
add_executable(test_phosphor_shell_power tst_power.cpp)
target_compile_features(test_phosphor_shell_power PRIVATE cxx_std_20)
target_link_libraries(test_phosphor_shell_power
    PRIVATE
        Qt6::Test
        Qt6::Qml
        Qt6::Quick
        PhosphorShellPower::PhosphorShellPowerQml
        PhosphorShellPowerQmlplugin
)
add_test(NAME test_phosphor_shell_power COMMAND test_phosphor_shell_power)
set_tests_properties(test_phosphor_shell_power
    PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        LABELS "phosphorshellpower"
        TIMEOUT 30
)

# Behavioural cover for the menu itself, against a fake SessionHost registered
# into Phosphor.Service.Session. Separate binary from tst_power because it
# registers a type under a URI the shell owns, which must not leak into the
# module-resolution test above.
add_executable(test_phosphor_power_menu tst_powermenu.cpp)
target_compile_features(test_phosphor_power_menu PRIVATE cxx_std_20)
target_link_libraries(test_phosphor_power_menu
    PRIVATE
        Qt6::Test
        Qt6::Qml
        Qt6::Quick
        PhosphorShellPower::PhosphorShellPowerQml
        PhosphorShellPowerQmlplugin
)
add_test(NAME test_phosphor_power_menu COMMAND test_phosphor_power_menu)
set_tests_properties(test_phosphor_power_menu
    PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        LABELS "phosphorshellpower"
        TIMEOUT 30
)

# Private session bus, as for the sibling shell-tier QML tests.
# PowerMenu already imports Phosphor.Service.Session, so the moment it becomes
# loadable in-test the engine starts touching the developer's real session bus
# with nothing in the diff to signal it.
# EXISTS-guarded like both siblings: this library declares itself
# standalone-configurable, and a hard include of a path outside the extracted
# subtree would fail at generate time after a `git subtree split`.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PhosphorTestIsolation.cmake")
    include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PhosphorTestIsolation.cmake)
    # Per-target rather than one list call: the helper also sets TEST
    # properties (ENVIRONMENT, TIMEOUT) keyed on a single test name.
    phosphor_apply_test_isolation(test_phosphor_shell_power)
    phosphor_apply_test_isolation(test_phosphor_power_menu)
endif()

