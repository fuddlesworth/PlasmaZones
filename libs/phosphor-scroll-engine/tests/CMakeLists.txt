# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# PhosphorScrollEngine unit tests. Headless — pure strip-model logic, no QPA.

find_package(Qt6 REQUIRED COMPONENTS Test)

function(pse_add_test _name)
    add_executable(${_name} ${ARGN})
    set_target_properties(${_name} PROPERTIES AUTOMOC ON)
    target_compile_features(${_name} PRIVATE cxx_std_20)
    target_link_libraries(${_name}
        PRIVATE
            Qt6::Test
            Qt6::Core
            PhosphorScrollEngine::PhosphorScrollEngine
    )
    add_test(NAME ${_name} COMMAND ${_name})
    set_tests_properties(${_name} PROPERTIES LABELS "phosphorscrollengine")
endfunction()

pse_add_test(test_scrollstrip_core test_scrollstrip_core.cpp)
pse_add_test(test_scrollstrip_ops test_scrollstrip_ops.cpp)
pse_add_test(test_scrollengine_smoke test_scrollengine_smoke.cpp)
