# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorPointer — pointer-shader subsystem for Phosphor-based shells.
#
# The fourth shader-pack family beside overlay (phosphor-shaders), animation
# (phosphor-animation) and surface (phosphor-surface): packs that decorate the
# mouse pointer (trails, halos, click ripples, sparks).
#
#   * PointerShaderContract — cross-runtime named-uniform contract.
#   * PointerShaderUniforms — std140 mirror of the UBO tail, pinned.
#   * PointerShaderEffect   — parsed metadata pack.
#   * PointerShaderRegistry — Registry<PointerPack> + MetadataPackLoader with
#     search paths, live reload, param translation and the p_<id> preamble.
#   * PointerFrameState     — the per-frame tail snapshot both hosts share.
#   * PointerUniformExtension — IUniformExtension writing the tail on the
#     Qt-RHI preview runtime.
#   * PointerHistory        — the ring-buffer pointer sampler both hosts use.
#
# Mirrors libs/phosphor-surface. Rendering lives in the consumers.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORPOINTER_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorPointer VERSION ${PHOSPHORPOINTER_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ===============================================================================
# Dependencies
# ===============================================================================

# Pin to 6.10 to match phosphor-shaders, whose headers this library consumes.
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui)

if(NOT TARGET PhosphorFsLoader::PhosphorFsLoader)
    find_package(PhosphorFsLoader 0.1.0 REQUIRED)
endif()

if(NOT TARGET PhosphorShaders::PhosphorShaders)
    find_package(PhosphorShaders CONFIG REQUIRED)
endif()

if(NOT TARGET PhosphorRegistry::PhosphorRegistry)
    find_package(PhosphorRegistry 0.1.0 REQUIRED)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

# ===============================================================================
# Public headers
# ===============================================================================

set(phosphorpointer_public_HDRS
    include/PhosphorPointer/PointerShaderContract.h
    include/PhosphorPointer/PointerShaderUniforms.h
    include/PhosphorPointer/PointerShaderEffect.h
    include/PhosphorPointer/PointerShaderRegistry.h
    include/PhosphorPointer/PointerFrameState.h
    include/PhosphorPointer/PointerUniformExtension.h
    include/PhosphorPointer/PointerHistory.h
)

# ===============================================================================
# Sources
# ===============================================================================

set(phosphorpointer_SRCS
    src/pointershadereffect.cpp
    src/pointershaderregistry.cpp
    src/pointeruniformextension.cpp
    src/pointerhistory.cpp
    src/pointer_contract_pins.cpp
)

# ===============================================================================
# PhosphorPointer Library
# ===============================================================================

add_library(PhosphorPointer SHARED
    ${phosphorpointer_public_HDRS}
    ${phosphorpointer_SRCS}
)

add_library(PhosphorPointer::PhosphorPointer ALIAS PhosphorPointer)

generate_export_header(PhosphorPointer
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorPointer/phosphorpointer_export.h
    EXPORT_MACRO_NAME PHOSPHORPOINTER_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

target_include_directories(PhosphorPointer
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorPointer PUBLIC cxx_std_20)

# PhosphorRegistry / PhosphorFsLoader / PhosphorShaders are PUBLIC because
# PointerShaderRegistry's public header exposes Registry<PointerPack>,
# MetadataPackLoader<PointerPack> and the PhosphorFsLoader enums in its
# signatures; PointerShaderContract.h / PointerShaderUniforms.h include
# PhosphorShaders headers.
target_link_libraries(PhosphorPointer
    PUBLIC
        Qt6::Core
        Qt6::Gui
        PhosphorRegistry::PhosphorRegistry
        PhosphorFsLoader::PhosphorFsLoader
        PhosphorShaders::PhosphorShaders
)

set_target_properties(PhosphorPointer PROPERTIES
    VERSION ${PHOSPHORPOINTER_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# ===============================================================================
# Tests
# ===============================================================================

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorPointer" OR BUILD_TESTING)
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

install(TARGETS PhosphorPointer
    EXPORT PhosphorPointerTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

install(FILES ${phosphorpointer_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorPointer
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorPointer/phosphorpointer_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorPointer
)

install(EXPORT PhosphorPointerTargets
    FILE PhosphorPointerTargets.cmake
    NAMESPACE PhosphorPointer::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorPointer
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorPointerConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorPointerConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorPointer
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorPointerConfigVersion.cmake"
    VERSION ${PHOSPHORPOINTER_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorPointerConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorPointerConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorPointer
)
