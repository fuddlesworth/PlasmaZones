# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Qt Test harness for the launcher core. Pure C++ over the matcher and the
# .desktop parser; the scanner tests read the fixture tree under
# fixtures/, whose path is compiled in so ctest needs no working directory.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Test)

# Shared test isolation: a private D-Bus session plus a per-target XDG
# sandbox. Guarded so a standalone `git subtree split` configure of this
# library still works without the top-level module.
if(EXISTS "${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake")
    include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
endif()

function(phosphorshelllauncher_add_test name)
    add_executable(${name} ${name}.cpp)
    target_compile_features(${name} PRIVATE cxx_std_20)
    target_compile_definitions(${name}
        PRIVATE PHOSPHOR_LAUNCHER_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
    target_link_libraries(${name}
        PRIVATE
            Qt6::Core
            Qt6::Test
            PhosphorShellLauncher::PhosphorShellLauncher
    )
    add_test(NAME ${name} COMMAND ${name})
    set_property(TEST ${name} APPEND PROPERTY LABELS "phosphorshelllauncher")
    if(COMMAND phosphor_apply_test_isolation)
        phosphor_apply_test_isolation(${name})
    endif()
endfunction()

phosphorshelllauncher_add_test(test_fuzzymatcher)
phosphorshelllauncher_add_test(test_desktopentry)
