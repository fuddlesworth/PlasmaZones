# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.10 REQUIRED COMPONENTS Test)

add_executable(test_phosphordbus_client
    test_phosphordbus_client.cpp
)

target_link_libraries(test_phosphordbus_client
    PRIVATE
        PhosphorDBus::PhosphorDBus
        Qt6::DBus
        Qt6::Test
)

add_test(NAME test_phosphordbus_client COMMAND test_phosphordbus_client)
