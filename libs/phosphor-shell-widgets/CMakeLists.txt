# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorShellWidgets, the Phosphor.Widgets M3 atom library.
#
# A pure-QML module of themeable building blocks (button, slider, text
# field, card, pill, ripple, elevation shadow) that every Phosphor shell
# surface composes. The atoms bind through PhosphorTheme's Theme / Motion
# / StateLayer singletons, so a palette or motion retune propagates here
# with no per-widget edits.
#
# Phase 3.1 deliverable per docs/phosphor-shell-design/04-implementation-plan.md.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSHELLWIDGETS_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorShellWidgets VERSION ${PHOSPHORSHELLWIDGETS_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick)

# The atoms import Phosphor.Theme for Theme / Motion / StateLayer. The
# QML import scanner (qmllint, qmlcachegen) needs the registered theme
# module visible at build time, and consumers need the runtime plugin
# linked so the import resolves. Find PhosphorTheme from a sibling
# subproject build first, then fall back to an installed package.
if(NOT TARGET PhosphorTheme::PhosphorThemeQml)
    find_package(PhosphorTheme CONFIG QUIET)
endif()

# Qt 6.5+ resource-prefix policy. NEW = use `:/qt/qml/` as the QML module
# resource prefix.
if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ═══════════════════════════════════════════════════════════════════════════════
# QML module, STATIC (in-tree linkable)
# ═══════════════════════════════════════════════════════════════════════════════
#
# Pure-QML module: the atoms are .qml files with no backing C++ types.
# The glue translation unit gives qt_add_qml_module a compilation unit
# for the generated plugin + type registrar.

set(_phosphor_widgets_qml_files
    qml/Phosphor/Widgets/ElevationShadow.qml
    qml/Phosphor/Widgets/PhosphorRipple.qml
    qml/Phosphor/Widgets/PhosphorButton.qml
    qml/Phosphor/Widgets/PhosphorPill.qml
    qml/Phosphor/Widgets/PhosphorCard.qml
    qml/Phosphor/Widgets/PhosphorSlider.qml
    qml/Phosphor/Widgets/PhosphorTextField.qml
)

# qt_add_qml_module nests QML_FILES under their source-relative path
# beneath the module URI prefix. Pin the resource alias to the basename
# so import resolution doesn't see a doubled `qml/Phosphor/Widgets/`
# segment. Same pattern as PhosphorTheme / PhosphorPopout.
foreach(_qml_file IN LISTS _phosphor_widgets_qml_files)
    get_filename_component(_qml_basename ${_qml_file} NAME)
    set_source_files_properties(${_qml_file}
        PROPERTIES
            QT_RESOURCE_ALIAS ${_qml_basename}
    )
endforeach()

# Directory-scope suppression of -Wmissing-include-dirs for the targets
# qt_add_qml_module generates here: Qt adds <target>_autogen/include to each
# sub-target's -I list, but when a module carries no Q_OBJECT sources AUTOMOC
# never creates that directory (and --clean-first deletes it), so cc1plus
# warns on a Qt-managed scratch path. Full rationale: src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(PhosphorShellWidgetsQml
    URI Phosphor.Widgets
    VERSION 1.0
    STATIC
    # The atoms import Phosphor.Theme (Theme / Motion / StateLayer) and
    # QtQuick.Effects (MultiEffect, used by ElevationShadow). Declaring
    # the imports here lets qmllint and qmlcachegen resolve symbols at
    # build time; QtQuick.Effects ships with Qt6::Quick, and the
    # Phosphor.Theme runtime registration is handled by the
    # PhosphorThemeQmlplugin link below.
    IMPORTS
        Phosphor.Theme
        QtQuick.Effects
    SOURCES
        src/phosphorshellwidgets_qml_plugin.cpp
    QML_FILES ${_phosphor_widgets_qml_files}
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Phosphor/Widgets
)

add_library(PhosphorShellWidgets::PhosphorShellWidgetsQml ALIAS PhosphorShellWidgetsQml)

target_compile_features(PhosphorShellWidgetsQml PUBLIC cxx_std_20)

target_link_libraries(PhosphorShellWidgetsQml
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
)

# Link the PhosphorTheme QML plugin and module when available so
# consumers don't have to wire it themselves. The atoms' `import
# Phosphor.Theme` resolves through the linked plugin's runtime
# registration. In-tree builds always have these targets; out-of-tree
# consumers get a clear missing-target failure instead of a runtime
# "module not found" from QML.
if(TARGET PhosphorTheme::PhosphorThemeQml)
    target_link_libraries(PhosphorShellWidgetsQml PUBLIC PhosphorTheme::PhosphorThemeQml)
endif()
if(TARGET PhosphorThemeQmlplugin)
    target_link_libraries(PhosphorShellWidgetsQml PUBLIC PhosphorThemeQmlplugin)
endif()

# Static archive: VERSION/SOVERSION apply to SHARED libraries only and are
# silently ignored on STATIC. Don't set them so the property surface
# matches what the target actually carries.

# No install rules: PhosphorShellWidgetsQml is a STATIC, in-tree-only QML
# module (like PhosphorPopout's PhosphorPopoutQml). It is linked directly
# by the bundled shell and the kitchen-sink demo, so it never deploys as a
# standalone artifact; its runtime registration rides in whatever binary
# links it. There is no C++ core library to install either (the atoms are
# pure QML). Installing the static module would also drag its Qt-generated
# resource targets + the PhosphorTheme targets it links into an export
# set they don't belong to. A standalone installed deployment lands
# alongside the layer-shell-hosted shell binary in Phase 4.

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorShellWidgets" OR BUILD_TESTING)
    # enable_testing() is a no-op when KDECMakeSettings already included
    # CTest at the top level. The redundant call lets a standalone build
    # of this subproject still discover ctest.
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()
