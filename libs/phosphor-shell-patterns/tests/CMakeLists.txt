# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.10 REQUIRED COMPONENTS Test)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
add_executable(test_patterns
    test_patterns.cpp
)

target_link_libraries(test_patterns
    PRIVATE
        PhosphorShellPatterns::PhosphorShellPatterns
        Qt6::Test
)

add_test(NAME test_patterns COMMAND test_patterns)
phosphor_apply_test_isolation(test_patterns)
phosphor_append_test_environment(test_patterns "QT_QPA_PLATFORM=offscreen")
