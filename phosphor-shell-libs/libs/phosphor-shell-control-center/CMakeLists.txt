# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorShellControlCenter, the Phosphor.ControlCenter tile surface.
#
# A pure-QML module: ControlCenter (the tile grid + detail routing),
# the shared Tile chrome, and DetailPanel. Themed through phosphor-theme
# and built on the phosphor-shell-widgets atoms (PhosphorRipple).
#
# The concrete tiles are supplied through Registry<IControlCenterTileFactory>
# (phosphor-registry, Phase 1.3), so this module holds no service
# dependency: it never links phosphor-service-*, and a tile that needs
# NetworkManager is the shell's problem to register, not this library's to
# know about.
#
# Phase 4.4 deliverable per docs/phosphor-shell-design/04-implementation-plan.md.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSHELLCONTROLCENTER_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorShellControlCenter VERSION ${PHOSPHORSHELLCONTROLCENTER_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

# QuickShapes is needed in THIS directory scope, not merely inherited:
# PhosphorRipple (Phosphor.Widgets) paints its press ripple with a
# QtQuick.Shapes Shape, so the static qml plugin's dependency walk
# resolves Qt6::QuickShapes here and warns at configure time without it.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickShapes)

# Tile / DetailPanel draw their glyphs with Kirigami.Icon, which resolves
# freedesktop icon names through QIcon::fromTheme and falls back to its own
# glyph for a name the theme lacks. REQUIRED, not QUIET: a missing Kirigami
# should fail the configure rather than yield a surface whose every glyph
# silently never loads. This is one of four shell-tier libraries requiring KF6
# Kirigami, alongside phosphor-shell-bar, phosphor-shell-power and phosphor-shell-launcher.
find_package(KF6Kirigami REQUIRED)

# The tiles import Phosphor.Theme (tokens) and Phosphor.Widgets
# (PhosphorRipple). The QML import scanner needs those modules visible at
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

set(_phosphor_control_center_qml_files
    qml/Phosphor/ControlCenter/ControlCenter.qml
    qml/Phosphor/ControlCenter/Tile.qml
    qml/Phosphor/ControlCenter/SliderTile.qml
    qml/Phosphor/ControlCenter/DetailPanel.qml
    # The built-in tile catalog. These import Phosphor.Service.* modules
    # that are registered by the host process at load, exactly as the bar's
    # service-bound widgets do, so the library still links no service.
    qml/Phosphor/ControlCenter/Tiles/NetworkTile.qml
    qml/Phosphor/ControlCenter/Tiles/BluetoothTile.qml
    qml/Phosphor/ControlCenter/Tiles/AudioTile.qml
    qml/Phosphor/ControlCenter/Tiles/BrightnessTile.qml
    qml/Phosphor/ControlCenter/Tiles/IdleTile.qml
)

# Pin each QML file's resource alias to its basename so import resolution
# doesn't see a doubled path segment. Same pattern as the sibling libs.
foreach(_qml_file IN LISTS _phosphor_control_center_qml_files)
    get_filename_component(_qml_basename ${_qml_file} NAME)
    set_source_files_properties(${_qml_file}
        PROPERTIES
            QT_RESOURCE_ALIAS ${_qml_basename}
    )
endforeach()

# Directory-scope suppression of -Wmissing-include-dirs for the targets
# qt_add_qml_module generates here. Full rationale: src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(PhosphorShellControlCenterQml
    URI Phosphor.ControlCenter
    VERSION 1.0
    STATIC
    # DEPENDENCIES, never IMPORTS. IMPORTS emits a qmldir `import` line,
    # which injects the named module's UNQUALIFIED type names into every
    # file of this module, last entry winning. Kirigami exports a type
    # called `Theme`, which would shadow the Phosphor.Theme `Theme`
    # singleton and make every M3 colour token read undefined — the exact
    # bug that cost the bar five audit passes to find. DEPENDENCIES
    # satisfies the import scanner without injecting anything.
    DEPENDENCIES
        Phosphor.Theme
        Phosphor.Widgets
        QtQuick.Layouts
        org.kde.kirigami
    SOURCES
        src/phosphorshellcontrolcenter_qml_plugin.cpp
    QML_FILES ${_phosphor_control_center_qml_files}
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Phosphor/ControlCenter
)

add_library(PhosphorShellControlCenter::PhosphorShellControlCenterQml ALIAS PhosphorShellControlCenterQml)

target_compile_features(PhosphorShellControlCenterQml PUBLIC cxx_std_20)

target_link_libraries(PhosphorShellControlCenterQml
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        KF6::Kirigami
)

# Link the sibling QML modules + their plugin registrars so the
# Phosphor.Theme / Phosphor.Widgets imports resolve at runtime. In-tree
# builds always have these targets.
if(TARGET PhosphorShellWidgets::PhosphorShellWidgetsQml)
    target_link_libraries(PhosphorShellControlCenterQml PUBLIC PhosphorShellWidgets::PhosphorShellWidgetsQml)
endif()
if(TARGET PhosphorShellWidgetsQmlplugin)
    target_link_libraries(PhosphorShellControlCenterQml PUBLIC PhosphorShellWidgetsQmlplugin)
endif()
if(TARGET PhosphorTheme::PhosphorThemeQml)
    target_link_libraries(PhosphorShellControlCenterQml PUBLIC PhosphorTheme::PhosphorThemeQml)
endif()
if(TARGET PhosphorThemeQmlplugin)
    target_link_libraries(PhosphorShellControlCenterQml PUBLIC PhosphorThemeQmlplugin)
endif()

# No install rules: PhosphorShellControlCenterQml is a STATIC, in-tree-only
# QML module (like PhosphorShellOsdQml / PhosphorShellWidgetsQml). It is
# linked directly by the shell; a standalone installed deployment lands
# with the shell binary in Phase 4.

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorShellControlCenter" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()
