# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
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
# The test name is not the target name here, so both are passed.
phosphor_apply_test_isolation(test_contextresolver PhosphorContext::ContextResolver)
set_property(TEST PhosphorContext::ContextResolver APPEND PROPERTY LABELS "phosphorcontextresolver")
# No QT_QPA_PLATFORM=offscreen here, unlike most sibling lib tests:
# QTEST_GUILESS_MAIN runs on a QCoreApplication, so no platform plugin is
# ever loaded and headless runs need no help.
