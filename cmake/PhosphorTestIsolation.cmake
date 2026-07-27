# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# One place to apply the two isolations every PlasmaZones test needs.
#
# LGPL-2.1-or-later, matching `PhosphorLibTesting.cmake`: every consumer is an
# `LGPL` library's own test tree, and CLAUDE.md's test-license split exists so a
# library's build tree is not tainted with GPL.
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

# Beside this module rather than under `tests/unit`, so an LGPL library test tree
# does not reach into the GPL app test tree for it. (The conf file is LGPL for the
# same reason.)
#
# This does NOT make the module reachable from a standalone library configure.
# Every caller includes it as `${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake`,
# which in a standalone configure of e.g. libs/phosphor-fsloader resolves inside
# that library, where there is no `cmake/` directory — so the `include()` errors
# out before this path is consulted. A standalone build that wants the isolation
# has to guard its own include.
set(_phosphor_test_session_bus_conf "${CMAKE_CURRENT_LIST_DIR}/test-session-bus.conf")

# Apply the standard test isolation to an already-registered test.
#
#   phosphor_apply_test_isolation(<target> [<test-name>])
#
# Call AFTER add_test(). <test-name> defaults to <target>, which is the case for
# almost every caller; pass it explicitly when a tree registers its test under a
# name that is not the target name (e.g. a namespaced `Foo::Bar`). The two are
# genuinely different things — TEST_LAUNCHER is a TARGET property while ENVIRONMENT
# is a TEST property — and conflating them silently applied half the isolation.
function(phosphor_apply_test_isolation _target)
    if(NOT TARGET ${_target})
        message(FATAL_ERROR "phosphor_apply_test_isolation: '${_target}' is not a target")
    endif()
    set(_test_name "${_target}")
    if(ARGC GREATER 1)
        set(_test_name "${ARGV1}")
    endif()

    if(_phosphor_dbus_run_session AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.29
       AND EXISTS "${_phosphor_test_session_bus_conf}")
        set_target_properties(${_target} PROPERTIES
            TEST_LAUNCHER
            "${_phosphor_dbus_run_session};--config-file=${_phosphor_test_session_bus_conf};--")
    elseif(NOT _phosphor_isolation_warned)
        # Once per configure, not once per target.
        set(_phosphor_isolation_warned TRUE CACHE INTERNAL "")
        message(STATUS "PlasmaZones: dbus-run-session, CMake >= 3.29, or the test bus config is "
                       "unavailable — tests will use the ambient session bus")
    endif()

    # Per-target subdirectories, so a test that leaves state behind cannot
    # affect the next one and a parallel ctest run cannot interleave writes.
    #
    # CREATED at configure time. `QStandardPaths::writableLocation` does not
    # mkpath, so a test that writes without its own `QDir::mkpath` would fail
    # against a root that does not exist.
    set(_xdg "${CMAKE_BINARY_DIR}/test-xdg/${_target}")
    file(MAKE_DIRECTORY "${_xdg}/config" "${_xdg}/data" "${_xdg}/state" "${_xdg}/cache")

    # APPEND, and every caller must APPEND too. A plain
    # `set_tests_properties(... PROPERTIES ENVIRONMENT ...)` anywhere after this
    # call REPLACES the whole list rather than adding to it, which is how the
    # first version of this helper ended up setting four variables that never
    # reached a single test. `phosphor_append_test_environment` below exists so
    # a caller cannot get that wrong.
    set_property(TEST ${_test_name} APPEND PROPERTY ENVIRONMENT
        "XDG_CONFIG_HOME=${_xdg}/config"
        "XDG_DATA_HOME=${_xdg}/data"
        "XDG_STATE_HOME=${_xdg}/state"
        "XDG_CACHE_HOME=${_xdg}/cache")
endfunction()

# Pin XDG_DATA_DIRS to the target's own sandbox, for tests that SCAN for packs.
#
#   phosphor_pin_xdg_data_dirs(<target> [<test-name>])
#
# Deliberately OPT-IN rather than part of the isolation above, because the two
# groups of tests want opposite things. A pack-scanning test that leaves
# XDG_DATA_DIRS ambient enumerates whatever is installed in the developer's real
# /usr/share, so on a machine with PlasmaZones installed "are the BUNDLED packs
# valid?" silently becomes "are the bundled packs plus everything installed
# locally valid?" — a test that passes or fails depending on the host. But other
# tests legitimately need the real system dirs (the icon-theme resolver walks
# them, and drives the variable itself), so pinning it for everything would break
# them.
#
# Call this for anything that enumerates packs, shaders, layouts or algorithms
# from a search path. Leave it off otherwise.
function(phosphor_pin_xdg_data_dirs _target)
    set(_test_name "${_target}")
    if(ARGC GREATER 1)
        set(_test_name "${ARGV1}")
    endif()
    set(_xdg "${CMAKE_BINARY_DIR}/test-xdg/${_target}")
    file(MAKE_DIRECTORY "${_xdg}/data")
    set_property(TEST ${_test_name} APPEND PROPERTY ENVIRONMENT "XDG_DATA_DIRS=${_xdg}/data")
endfunction()

# Add per-test environment variables WITHOUT clobbering what
# `phosphor_apply_test_isolation` set. Use this instead of
# `set_tests_properties(... PROPERTIES ENVIRONMENT ...)`, which is a SET.
#
# The parameter is a ctest TEST NAME, not a target name — it feeds
# `set_property(TEST ...)`. Those differ wherever `add_test(NAME x COMMAND y)`
# uses x != y, and conflating them is what silently applied half the isolation
# the first time round.
function(phosphor_append_test_environment _test_name)
    set_property(TEST ${_test_name} APPEND PROPERTY ENVIRONMENT ${ARGN})
endfunction()
