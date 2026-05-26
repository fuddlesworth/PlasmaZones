# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-theme-demo — swatch sheet for the PhosphorTheme tokens.
#
# Renders every M3 token as a labelled card. Hot-reloads when
# ~/.local/share/phosphor/palettes/current.json changes. Acts as the
# acceptance test for libs/phosphor-theme — if the demo doesn't update
# live, the library isn't wired right.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick)

# AUTOMOC is required so the QML module's metatype generation picks up
# any Q_OBJECT we add here.
set(CMAKE_AUTOMOC ON)

# Demo QML lives in this directory; bundle as its own QML module so
# qmlcachegen can compile it.
qt_add_qml_module(phosphor_theme_demo_qml
    URI Phosphor.ThemeDemo
    VERSION 1.0
    STATIC
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Phosphor/ThemeDemo
    QML_FILES
        Main.qml
        Swatch.qml
)

add_executable(phosphor-theme-demo
    main.cpp
)

target_link_libraries(phosphor-theme-demo
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        # Static QML modules ship both a library (`*Qml`) and a plugin
        # registrar (`*Qmlplugin`). The registrar is what actually
        # publishes the module to QQmlEngine at runtime — without it,
        # `import Phosphor.Theme` fails at load time with "plugin
        # PhosphorThemeQmlplugin not found" even though the library
        # was linked. Link both targets here AND the demo's own
        # plugin so each `import` resolves.
        phosphor_theme_demo_qmlplugin
        PhosphorTheme::PhosphorTheme
        PhosphorThemeQml
        PhosphorThemeQmlplugin
)
