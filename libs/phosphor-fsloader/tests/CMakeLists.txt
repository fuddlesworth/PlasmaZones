# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# PhosphorFsLoader unit tests. Headless — uses offscreen QPA so that
# QFileSystemWatcher and QTimer-based debounce work without a display.

find_package(Qt6 REQUIRED COMPONENTS Test)

function(pfl_add_test _name)
    set(_sources ${ARGN})
    add_executable(${_name} ${_sources})
    set_target_properties(${_name} PROPERTIES AUTOMOC ON)
    target_include_directories(${_name}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(${_name}
        PRIVATE
            Qt6::Test
            Qt6::Core
            PhosphorFsLoader::PhosphorFsLoader
    )
    add_test(NAME ${_name} COMMAND ${_name})
    set_tests_properties(${_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=phosphorfsloader.debug=false"
        LABELS "phosphorfsloader"
    )
endfunction()

pfl_add_test(pfl_test_directoryloader test_directoryloader.cpp)
pfl_add_test(pfl_test_watcheddirectoryset test_watcheddirectoryset.cpp)
