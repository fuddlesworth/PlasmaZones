# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.10 REQUIRED COMPONENTS Test)

add_executable(test_overlay_smoke
    test_overlay_smoke.cpp
)

target_link_libraries(test_overlay_smoke
    PRIVATE
        PhosphorOverlay::PhosphorOverlay
        Qt6::Test
)

add_test(NAME test_overlay_smoke COMMAND test_overlay_smoke)
set_tests_properties(test_overlay_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
