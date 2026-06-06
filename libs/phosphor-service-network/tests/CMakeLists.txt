# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

function(_phosphorservicenetwork_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServiceNetwork::PhosphorServiceNetwork
    )
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target}
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            LABELS "phosphorservicenetwork"
    )
endfunction()

# Smoke test: model role names + enum wire-constant pins + lifecycle on an
# idle host (no NetworkManager on the bus, no devices to enumerate). CI
# runners have no NetworkManager, so this is what we can verify
# deterministically; live D-Bus coverage requires a fake
# org.freedesktop.NetworkManager fixture and is its own follow-up.
_phosphorservicenetwork_test(test_phosphorservicenetwork_smoke test_smoke.cpp)
