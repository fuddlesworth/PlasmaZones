# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
# CMAKE_CURRENT_LIST_DIR, not CMAKE_SOURCE_DIR: the latter is the repo root
# in-tree but this LIBRARY's directory in a standalone configure, so the include
# looked for libs/<lib>/cmake/PhosphorTestIsolation.cmake and hard-failed at
# generate time. The relative walk reaches the repo root from either. The EXISTS
# guard covers the remaining case, a genuine extracted `git subtree split` where
# the repo-root module is not in the subtree at all.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PhosphorTestIsolation.cmake")
    include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PhosphorTestIsolation.cmake)
else()
    # Stand-ins so the call sites below need no guard of their own.
    # phosphor_append_test_environment is reproduced EXACTLY, because it is
    # self-contained and because callers use it for things like
    # QT_QPA_PLATFORM=offscreen that the tests need to pass at all — a no-op
    # there would break them rather than merely un-isolate them. The isolation
    # helper degrades to nothing: its private D-Bus session and XDG sandbox need
    # module-level state that does not exist without the module.
    function(phosphor_apply_test_isolation _target)
    endfunction()
    function(phosphor_append_test_environment _test_name)
        set_property(TEST ${_test_name} APPEND PROPERTY ENVIRONMENT ${ARGN})
    endfunction()
endif()
function(_phosphorservicepipewire_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServicePipeWire::PhosphorServicePipeWire
    )
    add_test(NAME ${target} COMMAND ${target})
    phosphor_apply_test_isolation(${target})
    # APPEND, never a bare SET: `set_tests_properties(... PROPERTIES
    # ENVIRONMENT ...)` REPLACES the whole list and would drop the XDG
    # sandbox `phosphor_apply_test_isolation` just installed.
    phosphor_append_test_environment(${target} "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(${target} PROPERTIES
            LABELS "phosphorservicepipewire"
    )
endfunction()

# Smoke test: connection lifecycle without a guaranteed PipeWire daemon
# on the CI runner. The test exercises the QThread + loop teardown path,
# the idempotency contract, and the rest of the public surface; per
# docs/phosphor-shell-design/04-implementation-plan.md § 2.1 milestone 9,
# this is the canonical contract coverage for the lib.
#
# HOST AUDIO STATE WARNING (writeAPIsAreSafeForUnknownTargets):
#
# When the test binary runs on a host with a live PipeWire daemon AND
# a session manager (the usual dev-laptop case), the
# `writeAPIsAreSafeForUnknownTargets` slot in `test_smoke.cpp` issues a
# real mute / unmute toggle against the first Audio/Sink it finds and
# tries to restore the prior state on the way out. The restore is best-
# effort: if a parallel session manager (e.g. WirePlumber re-asserting
# defaults, a media player reacting to focus, KDE Plasma's audio
# applet) re-flips the mute state between the test's restore write and
# the echo wait, the host can be left briefly muted (or unmuted) at the
# moment the test exits. The test logs a QWARN in that case but does
# not fail.
#
# Opt-out env var:
#   PHOSPHOR_PW_TESTS_ALLOW_WRITE  live-write runs unless set to literal `0`
#                                  (the implementation treats any value
#                                  except `0` — including unset — as
#                                  enable). Under CTEST the variable is
#                                  pinned to `0` below, so an unattended
#                                  `ctest` sweep is always read-only; the
#                                  dev-default "exercise the full write
#                                  path" contract applies to running the
#                                  binary directly.
#
# Headless / CI runners are unaffected — the live-write branch only
# fires when a daemon is reachable AND a sink is present. The opt-out
# exists for dev hosts where:
#   - the user is recording audio / on a call and cannot tolerate even
#     a millisecond of unintended mute
#   - the user is running the test against a session manager known to
#     fight back hard against external mute writes, producing noisy
#     QWARNs on every test run
#   - the user wants to validate the no-daemon contract without
#     touching real audio state
_phosphorservicepipewire_test(test_phosphorservicepipewire_smoke test_smoke.cpp)
# Default the ctest run to read-only: an unattended `ctest` sweep must never
# toggle the host's real sink mute (the shared isolation module's "no downside"
# claim holds only with this pin). Developers wanting the full write path run
# the binary directly with PHOSPHOR_PW_TESTS_ALLOW_WRITE unset or =1.
phosphor_append_test_environment(test_phosphorservicepipewire_smoke "PHOSPHOR_PW_TESTS_ALLOW_WRITE=0")
