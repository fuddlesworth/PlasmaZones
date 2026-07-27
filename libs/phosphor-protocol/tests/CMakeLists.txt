# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.10 REQUIRED COMPONENTS Test)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
add_executable(test_phosphorprotocol
    test_phosphorprotocol.cpp
)

target_link_libraries(test_phosphorprotocol
    PRIVATE
        PhosphorProtocol::PhosphorProtocol
        Qt6::Test
)

add_test(NAME test_phosphorprotocol COMMAND test_phosphorprotocol)
phosphor_apply_test_isolation(test_phosphorprotocol)
set_property(TEST test_phosphorprotocol APPEND PROPERTY LABELS "phosphorprotocol")
# No QT_QPA_PLATFORM=offscreen here, unlike most sibling lib tests:
# QTEST_GUILESS_MAIN runs on a QCoreApplication, so no platform plugin is
# ever loaded and headless runs need no help.
