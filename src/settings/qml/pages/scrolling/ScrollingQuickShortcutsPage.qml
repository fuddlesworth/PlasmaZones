// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Scrolling → Quick Shortcuts: the nine Meta+Alt+digit slots as they
 * apply to scrolling monitors, each holding a scrolling template.
 *
 * The rows themselves are the shared QuickLayoutSlotsCard in its scrolling
 * view mode, so the snapping and tiling pages stay row-for-row identical.
 */
SettingsFlickable {
    id: root

    readonly property int viewMode: 2

    contentHeight: mainCol.implicitHeight
    clip: true

    ColumnLayout {
        id: mainCol

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Assign scrolling templates to keyboard shortcuts for quick switching.")
            // Kirigami.InlineMessage defaults to visible: false.
            visible: true
        }

        // Quick Template Shortcuts
        Item {
            Layout.fillWidth: true
            implicitHeight: quickSlotsCard.implicitHeight

            QuickLayoutSlotsCard {
                id: quickSlotsCard

                anchors.fill: parent
                appSettings: settingsController
                viewMode: root.viewMode
            }
        }
    }
}
