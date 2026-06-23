# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

function(_phosphorservicepipewire_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServicePipeWire::PhosphorServicePipeWire
    )
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target}
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
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
#                                  enable, matching the dev-default
#                                  "exercise the full write path on
#                                  developer hosts" contract).
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
