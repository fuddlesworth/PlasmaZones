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
# live logind. The capability / action / inhibitor coverage lives in the
# fake-logind behaviour test below.
_phosphorservicesession_test(test_phosphorservicesession_smoke test_smoke.cpp)

# QML facade: a real QQmlEngine imports Phosphor.Service.Session 1.0,
# instantiates SessionHost, and exercises the QML-visible surface (the
# Availability enum, the interactive property, the safe logout() invokable).
_phosphorservicesession_test(test_phosphorservicesession_qmlfacade test_qmlfacade.cpp)

# SessionHost behaviour against an in-process fake logind Manager + Session on
# the session bus: capability parsing, action routing + interactive flag,
# capability-gated refusal, inhibitor what/mode, the PrepareForSleep handshake,
# and lock / terminate / session-signal surfacing. QSKIPs without a session bus.
_phosphorservicesession_test(test_phosphorservicesession_sessionhost test_sessionhost.cpp)
