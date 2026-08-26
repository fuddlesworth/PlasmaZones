// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import org.kde.kirigami as Kirigami
import QtQuick

/**
 * Stand-in subject for surface-shader (decoration) previews.
 *
 * A surface pack decorates a live window or overlay card: it samples the
 * subject as `uTexture0` and replaces it with a clipped / bordered / tinted
 * version. A preview therefore needs a SUBJECT, the way a zone-overlay preview
 * needs zones. This is that subject — a card standing in for the decorated
 * window.
 *
 * ## Why it wraps PopupFrame rather than drawing its own frame
 *
 * PopupFrame already publishes the exact contract the decoration host binds
 * against: `shaderAnchor` / `shaderContentRect` on an oversized capture item,
 * plus a deliberately SQUARE-cornered opaque body so the pack owns the corner
 * radius (a pre-rounded body under a tighter pack clip leaves transparent
 * corner notches). Re-implementing that here would be a second copy of a
 * contract that has already been debugged, and it would drift.
 *
 * ## Why it lives in the shared module
 *
 * Two consumers must show the SAME subject or the browser's static thumbnail
 * stops matching its own live preview: the settings decoration preview pane,
 * and the offline thumbnail renderer that bakes `preview.png` per pack.
 *
 * ## Content
 *
 * The body is not decorative filler. Packs in the glass / blur / duotone /
 * tint families transform what they sample, so a flat card would render them
 * indistinguishable from each other and from no pack at all. The title bar,
 * text rules and accent block give those packs real structure — edges,
 * colour, and contrast — to act on.
 *
 * Deliberately free of i18n: this module is loaded by both a translated
 * application and a headless rendering tool. The host supplies `title`
 * already translated (or a plain string in the tool).
 */
PopupFrame {
    id: root

    /// Card caption, supplied already-translated by the host.
    property string title: "Window"

    /// Drives packs that mix an active against an inactive appearance
    /// (`uSurfaceFocused`). The preview host binds its own focus toggle here
    /// AND to the shader item; this half only styles the stand-in card so the
    /// two read consistently.
    property bool focusedLook: true

    implicitWidth: Kirigami.Units.gridUnit * 22
    implicitHeight: Kirigami.Units.gridUnit * 14

    Column {
        anchors.fill: parent
        spacing: 0

        // Title bar. Gives border and corner packs a strong horizontal edge to
        // sit against, and shifts with focus so focus-reactive packs are
        // legible next to the card they decorate.
        Rectangle {
            id: titleBar

            width: parent.width
            height: Kirigami.Units.gridUnit * 2
            color: root.focusedLook ? Kirigami.Theme.highlightColor : Kirigami.Theme.alternateBackgroundColor

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Kirigami.Units.largeSpacing
                anchors.verticalCenter: parent.verticalCenter
                text: root.title
                elide: Text.ElideRight
                width: parent.width - Kirigami.Units.largeSpacing * 2
                color: root.focusedLook ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor
                font.bold: true
            }
        }

        Item {
            width: parent.width
            height: parent.height - titleBar.height

            Column {
                anchors.fill: parent
                anchors.margins: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.largeSpacing

                // Text rules: high-frequency detail. Blur and frosted packs are
                // only distinguishable from a plain tint when there is
                // something fine-grained to smear.
                Repeater {
                    model: 4

                    Rectangle {
                        required property int index

                        width: parent.width * (index % 2 === 0 ? 0.92 : 0.64)
                        height: Kirigami.Units.gridUnit * 0.5
                        radius: height / 2
                        color: Kirigami.Theme.textColor
                        opacity: 0.35
                    }
                }

                // Saturated accent block: duotone and tint packs remap colour,
                // which a grey card cannot show.
                Rectangle {
                    width: parent.width * 0.45
                    height: Kirigami.Units.gridUnit * 2.5
                    radius: Kirigami.Units.smallSpacing
                    color: Kirigami.Theme.positiveTextColor
                }
            }
        }
    }
}
