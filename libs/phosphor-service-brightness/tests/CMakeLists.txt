# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

function(_phosphorservicebrightness_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServiceBrightness::PhosphorServiceBrightness
    )
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target}
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            LABELS "phosphorservicebrightness"
    )
endfunction()

# Smoke test: exercises the library contract against a fake sysfs tree and an
# in-process fake logind session. Covers QML registration idempotency, sysfs
# enumeration with the kbd_backlight filter, percentage math with zero-max
# safety, the logind write path (including the late session-path binding), the
# external-display (DDC/CI) device surface, live watcher updates, and the
# model's add/remove/dataChanged transactions.
_phosphorservicebrightness_test(test_phosphorservicebrightness_smoke test_smoke.cpp)
