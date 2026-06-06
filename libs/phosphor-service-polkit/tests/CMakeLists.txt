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

# Smoke test: exercises the deterministic facade contract (QML-registration
# idempotency, the default object path, inert construction / registration against
# a bogus session so no real polkitd registration can occur, and the
# cancel / authenticate / respond no-op guards).
_phosphorservicepolkit_test(test_phosphorservicepolkit_smoke test_smoke.cpp)

# QML facade test: a real QQmlEngine imports the module and proves the
# imperative registration yields a usable facade (PolkitAgent instantiates and
# binds; its registered / activeRequest properties read through QML, with
# activeRequest null on a bare agent).
_phosphorservicepolkit_test(test_phosphorservicepolkit_qmlfacade test_qmlfacade.cpp)

# Decode unit test: drives the internal Details/Identity -> Qt-types decode with
# real polkit-qt values. It links PolkitQt6-1::Core directly (the lib links it
# PRIVATE, so it is not transitive) and includes the lib's src/ for the internal
# polkitdecode.h header.
find_package(PolkitQt6-1 REQUIRED)
_phosphorservicepolkit_test(test_phosphorservicepolkit_decode test_decode.cpp)
target_link_libraries(test_phosphorservicepolkit_decode PRIVATE PolkitQt6-1::Core)
target_include_directories(test_phosphorservicepolkit_decode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
