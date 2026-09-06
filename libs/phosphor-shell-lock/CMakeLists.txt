# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorShellLock, the Phosphor.Lock lockscreen surface (A3 §6).
#
# One C++ type, LockSurface (a QQuickWindow that is one output's
# ext_session_lock_surface_v1, built on phosphor-wayland's LockSurface
# marker and the QPA plugin's lock-surface role), plus the QML content:
# LockScreen (the per-output composition), LockLayout (the placement map
# as static spectrum outlines), LockAuthField (the 320 x 44 password
# field), LockController (the one password behind every screen) and
# LockRegion.js (the largest empty region of a placement map).
#
# Authentication is NOT here: the shell hands a LockService
# (phosphor-service-lock) to LockController, which is duck-typed so the
# tests drive it with a fake. Themed through phosphor-theme, built on the
# phosphor-shell-widgets atoms (TabularText).
#
# Phase 4 deliverable per docs/phosphor-shell-design/04-implementation-plan.md.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSHELLLOCK_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorShellLock VERSION ${PHOSPHORSHELLLOCK_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

# QuickShapes is not imported by this module's own QML; it is found here
# so the static plugin's dependency walk resolves the Qt6::QuickShapes
# target that Phosphor.Widgets links (its PhosphorRipple Shape) without a
# configure warning. Same reason the launcher module finds it.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickShapes)

# Kirigami.Icon draws the app glyph in each outline and the field's lock
# glyph. Same requirement as the bar, power, control-center and launcher
# modules: a host without Kirigami cannot build this tier.
find_package(KF6Kirigami REQUIRED)

# The lock surface role comes from phosphor-wayland (LockSurface +
# SessionLock). An in-tree sibling under BUILD_PHOSPHOR_SHELL; only
# find_package it when building this lib stand-alone.
if(NOT TARGET PhosphorWayland::PhosphorWayland)
    find_package(PhosphorWayland REQUIRED)
endif()

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

set(_phosphor_lock_qml_files
    qml/Phosphor/Lock/LockScreen.qml
    qml/Phosphor/Lock/LockLayout.qml
    qml/Phosphor/Lock/LockAuthField.qml
    qml/Phosphor/Lock/LockController.qml
    qml/Phosphor/Lock/LockRegion.js
)

# Pin each QML file's resource alias to its basename so import resolution
# doesn't see a doubled path segment. Same pattern as the sibling libs.
foreach(_qml_file IN LISTS _phosphor_lock_qml_files)
    get_filename_component(_qml_basename ${_qml_file} NAME)
    set_source_files_properties(${_qml_file}
        PROPERTIES
            QT_RESOURCE_ALIAS ${_qml_basename}
    )
endforeach()

set(_phosphor_lock_qml_sources
    src/phosphorshelllock_qml_plugin.cpp
    src/locksurfacewindow.cpp
    # Listed so AUTOMOC on the module target emits the metatype JSON that
    # qmltyperegistrar turns into the `LockSurface` registration.
    include/PhosphorShellLock/LockSurfaceWindow.h
)

# Directory-scope suppression of -Wmissing-include-dirs for the targets
# qt_add_qml_module generates here. Full rationale: src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(PhosphorShellLockQml
    URI Phosphor.Lock
    VERSION 1.0
    STATIC
    # DEPENDENCIES, never IMPORTS: IMPORTS injects a module's unqualified
    # type names into every file here, and Kirigami exports a `Theme` that
    # would shadow the Phosphor.Theme singleton. Same rule as the bar,
    # power, control-center and launcher modules. QtQuick.Effects ships
    # with Qt6::Quick and resolves at runtime.
    DEPENDENCIES
        Phosphor.Theme
        Phosphor.Widgets
        QtQuick.Effects
        org.kde.kirigami
    SOURCES ${_phosphor_lock_qml_sources}
    QML_FILES ${_phosphor_lock_qml_files}
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Phosphor/Lock
)

add_library(PhosphorShellLock::PhosphorShellLockQml ALIAS PhosphorShellLockQml)

target_include_directories(PhosphorShellLockQml
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        # Qt 6.10+ qmltyperegistrar emits basename #includes in the generated
        # *_qmltyperegistrations.cpp; the header subdirectory must be on the path.
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/PhosphorShellLock>
)

target_compile_features(PhosphorShellLockQml PUBLIC cxx_std_20)

target_link_libraries(PhosphorShellLockQml
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        KF6::Kirigami
        PhosphorWayland::PhosphorWayland
)

# Link the sibling QML modules + their plugin registrars so the
# Phosphor.Theme / Phosphor.Widgets imports resolve at runtime. In-tree
# builds always have these targets.
if(TARGET PhosphorShellWidgets::PhosphorShellWidgetsQml)
    target_link_libraries(PhosphorShellLockQml PUBLIC PhosphorShellWidgets::PhosphorShellWidgetsQml)
endif()
if(TARGET PhosphorShellWidgetsQmlplugin)
    target_link_libraries(PhosphorShellLockQml PUBLIC PhosphorShellWidgetsQmlplugin)
endif()
if(TARGET PhosphorTheme::PhosphorThemeQml)
    target_link_libraries(PhosphorShellLockQml PUBLIC PhosphorTheme::PhosphorThemeQml)
endif()
if(TARGET PhosphorThemeQmlplugin)
    target_link_libraries(PhosphorShellLockQml PUBLIC PhosphorThemeQmlplugin)
endif()

# No install rules: PhosphorShellLockQml is a STATIC, in-tree-only QML
# module (like PhosphorShellOsdQml). It is linked directly by the shell.

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorShellLock" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()
