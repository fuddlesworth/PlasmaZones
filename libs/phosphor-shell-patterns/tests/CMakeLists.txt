# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.10 REQUIRED COMPONENTS Test)

add_executable(test_patterns
    test_patterns.cpp
)

target_link_libraries(test_patterns
    PRIVATE
        PhosphorShellPatterns::PhosphorShellPatterns
        Qt6::Test
)

add_test(NAME test_patterns COMMAND test_patterns)
set_tests_properties(test_patterns PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
