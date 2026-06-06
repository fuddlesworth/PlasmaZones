# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

function(_phosphorservicesession_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServiceSession::PhosphorServiceSession
    )
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target}
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            LABELS "phosphorservicesession"
    )
endfunction()

# Smoke test: QML registration idempotency and inert construction without a
# live logind. The capability / action / inhibitor coverage lands alongside
# those milestones.
_phosphorservicesession_test(test_phosphorservicesession_smoke test_smoke.cpp)
