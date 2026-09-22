# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorOverlay - helper library for building overlays with PhosphorLayer.
#
# Provides the per-screen layer-shell shell host, per-slot animator
# coordination, and screen hot-plug / rekey plumbing that any Phosphor
# shell needs to compose multi-slot overlays. Content (zones, OSDs,
# pickers, snap-assist) stays with the consumer; this library knows
# about surfaces and slots only.
#
# Depends on Qt6::Core, Qt6::Quick, and the Phosphor primitive libs the
# overlay subsystem rides on:
#   • PhosphorLayer            (wlr-layer-shell wire primitives)
#   • PhosphorShellPatterns    (UI-pattern recipes - axis 2)
#   • PhosphorSurfaces         (managed surface lifecycle + scope-gen)
#   • PhosphorAnimation        (SurfaceAnimator for show/hide)
#   • PhosphorScreens          (screen-topology + identity)

cmake_minimum_required(VERSION 3.16)

set(PHOSPHOROVERLAY_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorOverlay VERSION ${PHOSPHOROVERLAY_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Quick)

# In-tree builds set up the Phosphor::* targets via add_subdirectory in
# the top-level CMakeLists; stand-alone builds need find_package. Guard
# each one so the in-tree path is a no-op.
if(NOT TARGET PhosphorLayer::PhosphorLayer)
    find_package(PhosphorLayer CONFIG REQUIRED)
endif()
if(NOT TARGET PhosphorShellPatterns::PhosphorShellPatterns)
    find_package(PhosphorShellPatterns CONFIG REQUIRED)
endif()
if(NOT TARGET PhosphorSurfaces::PhosphorSurfaces)
    find_package(PhosphorSurfaces CONFIG REQUIRED)
endif()
if(NOT TARGET PhosphorAnimation::PhosphorAnimation)
    find_package(PhosphorAnimation CONFIG REQUIRED)
endif()
if(NOT TARGET PhosphorScreens::Core)
    find_package(PhosphorScreens CONFIG REQUIRED)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorOverlay Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphoroverlay_SRCS
    src/shellhost.cpp
)

set(phosphoroverlay_public_HDRS
    include/PhosphorOverlay/PhosphorOverlay.h
    include/PhosphorOverlay/ShellHost.h
    include/PhosphorOverlay/ShellState.h
    include/PhosphorOverlay/SlotEntry.h
)

add_library(PhosphorOverlay SHARED
    ${phosphoroverlay_public_HDRS}
    ${phosphoroverlay_SRCS}
)

add_library(PhosphorOverlay::PhosphorOverlay ALIAS PhosphorOverlay)

generate_export_header(PhosphorOverlay
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorOverlay/phosphoroverlay_export.h
    EXPORT_MACRO_NAME PHOSPHOROVERLAY_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorOverlay
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorOverlay PUBLIC cxx_std_20)

target_link_libraries(PhosphorOverlay
    PUBLIC
        Qt6::Core
        Qt6::Quick
        PhosphorLayer::PhosphorLayer
        PhosphorShellPatterns::PhosphorShellPatterns
        PhosphorSurfaces::PhosphorSurfaces
        PhosphorAnimation::PhosphorAnimation
        PhosphorScreens::Core
)

set_target_properties(PhosphorOverlay PROPERTIES
    VERSION ${PHOSPHOROVERLAY_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorOverlay" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS PhosphorOverlay
    EXPORT PhosphorOverlayTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

install(DIRECTORY include/PhosphorOverlay/
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorOverlay
    FILES_MATCHING PATTERN "*.h"
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorOverlay/phosphoroverlay_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorOverlay
)

install(EXPORT PhosphorOverlayTargets
    FILE PhosphorOverlayTargets.cmake
    NAMESPACE PhosphorOverlay::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorOverlay
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorOverlayConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorOverlayConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorOverlay
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorOverlayConfigVersion.cmake"
    VERSION ${PHOSPHOROVERLAY_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorOverlayConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorOverlayConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorOverlay
)
