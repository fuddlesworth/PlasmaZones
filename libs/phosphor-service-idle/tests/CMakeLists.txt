# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

function(_phosphorserviceidle_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServiceIdle::PhosphorServiceIdle
    )
    add_test(NAME ${target} COMMAND ${target})
    # All suites are QTEST_GUILESS_MAIN (QCoreApplication) — no QPA platform
    # is loaded, so no offscreen ENVIRONMENT is needed.
    set_tests_properties(${target}
        PROPERTIES
            LABELS "phosphorserviceidle"
    )
endfunction()

# Smoke test: exercises the deterministic facade contract (QML-registration
# idempotency and inert construction with no live compositor). The full idle
# state machine needs a live compositor and is driven through the CLI demo.
_phosphorserviceidle_test(test_phosphorserviceidle_smoke test_smoke.cpp)

# Facade test: the deterministic IdleService surface (stage config round trip +
# sort, inhibition toggling, inert defaults) with no QPA / no compositor.
_phosphorserviceidle_test(test_phosphorserviceidle_facade test_facade.cpp)

# QML facade test: a real QQmlEngine imports the module and proves the imperative
# registration yields a usable facade (IdleService instantiates, its stages
# property writes from QML, and inhibit/release are invokable).
_phosphorserviceidle_test(test_phosphorserviceidle_qmlfacade test_qmlfacade.cpp)

# State-machine unit test: exercises the pure stage-ladder logic on a fake idle
# source. It compiles idlestatemachine.cpp directly (and includes the internal
# src/ headers) rather than linking the lib, so it needs no compositor and no
# phosphor-wayland; the internal classes stay unexported.
add_executable(test_phosphorserviceidle_statemachine
    test_statemachine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/idlestatemachine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/iidlesource.cpp
)
target_include_directories(test_phosphorserviceidle_statemachine
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(test_phosphorserviceidle_statemachine PRIVATE Qt6::Test)
add_test(NAME test_phosphorserviceidle_statemachine COMMAND test_phosphorserviceidle_statemachine)
set_tests_properties(test_phosphorserviceidle_statemachine
    PROPERTIES
        LABELS "phosphorserviceidle"
)

# Inhibition unit test: ref-count semantics on the aggregator, compiled directly
# (no lib link, no compositor).
add_executable(test_phosphorserviceidle_inhibition
    test_inhibition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/idleinhibitionmanager.cpp
)
target_include_directories(test_phosphorserviceidle_inhibition
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(test_phosphorserviceidle_inhibition PRIVATE Qt6::Test)
add_test(NAME test_phosphorserviceidle_inhibition COMMAND test_phosphorserviceidle_inhibition)
set_tests_properties(test_phosphorserviceidle_inhibition
    PROPERTIES
        LABELS "phosphorserviceidle"
)
