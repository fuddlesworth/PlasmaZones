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
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

phosphoripc_add_test(test_phosphor_ipc_protocol)
phosphoripc_add_test(test_phosphor_ipc_router)
phosphoripc_add_test(test_phosphor_ipc_schema)
phosphoripc_add_test(test_phosphor_ipc_e2e)
phosphoripc_add_test(test_phosphor_ipc_subscribe)
