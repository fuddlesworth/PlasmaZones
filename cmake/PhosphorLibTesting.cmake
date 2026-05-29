# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# phosphor_lib_enable_tests(standaloneProjectName)
#
# The two-line "build my tests/ if I'm the top-level project, or if a
# parent set BUILD_TESTING" gate that every phosphor library reaches
# for. Extracted so a Phase-2.x library can drop the boilerplate.
#
# Args:
#   standaloneProjectName  CMAKE_PROJECT_NAME value when the library
#                          is configured as a standalone project (i.e.
#                          the `project(...)` call in the library's own
#                          CMakeLists.txt). Matches the `if(NOT
#                          PROJECT_VERSION) project(<Name> ...) endif()`
#                          guard each lib uses to support both in-tree
#                          and out-of-tree configures.
#
# Behaviour:
#   - When this library is the top-level CMake project OR a parent
#     project set BUILD_TESTING, calls enable_testing() and includes
#     ${CMAKE_CURRENT_SOURCE_DIR}/tests if that subdirectory exists.
#   - enable_testing() is a no-op when KDECMakeSettings already
#     included CTest at the top level. The redundant call lets a
#     standalone build of this subproject still discover ctest.
function(phosphor_lib_enable_tests standaloneProjectName)
    if(CMAKE_PROJECT_NAME STREQUAL "${standaloneProjectName}" OR BUILD_TESTING)
        enable_testing()
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
            add_subdirectory(tests)
        endif()
    endif()
endfunction()
