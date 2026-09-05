# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorShellDashboard, the Phosphor.Dashboard module: the two full-screen
# placement-map surfaces of A3 §7 and §10.
#
#   Dashboard   every desktop's placement map at once (PlacementMap
#               .forScreenDesktop per cell), plus the calendar and media
#               cells in the same grammar; the shell's one scale transition.
#   Cheatsheet  the keybind sheet drawn ON the placement map: chord labels
#               on the rects they act on (ChordLayout.js), the non-spatial
#               chords in a right-edge column, over FullMap (the screen's
#               cells at 1:1).
#   ShortcutCatalog  the C++ reader behind the sheet: org.plasmazones
#               .Control.getShortcutsJson plus its shortcutsChanged relay.
#
# Themed through phosphor-theme, drawn with the phosphor-shell-widgets
# atoms (PlacementMiniature, TabularText, SpectrumUnderline). Phase 4
# deliverable per docs/phosphor-shell-design/04-implementation-plan.md.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSHELLDASHBOARD_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorShellDashboard VERSION ${PHOSPHORSHELLDASHBOARD_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick DBus)

# The surfaces import Phosphor.Theme (tokens) and Phosphor.Widgets (the
# miniature and the text atoms). In-tree builds always have the sibling
# targets; fall back to installed packages.
if(NOT TARGET PhosphorTheme::PhosphorThemeQml)
    find_package(PhosphorTheme CONFIG QUIET)
endif()
if(NOT TARGET PhosphorShellWidgets::PhosphorShellWidgetsQml)
    find_package(PhosphorShellWidgets CONFIG QUIET)
endif()
# ShortcutCatalog speaks to the daemon by the protocol library's service
# constants; nothing here links the daemon itself.
if(NOT TARGET PhosphorProtocol::PhosphorProtocol)
    find_package(PhosphorProtocol CONFIG REQUIRED)
endif()

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ═══════════════════════════════════════════════════════════════════════════════
# QML module, STATIC (in-tree linkable)
# ═══════════════════════════════════════════════════════════════════════════════

set(_phosphor_dashboard_qml_files
    qml/Phosphor/Dashboard/Dashboard.qml
    qml/Phosphor/Dashboard/DesktopCell.qml
    qml/Phosphor/Dashboard/CalendarCell.qml
    qml/Phosphor/Dashboard/MediaCell.qml
    qml/Phosphor/Dashboard/Cheatsheet.qml
    qml/Phosphor/Dashboard/FullMap.qml
    qml/Phosphor/Dashboard/ChordPill.qml
)
set(_phosphor_dashboard_js_files
    qml/Phosphor/Dashboard/ChordLayout.js
)

# Pin each file's resource alias to its basename so import resolution does
# not see a doubled path segment. Same pattern as the sibling libs.
foreach(_qml_file IN LISTS _phosphor_dashboard_qml_files _phosphor_dashboard_js_files)
    get_filename_component(_qml_basename ${_qml_file} NAME)
    set_source_files_properties(${_qml_file}
        PROPERTIES
            QT_RESOURCE_ALIAS ${_qml_basename}
    )
endforeach()

# Directory-scope suppression of -Wmissing-include-dirs for the targets
# qt_add_qml_module generates here. Full rationale: src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(PhosphorShellDashboardQml
    URI Phosphor.Dashboard
    VERSION 1.0
    STATIC
    # DEPENDENCIES, never IMPORTS: IMPORTS injects a module's unqualified
    # type names into every file here, and Kirigami (which
    # Phosphor.Widgets pulls for PlacementMiniature's icon) exports a
    # `Theme` that would shadow the Phosphor.Theme singleton.
    DEPENDENCIES
        Phosphor.Theme
        Phosphor.Widgets
        QtQuick.Layouts
    SOURCES
        src/phosphorshelldashboard_qml_plugin.cpp
        src/shortcutcatalog.h
        src/shortcutcatalog.cpp
    QML_FILES ${_phosphor_dashboard_qml_files} ${_phosphor_dashboard_js_files}
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Phosphor/Dashboard
)

add_library(PhosphorShellDashboard::PhosphorShellDashboardQml ALIAS PhosphorShellDashboardQml)

target_compile_features(PhosphorShellDashboardQml PUBLIC cxx_std_20)

target_link_libraries(PhosphorShellDashboardQml
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
    PRIVATE
        Qt6::DBus
        PhosphorProtocol::PhosphorProtocol
)

# Link the sibling QML modules + their plugin registrars so the
# Phosphor.Theme / Phosphor.Widgets imports resolve at runtime.
if(TARGET PhosphorShellWidgets::PhosphorShellWidgetsQml)
    target_link_libraries(PhosphorShellDashboardQml PUBLIC PhosphorShellWidgets::PhosphorShellWidgetsQml)
endif()
if(TARGET PhosphorShellWidgetsQmlplugin)
    target_link_libraries(PhosphorShellDashboardQml PUBLIC PhosphorShellWidgetsQmlplugin)
endif()
if(TARGET PhosphorTheme::PhosphorThemeQml)
    target_link_libraries(PhosphorShellDashboardQml PUBLIC PhosphorTheme::PhosphorThemeQml)
endif()
if(TARGET PhosphorThemeQmlplugin)
    target_link_libraries(PhosphorShellDashboardQml PUBLIC PhosphorThemeQmlplugin)
endif()

# No install rules: a STATIC, in-tree-only QML module like its siblings,
# linked by the shell binary and its tests.

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorShellDashboard" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()
