# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorShellOsd, the Phosphor.OSD on-screen-display framework.
#
# A pure-QML module: OSDHost (the per-screen OSD surface manager, owning
# timers / debounce / dedupe / fade / screen routing) plus the shared
# OSDCard chrome and the four built-in OSD delegates (Volume, Brightness,
# Mic, CapsLock). Themed through phosphor-theme and built on the
# phosphor-shell-widgets atoms (ElevationShadow).
#
# Phase 3.3 deliverable per docs/phosphor-shell-design/04-implementation-plan.md.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSHELLOSD_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorShellOsd VERSION ${PHOSPHORSHELLOSD_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickShapes)

# The OSDs import Phosphor.Theme (tokens) and Phosphor.Widgets
# (ElevationShadow). The QML import scanner needs those modules visible at
# build time, and consumers need their runtime plugins linked. In-tree
# builds always have the sibling targets; fall back to installed packages.
if(NOT TARGET PhosphorTheme::PhosphorThemeQml)
    find_package(PhosphorTheme CONFIG QUIET)
endif()
if(NOT TARGET PhosphorShellWidgets::PhosphorShellWidgetsQml)
    find_package(PhosphorShellWidgets CONFIG QUIET)
endif()

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ═══════════════════════════════════════════════════════════════════════════════
# QML module, STATIC (in-tree linkable)
# ═══════════════════════════════════════════════════════════════════════════════

set(_phosphor_osd_qml_files
    qml/Phosphor/OSD/OSDHost.qml
    qml/Phosphor/OSD/OSDCard.qml
    qml/Phosphor/OSD/VolumeOSD.qml
    qml/Phosphor/OSD/BrightnessOSD.qml
    qml/Phosphor/OSD/MicOSD.qml
    qml/Phosphor/OSD/CapsLockOSD.qml
)

# Pin each QML file's resource alias to its basename so import resolution
# doesn't see a doubled path segment. Same pattern as the sibling libs.
foreach(_qml_file IN LISTS _phosphor_osd_qml_files)
    get_filename_component(_qml_basename ${_qml_file} NAME)
    set_source_files_properties(${_qml_file}
        PROPERTIES
            QT_RESOURCE_ALIAS ${_qml_basename}
    )
endforeach()

# Directory-scope suppression of -Wmissing-include-dirs for the targets
# qt_add_qml_module generates here. Full rationale: src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(PhosphorShellOsdQml
    URI Phosphor.OSD
    VERSION 1.0
    STATIC
    IMPORTS
        Phosphor.Theme
        Phosphor.Widgets
        QtQuick.Shapes
        QtQuick.Layouts
    SOURCES
        src/phosphorshellosd_qml_plugin.cpp
    QML_FILES ${_phosphor_osd_qml_files}
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Phosphor/OSD
)

add_library(PhosphorShellOsd::PhosphorShellOsdQml ALIAS PhosphorShellOsdQml)

target_compile_features(PhosphorShellOsdQml PUBLIC cxx_std_20)

target_link_libraries(PhosphorShellOsdQml
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickShapes
)

# Link the sibling QML modules + their plugin registrars so the
# Phosphor.Theme / Phosphor.Widgets imports resolve at runtime. In-tree
# builds always have these targets.
if(TARGET PhosphorShellWidgets::PhosphorShellWidgetsQml)
    target_link_libraries(PhosphorShellOsdQml PUBLIC PhosphorShellWidgets::PhosphorShellWidgetsQml)
endif()
if(TARGET PhosphorShellWidgetsQmlplugin)
    target_link_libraries(PhosphorShellOsdQml PUBLIC PhosphorShellWidgetsQmlplugin)
endif()
if(TARGET PhosphorTheme::PhosphorThemeQml)
    target_link_libraries(PhosphorShellOsdQml PUBLIC PhosphorTheme::PhosphorThemeQml)
endif()
if(TARGET PhosphorThemeQmlplugin)
    target_link_libraries(PhosphorShellOsdQml PUBLIC PhosphorThemeQmlplugin)
endif()

# No install rules: PhosphorShellOsdQml is a STATIC, in-tree-only QML
# module (like PhosphorShellWidgetsQml / PhosphorPopoutQml). It is linked
# directly by the shell and the osd demo; a standalone installed
# deployment lands with the shell binary in Phase 4.

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorShellOsd" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()
