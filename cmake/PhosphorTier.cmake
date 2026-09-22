# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# phosphor_tier_target(<name>)
#
# Declares an aggregate custom target named <name> that depends on every
# buildable target declared under the calling directory, recursively. Each
# tier CMakeLists calls it last, so `cmake --build build --target
# phosphor-tier` builds exactly that tier (and, through ordinary link
# dependencies, whatever upstream tiers it needs) and nothing else. The
# moon workspace's per-project build task is built on this, and ctest's
# --test-dir <build>/<tier> covers the matching test selection, so the two
# together give each tier its own build and test verb without a second
# configure.
#
# Interface libraries have nothing to build and utility targets (qmllint
# aggregates, tooling helpers, the per-module _qmlcache steps) are not part
# of a tier's deliverable, so both are skipped.
function(phosphor_tier_target name)
    set(_tier_targets "")
    _phosphor_tier_collect("${CMAKE_CURRENT_SOURCE_DIR}" _tier_targets)
    add_custom_target(${name})
    if(_tier_targets)
        add_dependencies(${name} ${_tier_targets})
    endif()
endfunction()

function(_phosphor_tier_collect dir out_var)
    set(_acc "${${out_var}}")
    # A directory added with EXCLUDE_FROM_ALL (the vendored Luau tree that
    # phosphor-scripting unpacks into the build directory) is not part of
    # the tier's deliverable either: `cmake --build` skips it, so the tier
    # target must too, or it drags in analyzer targets the product never
    # links. The same property is set on every target declared there.
    get_property(_excluded DIRECTORY "${dir}" PROPERTY EXCLUDE_FROM_ALL)
    if(_excluded)
        set(${out_var} "${_acc}" PARENT_SCOPE)
        return()
    endif()
    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _targets)
        get_target_property(_type ${_t} TYPE)
        if(_type STREQUAL "INTERFACE_LIBRARY" OR _type STREQUAL "UTILITY")
            continue()
        endif()
        get_target_property(_t_excluded ${_t} EXCLUDE_FROM_ALL)
        if(_t_excluded)
            continue()
        endif()
        list(APPEND _acc ${_t})
    endforeach()
    get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_sub IN LISTS _subdirs)
        _phosphor_tier_collect("${_sub}" _acc)
    endforeach()
    set(${out_var} "${_acc}" PARENT_SCOPE)
endfunction()
