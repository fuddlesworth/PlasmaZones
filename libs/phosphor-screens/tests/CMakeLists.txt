# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# PhosphorScreens unit tests. Headless — uses offscreen QPA.

find_package(Qt6 REQUIRED COMPONENTS Test)

function(ps_add_test _name)
    set(_sources ${ARGN})
    add_executable(${_name} ${_sources})
    target_include_directories(${_name}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(${_name}
        PRIVATE
            Qt6::Test
            Qt6::Core
            Qt6::Gui
            PhosphorScreens::PhosphorScreens
    )
    add_test(NAME ${_name} COMMAND ${_name})
    # Offscreen QPA so QTimer-based tests work without a display server.
    set_tests_properties(${_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=phosphorscreens.debug=false"
        LABELS "phosphorscreens"
    )
endfunction()

ps_add_test(ps_test_virtualscreen test_virtualscreen.cpp)
ps_add_test(ps_test_inmemoryconfigstore test_inmemoryconfigstore.cpp)
ps_add_test(ps_test_swapper test_swapper.cpp)
ps_add_test(ps_test_manager_lifecycle test_manager_lifecycle.cpp)
ps_add_test(ps_test_resolver test_resolver.cpp)
