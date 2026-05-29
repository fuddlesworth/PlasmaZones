# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

function(_phosphorserviceupower_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServiceUPower::PhosphorServiceUPower
    )
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target}
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            LABELS "phosphorserviceupower"
    )
endfunction()

# Smoke test: model role names + lifecycle on an idle host (no UPower
# on the bus, no devices to enumerate). The CI runners have no UPower,
# so this is what we can verify deterministically; live D-Bus coverage
# requires a fake org.freedesktop.UPower fixture and is its own
# follow-up.
_phosphorserviceupower_test(test_phosphorserviceupower_smoke test_smoke.cpp)
