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
# on the CI runner. The test exercises the QThread + loop teardown path
# and the idempotency contract; live-daemon coverage is its own follow-
# up once a `pw-fixture` test fixture lands.
_phosphorservicepipewire_test(test_phosphorservicepipewire_smoke test_smoke.cpp)
