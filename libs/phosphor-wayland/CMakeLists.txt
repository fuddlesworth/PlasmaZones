# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorWayland — Wayland layer-shell integration (wlr-layer-shell-v1)
#
# A Qt6/C++20 library that wraps zwlr_layer_shell_v1 behind a Qt-friendly
# QPA shell-integration plugin (`phosphorwayland-qpa`) plus a thin
# `LayerSurface` helper for setting layer-surface properties on a
# QWindow. The shader pipeline (BaseUniforms, IUniformExtension,
# ShaderIncludeResolver, IWallpaperProvider, ShaderRegistry) lives
# in the sibling `phosphor-shaders` library — this lib is now purely
# the Wayland integration. Phase D of the architecture refactor
# (docs/phosphor-architecture-refactor-plan.md) renames this lib to
# `phosphor-wayland` once the only remaining consumer is the QPA
# plugin + LayerSurface.
#
# Can be built standalone or as a subdirectory of PlasmaZones.

cmake_minimum_required(VERSION 3.16)

# PhosphorWayland's own version — used for SOVERSION. When split to a
# standalone repo, this becomes the project() version.
set(PHOSPHORWAYLAND_VERSION "0.1.0")

# When built as a subdirectory, inherit the parent project's settings.
# When standalone, set our own.
if(NOT PROJECT_VERSION)
    project(PhosphorWayland VERSION ${PHOSPHORWAYLAND_VERSION} LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

# These are likely already found by the parent project when built in-tree.
find_package(Qt6 REQUIRED COMPONENTS Core Gui)

if(NOT TARGET Qt6::GuiPrivate)
    find_package(Qt6GuiPrivate CONFIG REQUIRED)
endif()
find_package(Qt6 REQUIRED COMPONENTS WaylandClient)
if(NOT TARGET Qt6::WaylandClientPrivate)
    find_package(Qt6WaylandClientPrivate REQUIRED)
endif()
find_package(Wayland REQUIRED COMPONENTS Client)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

# ═══════════════════════════════════════════════════════════════════════════════
# Wayland Protocol Code Generation
# ═══════════════════════════════════════════════════════════════════════════════

set(_ps_proto_dir ${CMAKE_CURRENT_SOURCE_DIR}/protocols)
set(_ps_gen_dir ${CMAKE_CURRENT_BINARY_DIR}/generated-protocols)
file(MAKE_DIRECTORY ${_ps_gen_dir})

add_custom_command(
    OUTPUT ${_ps_gen_dir}/wlr-layer-shell-unstable-v1-client-protocol.h
           ${_ps_gen_dir}/wlr-layer-shell-unstable-v1-protocol.c
    COMMAND ${WAYLAND_SCANNER} client-header
            ${_ps_proto_dir}/wlr-layer-shell-unstable-v1.xml
            ${_ps_gen_dir}/wlr-layer-shell-unstable-v1-client-protocol.h
    COMMAND ${WAYLAND_SCANNER} private-code
            ${_ps_proto_dir}/wlr-layer-shell-unstable-v1.xml
            ${_ps_gen_dir}/wlr-layer-shell-unstable-v1-protocol.c
    DEPENDS ${_ps_proto_dir}/wlr-layer-shell-unstable-v1.xml
    COMMENT "PhosphorWayland: generating wlr-layer-shell protocol code"
)

# Generated C code: skip PCH and unity build
set_source_files_properties(
    ${_ps_gen_dir}/wlr-layer-shell-unstable-v1-protocol.c
    src/qpa/xdg_popup_stub.c
    PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON
        SKIP_UNITY_BUILD_INCLUSION ON
)

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorWayland Library
# ═══════════════════════════════════════════════════════════════════════════════

# --- Public headers with Q_OBJECT (must be listed for AUTOMOC) ---
set(phosphorwayland_public_HDRS
    include/PhosphorWayland/LayerSurface.h
)

# --- Layer-shell sources ---
set(phosphorwayland_layershell_SRCS
    src/layersurface.cpp
    src/qpa/layershellintegration.cpp
    src/qpa/layershellintegration.h
    src/qpa/layershellwindow.cpp
    src/qpa/layershellwindow.h
    src/qpa/xdg_popup_stub.c
    ${_ps_gen_dir}/wlr-layer-shell-unstable-v1-protocol.c
)

add_library(PhosphorWayland SHARED
    ${phosphorwayland_public_HDRS}
    ${phosphorwayland_layershell_SRCS}
)

add_library(PhosphorWayland::PhosphorWayland ALIAS PhosphorWayland)

generate_export_header(PhosphorWayland
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphorwayland_export.h
    EXPORT_MACRO_NAME PHOSPHORWAYLAND_EXPORT
)

# Resolve KDE_INSTALL_INCLUDEDIR here so INSTALL_INTERFACE can reference it.
# Without this, a consumer that overrides CMAKE_INSTALL_INCLUDEDIR (or the
# KDE-flavoured equivalent) gets an export that hard-codes bare `include/`
# while install(DIRECTORY) below still writes to the overridden location —
# find_package resolves but consumers fail to see the headers.
if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

target_include_directories(PhosphorWayland
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${_ps_gen_dir}
)

# C++20 as a PUBLIC target feature: CMAKE_CXX_STANDARD is only set by the
# `if(NOT PROJECT_VERSION)` block above — i.e. only when PhosphorWayland is
# the top-level project. When built as a subdirectory of PlasmaZones (or
# any other parent) consumers inherit whatever standard the parent picked.
# Declare the requirement on the target so find_package(PhosphorWayland)
# consumers get the right compile-feature propagation regardless of build
# configuration.
target_compile_features(PhosphorWayland PUBLIC cxx_std_20)

target_link_libraries(PhosphorWayland
    PUBLIC
        Qt6::Core
        Qt6::Gui
    PRIVATE
        Qt6::GuiPrivate
        Qt6::WaylandClientPrivate
        Wayland::Client
)

set_target_properties(PhosphorWayland PROPERTIES
    VERSION ${PHOSPHORWAYLAND_VERSION}
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
# QPA Shell Integration Plugin (loaded at runtime by Qt)
# ═══════════════════════════════════════════════════════════════════════════════

add_library(phosphorwayland-qpa MODULE src/qpa/phosphorwaylandplugin.cpp)
target_link_libraries(phosphorwayland-qpa PRIVATE
    PhosphorWayland
    Qt6::WaylandClientPrivate
    Wayland::Client
)
target_include_directories(phosphorwayland-qpa PRIVATE ${_ps_gen_dir})
set_target_properties(phosphorwayland-qpa PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins/wayland-shell-integration
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

# Use KDE install dirs when available (in-tree build), fall back to GNUInstallDirs
if(NOT DEFINED KDE_INSTALL_LIBDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_DATADIR ${CMAKE_INSTALL_DATADIR})
endif()

if(NOT DEFINED KDE_INSTALL_BINDIR)
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

# Library
install(TARGETS PhosphorWayland
    EXPORT PhosphorWaylandTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

# Export the targets file so downstream find_package(PhosphorWayland) works AND
# so dependent libraries (PhosphorRendering) can install their own export sets
# that reference PhosphorWayland::PhosphorWayland. Without this, install(EXPORT) on
# any consumer fails: "target requires target PhosphorWayland that is not in any
# export set."
install(EXPORT PhosphorWaylandTargets
    FILE PhosphorWaylandTargets.cmake
    NAMESPACE PhosphorWayland::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorWayland
)

# Generate and install a minimal find_package config so consumers can
# `find_dependency(PhosphorWayland)`.
include(CMakePackageConfigHelpers)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/PhosphorWaylandConfig.cmake.in"
"@PACKAGE_INIT@\n"
"include(CMakeFindDependencyMacro)\n"
"find_dependency(Qt6 COMPONENTS Core Gui)\n"
"include(\"\${CMAKE_CURRENT_LIST_DIR}/PhosphorWaylandTargets.cmake\")\n"
"check_required_components(PhosphorWayland)\n"
)
configure_package_config_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorWaylandConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorWaylandConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorWayland
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorWaylandConfigVersion.cmake"
    VERSION ${PHOSPHORWAYLAND_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorWaylandConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorWaylandConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorWayland
)

# Public headers
install(DIRECTORY include/PhosphorWayland/
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorWayland
    FILES_MATCHING PATTERN "*.h"
)

# Export header
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/phosphorwayland_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorWayland
)

# QPA plugin
if(NOT DEFINED QT6_INSTALL_PLUGINS)
    get_target_property(QT6_INSTALL_PLUGINS Qt6::Core QT_INSTALL_PLUGINS)
    if(NOT QT6_INSTALL_PLUGINS)
        set(QT6_INSTALL_PLUGINS "${KDE_INSTALL_LIBDIR}/qt6/plugins")
    endif()
endif()
install(TARGETS phosphorwayland-qpa
    DESTINATION ${QT6_INSTALL_PLUGINS}/wayland-shell-integration
)

# Protocol XML (for downstream development)
install(DIRECTORY protocols/
    DESTINATION ${KDE_INSTALL_DATADIR}/phosphorwayland/protocols
    FILES_MATCHING PATTERN "*.xml"
)
