# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorSnapEngine — Zone-based snap placement engine.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSNAPENGINE_VERSION "0.2.0")

# The Qt floor this library requires AND advertises in its generated
# PhosphorSnapEngineConfig.cmake. Taken from the top-level QT_MIN_VERSION when built in-tree so
# the two cannot drift, with its own value for a STANDALONE build, where that
# variable does not exist. The fallback is not optional: the Config template
# substitutes this, and an undefined variable expands to nothing and ships a config
# whose find_dependency(Qt6 ...) carries no version floor at all.
if(DEFINED QT_MIN_VERSION)
    set(PHOSPHORSNAPENGINE_QT_MIN ${QT_MIN_VERSION})
else()
    set(PHOSPHORSNAPENGINE_QT_MIN "6.10.0")
endif()

if(NOT PROJECT_VERSION)
    project(PhosphorSnapEngine VERSION ${PHOSPHORSNAPENGINE_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

include(GenerateExportHeader)

set(CMAKE_AUTOMOC ON)

# Sibling Phosphor libraries this one links. Each lookup is GUARDED rather than
# unconditional: in-tree the parent add_subdirectory()s the sibling before this
# directory, so its ALIAS target already exists and the find_package is skipped.
# A STANDALONE configure (the project() guard above says this directory supports
# one) has no such target, and without these it failed at target_link_libraries
# with "links to X::X but the target was not found".

# the shared placement policy every engine builds on
if(NOT TARGET PhosphorEngine::PhosphorEngine)
    find_package(PhosphorEngine CONFIG REQUIRED)
endif()

# the window-identity types the placement API keys on
if(NOT TARGET PhosphorIdentity::PhosphorIdentity)
    find_package(PhosphorIdentity CONFIG REQUIRED)
endif()

# the rule store and evaluator
if(NOT TARGET PhosphorRules::PhosphorRules)
    find_package(PhosphorRules CONFIG REQUIRED)
endif()

# the zone and layout model
if(NOT TARGET PhosphorZones::PhosphorZones)
    find_package(PhosphorZones CONFIG REQUIRED)
endif()

# the D-Bus contract types. Linked as PhosphorProtocol::Types, an alias whose
# target name differs from the package name, which is why the first sweep
# over Pkg::Pkg spellings did not catch it.
if(NOT TARGET PhosphorProtocol::PhosphorProtocol)
    find_package(PhosphorProtocol CONFIG REQUIRED)
endif()

# the screen model. Linked as PhosphorScreens::Runtime, an alias whose target
# name differs from the package name.
if(NOT TARGET PhosphorScreens::PhosphorScreens)
    find_package(PhosphorScreens CONFIG REQUIRED)
endif()

add_library(PhosphorSnapEngine SHARED
    include/PhosphorSnapEngine/SnapEngine.h
    include/PhosphorSnapEngine/PlacementDirective.h
    include/PhosphorSnapEngine/SnapState.h
    include/PhosphorSnapEngine/ISnapSettings.h
    include/PhosphorSnapEngine/INavigationStateProvider.h
    include/PhosphorSnapEngine/IZoneAdjacencyResolver.h
    include/PhosphorSnapEngine/snapnavigationtargets.h
    src/INavigationStateProvider.cpp
    src/IZoneAdjacencyResolver.cpp
    src/SnapEngine.cpp
    src/membership.cpp
    src/SnapState.cpp
    src/calculate.cpp
    src/commit.cpp
    src/float.cpp
    src/free_size.cpp
    src/lifecycle.cpp
    src/navigation.cpp
    src/navigation_actions.cpp
    src/navigation_crosssurface.cpp
    src/resnap_calc.cpp
    src/snapnavigationtargets.cpp
    src/snapenginelogging.h
    src/snapenginelogging.cpp
)
add_library(PhosphorSnapEngine::PhosphorSnapEngine ALIAS PhosphorSnapEngine)

generate_export_header(PhosphorSnapEngine
    EXPORT_FILE_NAME phosphorsnapengine_export.h
    EXPORT_MACRO_NAME PHOSPHORSNAPENGINE_EXPORT
)

target_include_directories(PhosphorSnapEngine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_compile_features(PhosphorSnapEngine PUBLIC cxx_std_20)

target_link_libraries(PhosphorSnapEngine
    PUBLIC
        Qt6::Core
        Qt6::Gui
        PhosphorEngine::PhosphorEngine
        PhosphorZones::PhosphorZones
        PhosphorProtocol::Types
        # PUBLIC because the installed SnapEngine.h includes
        # <PhosphorRules/RuleEvaluator.h> and holds a RuleEvaluator member —
        # a PRIVATE link does not propagate the include dirs, so consumers of
        # the installed package could not compile against the header (in-tree
        # it only built because the daemon links PhosphorRules itself).
        PhosphorRules::PhosphorRules
    PRIVATE
        PhosphorScreens::Runtime  # SnapEngine queries screen geometry via ScreenManager
        PhosphorIdentity::PhosphorIdentity
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

set_target_properties(PhosphorSnapEngine PROPERTIES
    VERSION ${PHOSPHORSNAPENGINE_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

install(TARGETS PhosphorSnapEngine
    EXPORT PhosphorSnapEngineTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(DIRECTORY include/PhosphorSnapEngine
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/phosphorsnapengine_export.h"
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorSnapEngine
)

install(EXPORT PhosphorSnapEngineTargets
    FILE PhosphorSnapEngineTargets.cmake
    NAMESPACE PhosphorSnapEngine::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorSnapEngine
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorSnapEngineConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSnapEngineConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorSnapEngine
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSnapEngineConfigVersion.cmake"
    VERSION ${PHOSPHORSNAPENGINE_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSnapEngineConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSnapEngineConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorSnapEngine
)
