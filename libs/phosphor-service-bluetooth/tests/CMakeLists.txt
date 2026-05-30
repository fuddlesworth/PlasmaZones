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

# Smoke test: skeleton-level contract (QML type registration is callable and
# idempotent). Grows to cover model role names + enum wire-constant pins +
# lifecycle on an idle host as those land. CI runners have no BlueZ on the
# bus, so the host/model coverage is deterministic-without-a-daemon; live
# D-Bus coverage requires a fake org.bluez fixture and is its own follow-up.
_phosphorservicebluetooth_test(test_phosphorservicebluetooth_smoke test_smoke.cpp)
