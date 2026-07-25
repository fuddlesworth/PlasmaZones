# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorShellBar, the Phosphor.Bar surface module.
#
# A pure-QML module: BarHost (the per-screen connected-corner bar
# surface, one PanelWindow per monitor via PerScreen, rendered with the
# BarCanvas atom) plus the Slot left/center/right abstraction and the
# built-in bar-widget delegates. Widgets are instantiated through the
# IBarWidgetFactory registry the shell owns (BarController), so the
# module itself stays registry-agnostic and the delegates are pluggable.
# Themed through phosphor-theme and built on the phosphor-shell-widgets
# atoms (BarCanvas, PhosphorPill, PhosphorButton).
#
# Phase 4.1 deliverable per docs/phosphor-shell-design/04-implementation-plan.md.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSHELLBAR_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorShellBar VERSION ${PHOSPHORSHELLBAR_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickShapes)

# The bar imports Phosphor.Theme (tokens) and Phosphor.Widgets (BarCanvas,
# PhosphorPill, PhosphorButton). The QML import scanner needs those modules
# visible at build time, and consumers need their runtime plugins linked.
# In-tree builds always have the sibling targets; fall back to installed
# packages. The bar QML also imports Phosphor.Shell (PanelWindow, PerScreen,
# SystemClock, FileView, Toplevels) and the runtime-registered
# Phosphor.Service.* modules; those resolve in the shell process at load,
# the same way examples/phosphor-shell does, so they are NOT build-time
# IMPORTS here.
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

set(_phosphor_bar_qml_files
    qml/Phosphor/Bar/BarHost.qml
    qml/Phosphor/Bar/Slot.qml
    qml/Phosphor/Bar/Widgets/BarIconButton.qml
    qml/Phosphor/Bar/Widgets/Clock.qml
    qml/Phosphor/Bar/Widgets/FocusedApp.qml
    qml/Phosphor/Bar/Widgets/SystemMetrics.qml
    qml/Phosphor/Bar/Widgets/Network.qml
    qml/Phosphor/Bar/Widgets/Bluetooth.qml
    qml/Phosphor/Bar/Widgets/Audio.qml
    qml/Phosphor/Bar/Widgets/Battery.qml
    qml/Phosphor/Bar/Widgets/Tray.qml
    qml/Phosphor/Bar/Widgets/Media.qml
    qml/Phosphor/Bar/Widgets/ControlCenterButton.qml
    qml/Phosphor/Bar/Widgets/NotificationButton.qml
    qml/Phosphor/Bar/Widgets/PowerButton.qml
    qml/Phosphor/Bar/Widgets/Spacer.qml
)

# Pin each QML file's resource alias to its basename so import resolution
# doesn't see a doubled path segment, and so the Widgets/ source-tree
# subdir does not leak into the registered type name (the delegates must
# register as flat types Clock/Battery/... in Phosphor.Bar so the
# IBarWidgetFactory URI-by-name lookup resolves them). Same pattern as the
# sibling libs.
foreach(_qml_file IN LISTS _phosphor_bar_qml_files)
    get_filename_component(_qml_basename ${_qml_file} NAME)
    set_source_files_properties(${_qml_file}
        PROPERTIES
            QT_RESOURCE_ALIAS ${_qml_basename}
    )
endforeach()

# Directory-scope suppression of -Wmissing-include-dirs for the targets
# qt_add_qml_module generates here. Full rationale: src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(PhosphorShellBarQml
    URI Phosphor.Bar
    VERSION 1.0
    STATIC
    IMPORTS
        Phosphor.Theme
        Phosphor.Widgets
        QtQuick.Layouts
    SOURCES
        src/phosphorshellbar_qml_plugin.cpp
    QML_FILES ${_phosphor_bar_qml_files}
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Phosphor/Bar
)

add_library(PhosphorShellBar::PhosphorShellBarQml ALIAS PhosphorShellBarQml)

target_compile_features(PhosphorShellBarQml PUBLIC cxx_std_20)

target_link_libraries(PhosphorShellBarQml
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
    target_link_libraries(PhosphorShellBarQml PUBLIC PhosphorShellWidgets::PhosphorShellWidgetsQml)
endif()
if(TARGET PhosphorShellWidgetsQmlplugin)
    target_link_libraries(PhosphorShellBarQml PUBLIC PhosphorShellWidgetsQmlplugin)
endif()
if(TARGET PhosphorTheme::PhosphorThemeQml)
    target_link_libraries(PhosphorShellBarQml PUBLIC PhosphorTheme::PhosphorThemeQml)
endif()
if(TARGET PhosphorThemeQmlplugin)
    target_link_libraries(PhosphorShellBarQml PUBLIC PhosphorThemeQmlplugin)
endif()

# BarHost imports Phosphor.Shell for PerScreen.qml, which lives in the
# phosphor-shell QML module (distinct from the C++ Phosphor.Shell types
# like PanelWindow that ShellEngine registers imperatively). Link the QML
# module + its plugin registrar so PerScreen resolves at runtime; without
# this the bar is the first consumer of that module and it goes
# unregistered ("PerScreen is not a type").
if(TARGET PhosphorShell::PhosphorShellQml)
    target_link_libraries(PhosphorShellBarQml PUBLIC PhosphorShell::PhosphorShellQml)
endif()
if(TARGET PhosphorShellQmlplugin)
    target_link_libraries(PhosphorShellBarQml PUBLIC PhosphorShellQmlplugin)
endif()

# No install rules: PhosphorShellBarQml is a STATIC, in-tree-only QML
# module (like PhosphorShellWidgetsQml / PhosphorShellOsdQml). It is linked
# directly by the shell binary; a standalone installed deployment lands
# with the shell in Phase 4.

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorShellBar" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()
