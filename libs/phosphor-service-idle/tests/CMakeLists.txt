# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
# CMAKE_CURRENT_LIST_DIR, not CMAKE_SOURCE_DIR: the latter is the repo root
# in-tree but this LIBRARY's directory in a standalone configure, so the include
# looked for libs/<lib>/cmake/PhosphorTestIsolation.cmake and hard-failed at
# generate time. The relative walk reaches the repo root from either. The EXISTS
# guard covers the remaining case, a genuine extracted `git subtree split` where
# the repo-root module is not in the subtree at all.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PhosphorTestIsolation.cmake")
    include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PhosphorTestIsolation.cmake)
else()
    # Stand-ins so the call sites below need no guard of their own.
    # phosphor_append_test_environment is reproduced EXACTLY, because it is
    # self-contained and because callers use it for things like
    # QT_QPA_PLATFORM=offscreen that the tests need to pass at all — a no-op
    # there would break them rather than merely un-isolate them. The isolation
    # helper degrades to nothing: its private D-Bus session and XDG sandbox need
    # module-level state that does not exist without the module.
    function(phosphor_apply_test_isolation _target)
    endfunction()
    function(phosphor_append_test_environment _test_name)
        set_property(TEST ${_test_name} APPEND PROPERTY ENVIRONMENT ${ARGN})
    endfunction()
endif()
function(_phosphorserviceidle_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServiceIdle::PhosphorServiceIdle
    )
    add_test(NAME ${target} COMMAND ${target})
    phosphor_apply_test_isolation(${target})
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
phosphor_apply_test_isolation(test_phosphorserviceidle_statemachine)
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
phosphor_apply_test_isolation(test_phosphorserviceidle_inhibition)
set_tests_properties(test_phosphorserviceidle_inhibition
    PROPERTIES
        LABELS "phosphorserviceidle"
)
