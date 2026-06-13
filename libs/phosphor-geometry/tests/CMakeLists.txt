# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# PhosphorGeometry unit tests. Headless — pure geometry math, no QPA needed.

find_package(Qt6 REQUIRED COMPONENTS Test)

function(pg_add_test _name)
    add_executable(${_name} ${ARGN})
    set_target_properties(${_name} PROPERTIES AUTOMOC ON)
    target_compile_features(${_name} PRIVATE cxx_std_20)
    target_link_libraries(${_name}
        PRIVATE
            Qt6::Test
            Qt6::Core
            PhosphorGeometry::PhosphorGeometry
    )
    add_test(NAME ${_name} COMMAND ${_name})
    set_tests_properties(${_name} PROPERTIES LABELS "phosphorgeometry")
endfunction()

pg_add_test(pg_test_directional_neighbor test_directional_neighbor.cpp)
