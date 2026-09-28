# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorEngine — Unified placement engine contracts + base class.
#
# Shared library providing IPlacementEngine, IPlacementState,
# NavigationContext, LayerFocusSwitch, and PlacementEngineBase. All three
# placement engines (phosphor-snap-engine, phosphor-tile-engine,
# phosphor-scroll-engine) inherit PlacementEngineBase so daemons and
# compositor plugins get the shared placement-engine plumbing
# (settings injection, stale-window pruning, common signals) for free.
#
# Depends on Qt6::Core (QString, QStringList, QRect, QJsonObject, QObject).

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORENGINE_VERSION "0.3.0")

# The Qt floor this library requires AND advertises in its generated
# PhosphorEngineConfig.cmake. Taken from the top-level QT_MIN_VERSION when built in-tree so
# the two cannot drift, with its own value for a STANDALONE build, where that
# variable does not exist. The fallback is not optional: the Config template
# substitutes this, and an undefined variable expands to nothing and ships a config
# whose find_dependency(Qt6 ...) carries no version floor at all.
if(DEFINED QT_MIN_VERSION)
    set(PHOSPHORENGINE_QT_MIN ${QT_MIN_VERSION})
else()
    set(PHOSPHORENGINE_QT_MIN "6.10.0")
endif()

if(NOT PROJECT_VERSION)
    project(PhosphorEngine VERSION ${PHOSPHORENGINE_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorEngine Library (SHARED)
# ═══════════════════════════════════════════════════════════════════════════════

set(CMAKE_AUTOMOC ON)

# Sibling Phosphor libraries this one links. Each lookup is GUARDED rather than
# unconditional: in-tree the parent add_subdirectory()s the sibling before this
# directory, so its ALIAS target already exists and the find_package is skipped.
# A STANDALONE configure (the project() guard above says this directory supports
# one) has no such target, and without these it failed at target_link_libraries
# with "links to X::X but the target was not found".

# the geometry primitives the placement types are expressed in
if(NOT TARGET PhosphorGeometry::PhosphorGeometry)
    find_package(PhosphorGeometry CONFIG REQUIRED)
endif()

# the window-identity types the placement API keys on
if(NOT TARGET PhosphorIdentity::PhosphorIdentity)
    find_package(PhosphorIdentity CONFIG REQUIRED)
endif()

# the layout-provider interface
if(NOT TARGET PhosphorLayoutApi::PhosphorLayoutApi)
    find_package(PhosphorLayoutApi CONFIG REQUIRED)
endif()

# the D-Bus contract types. Linked as PhosphorProtocol::Types, an alias whose
# target name differs from the package name, which is why the first sweep
# over Pkg::Pkg spellings did not catch it.
if(NOT TARGET PhosphorProtocol::PhosphorProtocol)
    find_package(PhosphorProtocol CONFIG REQUIRED)
endif()

add_library(PhosphorEngine SHARED
    include/PhosphorEngine/PlacementEngineBase.h
    include/PhosphorEngine/GapResolution.h
    include/PhosphorEngine/LayerFocusSwitch.h
    include/PhosphorEngine/PerScreenStates.h
    include/PhosphorEngine/ScreenContextTracker.h
    include/PhosphorEngine/WindowPlacement.h
    include/PhosphorEngine/WindowPlacementStore.h
    src/LayerFocusSwitch.cpp
    src/PlacementEngineBase.cpp
    src/ScreenContextTracker.cpp
    src/WindowPlacementStore.cpp
    src/WindowPlacementStore_serialize.cpp
    src/WindowPlacementStore_claims.cpp
    src/GeometryUtils.cpp
    src/WindowRegistry.cpp
    include/PhosphorEngine/WindowRegistry.h
)
add_library(PhosphorEngine::PhosphorEngine ALIAS PhosphorEngine)

generate_export_header(PhosphorEngine
    EXPORT_FILE_NAME phosphorengine_export.h
    EXPORT_MACRO_NAME PHOSPHORENGINE_EXPORT
)

target_include_directories(PhosphorEngine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorEngine PUBLIC cxx_std_20)

target_link_libraries(PhosphorEngine
    PUBLIC
        Qt6::Core
        PhosphorGeometry::PhosphorGeometry
        PhosphorLayoutApi::PhosphorLayoutApi
        PhosphorProtocol::Types
        # PUBLIC, not PRIVATE: WindowPlacement.h is a public header and its
        # freeGeometryFor() resolves virtual screen ids through
        # VirtualScreenId::samePhysical. PhosphorIdentity is an INTERFACE
        # (header-only) target, so this is an include-path dependency with no
        # link-time cost.
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

# SONAME + symlinks for packagers. SOVERSION is the stable ABI version,
# decoupled from semver — kept at 0 across the phosphor-* catalog so
# packagers ship `lib*.so.0` until we make a deliberate ABI break.
# Hidden visibility pairs with generate_export_header — without it the
# PHOSPHORENGINE_EXPORT macro is a no-op and every symbol leaks.
set_target_properties(PhosphorEngine PROPERTIES
    VERSION ${PHOSPHORENGINE_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

install(TARGETS PhosphorEngine
    EXPORT PhosphorEngineTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(DIRECTORY include/PhosphorEngine
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/phosphorengine_export.h"
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorEngine
)

install(EXPORT PhosphorEngineTargets
    FILE PhosphorEngineTargets.cmake
    NAMESPACE PhosphorEngine::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorEngine
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorEngineConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorEngineConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorEngine
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorEngineConfigVersion.cmake"
    VERSION ${PHOSPHORENGINE_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorEngineConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorEngineConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorEngine
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorEngine" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
