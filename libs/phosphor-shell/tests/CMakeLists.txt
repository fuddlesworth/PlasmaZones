# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.10 REQUIRED COMPONENTS Test)

# Smoke test for the security boundary in ShellLoader (path-traversal /
# absolute-path / dotfile rejection). Adding more PhosphorShell tests
# in this directory keeps them under one CTest target prefix.
add_executable(test_phosphorshell_shellloader test_shellloader.cpp)
target_link_libraries(test_phosphorshell_shellloader
    PRIVATE
        Qt6::Test
        PhosphorShell::PhosphorShell
)
add_test(NAME test_phosphorshell_shellloader COMMAND test_phosphorshell_shellloader)
set_tests_properties(test_phosphorshell_shellloader
    PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        LABELS "phosphorshell"
)
