# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test Concurrent)

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

# PAM authenticator unit test: compiles the internal authenticator sources
# directly (they are not exported) and links PAM + Concurrent, so it needs no
# compositor and the internal classes stay unexported. Exercises the
# credential-free contract; authenticating a valid user is left to the CLI demo.
add_executable(test_phosphorservicelock_pam
    test_pamauthenticator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/pamauthenticator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/iauthenticator.cpp
)
target_include_directories(test_phosphorservicelock_pam
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_BINARY_DIR}/..
        ${PAM_INCLUDE_DIR}
)
target_link_libraries(test_phosphorservicelock_pam
    PRIVATE
        Qt6::Test
        Qt6::Concurrent
        ${PAM_LIBRARY}
)
add_test(NAME test_phosphorservicelock_pam COMMAND test_phosphorservicelock_pam)
set_tests_properties(test_phosphorservicelock_pam
    PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        LABELS "phosphorservicelock"
)
