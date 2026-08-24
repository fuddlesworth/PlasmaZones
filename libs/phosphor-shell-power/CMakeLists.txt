# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorShellPower, the Phosphor.Power surface module.
#
# A pure-QML module holding PowerMenu, the session menu the shell opens as a
# screen-centred Modal popout, and PowerTile, the one-action tile it repeats. The surface itself is not this module's
# concern: the shell's LayerPopoutTransport creates the layer-shell overlay
# and PopoutHost paints the scrim, so PowerMenu is only the panel that sits
# in the middle of it.
#
# Themed through phosphor-theme, built on the phosphor-shell-widgets atoms
# (PhosphorCard, PhosphorRipple), and depends on KF6 Kirigami for
# Kirigami.Icon's named-icon resolution on the action glyphs — the same
# reason phosphor-shell-bar does.
#
# Phase 4.6 deliverable per docs/phosphor-shell-design/04-implementation-plan.md.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSHELLPOWER_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorShellPower VERSION ${PHOSPHORSHELLPOWER_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick)

# Kirigami.Icon resolves freedesktop icon NAMES through QIcon::fromTheme; the
# shell's own image provider only serves the pixmaps tray items push, so it
# cannot resolve names. Declaring it here fails loudly at configure time on a
# host without Kirigami instead of producing a menu whose glyphs never load.
find_package(KF6Kirigami REQUIRED)

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

set(_phosphor_power_qml_files
    qml/Phosphor/Power/PowerMenu.qml
    qml/Phosphor/Power/PowerTile.qml
)

# Pin each QML file's resource alias to its basename, keeping the resource
# path flat and identical in shape to the sibling shell-tier modules. Nothing
# here lives in a subdirectory, so unlike the bar (where it flattens
# Widgets/Clock.qml to the type name Clock) it is consistency, not necessity.
foreach(_qml_file IN LISTS _phosphor_power_qml_files)
    get_filename_component(_qml_basename ${_qml_file} NAME)
    set_source_files_properties(${_qml_file}
        PROPERTIES
            QT_RESOURCE_ALIAS ${_qml_basename}
    )
endforeach()

# Directory-scope suppression of -Wmissing-include-dirs for the targets
# qt_add_qml_module generates here. Full rationale: src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(PhosphorShellPowerQml
    URI Phosphor.Power
    VERSION 1.0
    STATIC
    # DEPENDENCIES, never IMPORTS. IMPORTS emits a qmldir `import` line,
    # which injects the named module's UNQUALIFIED type names into every file
    # of this module, last entry winning. Kirigami exports a type called
    # `Theme`, which would shadow the Phosphor.Theme `Theme` singleton and
    # make every M3 colour token read undefined — the exact bug that cost the
    # bar five audit passes to find. DEPENDENCIES satisfies the import
    # scanner without injecting anything.
    DEPENDENCIES
        Phosphor.Theme
        Phosphor.Widgets
        QtQuick.Layouts
        org.kde.kirigami
    SOURCES
        src/phosphorshellpower_qml_plugin.cpp
    QML_FILES ${_phosphor_power_qml_files}
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Phosphor/Power
)

add_library(PhosphorShellPower::PhosphorShellPowerQml ALIAS PhosphorShellPowerQml)

target_compile_features(PhosphorShellPowerQml PUBLIC cxx_std_20)

target_link_libraries(PhosphorShellPowerQml
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        KF6::Kirigami
)

if(TARGET PhosphorShellWidgets::PhosphorShellWidgetsQml)
    target_link_libraries(PhosphorShellPowerQml PUBLIC PhosphorShellWidgets::PhosphorShellWidgetsQml)
endif()
if(TARGET PhosphorShellWidgetsQmlplugin)
    target_link_libraries(PhosphorShellPowerQml PUBLIC PhosphorShellWidgetsQmlplugin)
endif()
if(TARGET PhosphorTheme::PhosphorThemeQml)
    target_link_libraries(PhosphorShellPowerQml PUBLIC PhosphorTheme::PhosphorThemeQml)
endif()
if(TARGET PhosphorThemeQmlplugin)
    target_link_libraries(PhosphorShellPowerQml PUBLIC PhosphorThemeQmlplugin)
endif()

# The Phosphor.Service.Session type PowerMenu binds to (SessionHost) is
# registered imperatively at runtime by the shell process, not by a QML
# module, so it is deliberately not declared or linked here.

# No install rules: STATIC, in-tree-only QML module linked by the shell binary.

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorShellPower" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()
