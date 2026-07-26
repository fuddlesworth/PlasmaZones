# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# PhosphorFsLoader unit tests. Headless — uses offscreen QPA so that
# QFileSystemWatcher and QTimer-based debounce work without a display.

find_package(Qt6 REQUIRED COMPONENTS Test)
find_program(_pz_dbus_run_session dbus-run-session)

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
    # D-Bus isolation, matching tests/unit/CMakeLists.txt's block. Nothing here
    # touches D-Bus today, but the isolation is a repo-wide invariant: a future
    # test added through this helper that DID reach the stock session bus would
    # activate the installed daemon, which then holds the test's stdout pipe
    # open and hangs ctest after the test passes.
    if(_pz_dbus_run_session AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
        set_target_properties(${_name} PROPERTIES
            TEST_LAUNCHER
            "${_pz_dbus_run_session};--config-file=${CMAKE_SOURCE_DIR}/tests/unit/test-session-bus.conf;--")
    endif()
    set_tests_properties(${_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=phosphorfsloader.debug=false"
        LABELS "phosphorfsloader"
    )
endfunction()

pfl_add_test(pfl_test_directoryloader test_directoryloader.cpp)
pfl_add_test(pfl_test_metadatapackscanstrategy test_metadatapackscanstrategy.cpp)
pfl_add_test(pfl_test_schemavalidator test_schemavalidator.cpp)
pfl_add_test(pfl_test_watcheddirectoryset test_watcheddirectoryset.cpp)
