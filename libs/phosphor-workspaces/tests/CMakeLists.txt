# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# PhosphorWorkspaces unit tests. Headless — the WorkspaceMap is a pure value
# type and the WorkspaceReconciler's inputs are plain calls (no D-Bus), so the
# suites drive scripted notification sequences directly. GUILESS (the
# reconciler's ledger expiry and destroy debounce need an event dispatcher).

find_package(Qt6 REQUIRED COMPONENTS Test)

include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
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

pw_add_test(test_workspace_map test_workspace_map.cpp)
pw_add_test(test_workspace_reconciler test_workspace_reconciler.cpp)
pw_add_test(test_workspace_reconciler_overview test_workspace_reconciler_overview.cpp)
pw_add_test(test_workspace_ledger test_workspace_ledger.cpp)
pw_add_test(test_virtualdesktopmanager test_virtualdesktopmanager.cpp)
