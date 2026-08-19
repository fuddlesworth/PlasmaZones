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
 * Cards are VARIABLE-WIDTH: each card IS its column at scale — previewWidth
 * is the full-screen width at preview scale (aspect-locked by
 * computeZoneSelectorLayout) and the card takes widthFraction of it, so a
 * half-screen column is a half-width card, all cards sharing the
 * screen-height preview height. The width formula (fraction fallback, 8 px
 * floor, side padding) is mirrored by stripCardPreviewWidth in
 * zoneselectorlayout.h (bar width) and the insert-bar arithmetic in
 * ZoneSelectorContent.qml — change one, change all three.
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
    // Preview-scaled zone border, from the C++ preview-scale computation in
    // selector_update.cpp — so the user's configured border width and
    // corner radius reach the strip tiles. (The layout-mode ZonePreview
    // does not consume these pushed values; this card is their consumer.)
    property int tileBorderWidth: 1
    property int tileBorderRadius: 2

    // Palette seeds.
    property color highlightColor: Kirigami.Theme.highlightColor
    property color inactiveColor: Kirigami.Theme.alternateBackgroundColor
    property color zoneBorderColor: Kirigami.Theme.textColor
    property color backgroundColor: Kirigami.Theme.backgroundColor
    property color textColor: Kirigami.Theme.textColor
    property real activeOpacity: 0.5
    property real inactiveOpacity: 0.3

    // Font seeds (the user's configured zone-selector font, bound from
    // ZoneSelectorContent's root). In the layout-mode popup these reach the
    // zone-NUMBER labels via ZonePreview; LayoutCard's own caption uses the
    // theme small font unscaled. The strip caption is this card's only text,
    // so it takes the seeds directly — the strip card has no zone numbers to
    // carry them instead. Its top margin is deliberately fixed at
    // smallSpacing (labelTopMargin is not forwarded here; it sizes the
    // layout-mode label BAND, which this card does not have).
    property string fontFamily: ""
    property real fontSizeScale: 1
    property int fontWeight: Font.Bold
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false

    /// -1 none, 0 top half, 1 bottom half, 2 whole card.
    property int selectedHalf: -1
    // One padding vocabulary for every tile-separating gap in this card
    // (tab-strip spacing, tabbed-body margin, tile inset): floored at 1 so
    // a zero configured gap still separates the miniature tiles visually.
    readonly property int tilePadding: Math.max(1, zonePadding)
    // Inactive card border alpha, the strip twin of LayoutCard's named
    // style constants.
    readonly property real inactiveBorderAlpha: 0.2
    readonly property bool isTabbed: modelData.tabbed === true
    readonly property bool isActiveColumn: modelData.active === true
    readonly property var tiles: modelData.tiles || []
    // Real on-screen share of the work-area width, (0, 1] from the snapshot.
    // 0 (column resolved no rect) or a missing key falls back to a full-width
    // preview rather than collapsing the column to nothing.
    readonly property real widthFraction: (modelData.widthFraction > 0 && modelData.widthFraction <= 1) ? modelData.widthFraction : 1

    /// Whether this card belongs to a VERTICAL strip. The popup is a
    /// miniature of the strip, so the card's along-strip extent has to follow
    /// the same axis the real columns do.
    ///
    /// The serialized share and the tile rects stay STRIP-LOCAL (along-strip,
    /// across-strip) on the wire — the producer never transposes. This card is
    /// the single place the mapping to screen happens.
    property bool verticalAxis: false

    // The along-strip extent always carries widthFraction; which screen
    // dimension that is depends on the axis. widthFraction is the column's
    // share ALONG the strip, not a physical width, so on a vertical strip it
    // has to scale the preview's HEIGHT. Scaling previewWidth on both axes
    // renders a full-strip column on a portrait monitor as a square instead
    // of a band as tall as the preview.
    readonly property int alongExtent: Math.max(8, Math.round((verticalAxis ? previewHeight : previewWidth) * widthFraction))

    width: verticalAxis ? previewWidth + cardSidePadding * 2 : alongExtent + cardSidePadding * 2
    height: verticalAxis ? alongExtent + labelSpace + cardPadding : previewHeight + labelSpace + cardPadding

    Rectangle {
        id: cardBackground

        anchors.fill: parent
        radius: Kirigami.Units.largeSpacing
        color: card.backgroundColor
        border.width: card.isActiveColumn ? 2 : 1
        border.color: card.isActiveColumn ? card.highlightColor : Qt.rgba(card.textColor.r, card.textColor.g, card.textColor.b, card.inactiveBorderAlpha)
    }

    Item {
        id: preview

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Kirigami.Units.gridUnit
        // The card width formula minus the chrome: the whole preview IS the
        // column at its real on-screen share.
        width: card.width - card.cardSidePadding * 2
        height: card.verticalAxis ? card.alongExtent : card.previewHeight
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
            // 0.14: the tab strip takes the same share of the preview height
            // that the real tab indicator band takes of a column, eyeballed
            // against the live strip so the miniature reads as the same UI.
            height: visible ? Math.round(parent.height * 0.14) : 0
            spacing: card.tilePadding

            Repeater {
                model: card.isTabbed ? card.tiles : []

                delegate: Rectangle {
                    required property var modelData

                    width: Math.max(4, (tabStrip.width - tabStrip.spacing * Math.max(0, card.tiles.length - 1)) / Math.max(1, card.tiles.length))
                    height: tabStrip.height
                    radius: card.tileBorderRadius
                    // Single-apply alpha, ZonePreview's contract: the
                    // configured opacity is baked into the FILL colour
                    // (discarding the colour's own carried alpha so the two
                    // don't multiply), and item `opacity` stays 1 so it
                    // cannot multiply into the border.
                    color: {
                        const base = modelData.activeTab ? card.highlightColor : card.inactiveColor;
                        return Qt.rgba(base.r, base.g, base.b, modelData.activeTab ? card.activeOpacity : card.inactiveOpacity);
                    }
                    border.width: card.tileBorderWidth
                    border.color: card.zoneBorderColor
                }
            }
        }

        // Tabbed body: the single visible tile fills the space under the
        // tab strip.
        Rectangle {
            visible: card.isTabbed
            anchors.top: tabStrip.bottom
            anchors.topMargin: card.tilePadding
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            radius: card.tileBorderRadius
            // Baked alpha, same single-apply contract as the tab segments.
            color: Qt.rgba(card.inactiveColor.r, card.inactiveColor.g, card.inactiveColor.b, card.inactiveOpacity)
            border.width: card.tileBorderWidth
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
                // The rect arrives strip-local: modelData.x/width run ACROSS
                // the column, y/height ALONG it. On a horizontal strip that is
                // already screen x/y; on a vertical one the pair swaps, which
                // is the only transpose this component performs.
                x: card.verticalAxis ? modelData.y * preview.width : modelData.x * preview.width
                y: card.verticalAxis ? modelData.x * preview.height : modelData.y * preview.height
                width: Math.max(0, (card.verticalAxis ? modelData.height : modelData.width) * preview.width - card.tilePadding)
                height: Math.max(0, (card.verticalAxis ? modelData.width : modelData.height) * preview.height - card.tilePadding)
                radius: card.tileBorderRadius
                // Baked alpha, same single-apply contract as the tab segments.
                color: Qt.rgba(card.inactiveColor.r, card.inactiveColor.g, card.inactiveColor.b, card.inactiveOpacity)
                border.width: card.tileBorderWidth
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
        // Half 0 is the FIRST tile slot and half 1 appends — those are
        // positions in the column's stack, so the split follows the stack's
        // axis: top/bottom on a horizontal strip, left/right on a vertical
        // one. Splitting the wrong way would highlight a region that does not
        // correspond to the drop the hit-test just classified.
        x: card.selectedHalf === 2 ? 0 : (card.verticalAxis && card.selectedHalf === 1 ? preview.x + preview.width / 2 : preview.x)
        y: card.selectedHalf === 2 ? 0 : (!card.verticalAxis && card.selectedHalf === 1 ? preview.y + preview.height / 2 : preview.y)
        width: card.selectedHalf === 2 ? card.width : (card.verticalAxis ? preview.width / 2 : preview.width)
        height: card.selectedHalf === 2 ? card.height : (card.verticalAxis ? preview.height : preview.height / 2)
        radius: card.selectedHalf === 2 ? cardBackground.radius : card.tileBorderRadius
        // Baked alpha, same single-apply contract as the tiles above; the
        // border keeps the highlight colour's own carried alpha, exactly as
        // ZonePreview does.
        color: Qt.rgba(card.highlightColor.r, card.highlightColor.g, card.highlightColor.b, card.activeOpacity)
        border.width: 2
        border.color: card.highlightColor
    }

    // Caption: window / tab count.
    Text {
        anchors.top: preview.bottom
        anchors.topMargin: Kirigami.Units.smallSpacing
        anchors.horizontalCenter: parent.horizontalCenter
        // Bounded + elided, LayoutCard's caption contract: the card is
        // variable-width, and an unbounded caption on a narrow-share card
        // painted over the neighbouring card (the card root deliberately
        // does not clip — the whole-card highlight is sized to it).
        width: Math.max(0, card.width - card.cardSidePadding * 2)
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        color: card.textColor
        opacity: 0.8
        font.family: card.fontFamily.length > 0 ? card.fontFamily : Kirigami.Theme.defaultFont.family
        // pixelSize, not pointSize: pointSize is -1 for pixel-defined theme
        // fonts, and a negative product would be rejected, silently ignoring
        // the user's font-size scale.
        font.pixelSize: Math.max(1, Math.round((Kirigami.Theme.defaultFont.pixelSize > 0 ? Kirigami.Theme.defaultFont.pixelSize : Kirigami.Units.gridUnit * 0.6) * 0.85 * card.fontSizeScale))
        font.weight: card.fontWeight
        font.italic: card.fontItalic
        font.underline: card.fontUnderline
        font.strikeout: card.fontStrikeout
        // Plural strings carry %n (this project's i18np substitutes only %n;
        // %1 renders literally — see PR #801).
        text: card.isTabbed ? i18ncp("@info:label tabbed column tab count", "%n tab", "%n tabs", card.tiles.length) : i18ncp("@info:label column window count", "%n window", "%n windows", card.tiles.length)
    }
}
