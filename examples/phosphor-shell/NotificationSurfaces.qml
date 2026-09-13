// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Window
import Phosphor.Shell
import Phosphor.Notifications
import Phosphor.Theme

Item {
    id: root
    property bool locked: false
    property bool centerOpen: false
    signal openCenterRequested
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
                screenName: surface.screen ? surface.screen.name : ""
                decoration: ShellChrome.decorationComponent
                onOpenCenterRequested: root.openCenterRequested()
                // Restore expiry when a hidden card loses its view, including
                // center open, DND and output removal. Closed IDs are harmless.
                onToastDismissed: id => NotificationRegistry.setExpiryPaused(id, false)
                Component.onCompleted: ToastRegistry.attachHost(host, screenName, modelData.isPrimary)
                Component.onDestruction: {
                    clear();
                    ToastRegistry.detachHost(host);
                }
            }
        }
    }
}
