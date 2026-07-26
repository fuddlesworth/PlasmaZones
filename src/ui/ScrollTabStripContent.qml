// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Tab indicators for tabbed scrolling columns.
 *
 * Display-only and click-through: one compact pill per tabbed column,
 * centered on the column's top edge, listing the column's tabs with the
 * active one highlighted. The model arrives from C++ as `strips` — a list
 * of maps with x / y / width (shell-window coordinates) and
 * tabs (list of {title, active}). Updates are plain property writes; the
 * component is not re-instantiated per relayout.
 *
 * Overflow: a column narrower than its tab row left-anchors the row and
 * clips the TRAILING chips (no "+N" affordance) — a deliberate v1
 * trade-off for a passive indicator. The active tab is laid out first-fit
 * from the left, so it stays visible under the clip. The strip carries no
 * accessible surface because it is click-through by design.
 */

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Item {
    id: root

    /// Strip entries pushed by the daemon (see file doc).
    property var strips: []
    // User overlay font, pushed by the daemon (writeFontProperties) like
    // every other overlay slot; falls back to the theme's small font.
    property string fontFamily: ""
    property real fontSizeScale: 1.0
    property int fontWeight: Font.Normal
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false

    anchors.fill: parent

    Repeater {
        model: root.strips

        delegate: Rectangle {
            id: pill

            required property var modelData

            readonly property int columnX: modelData.x
            readonly property int columnY: modelData.y
            readonly property int columnWidth: modelData.width
            readonly property var tabs: modelData.tabs

            x: columnX + (columnWidth - width) / 2
            y: columnY + Kirigami.Units.smallSpacing
            width: Math.max(0, Math.min(tabRow.implicitWidth + Kirigami.Units.largeSpacing * 2, columnWidth - Kirigami.Units.largeSpacing * 2))
            height: tabRow.implicitHeight + Kirigami.Units.smallSpacing * 2
            radius: height / 2
            color: Qt.alpha(Kirigami.Theme.backgroundColor, 0.85)
            border.width: 1
            border.color: Qt.alpha(Kirigami.Theme.textColor, 0.2)
            clip: true

            Row {
                id: tabRow

                // Centered while it fits; once the row is wider than the pill,
                // pin it to the left padding instead — centering would clip
                // BOTH ends and could hide the active tab (which the model
                // lays out first-fit from the left).
                anchors.verticalCenter: parent.verticalCenter
                x: Math.max(Kirigami.Units.largeSpacing, (pill.width - width) / 2)
                spacing: Kirigami.Units.smallSpacing

                Repeater {
                    model: pill.tabs

                    delegate: Rectangle {
                        id: chip

                        required property var modelData

                        readonly property bool active: modelData.active === true

                        // The label's ELIDED width, not implicitWidth — a
                        // long title must not blow the chip (and thereby the
                        // row) past the pill's clamp and clip its siblings.
                        width: chipLabel.width + Kirigami.Units.largeSpacing
                        height: chipLabel.implicitHeight + Kirigami.Units.smallSpacing
                        radius: height / 2
                        color: chip.active ? Kirigami.Theme.highlightColor : "transparent"

                        QQC2.Label {
                            id: chipLabel

                            anchors.centerIn: parent
                            text: chip.modelData.title
                            elide: Text.ElideRight
                            width: Math.min(implicitWidth, Kirigami.Units.gridUnit * 8)
                            color: chip.active ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                            // pixelSize (not pointSize, which is -1 for
                            // pixel-defined theme fonts) scaled by the user's
                            // overlay font scale; the user family wins when set.
                            font.family: root.fontFamily.length > 0 ? root.fontFamily : Kirigami.Theme.smallFont.family
                            font.pixelSize: Math.round((Kirigami.Theme.smallFont.pixelSize > 0 ? Kirigami.Theme.smallFont.pixelSize : Kirigami.Units.gridUnit * 0.6) * root.fontSizeScale)
                            font.weight: root.fontWeight
                            font.italic: root.fontItalic
                            font.underline: root.fontUnderline
                            font.strikeout: root.fontStrikeout
                        }
                    }
                }
            }
        }
    }
}
