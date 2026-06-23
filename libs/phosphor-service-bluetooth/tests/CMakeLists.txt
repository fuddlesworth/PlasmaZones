# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

function(_phosphorservicebluetooth_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServiceBluetooth::PhosphorServiceBluetooth
    )
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target}
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            LABELS "phosphorservicebluetooth"
    )
endfunction()

# Smoke test: QML registration idempotency, adapter/device read surfaces,
# model role-name pins, inert-host lifecycle, the agent request/response state
# machine, and a delayed-reply round-trip. The bus-dependent cases run against
# an in-process fake org.bluez over a session-bus loopback (QSKIP'd when no
# session bus), so coverage is deterministic without a real BlueZ daemon.
_phosphorservicebluetooth_test(test_phosphorservicebluetooth_smoke test_smoke.cpp)
