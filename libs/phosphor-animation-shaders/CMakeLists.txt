# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorAnimationShaders — shader-backed animation pipeline
#
# Phase 6 of the phosphor-animation roadmap. Owns the shader side of
# animated transitions: metadata types (AnimationShaderEffect), per-event
# shader selection (ShaderProfile / ShaderProfileTree), registry discovery,
# and the asset pipeline for transition effect packs.
#
# Depends on PhosphorAnimation (for ProfilePaths walk-up) and Qt6 Core.
# Does NOT depend on PhosphorRendering — the rendering glue lives in the
# consumer (daemon / kwin-effect), not in this library. This keeps the
# library usable by headless tools (profile validators, config generators)
# that never instantiate a QQuickWindow.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORANIMATIONSHADERS_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorAnimationShaders VERSION ${PHOSPHORANIMATIONSHADERS_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core)

if(NOT TARGET PhosphorAnimation::PhosphorAnimation)
    find_package(PhosphorAnimation CONFIG REQUIRED)
endif()

if(NOT TARGET PhosphorFsLoader::PhosphorFsLoader)
    find_package(PhosphorFsLoader CONFIG REQUIRED)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorAnimationShaders Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphoranimationshaders_SRCS
    src/animationshadereffect.cpp
    src/animationshaderregistry.cpp
    src/shaderprofile.cpp
    src/shaderprofiletree.cpp
)

set(phosphoranimationshaders_public_HDRS
    include/PhosphorAnimationShaders/PhosphorAnimationShaders.h
    include/PhosphorAnimationShaders/AnimationShaderEffect.h
    include/PhosphorAnimationShaders/AnimationShaderRegistry.h
    include/PhosphorAnimationShaders/ShaderProfile.h
    include/PhosphorAnimationShaders/ShaderProfileTree.h
)

add_library(PhosphorAnimationShaders SHARED
    ${phosphoranimationshaders_SRCS}
    ${phosphoranimationshaders_public_HDRS}
)

add_library(PhosphorAnimationShaders::PhosphorAnimationShaders ALIAS PhosphorAnimationShaders)

generate_export_header(PhosphorAnimationShaders
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorAnimationShaders/phosphoranimationshaders_export.h
    EXPORT_MACRO_NAME PHOSPHORANIMATIONSHADERS_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

target_include_directories(PhosphorAnimationShaders
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorAnimationShaders PUBLIC cxx_std_20)

target_link_libraries(PhosphorAnimationShaders
    PUBLIC
        Qt6::Core
        PhosphorAnimation::PhosphorAnimation
        PhosphorFsLoader::PhosphorFsLoader
)

set_target_properties(PhosphorAnimationShaders PROPERTIES
    VERSION ${PHOSPHORANIMATIONSHADERS_VERSION}
    SOVERSION 0
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorAnimationShaders" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_BINDIR)
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS PhosphorAnimationShaders
    EXPORT PhosphorAnimationShadersTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

install(FILES ${phosphoranimationshaders_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorAnimationShaders
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorAnimationShaders/phosphoranimationshaders_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorAnimationShaders
)

install(EXPORT PhosphorAnimationShadersTargets
    FILE PhosphorAnimationShadersTargets.cmake
    NAMESPACE PhosphorAnimationShaders::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorAnimationShaders
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorAnimationShadersConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorAnimationShadersConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorAnimationShaders
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorAnimationShadersConfigVersion.cmake"
    VERSION ${PHOSPHORANIMATIONSHADERS_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorAnimationShadersConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorAnimationShadersConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorAnimationShaders
)
