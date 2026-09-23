# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorSurfaceQuick — the Qt Quick consumer of surface-shader packs.
#
# phosphor-surface is the pack subsystem (contract, metadata, registry,
# chain composition) and stays Core/Gui-only so the KWin effect can link it
# without Qt Quick. This library is the on-screen half for a Qt Quick scene:
#
#   * SurfaceShaderItem — a PhosphorRendering::ShaderEffect subclass that
#     renders one surface pack over one live item through the leaner surface
#     UBO (PhosphorSurfaceShaders::SurfaceUniformProfile).
#   * SurfaceDecoration.qml — the chain host: captures an anchor item, runs
#     one SurfaceShaderItem per resolved stage and draws the result over the
#     anchor's rect. Shipped as the QML module `org.phosphor.surface`, which
#     also registers SurfaceShaderItem.
#
# Consumers: the plasmazones daemon (OSD and popup decorations), the settings
# app (live decoration preview) and the phosphor-shell binary (ShellChrome).
# The shell tier depending on this library rather than on the plasmazones
# tier is what keeps the moon layer graph acyclic.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSURFACEQUICK_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorSurfaceQuick VERSION ${PHOSPHORSURFACEQUICK_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ===============================================================================
# Dependencies
# ===============================================================================

# Pin to 6.10 to match phosphor-rendering, whose ShaderEffect this subclasses.
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Qml Quick)
if(NOT TARGET Qt6::GuiPrivate)
    find_package(Qt6GuiPrivate CONFIG REQUIRED)
endif()

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

# PhosphorRendering — SurfaceShaderItem derives from ShaderEffect and drives a
# stock ShaderNodeRhi.
if(NOT TARGET PhosphorRendering::PhosphorRendering)
    find_package(PhosphorRendering 0.1.0 REQUIRED)
endif()

# PhosphorSurface — the surface UBO profile the node is created with, and the
# entry-point scaffold the registry generates.
if(NOT TARGET PhosphorSurface::PhosphorSurface)
    find_package(PhosphorSurface 0.1.0 REQUIRED)
endif()

# PhosphorShaders — ShaderRegistry::wallpaperSliceNormalized for the backdrop
# placement.
if(NOT TARGET PhosphorShaders::PhosphorShaders)
    find_package(PhosphorShaders CONFIG REQUIRED)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Suppress -Wmissing-include-dirs for the targets qt_add_qml_module generates
# below (qmltyperegistration, resource init, qmlplugin). Qt adds
# `<target>_autogen/include` to each sub-target's -I list; those sub-targets
# carry no Q_OBJECT, so AUTOMOC produces nothing and the directory is empty or,
# after `--clean-first`, missing, and cc1plus fires before AUTOMOC recreates
# it. DIRECTORY scope so every implicitly generated sub-target picks it up.
# Mirrors phosphor-animation and phosphor-control.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

# ===============================================================================
# Sources
# ===============================================================================

set(phosphorsurfacequick_public_HDRS
    include/PhosphorSurfaceQuick/SurfaceShaderItem.h
)

set(phosphorsurfacequick_SRCS
    src/surfaceshaderitem.cpp
)

set(phosphorsurfacequick_qml_FILES
    qml/SurfaceDecoration.qml
)

# ===============================================================================
# PhosphorSurfaceQuick Library
# ===============================================================================

add_library(PhosphorSurfaceQuick SHARED
    ${phosphorsurfacequick_public_HDRS}
    ${phosphorsurfacequick_SRCS}
)

add_library(PhosphorSurfaceQuick::PhosphorSurfaceQuick ALIAS PhosphorSurfaceQuick)

generate_export_header(PhosphorSurfaceQuick
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorSurfaceQuick/phosphorsurfacequick_export.h
    EXPORT_MACRO_NAME PHOSPHORSURFACEQUICK_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

if(NOT DEFINED KDE_INSTALL_QMLDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_QMLDIR ${CMAKE_INSTALL_LIBDIR}/qt6/qml)
endif()

target_include_directories(PhosphorSurfaceQuick
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorSurfaceQuick PUBLIC cxx_std_20)

# Qt6::Qml is PUBLIC because the public header includes <qqmlregistration.h>
# for QML_NAMED_ELEMENT. PhosphorRendering and PhosphorSurface are PUBLIC
# because the header derives from ShaderEffect and names ShaderNodeRhi.
target_link_libraries(PhosphorSurfaceQuick
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        PhosphorRendering::PhosphorRendering
        PhosphorSurface::PhosphorSurface
    PRIVATE
        PhosphorShaders::PhosphorShaders
        # ShaderNodeRhi.h includes <rhi/qrhi.h>; the same private-header
        # dependency plasmazones_rendering carried for the item.
        Qt6::GuiPrivate
)

set_target_properties(PhosphorSurfaceQuick PROPERTIES
    VERSION ${PHOSPHORSURFACEQUICK_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# ===============================================================================
# QML Module — org.phosphor.surface (STATIC, in-tree linkable)
# ===============================================================================
#
# Re-compiles the item source so qmltyperegistrar sees the QML_NAMED_ELEMENT
# marker and emits the registration into this module, beside the QML host.
# Same three-target shape as phosphor-control: the C++ library for C++
# consumers (the daemon's warm bake calls SurfaceShaderItem::surfaceIncludePaths),
# this STATIC module for in-tree QML consumers, and an optional SHARED variant
# below for the installed QML import path.
#
# STATIC modules cannot go into an export set, so
# PhosphorSurfaceQuick::PhosphorSurfaceQuickQml only resolves inside this tree.

qt_add_qml_module(PhosphorSurfaceQuickQml
    URI org.phosphor.surface
    VERSION 1.0
    STATIC
    RESOURCE_PREFIX /qt/qml
    SOURCES
        ${phosphorsurfacequick_public_HDRS}
        ${phosphorsurfacequick_SRCS}
    QML_FILES ${phosphorsurfacequick_qml_FILES}
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/org/phosphor/surface
)

add_library(PhosphorSurfaceQuick::PhosphorSurfaceQuickQml ALIAS PhosphorSurfaceQuickQml)

# Same sources as the SHARED library above under a second target name; define
# the export symbol so PHOSPHORSURFACEQUICK_EXPORT resolves to
# visibility("default") here too.
target_compile_definitions(PhosphorSurfaceQuickQml PRIVATE PhosphorSurfaceQuick_EXPORTS)

target_include_directories(PhosphorSurfaceQuickQml
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    PRIVATE
        # The generated <module>_qmltyperegistrations.cpp guards each registered
        # type with `#if __has_include(<SurfaceShaderItem.h>)` using the BARE
        # header name, so the header's own directory must be on the include
        # path for a non-unity build to resolve it. PRIVATE: consumers keep the
        # <PhosphorSurfaceQuick/…> spelling.
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/PhosphorSurfaceQuick>
)

target_compile_features(PhosphorSurfaceQuickQml PUBLIC cxx_std_20)

target_link_libraries(PhosphorSurfaceQuickQml
    PUBLIC
        PhosphorSurfaceQuick::PhosphorSurfaceQuick
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
    PRIVATE
        PhosphorShaders::PhosphorShaders
        # ShaderNodeRhi.h includes <rhi/qrhi.h>; the same private-header
        # dependency plasmazones_rendering carried for the item.
        Qt6::GuiPrivate
)

# ===============================================================================
# QML Module — SHARED (install-only runtime plugin)
# ===============================================================================
#
# Every in-tree consumer links the STATIC module, and a shell.qml or overlay
# QML file loaded from the user's config directory resolves the import from
# the plugin linked into that binary. The SHARED variant exists for an
# out-of-tree QML consumer that imports org.phosphor.surface from the
# installed QML path. Same opt-in shape as phosphor-control.

option(PHOSPHOR_SURFACE_QUICK_QML_INSTALL
    "Also build a SHARED variant of org.phosphor.surface and install it"
    OFF)

function(_phosphor_surface_quick_apply_plugin_rpath plugin_target)
    if(NOT TARGET ${plugin_target})
        message(WARNING "phosphor-surface-quick: plugin target ${plugin_target} not found at install-rpath set time"
                        " — packaging may need LD_LIBRARY_PATH for the QML module directory.")
        return()
    endif()
    set_target_properties(${plugin_target} PROPERTIES INSTALL_RPATH "$ORIGIN")
endfunction()

if(PHOSPHOR_SURFACE_QUICK_QML_INSTALL)
    qt_add_qml_module(PhosphorSurfaceQuickQmlShared
        URI org.phosphor.surface
        VERSION 1.0
        SHARED
        RESOURCE_PREFIX /qt/qml
        SOURCES
            ${phosphorsurfacequick_public_HDRS}
            ${phosphorsurfacequick_SRCS}
        QML_FILES ${phosphorsurfacequick_qml_FILES}
        OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml_install/org/phosphor/surface
    )

    target_compile_definitions(PhosphorSurfaceQuickQmlShared PRIVATE PhosphorSurfaceQuick_EXPORTS)

    target_include_directories(PhosphorSurfaceQuickQmlShared
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        PRIVATE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/PhosphorSurfaceQuick>
    )

    target_compile_features(PhosphorSurfaceQuickQmlShared PUBLIC cxx_std_20)

    target_link_libraries(PhosphorSurfaceQuickQmlShared
        PUBLIC
            PhosphorSurfaceQuick::PhosphorSurfaceQuick
            Qt6::Core
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
        PRIVATE
            PhosphorShaders::PhosphorShaders
            Qt6::GuiPrivate
    )

    set_target_properties(PhosphorSurfaceQuickQmlShared PROPERTIES
        VERSION ${PHOSPHORSURFACEQUICK_VERSION}
        SOVERSION 0
    )

    set(_phosphor_surface_quick_qml_shared_plugin_target PhosphorSurfaceQuickQmlSharedplugin)

    # The plugin and its backing library install side by side in the QML module
    # directory, which is off the linker's default search path; $ORIGIN resolves
    # the sibling. Deferred because Qt finalizes the plugin target at end of
    # directory scope.
    cmake_language(DEFER CALL _phosphor_surface_quick_apply_plugin_rpath
        ${_phosphor_surface_quick_qml_shared_plugin_target})
endif()

# ===============================================================================
# Tests
# ===============================================================================

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorSurfaceQuick" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ===============================================================================
# Install
# ===============================================================================

if(NOT DEFINED KDE_INSTALL_BINDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS PhosphorSurfaceQuick
    EXPORT PhosphorSurfaceQuickTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

install(FILES ${phosphorsurfacequick_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorSurfaceQuick
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorSurfaceQuick/phosphorsurfacequick_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorSurfaceQuick
)

install(EXPORT PhosphorSurfaceQuickTargets
    FILE PhosphorSurfaceQuickTargets.cmake
    NAMESPACE PhosphorSurfaceQuick::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorSurfaceQuick
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorSurfaceQuickConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSurfaceQuickConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorSurfaceQuick
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSurfaceQuickConfigVersion.cmake"
    VERSION ${PHOSPHORSURFACEQUICK_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSurfaceQuickConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSurfaceQuickConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorSurfaceQuick
)

if(PHOSPHOR_SURFACE_QUICK_QML_INSTALL)
    set(_phosphor_surface_quick_qml_install_dir "${KDE_INSTALL_QMLDIR}/org/phosphor/surface")

    install(TARGETS PhosphorSurfaceQuickQmlShared ${_phosphor_surface_quick_qml_shared_plugin_target}
        EXPORT PhosphorSurfaceQuickQmlTargets
        RUNTIME DESTINATION ${_phosphor_surface_quick_qml_install_dir}
        LIBRARY DESTINATION ${_phosphor_surface_quick_qml_install_dir}
        ARCHIVE DESTINATION ${_phosphor_surface_quick_qml_install_dir}
    )

    install(FILES
            ${CMAKE_BINARY_DIR}/qml_install/org/phosphor/surface/qmldir
            ${CMAKE_BINARY_DIR}/qml_install/org/phosphor/surface/PhosphorSurfaceQuickQmlShared.qmltypes
        DESTINATION ${_phosphor_surface_quick_qml_install_dir}
    )
endif()
