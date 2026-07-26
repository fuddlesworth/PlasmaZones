# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# One place to apply the two isolations every PlasmaZones test needs.
#
# This exists because the same block used to be hand-copied into each library's
# test wrapper, and the copies diverged: one tree had the D-Bus launcher, four
# sibling wrappers in the SAME file did not, and none of the library trees had
# the XDG sandbox at all — while the comment in the one that did called the
# isolation a repo-wide invariant.
#
#   * D-Bus: without a private bus, a test that touches D-Bus reaches the real
#     session bus and ACTIVATES the installed daemon, which then holds the
#     test's stdout pipe open and hangs ctest after the test itself passed. The
#     config declares no service directories, so activation is impossible.
#
#   * XDG: without a sandbox, anything that resolves a config or data path
#     writes into the developer's real ~/.config/plasmazones and
#     ~/.local/share/plasmazones.
#
# Both are cheap and neither has a downside for a test that needs neither, so
# they are applied unconditionally rather than per-target. `dbus-run-session`
# and CMake >= 3.29 (for TEST_LAUNCHER) are optional: without them tests run on
# whatever bus the environment provides, which is the pre-isolation status quo.

find_program(_phosphor_dbus_run_session dbus-run-session)

# Apply the standard test isolation to an already-registered test target.
# Call AFTER add_test(NAME <target> ...), with NAME == the target name.
function(phosphor_apply_test_isolation _target)
    if(NOT TARGET ${_target})
        message(FATAL_ERROR "phosphor_apply_test_isolation: '${_target}' is not a target")
    endif()

    if(_phosphor_dbus_run_session AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
        set_target_properties(${_target} PROPERTIES
            TEST_LAUNCHER
            "${_phosphor_dbus_run_session};--config-file=${CMAKE_SOURCE_DIR}/tests/unit/test-session-bus.conf;--")
    endif()

    # Per-target subdirectories, so a test that leaves state behind cannot
    # affect the next one and a parallel ctest run cannot interleave writes.
    set(_xdg "${CMAKE_BINARY_DIR}/test-xdg/${_target}")
    set_property(TEST ${_target} APPEND PROPERTY ENVIRONMENT
        "XDG_CONFIG_HOME=${_xdg}/config"
        "XDG_DATA_HOME=${_xdg}/data"
        "XDG_STATE_HOME=${_xdg}/state"
        "XDG_CACHE_HOME=${_xdg}/cache")
endfunction()
