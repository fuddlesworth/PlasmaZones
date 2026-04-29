# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorShaders — shader-domain primitives and registry.
#
# Shadertoy-compatible UBO layout (BaseUniforms), the consumer-side
# IUniformExtension contract, GLSL #include resolver, wallpaper-provider
# abstraction, and the metadata-pack-driven ShaderPackRegistry. The
# library is Wayland- and RHI-agnostic — phosphor-rendering builds on
# top of the headers here, and phosphor-shell (the Wayland integration)
# does not depend on it.
#
# Phase B1 of docs/phosphor-architecture-refactor-plan.md: extracted
# from phosphor-shell so the layer-shell QPA and shader concerns can be
# reasoned about independently. The eventual Phase C1 target
# `MetadataPackScanStrategy<Payload>` will let ShaderPackRegistry and
# AnimationShaderRegistry collapse onto the same pack-walker scaffold.
#
# Depends on Qt6 Core+Gui (Gui for QImage in the wallpaper cache) and
# PhosphorFsLoader for WatchedDirectorySet. No QML facade today —
# downstream PlasmaZones code exposes ShaderPackRegistry to QML via
# context properties, so this lib stays C++-only. A `Phosphor.Shaders`
# QML module can be added (alongside `QML_ELEMENT` annotations on the
# C++ types) when an out-of-tree consumer needs `import Phosphor.Shaders`
# resolution.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSHADERS_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorShaders VERSION ${PHOSPHORSHADERS_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui)

# PhosphorFsLoader — shared filesystem-loader scaffolding (used by
# ShaderPackRegistry's WatchedDirectorySet + ShaderScanStrategy).
# find_package is a no-op when building in-tree (parent CMakeLists adds
# the target first); the explicit lookup lets the lib stand alone
# against an installed PhosphorFsLoader.
if(NOT TARGET PhosphorFsLoader::PhosphorFsLoader)
    find_package(PhosphorFsLoader 0.1.0 REQUIRED)
endif()

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Public headers
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorshaders_public_HDRS
    include/PhosphorShaders/BaseUniforms.h
    include/PhosphorShaders/IUniformExtension.h
    include/PhosphorShaders/IWallpaperProvider.h
    include/PhosphorShaders/ShaderIncludeResolver.h
    include/PhosphorShaders/ShaderPackRegistry.h
)

# Extension-less Qt-style camelcase forwarders, installed alongside the .h files.
set(phosphorshaders_camelcase_HDRS
    include/PhosphorShaders/IUniformExtension
    include/PhosphorShaders/IWallpaperProvider
    include/PhosphorShaders/ShaderIncludeResolver
    include/PhosphorShaders/ShaderPackRegistry
)

set(phosphorshaders_SRCS
    src/shaderincluderesolver.cpp
    src/shaderpackregistry.cpp
    src/wallpaperprovider.cpp
)

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorShaders Library
# ═══════════════════════════════════════════════════════════════════════════════
#
# Regular SHARED library carrying the C++ surface — BaseUniforms,
# IUniformExtension, the include resolver, the wallpaper provider, and
# the pack registry. Exportable + installable so downstream find_package
# consumers (notably PhosphorRendering) can link it.
#
# No QML facade target here — see the file header for rationale. If a
# `Phosphor.Shaders` module is added later, follow phosphor-animation-qml
# 's pattern: separate STATIC qml-module target that links this library
# (so the QML registration sub-targets don't leak into install(EXPORT)).

add_library(PhosphorShaders SHARED
    ${phosphorshaders_public_HDRS}
    ${phosphorshaders_SRCS}
)

add_library(PhosphorShaders::PhosphorShaders ALIAS PhosphorShaders)

generate_export_header(PhosphorShaders
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorShaders/phosphorshaders_export.h
    EXPORT_MACRO_NAME PHOSPHORSHADERS_EXPORT
)

target_include_directories(PhosphorShaders
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(PhosphorShaders PUBLIC cxx_std_20)

target_link_libraries(PhosphorShaders
    PUBLIC
        Qt6::Core
        Qt6::Gui
        # PhosphorFsLoader is publicly exposed via ShaderPackRegistry's
        # `LiveReload` parameter — consumers including
        # `<PhosphorShaders/ShaderPackRegistry.h>` need its headers on
        # their include path.
        PhosphorFsLoader::PhosphorFsLoader
)

set_target_properties(PhosphorShaders PROPERTIES
    VERSION ${PHOSPHORSHADERS_VERSION}
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
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorShaders" OR BUILD_TESTING)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
        enable_testing()
        add_subdirectory(tests)
    endif()
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_BINDIR)
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

# The C++ library exports + installs as a normal find_package target.

install(TARGETS PhosphorShaders
    EXPORT PhosphorShadersTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

# Public headers — split between regular .h files and extensionless Qt-style
# camelcase forwarders. FILES_MATCHING with PATTERN doesn't pair well with
# non-".h" filenames, so install the forwarders explicitly.
install(DIRECTORY include/PhosphorShaders/
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorShaders
    FILES_MATCHING PATTERN "*.h"
)

install(FILES ${phosphorshaders_camelcase_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorShaders
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorShaders/phosphorshaders_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorShaders
)

# GLSL include library (consumed by user shaders via #include).
if(NOT DEFINED KDE_INSTALL_DATADIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_DATADIR ${CMAKE_INSTALL_DATADIR})
endif()
install(DIRECTORY shaders/
    DESTINATION ${KDE_INSTALL_DATADIR}/phosphorshaders/shaders
    FILES_MATCHING PATTERN "*.glsl"
)

install(EXPORT PhosphorShadersTargets
    FILE PhosphorShadersTargets.cmake
    NAMESPACE PhosphorShaders::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorShaders
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorShadersConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorShadersConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorShaders
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorShadersConfigVersion.cmake"
    VERSION ${PHOSPHORSHADERS_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorShadersConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorShadersConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorShaders
)
