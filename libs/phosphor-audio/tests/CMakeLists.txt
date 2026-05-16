# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

add_executable(test_phosphoraudio
    test_phosphoraudio.cpp
)

target_link_libraries(test_phosphoraudio
    PRIVATE
        PhosphorAudio::PhosphorAudio
        Qt6::Test
)

add_test(NAME test_phosphoraudio COMMAND test_phosphoraudio)
