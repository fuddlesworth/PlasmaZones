# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorSurface — surface-shader subsystem for Phosphor-based shells.
#
# A Qt6/C++20 library providing the third sibling shader category
# alongside phosphor-animation's animation/transition shaders and
# phosphor-shaders' overlay/zone-background shaders:
#
#   * SurfaceShaderContract — cross-runtime named-uniform contract for
#     persistent per-window surface layers (border / rounded corners /
#     focus tint). Dual-runtime: the same effect.frag runs on both the
#     kwin-effect compositor path and the daemon surface-layer path.
#   * SurfaceShaderEffect — parsed metadata pack (id, shader paths,
#     parameters, texture slots) with toJson/fromJson.
#   * SurfaceShaderRegistry — Registry<SurfacePack> +
#     MetadataPackLoader<SurfacePack> with search-path management,
#     live reload, friendly-param → slot-key translation, and the
#     generated p_<id> preamble.
#
# Mirrors libs/phosphor-animation's shader-pack subsystem; trimmed to the
# parts a surface shader needs (no QML, no rendering, no surface lifecycle).
# Multipass IS modelled here as pack metadata (SurfaceShaderEffect's
# bufferShaderPaths / bufferScale / bufferWrap, used by the glow pack); the
# buffer-chain composition itself lives in the consumers (kwin-effect /
# daemon), not this library.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSURFACE_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorSurface VERSION ${PHOSPHORSURFACE_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ===============================================================================
# Dependencies
# ===============================================================================

# Pin to 6.6 to match sibling phosphor-* libraries.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui)

# phosphor-fsloader -- filesystem-backed loader scaffolding (LiveReload /
# RegistrationOrder enums + MetadataPackLoader machinery via PhosphorRegistry).
if(NOT TARGET PhosphorFsLoader::PhosphorFsLoader)
    find_package(PhosphorFsLoader 0.1.0 REQUIRED)
endif()

# Header-only consumption of <PhosphorShaders/CustomParamsKey.h> plus the
# slot-assignment / preamble helpers (<PhosphorShaders/ShaderParamPreamble.h>).
if(NOT TARGET PhosphorShaders::PhosphorShaders)
    find_package(PhosphorShaders CONFIG REQUIRED)
endif()

# PhosphorRegistry — SurfaceShaderRegistry composes Registry<SurfacePack>
# + MetadataPackLoader<SurfacePack> (publicly exposed via its header).
if(NOT TARGET PhosphorRegistry::PhosphorRegistry)
    find_package(PhosphorRegistry 0.1.0 REQUIRED)
endif()

# Position-independent code for consistency with sibling libs.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Suppress -Wmissing-include-dirs for Qt-managed AUTOMOC scratch dirs.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

# ===============================================================================
# Public headers
# ===============================================================================

set(phosphorsurface_public_HDRS
    include/PhosphorSurface/SurfaceShaderContract.h
    include/PhosphorSurface/SurfaceShaderUniforms.h
    include/PhosphorSurface/SurfaceUniformProfile.h
    include/PhosphorSurface/SurfaceShaderEffect.h
    include/PhosphorSurface/SurfaceShaderRegistry.h
    include/PhosphorSurface/DecorationProfile.h
    include/PhosphorSurface/DecorationProfileTree.h
    include/PhosphorSurface/DecorationSupportedPaths.h
)

# ===============================================================================
# Sources
# ===============================================================================

set(phosphorsurface_SRCS
    src/surfaceuniformprofile.cpp
    src/surfaceshadereffect.cpp
    src/surfaceshaderregistry.cpp
    src/decorationprofile.cpp
    src/decorationprofiletree.cpp
    src/surface_contract_pins.cpp
)

# ===============================================================================
# PhosphorSurface Library
# ===============================================================================

add_library(PhosphorSurface SHARED
    ${phosphorsurface_public_HDRS}
    ${phosphorsurface_SRCS}
)

add_library(PhosphorSurface::PhosphorSurface ALIAS PhosphorSurface)

generate_export_header(PhosphorSurface
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorSurface/phosphorsurface_export.h
    EXPORT_MACRO_NAME PHOSPHORSURFACE_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

target_include_directories(PhosphorSurface
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorSurface PUBLIC cxx_std_20)

# PhosphorRegistry / PhosphorFsLoader / PhosphorShaders are PUBLIC because
# SurfaceShaderRegistry's public header exposes Registry<SurfacePack>,
# MetadataPackLoader<SurfacePack>, and the PhosphorFsLoader LiveReload /
# RegistrationOrder enums in its method signatures; SurfaceShaderContract.h
# includes <PhosphorShaders/CustomParamsKey.h>.
target_link_libraries(PhosphorSurface
    PUBLIC
        Qt6::Core
        Qt6::Gui
        PhosphorRegistry::PhosphorRegistry
        PhosphorFsLoader::PhosphorFsLoader
        PhosphorShaders::PhosphorShaders
)

set_target_properties(PhosphorSurface PROPERTIES
    VERSION ${PHOSPHORSURFACE_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# ===============================================================================
# Tests
# ===============================================================================

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorSurface" OR BUILD_TESTING)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
        enable_testing()
        add_subdirectory(tests)
    endif()
endif()

# ===============================================================================
# Install
# ===============================================================================

if(NOT DEFINED KDE_INSTALL_BINDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS PhosphorSurface
    EXPORT PhosphorSurfaceTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

install(FILES ${phosphorsurface_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorSurface
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorSurface/phosphorsurface_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorSurface
)

install(EXPORT PhosphorSurfaceTargets
    FILE PhosphorSurfaceTargets.cmake
    NAMESPACE PhosphorSurface::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorSurface
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorSurfaceConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSurfaceConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorSurface
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSurfaceConfigVersion.cmake"
    VERSION ${PHOSPHORSURFACE_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSurfaceConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSurfaceConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorSurface
)
