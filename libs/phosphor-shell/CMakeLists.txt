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

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Quick Qml)

if(NOT TARGET PhosphorLayer::PhosphorLayer)
    find_package(PhosphorLayer CONFIG REQUIRED)
endif()

if(POLICY CMP0090 OR COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
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
endif()

if(NOT DEFINED KDE_INSTALL_QMLDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_QMLDIR ${CMAKE_INSTALL_LIBDIR}/qt6/qml)
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
)

set_target_properties(PhosphorShell PROPERTIES
    VERSION ${PHOSPHORSHELL_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# ═══════════════════════════════════════════════════════════════════════════════
# QML Module — STATIC (in-tree linkable)
# ═══════════════════════════════════════════════════════════════════════════════

qt_add_qml_module(PhosphorShellQml
    URI PhosphorShell
    VERSION 1.0
    STATIC
    SOURCES
        src/panelwindow.cpp
        include/PhosphorShell/PanelWindow.h
        src/popupwindow.cpp
        include/PhosphorShell/PopupWindow.h
        src/screenmodel.cpp
        include/PhosphorShell/ScreenModel.h
        src/shellglobal.cpp
        include/PhosphorShell/ShellGlobal.h
        src/variants.cpp
        include/PhosphorShell/Variants.h
        src/process.cpp
        include/PhosphorShell/Process.h
        src/persistentproperties.cpp
        include/PhosphorShell/PersistentProperties.h
        src/environment.cpp
        include/PhosphorShell/Environment.h
        src/fileview.cpp
        include/PhosphorShell/FileView.h
        src/floatingwindow.cpp
        include/PhosphorShell/FloatingWindow.h
        src/lazyloader.cpp
        include/PhosphorShell/LazyLoader.h
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/PhosphorShell
)

add_library(PhosphorShell::PhosphorShellQml ALIAS PhosphorShellQml)

target_include_directories(PhosphorShellQml
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorShellQml PUBLIC cxx_std_20)

target_link_libraries(PhosphorShellQml
    PUBLIC
        PhosphorShell::PhosphorShell
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::Qml
)

set_target_properties(PhosphorShellQml PROPERTIES
    VERSION ${PHOSPHORSHELL_VERSION}
    SOVERSION 0
)

# ═══════════════════════════════════════════════════════════════════════════════
# QML Module — SHARED (install-only runtime plugin)
# ═══════════════════════════════════════════════════════════════════════════════

option(PHOSPHOR_SHELL_QML_INSTALL
    "Also build a SHARED variant of PhosphorShell QML module and install it"
    ON)

if(PHOSPHOR_SHELL_QML_INSTALL)
    qt_add_qml_module(PhosphorShellQmlShared
        URI PhosphorShell
        VERSION 1.0
        SHARED
        SOURCES
            src/panelwindow.cpp
            include/PhosphorShell/PanelWindow.h
            src/popupwindow.cpp
            include/PhosphorShell/PopupWindow.h
            src/screenmodel.cpp
            include/PhosphorShell/ScreenModel.h
            src/shellglobal.cpp
            include/PhosphorShell/ShellGlobal.h
            src/variants.cpp
            include/PhosphorShell/Variants.h
            src/process.cpp
            include/PhosphorShell/Process.h
            src/persistentproperties.cpp
            include/PhosphorShell/PersistentProperties.h
            src/environment.cpp
            include/PhosphorShell/Environment.h
            src/fileview.cpp
            include/PhosphorShell/FileView.h
            src/floatingwindow.cpp
            include/PhosphorShell/FloatingWindow.h
            src/lazyloader.cpp
            include/PhosphorShell/LazyLoader.h
        OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml_install/PhosphorShell
    )

    target_include_directories(PhosphorShellQmlShared
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
            $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    )

    target_compile_definitions(PhosphorShellQmlShared PRIVATE
        PhosphorShell_EXPORTS
    )

    target_link_libraries(PhosphorShellQmlShared
        PUBLIC
            PhosphorShell::PhosphorShell
            Qt6::Core
            Qt6::Gui
            Qt6::Quick
            Qt6::Qml
    )

    set_target_properties(PhosphorShellQmlShared PROPERTIES
        VERSION ${PHOSPHORSHELL_VERSION}
        SOVERSION 0
    )

    set(_phosphor_shell_qml_shared_plugin_target PhosphorShellQmlSharedplugin)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_BINDIR)
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS PhosphorShell
    EXPORT PhosphorShellTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
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

if(PHOSPHOR_SHELL_QML_INSTALL)
    set(_phosphor_shell_qml_install_dir "${KDE_INSTALL_QMLDIR}/PhosphorShell")

    install(TARGETS PhosphorShellQmlShared ${_phosphor_shell_qml_shared_plugin_target}
        EXPORT PhosphorShellQmlTargets
        RUNTIME DESTINATION ${_phosphor_shell_qml_install_dir}
        LIBRARY DESTINATION ${_phosphor_shell_qml_install_dir}
        ARCHIVE DESTINATION ${_phosphor_shell_qml_install_dir}
    )

    install(FILES
            ${CMAKE_BINARY_DIR}/qml_install/PhosphorShell/qmldir
            ${CMAKE_BINARY_DIR}/qml_install/PhosphorShell/PhosphorShellQmlShared.qmltypes
        DESTINATION ${_phosphor_shell_qml_install_dir}
    )
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorShell" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
