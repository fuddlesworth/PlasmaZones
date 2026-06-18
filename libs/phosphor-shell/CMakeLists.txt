# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorShell — QML shell infrastructure for Phosphor-based desktop shells.
#
# Provides the shell lifecycle (XDG config discovery, QML engine management)
# and user-facing QML types (PanelWindow) that create layer-shell surfaces.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSHELL_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorShell VERSION ${PHOSPHORSHELL_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Quick Qml WaylandClient)
if(NOT TARGET Qt6::WaylandClientPrivate)
    find_package(Qt6WaylandClientPrivate REQUIRED)
endif()

if(NOT TARGET PhosphorLayer::PhosphorLayer)
    find_package(PhosphorLayer CONFIG REQUIRED)
endif()
if(NOT TARGET PhosphorRendering::PhosphorRendering)
    find_package(PhosphorRendering CONFIG REQUIRED)
endif()
if(NOT TARGET PhosphorShaders::PhosphorShaders)
    find_package(PhosphorShaders CONFIG REQUIRED)
endif()
if(NOT TARGET PhosphorWayland::PhosphorWayland)
    find_package(PhosphorWayland CONFIG REQUIRED)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Public Headers
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorshell_public_HDRS
    include/PhosphorShell/PhosphorShell.h
    include/PhosphorShell/ShellLoader.h
    include/PhosphorShell/ShellEngine.h
    include/PhosphorShell/PanelWindow.h
    include/PhosphorShell/PopupWindow.h
    include/PhosphorShell/ScreenModel.h
    include/PhosphorShell/ShellGlobal.h
    include/PhosphorShell/Variants.h
    include/PhosphorShell/Process.h
    include/PhosphorShell/PersistentProperties.h
    include/PhosphorShell/Environment.h
    include/PhosphorShell/FileView.h
    include/PhosphorShell/FloatingWindow.h
    include/PhosphorShell/LazyLoader.h
    include/PhosphorShell/Toplevels.h
    include/PhosphorShell/WallpaperService.h
    include/PhosphorShell/SystemClock.h
)

# ═══════════════════════════════════════════════════════════════════════════════
# Library Sources
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorshell_SRCS
    src/shellloader.cpp
    src/shellengine.cpp
    src/panelwindow.cpp
    src/popupwindow.cpp
    src/screenmodel.cpp
    src/shellglobal.cpp
    src/variants.cpp
    src/process.cpp
    src/persistentproperties.cpp
    src/environment.cpp
    src/fileview.cpp
    src/floatingwindow.cpp
    src/lazyloader.cpp
    src/toplevels.cpp
    src/wallpaperservice.cpp
    src/systemclock.cpp
)

# ═══════════════════════════════════════════════════════════════════════════════
# Main Shared Library
# ═══════════════════════════════════════════════════════════════════════════════

add_library(PhosphorShell SHARED
    ${phosphorshell_public_HDRS}
    ${phosphorshell_SRCS}
)

add_library(PhosphorShell::PhosphorShell ALIAS PhosphorShell)

generate_export_header(PhosphorShell
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorShell/phosphorshell_export.h
    EXPORT_MACRO_NAME PHOSPHORSHELL_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorShell
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorShell PUBLIC cxx_std_20)

target_link_libraries(PhosphorShell
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::Qml
        PhosphorLayer::PhosphorLayer
        PhosphorRendering::PhosphorRendering
        PhosphorShaders::PhosphorShaders
        PhosphorWayland::PhosphorWayland
    PRIVATE
        # WaylandClient + private headers needed by popupwindow.cpp to call
        # xdg_popup.reposition directly. Qt's xdg-shell client plugin owns
        # the xdg_popup proxy lifecycle, but reposition is just a stateless
        # request on the same proxy — same approach quickshell uses
        # (src/wayland/popupanchor.cpp).
        Qt6::WaylandClient
        Qt6::WaylandClientPrivate
)

set_target_properties(PhosphorShell PROPERTIES
    VERSION ${PHOSPHORSHELL_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS PhosphorShell
    EXPORT PhosphorShellTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(FILES ${phosphorshell_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorShell
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorShell/phosphorshell_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorShell
)

install(EXPORT PhosphorShellTargets
    FILE PhosphorShellTargets.cmake
    NAMESPACE PhosphorShell::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorShell
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorShellConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorShellConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorShell
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorShellConfigVersion.cmake"
    VERSION ${PHOSPHORSHELL_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorShellConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorShellConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorShell
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorShell" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
