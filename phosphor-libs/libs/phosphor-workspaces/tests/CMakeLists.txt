# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 REQUIRED COMPONENTS Test DBus)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
# CMAKE_CURRENT_LIST_DIR, not CMAKE_SOURCE_DIR: the latter is the repo root
# in-tree but this LIBRARY's directory in a standalone configure, so the include
# looked for <tier>/libs/<lib>/cmake/PhosphorTestIsolation.cmake and hard-failed at
# generate time. The relative walk reaches the repo root from either. The EXISTS
# guard covers the remaining case, a genuine extracted `git subtree split` where
# the repo-root module is not in the subtree at all.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/PhosphorTestIsolation.cmake")
    include(${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/PhosphorTestIsolation.cmake)
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

# The workspace-map and reconciler suites. Headless: the WorkspaceMap is a pure
# value type and the WorkspaceReconciler's inputs are plain calls (no D-Bus), so
# the suites drive scripted notification sequences directly. GUILESS, because
# the reconciler's ledger expiry and destroy debounce need an event dispatcher.
function(pw_add_test _name)
    add_executable(${_name} ${ARGN})
    set_target_properties(${_name} PROPERTIES AUTOMOC ON)
    target_compile_features(${_name} PRIVATE cxx_std_20)
    target_link_libraries(${_name}
        PRIVATE
            Qt6::Test
            Qt6::Core
            PhosphorWorkspaces::PhosphorWorkspaces
    )
    add_test(NAME ${_name} COMMAND ${_name})
    phosphor_apply_test_isolation(${_name})
    set_tests_properties(${_name} PROPERTIES LABELS "phosphorworkspaces")
endfunction()

pw_add_test(pw_test_workspace_map test_workspace_map.cpp)
pw_add_test(pw_test_workspace_reconciler test_workspace_reconciler.cpp)
pw_add_test(pw_test_workspace_ledger test_workspace_ledger.cpp)
pw_add_test(pw_test_virtualdesktopmanager test_virtualdesktopmanager.cpp)

# The ActivityManager suite drives the manager against a fake activity service,
# so it has nothing to test in a build without the activities library.
if(TARGET Plasma::Activities OR TARGET KF6::Activities)
    # The private bus matters more here than for most suites: the test registers a
    # fake org.kde.ActivityManager, and on the ambient session bus it would either
    # meet the real kactivitymanagerd (the test then skips) or stand in for it in
    # the developer's own session.
    add_executable(pw_test_activitymanager
        test_activitymanager.cpp
    )

    target_link_libraries(pw_test_activitymanager
        PRIVATE
            PhosphorWorkspaces::PhosphorWorkspaces
            Qt6::DBus
            Qt6::Test
    )

    add_test(NAME pw_test_activitymanager COMMAND pw_test_activitymanager)
    phosphor_apply_test_isolation(pw_test_activitymanager)
    set_property(TEST pw_test_activitymanager APPEND PROPERTY LABELS "phosphorworkspaces")
    # No QT_QPA_PLATFORM=offscreen: QTEST_GUILESS_MAIN runs on a QCoreApplication,
    # so no platform plugin is ever loaded.
endif()
