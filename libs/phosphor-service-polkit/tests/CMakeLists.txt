# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

function(_phosphorservicepolkit_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServicePolkit::PhosphorServicePolkit
    )
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target}
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            LABELS "phosphorservicepolkit"
    )
endfunction()

# Smoke test: exercises the milestone-1 contract (QML-registration idempotency,
# the default object path, and inert construction / registration against a
# bogus session so no real polkitd registration can occur).
_phosphorservicepolkit_test(test_phosphorservicepolkit_smoke test_smoke.cpp)

# QML facade test: a real QQmlEngine imports the module and proves the
# imperative registration yields a usable facade (PolkitAgent instantiates and
# binds; the AuthRequest-typed activeRequest property is readable).
_phosphorservicepolkit_test(test_phosphorservicepolkit_qmlfacade test_qmlfacade.cpp)
