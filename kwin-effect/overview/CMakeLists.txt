# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PlasmaZones Workspace Overview: a second KWin effect plugin beside
# kwin_effect_plasmazones. A plugin factory creates exactly one Effect, and
# the main effect is an OffscreenEffect, so the QuickSceneEffect-based
# overview lives in its own .so. The two talk to the daemon independently
# over D-Bus and share nothing in-process.
#
# The QML module is embedded in the plugin binary (NO_PLUGIN, same target):
# the rcc static initializer registers it when KWin dlopens the plugin, and
# loadFromModule resolves it through the engine's default qrc:/qt/qml import
# path. Nothing is installed under KDE_INSTALL_QMLDIR.

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui DBus Qml Quick)

set(plasmazones_overview_SRCS
    main.cpp
    overvieweffect.cpp
    overvieweffect.h
    daemonclient.cpp
    daemonclient.h
    overviewlogging.cpp
    overviewlogging.h
)

kcoreaddons_add_plugin(kwin_effect_plasmazones_overview
    SOURCES ${plasmazones_overview_SRCS}
    INSTALL_NAMESPACE "kwin/effects/plugins"
)

qt6_add_qml_module(kwin_effect_plasmazones_overview
    NO_PLUGIN
    URI org.plasmazones.overview
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/WorkspaceColumn.qml
        qml/WorkspaceCell.qml
        qml/WindowTile.qml
        qml/WorkspaceLabel.qml
        qml/WorkspaceGapDropArea.qml
        qml/DragProxy.qml
        qml/DockLayer.qml
)

target_link_libraries(kwin_effect_plasmazones_overview PRIVATE
    KWin::kwin
    Qt6::Core
    Qt6::Gui
    Qt6::DBus
    Qt6::Qml
    Qt6::Quick
    # Service names, interface names, the daemon client helpers.
    PhosphorProtocol::PhosphorProtocol
    # The canonical screen-id and window-id builders, so the ids this plugin
    # sends to the daemon are byte-identical to the main effect's.
    PhosphorIdentity::PhosphorIdentity
    # The motion profile tree: the open/close duration is the desktop.switch
    # motion profile, resolved the way the main effect resolves it.
    PhosphorAnimation::PhosphorAnimation
)

target_include_directories(kwin_effect_plasmazones_overview PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)
