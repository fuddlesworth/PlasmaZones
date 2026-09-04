# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorShellBar, the Phosphor.Bar surface module.
#
# A pure-QML module: BarHost (the connected-corner bar surface, a single
# PanelWindow on the primary output, rendered with the BarCanvas atom)
# plus the Slot left/center/right abstraction and the
# built-in bar-widget delegates. Widgets are instantiated through the
# IBarWidgetFactory registry the shell owns (BarController), so the
# module itself stays registry-agnostic and the delegates are pluggable.
# Themed through phosphor-theme and built on the phosphor-shell-widgets
# atoms (BarCanvas, ElevationShadow, PhosphorRipple). This lib depends on
# KF6 Kirigami, for Kirigami.Icon's named-icon resolution in the status
# widgets. The other shell-tier libraries that need it for the same reason
# are phosphor-shell-power, phosphor-shell-control-center and
# phosphor-shell-launcher.
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

# QuickShapes has to be found in THIS directory scope, not merely inherited
# from a parent: the bar draws through BarCanvas / ConnectedShape and
# PhosphorRipple, all of which are QtQuick.Shapes types, so the static qml
# plugin's dependency walk resolves Qt6::QuickShapes here and warns at
# configure time without it.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickShapes)

# Kirigami.Icon resolves freedesktop icon NAMES through QIcon::fromTheme and
# tints them via isMask; the shell's own image provider only serves the
# pixmaps tray items push, so it cannot resolve names. Seven files in this
# module import it (six widgets plus the shared BarIconButton atom, which
# four more widgets build on); they rely on it, which makes the bar the
# first KF6 consumer in the shell tier.
# Declaring it here fails loudly at configure time on a host without Kirigami
# instead of silently producing a bar whose icons never load, and lets the
# QML import scanner compile those files through qmlcachegen.
find_package(KF6Kirigami REQUIRED)

# The bar imports Phosphor.Theme (the Tokens / Theme / Motion / StateLayer
# singletons) and Phosphor.Widgets (BarCanvas, ElevationShadow,
# PhosphorRipple). The QML import scanner needs those modules visible at
# build time, and consumers need their runtime plugins linked.
# In-tree builds always have the sibling targets; fall back to installed
# packages. The bar QML also imports Phosphor.Shell (PanelWindow in BarHost;
# SystemClock, SystemUsage, Toplevels, Workspaces in the widgets) and the runtime-registered
# Phosphor.Service.* modules; those resolve in the shell process at load,
# the same way examples/phosphor-shell does, so they are not declared to the
# module at all. org.kde.kirigami IS declared (see DEPENDENCIES below),
# because unlike those it is an external module the import scanner must
# resolve at build time.
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
    qml/Phosphor/Bar/Widgets/BarWidget.qml
    qml/Phosphor/Bar/Widgets/Clock.qml
    qml/Phosphor/Bar/Widgets/FocusedApp.qml
    qml/Phosphor/Bar/Widgets/Workspaces.qml
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
    # DEPENDENCIES, never IMPORTS. IMPORTS emits a qmldir `import` line,
    # which injects the named module's UNQUALIFIED type names into every
    # file of this module, last entry winning. Kirigami exports a type
    # called `Theme`, so listing it under IMPORTS shadowed the
    # Phosphor.Theme `Theme` singleton across the whole bar and made every
    # M3 colour token read undefined. DEPENDENCIES satisfies the import
    # scanner without injecting anything, and every file already imports
    # what it uses by name. QtQuick / QtQml resolve implicitly and are
    # deliberately not listed.
    DEPENDENCIES
        Phosphor.Theme
        Phosphor.Widgets
        QtQuick.Layouts
        org.kde.kirigami
    SOURCES
        src/phosphorshellbar_qml_plugin.cpp
    QML_FILES ${_phosphor_bar_qml_files}
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Phosphor/Bar
)

add_library(PhosphorShellBar::PhosphorShellBarQml ALIAS PhosphorShellBarQml)

target_compile_features(PhosphorShellBarQml PUBLIC cxx_std_20)

# No Qt6::QuickShapes here: the bar QML imports no QtQuick.Shapes itself. The
# Shapes usage lives in Phosphor.Widgets' ConnectedShape, which BarCanvas
# instantiates; that module's own target links Qt6::QuickShapes and propagates
# it through the PUBLIC link below.
target_link_libraries(PhosphorShellBarQml
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        # The widgets' Kirigami.Icon import; enforced so a host without
        # Kirigami fails at configure rather than at first paint.
        KF6::Kirigami
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

# The Phosphor.Shell types the bar uses (PanelWindow, SystemClock,
# SystemUsage, Toplevels, Workspaces) are all registered imperatively at runtime by ShellEngine in
# the shell process, not by the phosphor-shell QML module. The only type
# that module's registrar provides is PerScreen.qml, which the bar does not
# use, so it is deliberately NOT linked here.

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
