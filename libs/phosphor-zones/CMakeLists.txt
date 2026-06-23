# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorZones — manual zone-layout primitives.
#
# Passive data + geometry layer: Zone, Layout, ZoneDetector, ZoneHighlighter,
# layout JSON serialization, and the IZoneDetector / ILayoutRegistry abstract
# contracts. Implements PhosphorLayout::ILayoutSource so editor / settings
# code can render layout previews without depending on the daemon.
#
# This library does NOT include the runtime engine that snaps windows to
# zones (that's phosphor-snap, future work). It also does NOT include
# autotile algorithms (phosphor-tile-algo, future work).
#
# Self-contained: depends only on Qt6 + PhosphorLayoutApi (PUBLIC) and
# PhosphorIdentity (PRIVATE — used for virtual-screen id parsing
# (VirtualScreenId) in geometryutils / layoutregistry_assignments; the dep
# doesn't leak through any public PhosphorZones header).

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

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui)

# PhosphorLayoutApi provides the LayoutPreview / ILayoutSource contract that
# this library implements via ZonesLayoutSource (see src/zoneslayoutsource.cpp).
# Found via add_subdirectory in Phosphor in-tree builds; falls back to find_package
# when this library is consumed standalone.
if(NOT TARGET PhosphorLayoutApi::PhosphorLayoutApi)
    find_package(PhosphorLayoutApi CONFIG REQUIRED)
endif()

# PhosphorScreens supplies ScreenIdentity / VirtualScreen (LayoutRegistry's
# assignment cascade) and ScreenManager geometry queries (GeometryUtils, whose
# public GeometryUtils.h surfaces a ScreenManager*) — so this links the Runtime
# target. PRIVATE: callers of the ScreenManager-taking GeometryUtils functions
# own and link their own PhosphorScreens, so the dep need not propagate.
if(NOT TARGET PhosphorScreens::Runtime)
    find_package(PhosphorScreens CONFIG REQUIRED)
endif()

if(NOT TARGET PhosphorGeometry::PhosphorGeometry)
    find_package(PhosphorGeometry CONFIG REQUIRED)
endif()

if(NOT TARGET PhosphorIdentity::PhosphorIdentity)
    find_package(PhosphorIdentity CONFIG REQUIRED)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorzones_public_HDRS
    include/PhosphorZones/PhosphorZones.h
    include/PhosphorZones/GeometryUtils.h
    include/PhosphorZones/Zone.h
    include/PhosphorZones/ZoneDefaults.h
    include/PhosphorZones/ZoneJsonKeys.h
    include/PhosphorZones/Layout.h
    include/PhosphorZones/LayoutSettingsStore.h
    include/PhosphorZones/ZoneDetector.h
    include/PhosphorZones/ZoneHighlighter.h
    include/PhosphorZones/LayoutUtils.h
    include/PhosphorZones/IZoneDetector.h
    include/PhosphorZones/IZoneLayoutRegistry.h
    include/PhosphorZones/ZonesLayoutSource.h
    include/PhosphorZones/ZonesLayoutSourceFactory.h
    include/PhosphorZones/AssignmentEntry.h
    include/PhosphorZones/LayoutRegistry.h
    include/PhosphorZones/LayoutFactory.h
    include/PhosphorZones/LayoutComputeTypes.h
    include/PhosphorZones/LayoutWorker.h
    include/PhosphorZones/LayoutComputeService.h
)

set(phosphorzones_SRCS
    src/zone.cpp
    src/layout.cpp
    src/layoutsettingsstore.cpp
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
    src/layoutregistry.cpp
    src/layoutregistry_assignments.cpp
    src/layoutregistry_batch.cpp
    src/layoutregistry_rulehelpers.cpp
    src/layoutregistry_overrides.cpp
    src/layoutregistry_persistence.cpp
    src/geometryutils.cpp
    src/layoutfactory.cpp
    src/layoutworker.cpp
    src/layoutcomputeservice.cpp
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

# Suppress GCC 14's `-Wsfinae-incomplete` for moc-generated TUs in this
# library. Qt6's MOC emits boilerplate that probes Q_OBJECT subclasses
# in SFINAE expressions before their full definition is visible
# (forward decls in cross-cutting headers like IZoneDetector.h /
# GeometryUtils.h followed by the full class in Zone.h / Layout.h),
# which trips the new GCC 14 diagnostic. The pattern is technically
# non-portable per [class.mem]/27 but is the de-facto Qt6 build shape
# and works correctly on every supported compiler. Scoped PRIVATE so
# downstream consumers still see the diagnostic on their own code.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-Wno-sfinae-incomplete" P_HAS_NO_SFINAE_INCOMPLETE)
if(P_HAS_NO_SFINAE_INCOMPLETE)
    target_compile_options(PhosphorZones PRIVATE -Wno-sfinae-incomplete)
endif()

target_link_libraries(PhosphorZones
    PUBLIC
        Qt6::Core
        Qt6::Gui
        PhosphorLayoutApi::PhosphorLayoutApi
        PhosphorGeometry::PhosphorGeometry
        # LayoutRegistry resolves the assignment cascade through a
        # WindowRuleStore + RuleEvaluator; the store type is referenced in
        # the public LayoutRegistry.h ctor signature so the dep is PUBLIC.
        PhosphorWindowRules::PhosphorWindowRules
    PRIVATE
        # Uses PhosphorIdentity::VirtualScreenId for virtual-screen id parsing
        # (geometryutils / layoutregistry_assignments); PRIVATE because the dep
        # doesn't leak through any public PhosphorZones header.
        PhosphorIdentity::PhosphorIdentity
        # GeometryUtils calls ScreenManager geometry queries, and LayoutRegistry
        # uses ScreenIdentity / VirtualScreen for assignment-cascade screen-id
        # normalisation — so this links the Runtime target (ScreenManager), which
        # re-exports Core's POD types. PRIVATE: callers of the GeometryUtils
        # functions that take a ScreenManager* pass one they already own and link
        # PhosphorScreens themselves, so the dep need not propagate.
        PhosphorScreens::Runtime
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
