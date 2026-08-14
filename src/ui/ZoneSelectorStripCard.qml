// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

/**
 * One column of the strip-mode zone selector: a card representing a
 * scrolling column, its tiles stacked by their resolved fractions (or a
 * tab strip when the column is tabbed), plus a badge row for minimized
 * tiles. Rendering only — the C++ hit-test (selector_strip.cpp) reads this
 * card's rendered rect back by objectName + `index` and computes the
 * gap / half / whole-card targets itself, so this file carries no pointer
 * handling, mirroring LayoutCard's role in the layout-mode popup.
 *
 * Cards are uniform-width on purpose: computeZoneSelectorLayout sizes the
 * bar and the scroll content assuming one cell size per card, and the
 * insert-bar overlays in ZoneSelectorContent.qml position arithmetically
 * off the same uniformity. Column proportion is conveyed by the tile
 * geometry inside the preview, not by the card's footprint.
 */
Item {
    id: card

    objectName: "stripColumnCard"

    required property var modelData
    required property int index

    // Geometry seeds (bound from ZoneSelectorContent's root properties).
    property int previewWidth: 180
    property int previewHeight: 101
    property int cardPadding: 26
    property int cardSidePadding: 18
    property int labelSpace: 28
    property int zonePadding: 1

    // Palette seeds.
    property color highlightColor: Kirigami.Theme.highlightColor
    property color inactiveColor: Kirigami.Theme.alternateBackgroundColor
    property color zoneBorderColor: Kirigami.Theme.textColor
    property color backgroundColor: Kirigami.Theme.backgroundColor
    property color textColor: Kirigami.Theme.textColor
    property real activeOpacity: 0.5
    property real inactiveOpacity: 0.3

    /// -1 none, 0 top half, 1 bottom half, 2 whole card.
    property int selectedHalf: -1
    readonly property bool isTabbed: modelData.tabbed === true
    readonly property bool isActiveColumn: modelData.active === true
    readonly property var tiles: modelData.tiles || []
    readonly property int minimizedCount: {
        var n = 0;
        for (var i = 0; i < tiles.length; ++i) {
            if (tiles[i].minimized)
                n++;
        }
        return n;
    }

    width: previewWidth + cardSidePadding * 2
    height: previewHeight + labelSpace + cardPadding

    Rectangle {
        id: cardBackground

        anchors.fill: parent
        radius: Kirigami.Units.largeSpacing
        color: card.backgroundColor
        border.width: card.isActiveColumn ? 2 : 1
        border.color: card.isActiveColumn ? card.highlightColor : Qt.rgba(card.textColor.r, card.textColor.g, card.textColor.b, 0.2)
    }

    Item {
        id: preview

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Kirigami.Units.gridUnit
        width: card.previewWidth
        height: card.previewHeight

        // Tab strip for a tabbed column: one segment per non-minimized tile.
        Row {
            id: tabStrip

            visible: card.isTabbed
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: visible ? Math.round(parent.height * 0.14) : 0
            spacing: Math.max(1, card.zonePadding)

            Repeater {
                model: card.isTabbed ? card.tiles : []

                delegate: Rectangle {
                    required property var modelData

                    visible: !modelData.minimized
                    width: visible ? Math.max(4, (tabStrip.width - tabStrip.spacing * Math.max(0, card.tiles.length - card.minimizedCount - 1)) / Math.max(1, card.tiles.length - card.minimizedCount)) : 0
                    height: tabStrip.height
                    radius: 2
                    color: modelData.activeTab ? card.highlightColor : card.inactiveColor
                    opacity: modelData.activeTab ? card.activeOpacity + 0.2 : card.inactiveOpacity
                    border.width: 1
                    border.color: card.zoneBorderColor
                }
            }
        }

        // Tabbed body: the single visible tile fills the space under the
        // tab strip.
        Rectangle {
            visible: card.isTabbed
            anchors.top: tabStrip.bottom
            anchors.topMargin: card.isTabbed ? Math.max(1, card.zonePadding) : 0
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            radius: 3
            color: card.inactiveColor
            opacity: card.inactiveOpacity
            border.width: 1
            border.color: card.zoneBorderColor
        }

        // Normal column: tiles stacked by their resolved fractions.
        Repeater {
            model: card.isTabbed ? [] : card.tiles

            delegate: Rectangle {
                required property var modelData

                visible: !modelData.minimized && !modelData.hidden && modelData.height > 0
                x: modelData.x * preview.width
                y: modelData.y * preview.height
                width: Math.max(0, modelData.width * preview.width - card.zonePadding)
                height: Math.max(0, modelData.height * preview.height - card.zonePadding)
                radius: 3
                color: card.inactiveColor
                opacity: card.inactiveOpacity
                border.width: 1
                border.color: card.zoneBorderColor
            }
        }

        // Half-target highlight for a normal column; whole-card for tabbed.
        Rectangle {
            visible: card.selectedHalf >= 0
            x: 0
            y: card.selectedHalf === 1 ? parent.height / 2 : 0
            width: parent.width
            height: card.selectedHalf === 2 ? parent.height : parent.height / 2
            radius: 3
            color: card.highlightColor
            opacity: card.activeOpacity
            border.width: 2
            border.color: card.highlightColor
        }
    }

    // Caption: window count, with a minimized-count hint when present.
    Text {
        anchors.top: preview.bottom
        anchors.topMargin: Kirigami.Units.smallSpacing
        anchors.horizontalCenter: parent.horizontalCenter
        color: card.textColor
        opacity: 0.8
        font.pointSize: Kirigami.Theme.defaultFont.pointSize * 0.85
        // Plural strings carry %n (this project's i18np substitutes only %n;
        // %1 renders literally — see PR #801).
        text: {
            var visibleCount = Math.max(1, card.tiles.length - card.minimizedCount);
            var label = card.isTabbed ? i18ncp("@info:label tabbed column tab count", "%n tab", "%n tabs", visibleCount) : i18ncp("@info:label column window count", "%n window", "%n windows", visibleCount);
            if (card.minimizedCount > 0)
                label = i18nc("@info:label column caption with minimized count", "%1 (%2)", label, i18ncp("@info:label minimized window count", "%n minimized", "%n minimized", card.minimizedCount));
            return label;
        }
    }
}
