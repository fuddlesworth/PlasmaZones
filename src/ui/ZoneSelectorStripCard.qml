// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

/**
 * One column of the strip-mode zone selector: a card representing a
 * scrolling column, its tiles stacked by their resolved fractions (or a
 * tab strip when the column is tabbed), with a window/tab count caption.
 * Rendering only — the C++ hit-test (selector_strip.cpp) reads this
 * card's rendered rect back by objectName + `index` and computes the
 * gap / half / whole-card targets itself, so this file carries no pointer
 * handling, mirroring LayoutCard's role in the layout-mode popup.
 *
 * Cards are uniform-width on purpose: computeZoneSelectorLayout sizes the
 * bar and the scroll content assuming one cell size per card, and the
 * insert-bar overlays in ZoneSelectorContent.qml position arithmetically
 * off the same uniformity. Column proportion is conveyed by the tile
 * geometry inside the preview, not by the card's footprint.
 *
 * PRODUCER CONTRACT: every tile map carries numeric x/y/width/height and a
 * boolean activeTab (stripcardserialize.cpp inserts all of them
 * unconditionally). The geometry bindings below rely on that — a missing
 * field would propagate as NaN into item geometry with no diagnostic — so
 * any second producer must honour the same shape.
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

    // Font seeds (the user's configured zone-selector font, bound from
    // ZoneSelectorContent's root — the layout-mode cards honour them, so the
    // strip caption must too).
    property string fontFamily: ""
    property real fontSizeScale: 1
    property int fontWeight: Font.Bold
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false

    /// -1 none, 0 top half, 1 bottom half, 2 whole card.
    property int selectedHalf: -1
    readonly property bool isTabbed: modelData.tabbed === true
    readonly property bool isActiveColumn: modelData.active === true
    readonly property var tiles: modelData.tiles || []

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
        // A min-height-overflowing stack legitimately resolves tail tiles
        // past the column (relRect y > 1); clip so they cannot paint over
        // the caption or a neighbouring card.
        clip: true

        // Tab strip for a tabbed column: one segment per tab.
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

                    width: Math.max(4, (tabStrip.width - tabStrip.spacing * Math.max(0, card.tiles.length - 1)) / Math.max(1, card.tiles.length))
                    height: tabStrip.height
                    radius: 2
                    color: modelData.activeTab ? card.highlightColor : card.inactiveColor
                    opacity: modelData.activeTab ? card.activeOpacity : card.inactiveOpacity
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

        // Normal column: tiles stacked by their resolved fractions. The
        // width/height gates double as the guard against a producer that
        // omitted a geometry field (see the PRODUCER CONTRACT above): a
        // missing field compares false and hides the tile instead of
        // silently mispositioning it with NaN geometry.
        Repeater {
            model: card.isTabbed ? [] : card.tiles

            delegate: Rectangle {
                required property var modelData

                visible: modelData.width > 0 && modelData.height > 0
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
    }

    // Half-target highlight: top / bottom half of the preview for a normal
    // column, the WHOLE CARD for the tabbed dock (half 2) — the C++ commit
    // treats half 2 as docking onto the entire column, so the highlight
    // footprint matches that semantic rather than stopping at the preview.
    Rectangle {
        visible: card.selectedHalf >= 0
        x: card.selectedHalf === 2 ? 0 : preview.x
        y: card.selectedHalf === 2 ? 0 : (card.selectedHalf === 1 ? preview.y + preview.height / 2 : preview.y)
        width: card.selectedHalf === 2 ? card.width : preview.width
        height: card.selectedHalf === 2 ? card.height : preview.height / 2
        radius: card.selectedHalf === 2 ? cardBackground.radius : 3
        color: card.highlightColor
        opacity: card.activeOpacity
        border.width: 2
        border.color: card.highlightColor
    }

    // Caption: window / tab count.
    Text {
        anchors.top: preview.bottom
        anchors.topMargin: Kirigami.Units.smallSpacing
        anchors.horizontalCenter: parent.horizontalCenter
        color: card.textColor
        opacity: 0.8
        font.family: card.fontFamily.length > 0 ? card.fontFamily : Kirigami.Theme.defaultFont.family
        font.pointSize: Kirigami.Theme.defaultFont.pointSize * 0.85 * card.fontSizeScale
        font.weight: card.fontWeight
        font.italic: card.fontItalic
        font.underline: card.fontUnderline
        font.strikeout: card.fontStrikeout
        // Plural strings carry %n (this project's i18np substitutes only %n;
        // %1 renders literally — see PR #801).
        text: card.isTabbed ? i18ncp("@info:label tabbed column tab count", "%n tab", "%n tabs", card.tiles.length) : i18ncp("@info:label column window count", "%n window", "%n windows", card.tiles.length)
    }
}
