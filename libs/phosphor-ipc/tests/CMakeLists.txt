# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 REQUIRED COMPONENTS Test Network)

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
