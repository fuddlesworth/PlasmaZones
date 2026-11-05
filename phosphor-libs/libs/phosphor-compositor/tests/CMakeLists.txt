# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 REQUIRED COMPONENTS Test DBus)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox. CMAKE_CURRENT_LIST_DIR, not CMAKE_SOURCE_DIR: the latter is the repo
# root in-tree but this LIBRARY's directory in a standalone configure. The
# relative walk reaches the repo root from either, and the EXISTS guard covers a
# genuine extracted `git subtree split` where the module is not in the subtree.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/PhosphorTestIsolation.cmake")
    include(${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/PhosphorTestIsolation.cmake)
else()
    # Stand-ins so the call sites below need no guard of their own.
    function(phosphor_apply_test_isolation _target)
    endfunction()
endif()

# The private bus matters more here than for most suites: the test owns the
# daemon's well-known name with a fake, and on the ambient session bus it would
# either meet the real daemon (the test then skips) or stand in for it in the
# developer's own session.
add_executable(pcomp_test_daemonclient
    test_daemonclient.cpp
)

target_link_libraries(pcomp_test_daemonclient
    PRIVATE
        PhosphorCompositor::PhosphorCompositor
        Qt6::DBus
        Qt6::Test
)

add_test(NAME pcomp_test_daemonclient COMMAND pcomp_test_daemonclient)
phosphor_apply_test_isolation(pcomp_test_daemonclient)
set_property(TEST pcomp_test_daemonclient APPEND PROPERTY LABELS "phosphorcompositor")
# No QT_QPA_PLATFORM=offscreen: QTEST_GUILESS_MAIN runs on a QCoreApplication,
# so no platform plugin is ever loaded.
