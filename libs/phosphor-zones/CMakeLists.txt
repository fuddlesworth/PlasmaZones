# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorZones — manual zone-layout primitives.
#
# Passive data + geometry layer: Zone, Layout, ZoneDetector, ZoneHighlighter,
# layout JSON serialization, and the IZoneDetector / ILayoutManager abstract
# contracts. Implements PhosphorLayout::ILayoutSource so editor / settings
# code can render layout previews without depending on the daemon.
#
# This library does NOT include the runtime engine that snaps windows to
# zones (that's phosphor-snap, future work). It also does NOT include
# autotile algorithms (phosphor-tile-algo, future work).
#
# Self-contained: depends only on Qt6 + PhosphorLayoutApi (PUBLIC) and
# PhosphorIdentity (PRIVATE — used by Layout::matchAppRule for
# composite window-id parsing; the dep doesn't leak through any public
# PhosphorZones header).

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORZONES_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorZones VERSION ${PHOSPHORZONES_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 REQUIRED COMPONENTS Core Gui)

# PhosphorLayoutApi provides the LayoutPreview / ILayoutSource contract that
# this library implements via ZonesLayoutSource (see src/zoneslayoutsource.cpp).
# Found via add_subdirectory in PZ in-tree builds; falls back to find_package
# when this library is consumed standalone.
if(NOT TARGET PhosphorLayoutApi::PhosphorLayoutApi)
    find_package(PhosphorLayoutApi CONFIG REQUIRED)
endif()

# PhosphorConfig::IBackend is taken by reference in LayoutManager's public
# constructor so callers can wire any persistent-store backend they own.
# PUBLIC because the type leaks through the header.
if(NOT TARGET PhosphorConfig::PhosphorConfig)
    find_package(PhosphorConfig CONFIG REQUIRED)
endif()

# PhosphorScreens supplies ScreenIdentity (canonical id ↔ connector name)
# and VirtualScreen helpers used by LayoutManager's assignment cascade.
# PRIVATE because no public header leaks the type.
if(NOT TARGET PhosphorScreens::PhosphorScreens)
    find_package(PhosphorScreens CONFIG REQUIRED)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorzones_public_HDRS
    include/PhosphorZones/PhosphorZones.h
    include/PhosphorZones/Zone.h
    include/PhosphorZones/ZoneDefaults.h
    include/PhosphorZones/ZoneJsonKeys.h
    include/PhosphorZones/Layout.h
    include/PhosphorZones/ZoneDetector.h
    include/PhosphorZones/ZoneHighlighter.h
    include/PhosphorZones/LayoutUtils.h
    include/PhosphorZones/IZoneDetector.h
    include/PhosphorZones/IBuiltInLayouts.h
    include/PhosphorZones/ILayoutAssignments.h
    include/PhosphorZones/ILayoutManager.h
    include/PhosphorZones/ILayoutPersistence.h
    include/PhosphorZones/IZoneLayoutRegistry.h
    include/PhosphorZones/IQuickLayouts.h
    include/PhosphorZones/ZonesLayoutSource.h
    include/PhosphorZones/ZonesLayoutSourceFactory.h
    include/PhosphorZones/AssignmentEntry.h
    include/PhosphorZones/LayoutManager.h
)

set(phosphorzones_SRCS
    src/zone.cpp
    src/layout.cpp
    src/layout/factories.cpp
    src/layout/serialization.cpp
    src/zonedetector.cpp
    src/zonehighlighter.cpp
    src/layoututils.cpp
    src/zoneslayoutsource.cpp
    src/zoneslayoutsourcefactory.cpp
    src/zoneslogging.h
    src/zoneslogging.cpp
    src/interfaces.cpp
    src/layoutmanager.cpp
    src/layoutmanager_assignments.cpp
    src/layoutmanager_persistence.cpp
)

add_library(PhosphorZones SHARED
    ${phosphorzones_public_HDRS}
    ${phosphorzones_SRCS}
)

add_library(PhosphorZones::PhosphorZones ALIAS PhosphorZones)

generate_export_header(PhosphorZones
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphorzones_export.h
    EXPORT_MACRO_NAME PHOSPHORZONES_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

target_include_directories(PhosphorZones
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorZones PUBLIC cxx_std_20)

target_link_libraries(PhosphorZones
    PUBLIC
        Qt6::Core
        Qt6::Gui
        PhosphorLayoutApi::PhosphorLayoutApi
        # LayoutManager's public ctor takes a unique_ptr<PhosphorConfig::IBackend>
        # — type must be visible to callers that include LayoutManager.h.
        PhosphorConfig::PhosphorConfig
    PRIVATE
        # Layout::matchAppRule uses PhosphorIdentity::WindowId for
        # composite window-id parsing; PRIVATE because the dep doesn't
        # leak through any public PhosphorZones header.
        PhosphorIdentity::PhosphorIdentity
        # LayoutManager impl uses ScreenIdentity / VirtualScreen for
        # the assignment-cascade screen-id normalisation. PRIVATE — no
        # public header surfaces these types.
        PhosphorScreens::PhosphorScreens
)

set_target_properties(PhosphorZones PROPERTIES
    VERSION ${PHOSPHORZONES_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
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

install(TARGETS PhosphorZones
    EXPORT PhosphorZonesTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(EXPORT PhosphorZonesTargets
    FILE PhosphorZonesTargets.cmake
    NAMESPACE PhosphorZones::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorZones
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorZonesConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorZonesConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorZones
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorZonesConfigVersion.cmake"
    VERSION ${PHOSPHORZONES_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorZonesConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorZonesConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorZones
)

install(DIRECTORY include/PhosphorZones/
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorZones
    FILES_MATCHING PATTERN "*.h"
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/phosphorzones_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorZones
)
