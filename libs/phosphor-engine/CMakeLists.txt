# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorEngine — Unified placement engine contracts + base class.
#
# Shared library providing IPlacementEngine, IPlacementState,
# NavigationContext, and PlacementEngineBase. Both phosphor-zones (snap)
# and phosphor-tiles (autotile) inherit PlacementEngineBase so daemons
# and compositor plugins get the shared placement-engine plumbing
# (settings injection, stale-window pruning, common signals) for free.
#
# Depends on Qt6::Core (QString, QStringList, QRect, QJsonObject, QObject).

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORENGINE_VERSION "0.2.0")

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

add_library(PhosphorEngine SHARED
    include/PhosphorEngine/PlacementEngineBase.h
    include/PhosphorEngine/WindowPlacement.h
    include/PhosphorEngine/WindowPlacementStore.h
    src/PlacementEngineBase.cpp
    src/WindowPlacementStore.cpp
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
        PhosphorProtocol::Types
    PRIVATE
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
