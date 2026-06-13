# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-theme-demo, swatch sheet for the PhosphorTheme tokens.
#
# Renders every M3 token as a labelled card. Hot-reloads when
# ~/.local/share/phosphor/palettes/current.json changes. Acts as the
# acceptance test for libs/phosphor-theme, if the demo doesn't update
# live, the library isn't wired right.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

# CMAKE_AUTOMOC is NOT set here: the top level enables it globally and
# qt_add_qml_module enables it on its own targets — the sibling popout demo
# documents the same redundancy.

# Demo QML lives in this directory; bundle as its own QML module so
# qmlcachegen can compile it.
# Directory-scope suppression of -Wmissing-include-dirs for the targets
# qt_add_qml_module generates here: Qt adds <target>_autogen/include to each
# sub-target's -I list, but when a module carries no Q_OBJECT sources AUTOMOC
# never creates that directory (and --clean-first deletes it), so cc1plus
# warns on a Qt-managed scratch path. Full rationale: src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(phosphor_theme_demo_qml
    URI Phosphor.ThemeDemo
    VERSION 1.0
    STATIC
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Phosphor/ThemeDemo
    QML_FILES
        Main.qml
        Swatch.qml
    # PresetPalettes ships as a QML_SINGLETON inside this demo's own
    # module, hardcoded named palettes are demo decoration, not lib
    # contract. Compiling header + .cpp into the QML module static lib
    # gives both the type registration and the implementation in one
    # place; AUTOMOC scans the header and qmltyperegistrar publishes
    # `PhosphorThemeDemo::PresetPalettes` to QML.
    SOURCES
        PresetPalettes.h
        presetpalettes.cpp
)

# The QML module needs PhosphorTheme link visibility because
# presetpalettes.cpp instantiates PaletteStore (to read the canonical
# default tokens without duplicating them here) and references
# TokenNames constants.
target_link_libraries(phosphor_theme_demo_qml
    PRIVATE
        PhosphorTheme::PhosphorTheme
)
# Explicit, not inherited: PhosphorTheme propagates cxx_std_20 PUBLIC, but
# the sibling demos pin it per-target — keep the boilerplate identical so a
# copied demo doesn't silently lose the requirement.
target_compile_features(phosphor_theme_demo_qml PRIVATE cxx_std_20)

add_executable(phosphor-theme-demo
    main.cpp
)
target_compile_features(phosphor-theme-demo PRIVATE cxx_std_20)

target_link_libraries(phosphor-theme-demo
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        # Static QML modules ship both a library (`*Qml`) and a plugin
        # registrar (`*Qmlplugin`). The registrar is what actually
        # publishes the module to QQmlEngine at runtime, without it,
        # `import Phosphor.Theme` fails at load time with "plugin
        # PhosphorThemeQmlplugin not found" even though the library
        # was linked. Link both targets here AND the demo's own
        # plugin so each `import` resolves.
        phosphor_theme_demo_qmlplugin
        PhosphorTheme::PhosphorTheme
        PhosphorThemeQml
        PhosphorThemeQmlplugin
)
