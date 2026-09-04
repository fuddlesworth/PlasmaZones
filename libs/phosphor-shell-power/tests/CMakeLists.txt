# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test Qml Quick)

# Private session bus, as for the sibling shell-tier QML tests.
# PowerMenu already imports Phosphor.Service.Session, so the moment it becomes
# loadable in-test the engine starts touching the developer's real session bus
# with nothing in the diff to signal it.
# EXISTS-guarded like both siblings: this library declares itself
# standalone-configurable, and a hard include of a path outside the extracted
# subtree would fail at generate time after a `git subtree split`. Included at
# the TOP so the per-test environment can route through
# phosphor_append_test_environment — a bare `set_tests_properties(...
# ENVIRONMENT ...)` after the isolation call would replace the whole list and
# drop the XDG sandbox.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PhosphorTestIsolation.cmake")
    include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PhosphorTestIsolation.cmake)
    set(_phosphorshellpower_have_isolation TRUE)
else()
    set(_phosphorshellpower_have_isolation FALSE)
endif()

# Isolation + environment through one seam; see libs/phosphor-shell/tests for
# the rationale on both branches.
function(phosphorshellpower_isolate_test name)
    if(_phosphorshellpower_have_isolation)
        phosphor_apply_test_isolation(${name})
        phosphor_append_test_environment(${name} "QT_QPA_PLATFORM=offscreen")
    else()
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
    # TIMEOUT set AFTER the isolation call, which only supplies its 120s
    # default when no TIMEOUT is set yet — last write wins.
    set_tests_properties(${name} PROPERTIES
        LABELS "phosphorshellpower"
        TIMEOUT 30
    )
endfunction()

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
phosphorshellpower_isolate_test(test_phosphor_shell_power)

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
phosphorshellpower_isolate_test(test_phosphor_power_menu)
