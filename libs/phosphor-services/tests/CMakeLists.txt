# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

# Smoke test that the library links and the singletons hand out non-null
# instances. Full coverage (icon resolver + model + dbusmenu) lives in
# task #10 and goes in this same directory.
add_executable(test_phosphorservices_smoke test_smoke.cpp)
target_link_libraries(test_phosphorservices_smoke
    PRIVATE
        Qt6::Test
        PhosphorServices::PhosphorServices
)
add_test(NAME test_phosphorservices_smoke COMMAND test_phosphorservices_smoke)
set_tests_properties(test_phosphorservices_smoke
    PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        LABELS "phosphorservices"
)
