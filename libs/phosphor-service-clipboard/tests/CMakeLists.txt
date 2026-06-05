# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

function(_phosphorserviceclipboard_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServiceClipboard::PhosphorServiceClipboard
    )
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target}
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            LABELS "phosphorserviceclipboard"
    )
endfunction()

# Smoke test: exercises the deterministic facade contract (QML-registration
# idempotency and inert construction with no live compositor). The history
# model, persistence, and live clipboard read / write are exercised by the unit
# tests and the CLI demo as they land.
_phosphorserviceclipboard_test(test_phosphorserviceclipboard_smoke test_smoke.cpp)
