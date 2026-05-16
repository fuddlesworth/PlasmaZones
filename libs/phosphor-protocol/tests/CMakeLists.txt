# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

add_executable(test_phosphorprotocol
    test_phosphorprotocol.cpp
)

target_link_libraries(test_phosphorprotocol
    PRIVATE
        PhosphorProtocol::PhosphorProtocol
        Qt6::Test
)

add_test(NAME test_phosphorprotocol COMMAND test_phosphorprotocol)
