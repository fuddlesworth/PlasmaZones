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
find_package(Qt6GuiPrivate CONFIG REQUIRED)
find_package(Qt6ShaderToolsPrivate CONFIG REQUIRED)

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

generate_export_header(PhosphorRendering
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphorrendering_export.h
    EXPORT_MACRO_NAME PHOSPHORRENDERING_EXPORT
)

target_include_directories(PhosphorRendering
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

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
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_LIBDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

install(TARGETS PhosphorRendering
    EXPORT PhosphorRenderingTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

install(DIRECTORY include/PhosphorRendering/
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorRendering
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "ShaderCompiler"
    PATTERN "ShaderNodeRhi"
    PATTERN "ShaderEffect"
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/phosphorrendering_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorRendering
)

# ═══════════════════════════════════════════════════════════════════════════════
# CMake Package Config (for find_package(PhosphorRendering))
# ═══════════════════════════════════════════════════════════════════════════════
#
# Only generate export/config when PhosphorShell also has an export set.
# When built as a subdirectory of PlasmaZones (the default), the ALIAS target
# PhosphorRendering::PhosphorRendering is sufficient. The export set is for
# standalone installs where consumers use find_package().

include(CMakePackageConfigHelpers)

if(TARGET PhosphorShell AND NOT TARGET PhosphorShell::PhosphorShellExported)
    # PhosphorShell doesn't have an export set yet — skip export to avoid CMake error.
    # The install(TARGETS ... EXPORT) above still records the intent; it just won't
    # be written to disk until PhosphorShell gets matching export support.
    message(STATUS "PhosphorRendering: skipping CMake export (PhosphorShell has no export set)")
else()
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
endif()
