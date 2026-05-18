// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief One read-only monitor tile in the MonitorOverview strip.
 *
 * Shows a monitor's assigned layout, tiling on/off state and rule count.
 * Clicking the tile filters the rule list to that monitor — it is a
 * visualization, never an editor.
 */
Rectangle {
    id: tile

    /// `{ screenId, layoutName, tilingEnabled, ruleCount, assigned }` from
    /// `WindowRuleController.monitorOverview()`.
    required property var tileData
    /// True when this tile is the active monitor filter.
    property bool selected: false
    readonly property bool _assigned: tile.tileData.assigned === true

    signal clicked()

    implicitWidth: Kirigami.Units.gridUnit * 16
    implicitHeight: Kirigami.Units.gridUnit * 5
    radius: Kirigami.Units.smallSpacing
    color: tile.selected ? Kirigami.Theme.highlightColor : Kirigami.Theme.alternateBackgroundColor
    border.width: 1
    border.color: tile.selected ? Kirigami.Theme.highlightColor : Kirigami.Theme.separatorColor
    Accessible.role: Accessible.Button
    Accessible.name: i18n("Filter rules to monitor %1", tile.tileData.screenId)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.smallSpacing

        Label {
            text: tile.tileData.screenId
            font.bold: true
            color: tile.selected ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                source: tile._assigned ? (tile.tileData.tilingEnabled ? "view-grid" : "dialog-cancel") : "edit-none"
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
            }

            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                opacity: tile.selected ? 1 : 0.8
                color: tile.selected ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                text: {
                    if (!tile._assigned)
                        return i18n("Not assigned");

                    if (!tile.tileData.tilingEnabled)
                        return i18n("Tiling off");

                    if (tile.tileData.layoutName && tile.tileData.layoutName.length > 0)
                        return tile.tileData.layoutName;

                    return i18n("Assigned");
                }
            }

        }

        Label {
            Layout.fillWidth: true
            opacity: tile.selected ? 0.9 : 0.6
            color: tile.selected ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
            text: i18np("%1 rule", "%1 rules", tile.tileData.ruleCount)
        }

    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: tile.clicked()
    }

}
