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

# Smoke test: skeleton-level contract (QML type registration is callable and
# idempotent). Grows to cover sysfs enumeration, percentage math, the
# kbd_backlight filter, live watcher updates, and model role names against a
# fake sysfs tree as those land.
_phosphorservicebrightness_test(test_phosphorservicebrightness_smoke test_smoke.cpp)
