# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorShellPolkit, the Phosphor.Polkit authentication prompt.
#
# A pure-QML module: PolkitPrompt (the 360 px card hanging under a rose
# band on the requesting window's top edge, A3 §9), PolkitAnchor (which
# window and rect the card hangs from, through the placement map),
# PolkitDim (the 20% dim of everything but the requester) and the text
# action they share. The PolicyKit agent itself is
# libs/phosphor-service-polkit; this module only draws for it, duck-typing
# the agent so a fake stands in for the tests.
#
# Phase 4 deliverable per docs/phosphor-shell-design/identity/A3-surfaces.md §9.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSHELLPOLKIT_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorShellPolkit VERSION ${PHOSPHORSHELLPOLKIT_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickShapes)

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

set(_phosphor_polkit_qml_files
    qml/Phosphor/Polkit/PolkitPrompt.qml
    qml/Phosphor/Polkit/PolkitAnchor.qml
    qml/Phosphor/Polkit/PolkitDim.qml
    qml/Phosphor/Polkit/PolkitAction.qml
)

# Pin each QML file's resource alias to its basename so import resolution
# doesn't see a doubled path segment. Same pattern as the sibling libs.
foreach(_qml_file IN LISTS _phosphor_polkit_qml_files)
    get_filename_component(_qml_basename ${_qml_file} NAME)
    set_source_files_properties(${_qml_file}
        PROPERTIES
            QT_RESOURCE_ALIAS ${_qml_basename}
    )
endforeach()

# Directory-scope suppression of -Wmissing-include-dirs for the targets
# qt_add_qml_module generates here. Full rationale: src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(PhosphorShellPolkitQml
    URI Phosphor.Polkit
    VERSION 1.0
    STATIC
    DEPENDENCIES
        Phosphor.Theme
        Phosphor.Widgets
        QtQuick.Layouts
    SOURCES
        src/phosphorshellpolkit_qml_plugin.cpp
    QML_FILES ${_phosphor_polkit_qml_files}
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Phosphor/Polkit
)

add_library(PhosphorShellPolkit::PhosphorShellPolkitQml ALIAS PhosphorShellPolkitQml)

target_compile_features(PhosphorShellPolkitQml PUBLIC cxx_std_20)

target_link_libraries(PhosphorShellPolkitQml
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
)

if(TARGET PhosphorShellWidgets::PhosphorShellWidgetsQml)
    target_link_libraries(PhosphorShellPolkitQml PUBLIC PhosphorShellWidgets::PhosphorShellWidgetsQml)
endif()
if(TARGET PhosphorShellWidgetsQmlplugin)
    target_link_libraries(PhosphorShellPolkitQml PUBLIC PhosphorShellWidgetsQmlplugin)
endif()
if(TARGET PhosphorTheme::PhosphorThemeQml)
    target_link_libraries(PhosphorShellPolkitQml PUBLIC PhosphorTheme::PhosphorThemeQml)
endif()
if(TARGET PhosphorThemeQmlplugin)
    target_link_libraries(PhosphorShellPolkitQml PUBLIC PhosphorThemeQmlplugin)
endif()

# No install rules for the STATIC QML module, matching the sibling shell
# surfaces: it rides in whatever binary links it.

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorShellPolkit" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()
