# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

function(_phosphorservicelock_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServiceLock::PhosphorServiceLock
    )
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target}
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
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
set_tests_properties(test_phosphorservicelock_statemachine
    PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        LABELS "phosphorservicelock"
)
