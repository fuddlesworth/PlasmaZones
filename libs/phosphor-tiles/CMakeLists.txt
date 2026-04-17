# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorTiles — auto-tiling algorithm primitives.
#
# Stateless tiling-algorithm layer: TilingAlgorithm interface, AlgorithmRegistry
# singleton, SplitTree primitive (BSP node tree + serialization + rebuild),
# TilingState (per-screen mutable state consumed by previews), and the scripted-
# algorithm runtime (QJSEngine sandbox + loader + helpers). Implements
# PhosphorLayout::ILayoutSource via AutotileLayoutSource so editor / settings
# code can render autotile previews without depending on the daemon engine.
#
# This library does NOT include the runtime engine that actually places windows
# (that's PlasmaZones::AutotileEngine in src/autotile, future phosphor-tile-engine
# extraction). It also does NOT include daemon-side overflow handling, settings
# bridge, or per-screen config resolution — those are PlasmaZones-internal.
#
# Self-contained: depends only on Qt6 + PhosphorLayoutApi.  Logging is
# library-local (`PhosphorTiles::lcTilesLib`).  Strings in
# scriptedalgorithm.cpp ship as untranslated English placeholders — the
# application config layer overrides them via algorithm display name.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORTILES_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorTiles VERSION ${PHOSPHORTILES_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 REQUIRED COMPONENTS Core Qml)

# PhosphorLayoutApi provides the LayoutPreview / ILayoutSource contract that
# this library implements via AutotileLayoutSource. Found via add_subdirectory
# in PZ in-tree builds; falls back to find_package when this library is
# consumed standalone.
if(NOT TARGET PhosphorLayoutApi::PhosphorLayoutApi)
    find_package(PhosphorLayoutApi CONFIG REQUIRED)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphortiles_public_HDRS
    include/PhosphorTiles/PhosphorTiles.h
    include/PhosphorTiles/AutotileConstants.h
    include/PhosphorTiles/TilingAlgorithm.h
    include/PhosphorTiles/TilingState.h
    include/PhosphorTiles/SplitTree.h
    include/PhosphorTiles/AlgorithmRegistry.h
    include/PhosphorTiles/ScriptedAlgorithm.h
    include/PhosphorTiles/ScriptedAlgorithmHelpers.h
    include/PhosphorTiles/ScriptedAlgorithmJsBuiltins.h
    include/PhosphorTiles/ScriptedAlgorithmLoader.h
    include/PhosphorTiles/ScriptedAlgorithmSandbox.h
    include/PhosphorTiles/AutotileLayoutSource.h
)

set(phosphortiles_SRCS
    src/algorithmregistry.cpp
    src/autotilelayoutsource.cpp
    src/tilingalgorithm.cpp
    src/tilingstate.cpp
    src/tilingstateserialization.cpp
    src/splittree.cpp
    src/splittreerebuild.cpp
    src/splittreeserializer.cpp
    src/scriptedalgorithm.cpp
    src/scriptedalgorithmhelpers.cpp
    src/scriptedalgorithmjsbuiltins.cpp
    src/scriptedalgorithmloader.cpp
    src/scriptedalgorithmsandbox.cpp
    src/tileslogging.h
    src/tileslogging.cpp
    # Builtin JS helpers compiled into the binary as Qt resources.
    src/builtins.qrc
)

add_library(PhosphorTiles SHARED
    ${phosphortiles_public_HDRS}
    ${phosphortiles_SRCS}
)

add_library(PhosphorTiles::PhosphorTiles ALIAS PhosphorTiles)

generate_export_header(PhosphorTiles
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphortiles_export.h
    EXPORT_MACRO_NAME PHOSPHORTILES_EXPORT
)

# Resolve KDE_INSTALL_INCLUDEDIR here so INSTALL_INTERFACE can reference it.
# Same rationale as the other phosphor-* libs.
if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

target_include_directories(PhosphorTiles
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorTiles PUBLIC cxx_std_20)

target_link_libraries(PhosphorTiles
    PUBLIC
        Qt6::Core
        Qt6::Qml
        PhosphorLayoutApi::PhosphorLayoutApi
)

set_target_properties(PhosphorTiles PROPERTIES
    VERSION ${PHOSPHORTILES_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    # Unity builds merge multiple source files into one TU, which surfaces
    # spurious "defined but not used" warnings on anonymous-namespace helpers
    # that GCC can't fully track across the merge. The translation units are
    # small enough that unity buys us nothing here.
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_LIBDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

if(NOT DEFINED KDE_INSTALL_BINDIR)
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS PhosphorTiles
    EXPORT PhosphorTilesTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(EXPORT PhosphorTilesTargets
    FILE PhosphorTilesTargets.cmake
    NAMESPACE PhosphorTiles::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorTiles
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorTilesConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorTilesConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorTiles
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorTilesConfigVersion.cmake"
    VERSION ${PHOSPHORTILES_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorTilesConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorTilesConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorTiles
)

install(DIRECTORY include/PhosphorTiles/
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorTiles
    FILES_MATCHING PATTERN "*.h"
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/phosphortiles_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorTiles
)
