# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorShellNotifications, the Phosphor.Notifications toast framework.
#
# A pure-QML module: ToastHost (the top-right toast stack manager, owning
# the queue, per-toast lifecycle, and add/remove/reflow transitions) plus
# the Toast card (image + rich text + hover-to-pause auto-dismiss).
# Themed through phosphor-theme and built on the phosphor-shell-widgets
# atoms (ElevationShadow, StateLayer).
#
# Phase 3.4 deliverable per docs/phosphor-shell-design/04-implementation-plan.md.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSHELLNOTIFICATIONS_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorShellNotifications VERSION ${PHOSPHORSHELLNOTIFICATIONS_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickShapes)

# Toast imports Phosphor.Theme (tokens) and Phosphor.Widgets
# (ElevationShadow). The import scanner needs them visible at build time,
# and consumers need their runtime plugins linked. In-tree builds always
# have the sibling targets; fall back to installed packages.
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

set(_phosphor_notifications_qml_files
    qml/Phosphor/Notifications/Toast.qml
    qml/Phosphor/Notifications/ToastHost.qml
)

foreach(_qml_file IN LISTS _phosphor_notifications_qml_files)
    get_filename_component(_qml_basename ${_qml_file} NAME)
    set_source_files_properties(${_qml_file}
        PROPERTIES
            QT_RESOURCE_ALIAS ${_qml_basename}
    )
endforeach()

# Directory-scope suppression of -Wmissing-include-dirs. Full rationale:
# src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(PhosphorShellNotificationsQml
    URI Phosphor.Notifications
    VERSION 1.0
    STATIC
    IMPORTS
        Phosphor.Theme
        Phosphor.Widgets
        QtQuick.Shapes
        QtQuick.Layouts
    SOURCES
        src/phosphorshellnotifications_qml_plugin.cpp
    QML_FILES ${_phosphor_notifications_qml_files}
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Phosphor/Notifications
)

add_library(PhosphorShellNotifications::PhosphorShellNotificationsQml ALIAS PhosphorShellNotificationsQml)

target_compile_features(PhosphorShellNotificationsQml PUBLIC cxx_std_20)

target_link_libraries(PhosphorShellNotificationsQml
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickShapes
)

if(TARGET PhosphorShellWidgets::PhosphorShellWidgetsQml)
    target_link_libraries(PhosphorShellNotificationsQml PUBLIC PhosphorShellWidgets::PhosphorShellWidgetsQml)
endif()
if(TARGET PhosphorShellWidgetsQmlplugin)
    target_link_libraries(PhosphorShellNotificationsQml PUBLIC PhosphorShellWidgetsQmlplugin)
endif()
if(TARGET PhosphorTheme::PhosphorThemeQml)
    target_link_libraries(PhosphorShellNotificationsQml PUBLIC PhosphorTheme::PhosphorThemeQml)
endif()
if(TARGET PhosphorThemeQmlplugin)
    target_link_libraries(PhosphorShellNotificationsQml PUBLIC PhosphorThemeQmlplugin)
endif()

# No install rules: STATIC, in-tree-only QML module (like the sibling
# phosphor-shell-* QML modules). Linked directly by the shell and the
# toast demo; a standalone installed deployment lands with the shell
# binary in Phase 4.

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorShellNotifications" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()
