# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
function(_phosphorservicesession_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServiceSession::PhosphorServiceSession
    )
    add_test(NAME ${target} COMMAND ${target})
    phosphor_apply_test_isolation(${target})
    # APPEND, never a bare SET: `set_tests_properties(... PROPERTIES
    # ENVIRONMENT ...)` REPLACES the whole list and would drop the XDG
    # sandbox `phosphor_apply_test_isolation` just installed.
    phosphor_append_test_environment(${target} "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(${target} PROPERTIES
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
