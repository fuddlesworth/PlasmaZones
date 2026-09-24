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
function(_phosphorservicelock_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServiceLock::PhosphorServiceLock
    )
    add_test(NAME ${target} COMMAND ${target})
    phosphor_apply_test_isolation(${target})
    # APPEND, never a bare SET: `set_tests_properties(... PROPERTIES
    # ENVIRONMENT ...)` REPLACES the whole list and would drop the XDG
    # sandbox `phosphor_apply_test_isolation` just installed.
    phosphor_append_test_environment(${target} "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(${target} PROPERTIES
            LABELS "phosphorservicelock"
    )
endfunction()

# Smoke test: exercises the deterministic facade contract (QML-registration
# idempotency and inert construction with no live compositor). The PAM
# authenticator and lock state machine are exercised by the unit tests and the
# CLI demo as they land.
_phosphorservicelock_test(test_phosphorservicelock_smoke test_smoke.cpp)

# QML facade test: a real QQmlEngine imports the module and proves the imperative
# registration yields a usable facade (LockService instantiates, its supported /
# state / locked surface reads through QML, and the invokables run).
_phosphorservicelock_test(test_phosphorservicelock_qmlfacade test_qmlfacade.cpp)

# PAM authenticator unit test: links the lib and exercises the public
# PamAuthenticator's credential-free contract (configured service, fast-fail on
# bad input, the concurrency guard, and that a nonexistent user is rejected
# through the real off-thread PAM path). Authenticating a valid user needs real
# credentials and is left to the CLI demo.
_phosphorservicelock_test(test_phosphorservicelock_pam test_pamauthenticator.cpp)

# Lock state-machine unit test: compiles the internal state machine + seams
# directly (no lib link, no PAM, no compositor) and drives them with fakes, so
# the lock policy is exercised deterministically.
add_executable(test_phosphorservicelock_statemachine
    test_statemachine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/lockstatemachine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/isessionlock.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/iauthenticator.cpp
    # Listed so AUTOMOC generates the IAuthenticator metaobject: its source file
    # name (iauthenticator.cpp) does not match the public header (IAuthenticator.h).
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/PhosphorServiceLock/IAuthenticator.h
)
target_include_directories(test_phosphorservicelock_statemachine
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_BINARY_DIR}/..
)
target_link_libraries(test_phosphorservicelock_statemachine PRIVATE Qt6::Test)
add_test(NAME test_phosphorservicelock_statemachine COMMAND test_phosphorservicelock_statemachine)
phosphor_apply_test_isolation(test_phosphorservicelock_statemachine)
# APPEND, never a bare SET: `set_tests_properties(... PROPERTIES
# ENVIRONMENT ...)` REPLACES the whole list and would drop the XDG
# sandbox `phosphor_apply_test_isolation` just installed.
phosphor_append_test_environment(test_phosphorservicelock_statemachine "QT_QPA_PLATFORM=offscreen")
set_tests_properties(test_phosphorservicelock_statemachine PROPERTIES
    LABELS "phosphorservicelock"
)
