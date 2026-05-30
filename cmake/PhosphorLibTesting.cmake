# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# phosphor_lib_enable_tests(standaloneProjectName [testsDir])
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
#   testsDir               Optional. Path to the tests/ subdirectory to
#                          add. Defaults to
#                          "${CMAKE_CURRENT_SOURCE_DIR}/tests" — i.e.
#                          the tests/ sibling of the CMakeLists.txt
#                          that called this function. Passing an
#                          absolute path is supported for layouts where
#                          tests live outside the caller's source tree.
#                          Relative paths are resolved against
#                          CMAKE_CURRENT_SOURCE_DIR (the caller's
#                          source dir), matching how `add_subdirectory`
#                          resolves its source argument so the function
#                          stays compositional with the caller's
#                          intuition.
#
# Behaviour:
#   - When this library is the top-level CMake project OR a parent
#     project set BUILD_TESTING, calls enable_testing() and includes
#     the tests directory if it exists.
#   - enable_testing() is a no-op when KDECMakeSettings already
#     included CTest at the top level. The redundant call lets a
#     standalone build of this subproject still discover ctest.
function(phosphor_lib_enable_tests standaloneProjectName)
    if(ARGC GREATER 1)
        set(_pl_tests_dir "${ARGV1}")
        if(NOT IS_ABSOLUTE "${_pl_tests_dir}")
            set(_pl_tests_dir "${CMAKE_CURRENT_SOURCE_DIR}/${_pl_tests_dir}")
        endif()
    else()
        set(_pl_tests_dir "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    endif()
    if(CMAKE_PROJECT_NAME STREQUAL "${standaloneProjectName}" OR BUILD_TESTING)
        enable_testing()
        if(EXISTS "${_pl_tests_dir}/CMakeLists.txt")
            # Detect inside-tree vs outside-tree by literal-prefix match.
            # The previous MATCHES regex broke if CMAKE_CURRENT_SOURCE_DIR
            # contained characters with regex meaning (+, ., (, etc.) —
            # a project under e.g. /home/foo/dev+notes/ would silently take
            # the outside-tree branch. string(FIND) is a literal substring
            # check with no regex semantics.
            string(FIND "${_pl_tests_dir}" "${CMAKE_CURRENT_SOURCE_DIR}/" _pl_prefix_match)
            if(_pl_prefix_match EQUAL 0)
                # Inside-tree path: add_subdirectory infers a binary dir
                # by mirroring the source layout. Pass the source path
                # alone so the build-tree layout matches the existing
                # convention (tests/ next to the caller's CMakeLists.txt).
                add_subdirectory("${_pl_tests_dir}")
            else()
                # Outside-tree path: add_subdirectory requires an
                # explicit binary dir. Mirror the source dir's basename
                # under CMAKE_CURRENT_BINARY_DIR so the build tree
                # stays readable.
                get_filename_component(_pl_tests_bin "${_pl_tests_dir}" NAME)
                add_subdirectory("${_pl_tests_dir}" "${CMAKE_CURRENT_BINARY_DIR}/${_pl_tests_bin}")
            endif()
        endif()
    endif()
endfunction()
