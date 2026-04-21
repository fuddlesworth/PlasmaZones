# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorRendering — GPU-accelerated shader rendering via Qt RHI
#
# A Qt6/C++20 library for Shadertoy-compatible fragment shader rendering
# with multipass buffer support, texture management, and uniform extensions.
#
# Depends on PhosphorShell for BaseUniforms, IUniformExtension, and
# ShaderIncludeResolver.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORRENDERING_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorRendering VERSION ${PHOSPHORRENDERING_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick)
if(NOT TARGET Qt6::GuiPrivate)
    find_package(Qt6GuiPrivate CONFIG REQUIRED)
endif()
if(NOT TARGET Qt6::ShaderToolsPrivate)
    find_package(Qt6ShaderToolsPrivate CONFIG REQUIRED)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorRendering Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorrendering_SRCS
    src/shadercompiler.cpp
    src/shadernoderhicore.cpp
    src/shadernoderhipipeline.cpp
    src/shadernoderhiuniforms.cpp
    src/shadernoderhisetters.cpp
    src/shadereffect.cpp
)

set(phosphorrendering_public_HDRS
    include/PhosphorRendering/ShaderCompiler.h
    include/PhosphorRendering/ShaderNodeRhi.h
    include/PhosphorRendering/ShaderEffect.h
)

add_library(PhosphorRendering SHARED
    ${phosphorrendering_public_HDRS}
    ${phosphorrendering_SRCS}
)

add_library(PhosphorRendering::PhosphorRendering ALIAS PhosphorRendering)

# Generate the export header under the PhosphorRendering/ prefix so the
# same `#include <PhosphorRendering/phosphorrendering_export.h>` resolves in
# both in-tree builds (via BUILD_INTERFACE below) and installed consumers
# (the header is installed alongside the other public headers).
generate_export_header(PhosphorRendering
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorRendering/phosphorrendering_export.h
    EXPORT_MACRO_NAME PHOSPHORRENDERING_EXPORT
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

target_include_directories(PhosphorRendering
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# C++20 as a PUBLIC target feature: CMAKE_CXX_STANDARD is only set by the
# `if(NOT PROJECT_VERSION)` block above — i.e. only when PhosphorRendering
# is the top-level project. When built as a subdirectory of PlasmaZones (or
# any other parent) consumers inherit whatever standard the parent picked.
# Declare the requirement on the target so find_package(PhosphorRendering)
# consumers get the right compile-feature propagation regardless of build
# configuration.
target_compile_features(PhosphorRendering PUBLIC cxx_std_20)

target_link_libraries(PhosphorRendering
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        PhosphorShell::PhosphorShell
    PRIVATE
        Qt6::GuiPrivate
        Qt6::ShaderToolsPrivate
)

set_target_properties(PhosphorRendering PROPERTIES
    VERSION ${PHOSPHORRENDERING_VERSION}
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

install(TARGETS PhosphorRendering
    EXPORT PhosphorRenderingTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

# Headers (the regular .h files via DIRECTORY pattern, then the extensionless
# camelcase forwarders via explicit FILES — FILES_MATCHING with PATTERN doesn't
# pair well with non-".h" filenames and is easy to break by accident).
install(DIRECTORY include/PhosphorRendering/
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorRendering
    FILES_MATCHING PATTERN "*.h"
)

install(FILES
    include/PhosphorRendering/ShaderCompiler
    include/PhosphorRendering/ShaderNodeRhi
    include/PhosphorRendering/ShaderEffect
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorRendering
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorRendering/phosphorrendering_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorRendering
)

# ═══════════════════════════════════════════════════════════════════════════════
# CMake Package Config (for find_package(PhosphorRendering))
# ═══════════════════════════════════════════════════════════════════════════════
#
# PhosphorShell exports its targets (PhosphorShell::PhosphorShell) and provides
# a find_package config, so we can install our own export set unconditionally —
# downstream `find_package(PhosphorRendering)` resolves the PhosphorShell
# dependency through find_dependency() in our generated config.

include(CMakePackageConfigHelpers)

install(EXPORT PhosphorRenderingTargets
    FILE PhosphorRenderingTargets.cmake
    NAMESPACE PhosphorRendering::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorRendering
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorRenderingConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorRenderingConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorRendering
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorRenderingConfigVersion.cmake"
    VERSION ${PHOSPHORRENDERING_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorRenderingConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorRenderingConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorRendering
)
