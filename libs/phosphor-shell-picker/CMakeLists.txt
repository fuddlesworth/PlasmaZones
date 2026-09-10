# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorShellPicker, the Phosphor.Picker wallpaper and theme strip.
#
# The picker tries a palette on the real desktop (A3 §8): hovering a
# wallpaper candidate runs matugen on it and retints the live PaletteStore
# for as long as the candidate is hovered; Apply commits, leaving restores.
# Three C++ types carry the logic (the candidate scan, the debounced retint
# with its restore snapshot, the preset palettes) and the QML strip is the
# presentation. Built as one STATIC QML module, the way phosphor-theme
# compiles its C++ into Phosphor.Theme, so the types register through
# QML_ELEMENT and the tests link the same target.
#
# Phase 4 deliverable per docs/phosphor-shell-design/identity/A3-surfaces.md §8.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSHELLPICKER_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorShellPicker VERSION ${PHOSPHORSHELLPICKER_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickShapes)

# ThemePresets reads PaletteStore::defaultPalette() for the Dark tile, and
# the strip drives MatugenRunner + PaletteStore from Phosphor.Theme.
if(NOT TARGET PhosphorTheme::PhosphorTheme)
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
# QML module, STATIC (in-tree linkable), C++ types included
# ═══════════════════════════════════════════════════════════════════════════════

set(_phosphor_picker_sources
    include/PhosphorShellPicker/WallpaperCandidates.h
    include/PhosphorShellPicker/RetintController.h
    include/PhosphorShellPicker/ThemePresets.h
    src/wallpapercandidates.cpp
    src/retintcontroller.cpp
    src/themepresets.cpp
    src/phosphorshellpicker_qml_plugin.cpp
)

set(_phosphor_picker_qml_files
    qml/Phosphor/Picker/Picker.qml
    qml/Phosphor/Picker/CandidateThumb.qml
    qml/Phosphor/Picker/PaletteTile.qml
    qml/Phosphor/Picker/PickerAction.qml
    qml/Phosphor/Picker/WallpaperSurface.qml
)

# Pin each QML file's resource alias to its basename so import resolution
# doesn't see a doubled path segment. Same pattern as the sibling libs.
foreach(_qml_file IN LISTS _phosphor_picker_qml_files)
    get_filename_component(_qml_basename ${_qml_file} NAME)
    set_source_files_properties(${_qml_file}
        PROPERTIES
            QT_RESOURCE_ALIAS ${_qml_basename}
    )
endforeach()

# Directory-scope suppression of -Wmissing-include-dirs for the targets
# qt_add_qml_module generates here. Full rationale: src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(PhosphorShellPickerQml
    URI Phosphor.Picker
    VERSION 1.0
    STATIC
    # DEPENDENCIES, never IMPORTS: IMPORTS injects unqualified type names
    # into every file here. Same rule as the launcher and the bar.
    DEPENDENCIES
        Phosphor.Shell
        Phosphor.Theme
        Phosphor.Widgets
        QtQuick.Layouts
    SOURCES ${_phosphor_picker_sources}
    QML_FILES ${_phosphor_picker_qml_files}
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Phosphor/Picker
)

add_library(PhosphorShellPicker::PhosphorShellPickerQml ALIAS PhosphorShellPickerQml)

target_compile_features(PhosphorShellPickerQml PUBLIC cxx_std_20)

target_include_directories(PhosphorShellPickerQml
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        # Qt 6.10+ qmltyperegistrar emits basename #includes in the generated
        # *_qmltyperegistrations.cpp; the header subdirectory must be on the
        # path. Unity builds masked this by batching the generated TU with
        # sources that already include the headers.
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/PhosphorShellPicker>
)

target_link_libraries(PhosphorShellPickerQml
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        PhosphorTheme::PhosphorTheme
)

if(TARGET PhosphorShellWidgets::PhosphorShellWidgetsQml)
    target_link_libraries(PhosphorShellPickerQml PUBLIC PhosphorShellWidgets::PhosphorShellWidgetsQml)
endif()
if(TARGET PhosphorShellWidgetsQmlplugin)
    target_link_libraries(PhosphorShellPickerQml PUBLIC PhosphorShellWidgetsQmlplugin)
endif()
if(TARGET PhosphorTheme::PhosphorThemeQml)
    target_link_libraries(PhosphorShellPickerQml PUBLIC PhosphorTheme::PhosphorThemeQml)
endif()
if(TARGET PhosphorThemeQmlplugin)
    target_link_libraries(PhosphorShellPickerQml PUBLIC PhosphorThemeQmlplugin)
endif()
# The Phosphor.Shell module + registrar: WallpaperSurface is a PanelWindow,
# so `import Phosphor.Shell` must resolve wherever the picker is linked.
if(TARGET PhosphorShell::PhosphorShellQml)
    target_link_libraries(PhosphorShellPickerQml PUBLIC PhosphorShell::PhosphorShellQml)
endif()
if(TARGET PhosphorShellQmlplugin)
    target_link_libraries(PhosphorShellPickerQml PUBLIC PhosphorShellQmlplugin)
endif()

# No install rules for the STATIC QML module, matching the sibling shell
# surfaces: it rides in whatever binary links it.

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorShellPicker" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()
