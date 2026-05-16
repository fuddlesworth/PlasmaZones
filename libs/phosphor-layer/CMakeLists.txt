# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorLayer — wlr-layer-shell surface lifecycle management
#
# A Qt6/C++20 library for managing the lifecycle of layer-shell surfaces
# (overlays, panels, docks, backgrounds, modals, OSDs) with dependency
# injection, testable abstractions, and consumer-defined presets.
#
# Depends on Qt6 Core/Gui/Quick only. The default layer-shell transport
# (PhosphorWaylandTransport) is added in a later phase; step 1 ships only
# the abstract API + interfaces so consumers can start integrating now.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORLAYER_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorLayer VERSION ${PHOSPHORLAYER_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Build options
# ═══════════════════════════════════════════════════════════════════════════════

# PhosphorWaylandTransport wraps PhosphorWayland's wlr-layer-shell integration.
# Disable for minimal builds that only need XdgToplevelTransport (GNOME/
# Mutter, nested sessions, environments without wlr-layer-shell). When OFF,
# PhosphorWayland is not a build-time dependency and the corresponding header
# / source are excluded entirely.
option(PHOSPHORLAYER_WITH_PHOSPHORWAYLAND "Build PhosphorWaylandTransport (requires PhosphorWayland)" ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

# Minimum pinned to 6.6 to match PhosphorLayerConfig.cmake.in — consumers
# on older Qt would otherwise build in-tree but fail at find_package time
# with a mismatched error, and the library uses a handful of 6.6-era APIs
# (QWindow::visibility semantics, Q_DECLARE_FLAGS underlying-type handling)
# that silently misbehave on older versions rather than failing to compile.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick)
if(PHOSPHORLAYER_WITH_PHOSPHORWAYLAND)
    # PhosphorWayland provides the default ILayerShellTransport implementation.
    # Listed as PUBLIC dep so downstream consumers using PhosphorWaylandTransport
    # get PhosphorWayland headers on their include path transitively.
    find_package(PhosphorWayland CONFIG QUIET)
    if(NOT PhosphorWayland_FOUND)
        # In-tree build: PhosphorWayland was added via add_subdirectory() before us.
        if(NOT TARGET PhosphorWayland::PhosphorWayland)
            message(FATAL_ERROR "PhosphorLayer: PHOSPHORLAYER_WITH_PHOSPHORWAYLAND=ON but PhosphorWayland::PhosphorWayland not found. Either install PhosphorWayland, add it via add_subdirectory() before PhosphorLayer, or build with -DPHOSPHORLAYER_WITH_PHOSPHORWAYLAND=OFF for the xdg_toplevel-only path.")
        endif()
    endif()
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorLayer Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorlayer_SRCS
    src/role.cpp
    src/surface.cpp
    src/surfacefactory.cpp
    src/topologycoordinator.cpp
    src/defaults/defaultscreenprovider.cpp
    src/defaults/xdgtopleveltransport.cpp
    src/defaults/jsonsurfacestore.cpp
)

set(phosphorlayer_public_HDRS
    include/PhosphorLayer/PhosphorLayer.h
    include/PhosphorLayer/Role.h
    include/PhosphorLayer/SurfaceConfig.h
    include/PhosphorLayer/Surface.h
    include/PhosphorLayer/SurfaceFactory.h
    include/PhosphorLayer/ScreenSurfaceRegistry.h
    include/PhosphorLayer/TopologyCoordinator.h
    include/PhosphorLayer/IScreenProvider.h
    include/PhosphorLayer/ILayerShellTransport.h
    include/PhosphorLayer/IQmlEngineProvider.h
    include/PhosphorLayer/ISurfaceStore.h
    include/PhosphorLayer/ISurfaceAnimator.h
    include/PhosphorLayer/defaults/DefaultScreenProvider.h
    include/PhosphorLayer/defaults/XdgToplevelTransport.h
    include/PhosphorLayer/defaults/JsonSurfaceStore.h
    include/PhosphorLayer/defaults/NoOpSurfaceAnimator.h
)

if(PHOSPHORLAYER_WITH_PHOSPHORWAYLAND)
    list(APPEND phosphorlayer_SRCS src/defaults/phosphorwaylandtransport.cpp)
    list(APPEND phosphorlayer_public_HDRS include/PhosphorLayer/defaults/PhosphorWaylandTransport.h)
endif()

add_library(PhosphorLayer SHARED
    ${phosphorlayer_public_HDRS}
    ${phosphorlayer_SRCS}
)

add_library(PhosphorLayer::PhosphorLayer ALIAS PhosphorLayer)

# Generate the export header under the PhosphorLayer/ prefix so the same
# #include <PhosphorLayer/phosphorlayer_export.h> resolves in both in-tree
# builds (via BUILD_INTERFACE below) and installed consumers.
generate_export_header(PhosphorLayer
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorLayer/phosphorlayer_export.h
    EXPORT_MACRO_NAME PHOSPHORLAYER_EXPORT
)

# KDE_INSTALL_INCLUDEDIR (set below in the install block via GNUInstallDirs
# when not inherited from ECM) decides where our headers actually land.
# Resolve it here so the INSTALL_INTERFACE matches — consumers that set
# CMAKE_INSTALL_INCLUDEDIR to a non-default value (or build via ECM where
# KDE_INSTALL_INCLUDEDIR is already populated) get a consistent export.
if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

target_include_directories(PhosphorLayer
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# C++20 as a PUBLIC feature: CMAKE_CXX_STANDARD is only set by the
# `if(NOT PROJECT_VERSION)` block above, i.e. only when PhosphorLayer is the
# top-level project. When built as a subdirectory of PlasmaZones (or any
# other parent), consumers inherit whatever standard the parent set. Declare
# the requirement on the target so find_package(PhosphorLayer) consumers get
# the right compile-feature propagation regardless of build configuration.
target_compile_features(PhosphorLayer PUBLIC cxx_std_20)

target_link_libraries(PhosphorLayer
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
)

if(PHOSPHORLAYER_WITH_PHOSPHORWAYLAND)
    # PUBLIC so consumers using PhosphorWaylandTransport transitively get the
    # PhosphorWayland include path + link. The transport's implementation file
    # is the only TU that needs PhosphorWayland; the public header forward-
    # declares PhosphorWayland::LayerSurface so consumers that don't construct
    # a PhosphorWaylandTransport don't pay the include cost.
    target_link_libraries(PhosphorLayer PUBLIC PhosphorWayland::PhosphorWayland)
    target_compile_definitions(PhosphorLayer PUBLIC PHOSPHORLAYER_WITH_PHOSPHORWAYLAND=1)
endif()

set_target_properties(PhosphorLayer PROPERTIES
    VERSION ${PHOSPHORLAYER_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    # Unity builds merge multiple source files into one TU, which surfaces
    # spurious "defined but not used" warnings on anonymous-namespace
    # helpers that GCC can't fully track across the merge. The translation
    # units are small enough that unity buys us nothing here.
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

install(TARGETS PhosphorLayer
    EXPORT PhosphorLayerTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

# Headers (regular .h via DIRECTORY pattern, then extensionless camelcase
# forwarders via explicit FILES — FILES_MATCHING PATTERN doesn't pair well
# with non-".h" filenames).
install(DIRECTORY include/PhosphorLayer/
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorLayer
    FILES_MATCHING PATTERN "*.h"
)

install(FILES
    include/PhosphorLayer/Role
    include/PhosphorLayer/SurfaceConfig
    include/PhosphorLayer/Surface
    include/PhosphorLayer/SurfaceFactory
    include/PhosphorLayer/ScreenSurfaceRegistry
    include/PhosphorLayer/TopologyCoordinator
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorLayer
)

install(DIRECTORY include/PhosphorLayer/defaults/
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorLayer/defaults
    FILES_MATCHING PATTERN "*.h"
)

if(NOT PHOSPHORLAYER_WITH_PHOSPHORWAYLAND)
    # The DIRECTORY install above would have shipped PhosphorWaylandTransport.h
    # even though its implementation was excluded from the library. Remove it
    # from the install manifest after the fact — the header unconditionally
    # pulls PhosphorWayland symbols, so shipping it in a minimal build would
    # break any consumer that #includes it expecting the target to resolve.
    install(CODE "
        file(REMOVE
            \"\${CMAKE_INSTALL_PREFIX}/${KDE_INSTALL_INCLUDEDIR}/PhosphorLayer/defaults/PhosphorWaylandTransport.h\")
    ")
endif()

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorLayer/phosphorlayer_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorLayer
)

# ═══════════════════════════════════════════════════════════════════════════════
# CMake Package Config (for find_package(PhosphorLayer))
# ═══════════════════════════════════════════════════════════════════════════════

include(CMakePackageConfigHelpers)

install(EXPORT PhosphorLayerTargets
    FILE PhosphorLayerTargets.cmake
    NAMESPACE PhosphorLayer::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorLayer
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorLayerConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorLayerConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorLayer
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorLayerConfigVersion.cmake"
    VERSION ${PHOSPHORLAYER_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorLayerConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorLayerConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorLayer
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
