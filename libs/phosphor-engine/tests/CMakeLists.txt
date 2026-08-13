# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# PhosphorEngine unit tests. Headless — pure value/helper logic, no QPA needed.

find_package(Qt6 REQUIRED COMPONENTS Test)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
function(pe_add_test _name)
    add_executable(${_name} ${ARGN})
    set_target_properties(${_name} PROPERTIES AUTOMOC ON)
    target_compile_features(${_name} PRIVATE cxx_std_20)
    target_link_libraries(${_name}
        PRIVATE
            Qt6::Test
            Qt6::Core
            PhosphorEngine::PhosphorEngine
    )
    add_test(NAME ${_name} COMMAND ${_name})
    phosphor_apply_test_isolation(${_name})
    set_tests_properties(${_name} PROPERTIES LABELS "phosphorengine")
endfunction()

pe_add_test(test_engine_screencontexttracker test_screencontexttracker.cpp)
pe_add_test(test_engine_iplacementengine_defaults test_iplacementengine_defaults.cpp)
pe_add_test(test_engine_perscreenstates test_perscreenstates.cpp)
pe_add_test(test_engine_windowcontext test_windowcontext.cpp)
pe_add_test(test_engine_layerfocusswitch test_layerfocusswitch.cpp)
