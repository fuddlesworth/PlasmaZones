# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

add_executable(test_contextresolver test_contextresolver.cpp)
target_link_libraries(test_contextresolver
    PRIVATE
        PhosphorContextResolver::PhosphorContextResolver
        Qt6::Core
        Qt6::Test
)
set_target_properties(test_contextresolver PROPERTIES
    AUTOMOC ON
    UNITY_BUILD OFF
)
add_test(NAME PhosphorContext::ContextResolver COMMAND test_contextresolver)
