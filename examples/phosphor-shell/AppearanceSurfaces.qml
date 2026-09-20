// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import Phosphor.Shell
import Phosphor.Picker
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root
    property bool locked: false
    PerScreenPanels {
        model: PhosphorShell.screens
        delegate: PanelWindow {
            id: surface
            readonly property bool opened: !root.locked && PickerRegistry.openScreen === (screen ? screen.name : "") && PickerRegistry.openScreen !== ""
            edge: PanelWindow.Top
            alignment: PanelWindow.Fill
            thickness: modelData.height
            panelLayer: PanelWindow.LayerOverlay
            exclusiveZoneEnabled: false
            keyboardFocus: PanelWindow.OnDemand
            inputRegion: opened ? [Qt.rect(0, 0, width, height)] : []
            Keys.onEscapePressed: event => {
                if (opened)
                    PickerRegistry.hide();
                else
                    event.accepted = false;
            }
            Connections {
                target: PickerRegistry
                function onDesktopPreviewChanged() {
                    if (!surface.opened)
                        return;
                    if (PickerRegistry.desktopPreview)
                        backToAppearance.forceActiveFocus();
                    else
                        surface.focusAppearance();
                }
            }
            onOpenedChanged: if (opened)
                focusAppearance()
            function focusAppearance() {
                Qt.callLater(() => {
                    if (workspace.item)
                        workspace.item.forceActiveFocus();
                    if (surface.Window.window)
                        surface.Window.window.requestActivate();
                });
            }
            Component.onCompleted: if (opened)
                focusAppearance()
            Rectangle {
                anchors.fill: parent
                visible: surface.opened && !PickerRegistry.desktopPreview
                color: Qt.alpha(Appearance.recess, .26)
            }
            MouseArea {
                anchors.fill: parent
                enabled: surface.opened
                onClicked: PickerRegistry.hide()
            }
            Item {
                visible: surface.opened && PickerRegistry.desktopPreview
                x: 0
                y: Appearance.bottom ? 0 : Appearance.barHeight + 24
                width: parent.width
                height: parent.height - Appearance.barHeight - 24
                clip: true
                // Preserve the desktop's full-output crop while leaving the
                // real bar visible above this overlay.
                LookImage {
                    y: -parent.y
                    width: surface.width
                    height: surface.height
                    radius: 0
                    decodeWidth: surface.width * 2
                    path: {
                        const walls = Appearance.settings.wallpapers;
                        return (walls[PickerRegistry.openScreen] || walls[""] || {}).path || "";
                    }
                    fit: {
                        const walls = Appearance.settings.wallpapers;
                        return (walls[PickerRegistry.openScreen] || walls[""] || {}).fit || "fill";
                    }
                }
            }
            Loader {
                id: workspace
                active: surface.opened
                visible: surface.opened && !PickerRegistry.desktopPreview
                x: (parent.width - width) / 2
                y: Appearance.bottom ? 38 : 98
                width: Math.min(1224, parent.width - 48)
                height: Math.max(280, parent.height - y - (Appearance.bottom ? 96 : 42))
                sourceComponent: AppearanceWorkspace {
                    controller: PickerRegistry
                    availableWidgets: BarRegistry.factoryIds
                    decoration: ShellChrome.decorationComponent
                }
                onLoaded: surface.focusAppearance()
            }
            ShellSurface {
                visible: surface.opened && PickerRegistry.desktopPreview
                anchors.horizontalCenter: parent.horizontalCenter
                y: Appearance.bottom ? parent.height - Appearance.barHeight - 108 : parent.height - 84
                width: previewActions.implicitWidth + 32
                height: 52
                accented: true
                RowLayout {
                    id: previewActions
                    anchors.centerIn: parent
                    spacing: 16
                    LookText {
                        text: qsTr("Previewing your desktop")
                        size: 11
                    }
                    ShellButton {
                        id: backToAppearance
                        text: qsTr("Back to Appearance")
                        onClicked: {
                            PickerRegistry.desktopPreview = false;
                            surface.focusAppearance();
                        }
                    }
                    ShellButton {
                        text: qsTr("Apply changes")
                        highlighted: true
                        enabled: AppearanceStore.dirty && !AppearanceLibrary.busy
                        onClicked: PickerRegistry.apply()
                    }
                }
            }
        }
    }
}
