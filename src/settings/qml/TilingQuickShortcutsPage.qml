// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    readonly property var
    settingsBridge: QtObject {
        readonly property bool autotileEnabled: appSettings.autotileEnabled
        readonly property var layouts: settingsController.layouts
        readonly property int assignmentViewMode: 1

        signal quickLayoutSlotsChanged()
        signal tilingQuickLayoutSlotsChanged()

        function getQuickLayoutShortcut(n) {
            return settingsController.getQuickLayoutShortcut(n);
        }

        // Quick layout slots (tiling only)
        function getTilingQuickLayoutSlot(n) {
            return settingsController.getTilingQuickLayoutSlot(n);
        }

        function setTilingQuickLayoutSlot(n, id) {
            settingsController.setTilingQuickLayoutSlot(n, id);
            tilingQuickLayoutSlotsChanged();
        }

    }

    readonly property int viewMode: 1

    contentHeight: mainCol.implicitHeight
    clip: true

    QtObject {
        id: constants

        readonly property real labelSecondaryOpacity: 0.7
        readonly property int quickLayoutSlotCount: 9
    }

    ColumnLayout {
        id: mainCol

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Assign autotile algorithms to keyboard shortcuts for quick switching.")
            visible: true
        }

        // Quick Layout Shortcuts
        Item {
            Layout.fillWidth: true
            implicitHeight: quickSlotsCard.implicitHeight

            QuickLayoutSlotsCard {
                id: quickSlotsCard

                anchors.fill: parent
                appSettings: root.settingsBridge
                constants: constants
                viewMode: root.viewMode
            }

        }

    }

}
