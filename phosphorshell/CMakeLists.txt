# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorShell — Wayland layer-shell surfaces with GPU-accelerated shader effects
#
# A Qt6/C++20 library for creating zwlr_layer_shell_v1 surfaces with optional
# Shadertoy-compatible fragment shader rendering via Qt RHI.
#
# Can be built standalone or as a subdirectory of PlasmaZones.

cmake_minimum_required(VERSION 3.16)

# When built as a subdirectory, inherit the parent project's version.
# When standalone, set our own.
if(NOT PROJECT_VERSION)
    project(PhosphorShell VERSION 0.1.0 LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

# These are likely already found by the parent project when built in-tree.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick)
find_package(Qt6GuiPrivate CONFIG REQUIRED)
find_package(Qt6ShaderToolsPrivate CONFIG REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS WaylandClient ShaderTools)
find_package(Qt6WaylandClientPrivate REQUIRED)
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
    COMMENT "PhosphorShell: generating wlr-layer-shell protocol code"
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
# PhosphorShell Library
# ═══════════════════════════════════════════════════════════════════════════════

# --- Public headers with Q_OBJECT (must be listed for AUTOMOC) ---
set(phosphorshell_public_HDRS
    include/PhosphorShell/LayerSurface.h
    include/PhosphorShell/ShaderRegistry.h
    # ShaderEffect.h added when RHI render node is extracted
)

# --- Layer-shell sources ---
set(phosphorshell_layershell_SRCS
    src/layersurface.cpp
    src/qpa/layershellintegration.cpp
    src/qpa/layershellintegration.h
    src/qpa/layershellwindow.cpp
    src/qpa/layershellwindow.h
    src/qpa/xdg_popup_stub.c
    ${_ps_gen_dir}/wlr-layer-shell-unstable-v1-protocol.c
)

# --- Shader pipeline sources ---
set(phosphorshell_shader_SRCS
    src/shaderregistry.cpp
    src/shaderincluderesolver.cpp
    src/wallpaperprovider.cpp
    # ShaderEffect + RenderNode RHI deferred until zone UBO generalization
    # src/shadereffect.cpp
    # src/rendernode_rhi.cpp
)

add_library(PhosphorShell SHARED
    ${phosphorshell_public_HDRS}
    ${phosphorshell_layershell_SRCS}
    ${phosphorshell_shader_SRCS}
)

add_library(PhosphorShell::PhosphorShell ALIAS PhosphorShell)

generate_export_header(PhosphorShell
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphorshell_export.h
    EXPORT_MACRO_NAME PHOSPHORSHELL_EXPORT
)

target_include_directories(PhosphorShell
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${_ps_gen_dir}
)

target_link_libraries(PhosphorShell
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
    PRIVATE
        Qt6::GuiPrivate
        Qt6::WaylandClientPrivate
        Qt6::ShaderToolsPrivate
        Wayland::Client
)

set_target_properties(PhosphorShell PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# ═══════════════════════════════════════════════════════════════════════════════
# QPA Shell Integration Plugin (loaded at runtime by Qt)
# ═══════════════════════════════════════════════════════════════════════════════

add_library(phosphorshell-qpa MODULE src/qpa/phosphorshellplugin.cpp)
target_link_libraries(phosphorshell-qpa PRIVATE
    PhosphorShell
    Qt6::WaylandClientPrivate
    Wayland::Client
)
target_include_directories(phosphorshell-qpa PRIVATE ${_ps_gen_dir})
set_target_properties(phosphorshell-qpa PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins/wayland-shell-integration
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

include(GNUInstallDirs)

# Library
install(TARGETS PhosphorShell
    EXPORT PhosphorShellTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# Public headers
install(DIRECTORY include/PhosphorShell/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/PhosphorShell
    FILES_MATCHING PATTERN "*.h"
)

# Export header
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/phosphorshell_export.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/PhosphorShell
)

# QPA plugin
if(NOT DEFINED QT6_INSTALL_PLUGINS)
    get_target_property(QT6_INSTALL_PLUGINS Qt6::Core QT_INSTALL_PLUGINS)
    if(NOT QT6_INSTALL_PLUGINS)
        set(QT6_INSTALL_PLUGINS "${CMAKE_INSTALL_LIBDIR}/qt6/plugins")
    endif()
endif()
install(TARGETS phosphorshell-qpa
    DESTINATION ${QT6_INSTALL_PLUGINS}/wayland-shell-integration
)

# GLSL include library
install(DIRECTORY shaders/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/phosphorshell/shaders
    FILES_MATCHING PATTERN "*.glsl"
)

# Protocol XML (for downstream development)
install(DIRECTORY protocols/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/phosphorshell/protocols
    FILES_MATCHING PATTERN "*.xml"
)
