# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorPlacement — Window placement state tracking service.
#
# Tracks window→zone assignments, floating state, auto-snap, resnap,
# and rotation. Compositor-agnostic: any consumer that provides an
# IGeometryResolver gets full placement tracking without the daemon.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORPLACEMENT_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorPlacement VERSION ${PHOSPHORPLACEMENT_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

include(GenerateExportHeader)

set(CMAKE_AUTOMOC ON)

add_library(PhosphorPlacement SHARED
    include/PhosphorPlacement/IGeometryResolver.h
    include/PhosphorPlacement/PlacementConfig.h
    include/PhosphorPlacement/WindowTrackingService.h
    src/WindowTrackingService.cpp
    src/navigation.cpp
    src/resnap.cpp
    src/lifecycle.cpp
    src/virtualscreenmigration.cpp
    src/placementvalidation_p.h
    src/snap.cpp
    src/placementlogging.h
    src/placementlogging.cpp
)
add_library(PhosphorPlacement::PhosphorPlacement ALIAS PhosphorPlacement)

generate_export_header(PhosphorPlacement
    EXPORT_FILE_NAME phosphorplacement_export.h
    EXPORT_MACRO_NAME PHOSPHORPLACEMENT_EXPORT
)

target_include_directories(PhosphorPlacement
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_compile_features(PhosphorPlacement PUBLIC cxx_std_20)

target_link_libraries(PhosphorPlacement
    PUBLIC
        Qt6::Core
        Qt6::Gui
        PhosphorEngine::PhosphorEngine
        PhosphorSnapEngine::PhosphorSnapEngine
        PhosphorZones::PhosphorZones
        PhosphorProtocol::Types
    PRIVATE
        PhosphorScreens::Runtime  # WindowTrackingService drives a ScreenManager*
        PhosphorIdentity::PhosphorIdentity
        PhosphorWorkspaces::PhosphorWorkspaces
        PhosphorLayoutApi::PhosphorLayoutApi
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

set_target_properties(PhosphorPlacement PROPERTIES
    VERSION ${PHOSPHORPLACEMENT_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

install(TARGETS PhosphorPlacement
    EXPORT PhosphorPlacementTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(DIRECTORY include/PhosphorPlacement
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/phosphorplacement_export.h"
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorPlacement
)

install(EXPORT PhosphorPlacementTargets
    FILE PhosphorPlacementTargets.cmake
    NAMESPACE PhosphorPlacement::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorPlacement
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorPlacementConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorPlacementConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorPlacement
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorPlacementConfigVersion.cmake"
    VERSION ${PHOSPHORPLACEMENT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorPlacementConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorPlacementConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorPlacement
)
