# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
# CMAKE_CURRENT_LIST_DIR, not CMAKE_SOURCE_DIR: the latter is the repo root
# in-tree but this LIBRARY's directory in a standalone configure, so the include
# looked for libs/<lib>/cmake/PhosphorTestIsolation.cmake and hard-failed at
# generate time. The relative walk reaches the repo root from either. The EXISTS
# guard covers the remaining case, a genuine extracted `git subtree split` where
# the repo-root module is not in the subtree at all.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PhosphorTestIsolation.cmake")
    include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PhosphorTestIsolation.cmake)
else()
    # Stand-ins so the call sites below need no guard of their own.
    # phosphor_append_test_environment is reproduced EXACTLY, because it is
    # self-contained and because callers use it for things like
    # QT_QPA_PLATFORM=offscreen that the tests need to pass at all — a no-op
    # there would break them rather than merely un-isolate them. The isolation
    # helper degrades to nothing: its private D-Bus session and XDG sandbox need
    # module-level state that does not exist without the module.
    function(phosphor_apply_test_isolation _target)
    endfunction()
    function(phosphor_append_test_environment _test_name)
        set_property(TEST ${_test_name} APPEND PROPERTY ENVIRONMENT ${ARGN})
    endfunction()
endif()
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
