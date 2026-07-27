# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
function(_phosphorservicebluetooth_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServiceBluetooth::PhosphorServiceBluetooth
    )
    add_test(NAME ${target} COMMAND ${target})
    phosphor_apply_test_isolation(${target})
    # APPEND, never a bare SET: `set_tests_properties(... PROPERTIES
    # ENVIRONMENT ...)` REPLACES the whole list and would drop the XDG
    # sandbox `phosphor_apply_test_isolation` just installed.
    phosphor_append_test_environment(${target} "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(${target} PROPERTIES
            LABELS "phosphorservicebluetooth"
    )
endfunction()

# Smoke test: QML registration idempotency, adapter/device read surfaces,
# model role-name pins, inert-host lifecycle, the agent request/response state
# machine, and a delayed-reply round-trip. The bus-dependent cases run against
# an in-process fake org.bluez over a session-bus loopback (QSKIP'd when no
# session bus), so coverage is deterministic without a real BlueZ daemon.
_phosphorservicebluetooth_test(test_phosphorservicebluetooth_smoke test_smoke.cpp)
