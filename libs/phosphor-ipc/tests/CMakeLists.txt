# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 REQUIRED COMPONENTS Test Network)

function(phosphoripc_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name}
        PRIVATE
            PhosphorIpc::PhosphorIpc
            Qt6::Test
            Qt6::Network
    )
    add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    # offscreen QPA: even non-GUI tests pull QGuiApplication transitively
    # through Qt::Test on some Qt builds. TIMEOUT 30 caps ctest at 30s
    # per binary so a hung socket test can't wedge CI.
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 30
    )
endfunction()

phosphoripc_add_test(test_phosphor_ipc_protocol)
phosphoripc_add_test(test_phosphor_ipc_router)
phosphoripc_add_test(test_phosphor_ipc_schema)
phosphoripc_add_test(test_phosphor_ipc_e2e)
phosphoripc_add_test(test_phosphor_ipc_subscribe)
phosphoripc_add_test(test_phosphor_ipc_engine)
