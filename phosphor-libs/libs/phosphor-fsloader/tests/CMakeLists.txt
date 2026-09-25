# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# PhosphorFsLoader unit tests. Headless — uses offscreen QPA so that
# QFileSystemWatcher and QTimer-based debounce work without a display.

find_package(Qt6 REQUIRED COMPONENTS Test)

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
    phosphor_apply_test_isolation(${_name})
    # APPEND, never PROPERTIES ENVIRONMENT: a plain set would replace the
    # XDG sandbox phosphor_apply_test_isolation just installed.
    phosphor_append_test_environment(${_name}
        "QT_QPA_PLATFORM=offscreen"
        "QT_LOGGING_RULES=phosphorfsloader.debug=false")
    set_tests_properties(${_name} PROPERTIES LABELS "phosphorfsloader")
endfunction()

pfl_add_test(pfl_test_packpathguard test_packpathguard.cpp)
pfl_add_test(pfl_test_directoryloader test_directoryloader.cpp)
pfl_add_test(pfl_test_metadatapackscanstrategy test_metadatapackscanstrategy.cpp)
pfl_add_test(pfl_test_schemavalidator test_schemavalidator.cpp)
pfl_add_test(pfl_test_watcheddirectoryset test_watcheddirectoryset.cpp)
