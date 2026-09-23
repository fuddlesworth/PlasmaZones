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
    # Collecting nothing is always a mistake, and a silent one: the aggregate
    # would still be created, `cmake --build --target <tier>-tier` would succeed
    # instantly having compiled nothing, and `moon run <tier>:build` would
    # report green. The realistic trigger is the ordering rule above — call this
    # before an add_subdirectory and the subdirectory's targets do not exist
    # yet. Fail loudly instead, matching the engine QML firewall in the root
    # CMakeLists, which FATAL_ERRORs rather than letting a stale name through.
    if(NOT _tier_targets)
        message(FATAL_ERROR
            "phosphor_tier_target(${name}): collected no buildable targets under "
            "${CMAKE_CURRENT_SOURCE_DIR}. Call it AFTER every add_subdirectory for "
            "the tier, or the aggregate builds nothing and still reports success.")
    endif()
    add_custom_target(${name})
    add_dependencies(${name} ${_tier_targets})
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
