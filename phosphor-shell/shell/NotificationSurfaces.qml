// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Window
import Phosphor.Shell
import Phosphor.Ipc
import Phosphor.Notifications
import Phosphor.Theme

Item {
    id: root
    property bool locked: false
    property bool centerOpen: false
    signal openCenterRequested
    // Scripted notifications and bar shortcuts share the D-Bus lifecycle.
    IpcTarget {
        target: "notify"
        function send(summary: string, body: string): int {
            return NotificationRegistry.send(summary, body);
        }
        function toggle(): void {
            root.openCenterRequested();
        }
        function hide(): void {
            Popouts.close(Popouts.handleFor("bar.panel.notification"));
        }
    }
    Binding {
        target: NotificationRegistry
        property: "popupsSuppressed"
        value: root.locked || root.centerOpen
    }
    Connections {
        target: Popouts
        function onPopoutOpened(popoutId, handle) {
            if (popoutId === "bar.panel.notification")
                root.centerOpen = true;
        }
        function onPopoutClosed(popoutId, handle) {
            if (popoutId === "bar.panel.notification")
                root.centerOpen = false;
        }
    }
    PerScreenPanels {
        model: PhosphorShell.screens
        delegate: PanelWindow {
            id: surface
            edge: PanelWindow.Top
            alignment: PanelWindow.Fill
            thickness: modelData.height
            panelLayer: PanelWindow.LayerOverlay
            exclusiveZoneEnabled: false
            keyboardFocus: PanelWindow.OnDemand
            inputRegion: host.inputRects
            ToastHost {
                id: host
                anchors.fill: parent
                backend: NotificationRegistry
                property bool materialBlurred: Appearance.settings.material !== "solid"
                function applyMaterial() {
                    const region = materialBlurred && inputRects.length ? inputRects[0] : Qt.rect(0, 0, 0, 0);
                    ShellEffects.setBlurBehind(host, region, Qt.rect(0, 0, 0, 0), Appearance.radius);
                }
                onInputRectsChanged: Qt.callLater(applyMaterial)
                onMaterialBlurredChanged: Qt.callLater(applyMaterial)
                Window.onWindowChanged: Qt.callLater(applyMaterial)
                screenName: surface.screen ? surface.screen.name : ""
                decoration: ShellChrome.decorationComponent
                onOpenCenterRequested: root.openCenterRequested()
                // Restore expiry when a hidden card loses its view, including
                // center open, DND and output removal. Closed IDs are harmless.
                onToastDismissed: id => NotificationRegistry.setExpiryPaused(id, false)
                Component.onCompleted: {
                    ToastRegistry.attachHost(host, screenName, modelData.isPrimary);
                    Qt.callLater(applyMaterial);
                }
                Component.onDestruction: {
                    clear();
                    ToastRegistry.detachHost(host);
                }
            }
        }
    }
}
