# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorTiles — auto-tiling algorithm primitives.
#
# Stateless tiling-algorithm layer: TilingAlgorithm interface, AlgorithmRegistry
# (per-composition-root, no global singleton), SplitTree primitive (BSP node
# tree + serialization + rebuild),
# TilingState (per-screen mutable state consumed by previews), and the scripted-
# algorithm runtime (Luau-backed LuauTileAlgorithm + ScriptedAlgorithmLoader,
# evaluated through PhosphorScripting). Implements
# PhosphorLayout::ILayoutSource via AutotileLayoutSource so editor / settings
# code can render autotile previews without depending on the daemon engine.
#
# This library does NOT include the runtime engine that actually places windows
# (that lives in phosphor-tile-engine). It also does NOT include daemon-side
# overflow handling, settings bridge, or per-screen config resolution — those
# are host-application internals.
#
# Depends on Qt6::Core plus PhosphorLayoutApi, PhosphorEngine, PhosphorScripting
# and PhosphorFsLoader publicly, and PhosphorRegistry privately (see the link
# set below, which PhosphorTilesConfig.cmake.in mirrors).  Logging is
# library-local (`PhosphorTiles::lcTilesLib`).  Strings in
# luautilealgorithm.cpp ship as untranslated English placeholders — the
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

find_package(Qt6 6.10 REQUIRED COMPONENTS Core)

# PhosphorLayoutApi provides the LayoutPreview / ILayoutSource contract that
# this library implements via AutotileLayoutSource. Found via add_subdirectory
# in Phosphor in-tree builds; falls back to find_package when this library is
# consumed standalone.
if(NOT TARGET PhosphorLayoutApi::PhosphorLayoutApi)
    find_package(PhosphorLayoutApi CONFIG REQUIRED)
endif()

# phosphor-engine — IPlacementEngine contracts + PlacementEngineBase that
# AutotileLayoutSource and TilingState build on. PUBLIC link dep, but it
# CANNOT use the if(NOT TARGET) find_package fallback the other deps use:
# in-tree the engine subdirectory is added after this one, so the target
# does not exist yet at this point and the fallback would wrongly pull an
# installed copy. In-tree, CMake resolves the link-target name at generate
# time, so no find_package is needed. For a standalone configure (this
# directory IS the top level), resolve it explicitly here; the guard below
# never fires in-tree.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    find_package(PhosphorEngine CONFIG REQUIRED)
endif()

# phosphor-scripting — the embedded Luau host that LuauTileAlgorithm and
# ScriptedAlgorithmLoader evaluate scripts through. PUBLIC link dep, so a
# standalone configure must resolve it here too.
if(NOT TARGET PhosphorScripting::PhosphorScripting)
    find_package(PhosphorScripting 0.1.0 CONFIG REQUIRED)
endif()

# phosphor-fsloader — shared filesystem-loader scaffolding (used by
# ScriptedAlgorithmLoader's WatchedDirectorySet + LuauScanStrategy).
if(NOT TARGET PhosphorFsLoader::PhosphorFsLoader)
    find_package(PhosphorFsLoader 0.1.0 CONFIG REQUIRED)
endif()

# phosphor-registry — AlgorithmRegistry's storage is a Registry<TileAlgorithmEntry>.
# Used only in algorithmregistry.cpp (the header is pimpl), so a PRIVATE link.
if(NOT TARGET PhosphorRegistry::PhosphorRegistry)
    find_package(PhosphorRegistry 0.1.0 CONFIG REQUIRED)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphortiles_public_HDRS
    include/PhosphorTiles/PhosphorTiles.h
    include/PhosphorTiles/AutotileConstants.h
    include/PhosphorTiles/TilingAlgorithm.h
    include/PhosphorTiles/TilingParams.h
    include/PhosphorTiles/TilingState.h
    include/PhosphorTiles/SplitTree.h
    include/PhosphorTiles/AlgorithmPreviewParams.h
    include/PhosphorTiles/AlgorithmRegistry.h
    include/PhosphorTiles/ITileAlgorithmRegistry.h
    include/PhosphorTiles/LuauTileAlgorithm.h
    include/PhosphorTiles/TileScriptMetadata.h
    include/PhosphorTiles/ScriptedAlgorithmLoader.h
    include/PhosphorTiles/AutotileLayoutSource.h
    include/PhosphorTiles/AutotileLayoutSourceFactory.h
    include/PhosphorTiles/AutotilePreviewRender.h
)

set(phosphortiles_SRCS
    src/algorithmregistry.cpp
    src/itilealgorithmregistry.cpp
    src/autotilelayoutsource.cpp
    src/autotilelayoutsourcefactory.cpp
    src/tilingalgorithm.cpp
    src/tilingstate.cpp
    src/tilingstatedata.cpp
    src/splittree.cpp
    src/splittreerebuild.cpp
    src/scriptedalgorithmloader.cpp
    src/luautilealgorithm.cpp
    src/tilescriptmetadata.cpp
    src/tileslogging.h
    src/tileslogging.cpp
    # The pluau Luau standard library, compiled into the binary as a Qt resource.
    src/pluau.qrc
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
        PhosphorLayoutApi::PhosphorLayoutApi
        PhosphorEngine::PhosphorEngine
        PhosphorScripting::PhosphorScripting
        # PhosphorFsLoader is publicly exposed via
        # ScriptedAlgorithmLoader's `LiveReload` parameter — consumers
        # including `<PhosphorTiles/ScriptedAlgorithmLoader.h>` need
        # its headers on their include path.
        PhosphorFsLoader::PhosphorFsLoader
    PRIVATE
        # AlgorithmRegistry composes Registry<TileAlgorithmEntry> in its .cpp
        # only (pimpl header), so this stays private — not in the public ABI.
        PhosphorRegistry::PhosphorRegistry
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
if(NOT DEFINED KDE_INSTALL_DATADIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_DATADIR ${CMAKE_INSTALL_DATAROOTDIR})
endif()

# Ship the pluau standard-library type definitions so users authoring custom
# `.luau` algorithms get luau-lsp autocomplete and luau-analyze type-checking
# (see docs/architecture/luau-algorithm-authoring.md). Installed OUTSIDE the
# scanned algorithms subdirectory: the loader globs `*.luau`, so a `.d.luau`
# sitting next to the algorithms would be picked up by the scan and then
# skipped with a warning, since `pluau.d` fails the basename check.
install(FILES src/pluau/pluau.d.luau
    DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones)

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
