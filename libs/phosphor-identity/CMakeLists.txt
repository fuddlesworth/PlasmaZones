# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorIdentity — stable cross-process window identity primitives.
#
# Owns the composite window-id wire format (`appId|internalUuid`) plus the
# string utilities that parse, build, and pattern-match it.  Used by every
# PlasmaZones component that needs to refer to a window stably across
# process boundaries:
#
#   • The KWin effect (separate process) constructs ids from
#     KWin::Window::internalId() + desktopFileName() / windowClass().
#   • The daemon parses them in WindowTrackingService.
#   • PhosphorZones uses them in app-rule matching (Layout::matchAppRule).
#   • Future PhosphorTiles runtime engine will hold them per-tiled-window.
#
# This library carries no Qt-Widgets / Wayland / KWin / Gui dependencies —
# just Qt6::Core.  That makes it cheap to link from anywhere, including the
# KWin effect plugin which is highly cost-sensitive about transitive deps.
#
# INTERFACE library: every helper in WindowId.h is `inline`, so there is no
# compiled object code to ship — the previous SHARED target produced an
# empty .so that existed only to anchor an export header.  An INTERFACE
# target exposes the include dir + the Qt6::Core requirement to consumers
# without producing a build artifact or a runtime dlopen cost.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORIDENTITY_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorIdentity VERSION ${PHOSPHORIDENTITY_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core)

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphoridentity_public_HDRS
    include/PhosphorIdentity/PhosphorIdentity.h
    include/PhosphorIdentity/VirtualScreenId.h
    include/PhosphorIdentity/WindowId.h
)

add_library(PhosphorIdentity INTERFACE)
add_library(PhosphorIdentity::PhosphorIdentity ALIAS PhosphorIdentity)

# Attach the header set so IDEs see the public headers as project members
# even though INTERFACE libraries have no compilation step.
target_sources(PhosphorIdentity INTERFACE
    FILE_SET HEADERS
    BASE_DIRS include
    FILES ${phosphoridentity_public_HDRS}
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

target_include_directories(PhosphorIdentity
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorIdentity INTERFACE cxx_std_20)

target_link_libraries(PhosphorIdentity
    INTERFACE
        Qt6::Core
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_LIBDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

install(TARGETS PhosphorIdentity
    EXPORT PhosphorIdentityTargets
    FILE_SET HEADERS DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorIdentity
)

install(EXPORT PhosphorIdentityTargets
    FILE PhosphorIdentityTargets.cmake
    NAMESPACE PhosphorIdentity::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorIdentity
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorIdentityConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorIdentityConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorIdentity
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorIdentityConfigVersion.cmake"
    VERSION ${PHOSPHORIDENTITY_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorIdentityConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorIdentityConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorIdentity
)
