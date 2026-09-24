# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 REQUIRED COMPONENTS Test Network)

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
function(phosphoripc_add_test name)
    add_executable(${name} ${name}.cpp)
    # Qt6::Network is propagated transitively by PhosphorIpc (PUBLIC, for
    # QLocalServer/QLocalSocket) — only the e2e/subscribe socket suites
    # actually use it, so it is not linked explicitly here.
    target_link_libraries(${name}
        PRIVATE
            PhosphorIpc::PhosphorIpc
            Qt6::Test
    )
    add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    phosphor_apply_test_isolation(${name})
    # All suites are QTEST_GUILESS_MAIN (QCoreApplication) — no QPA platform
    # needed; even the engine test's QQmlEngine runs headless. TIMEOUT 30
    # caps ctest at 30s per binary so a hung socket test can't wedge CI.
    set_tests_properties(${name} PROPERTIES
        TIMEOUT 30
        LABELS "phosphoripc"
    )
endfunction()

phosphoripc_add_test(test_phosphor_ipc_protocol)
phosphoripc_add_test(test_phosphor_ipc_router)
phosphoripc_add_test(test_phosphor_ipc_schema)
phosphoripc_add_test(test_phosphor_ipc_e2e)
phosphoripc_add_test(test_phosphor_ipc_subscribe)
phosphoripc_add_test(test_phosphor_ipc_engine)
