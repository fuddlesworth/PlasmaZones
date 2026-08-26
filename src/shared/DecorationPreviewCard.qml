// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

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

    // Deliberately has NO focus state of its own.
    //
    // A real window dims its chrome when it loses focus, so an earlier version
    // restyled the title bar with the host's focus toggle. That made the
    // preview useless for its actual job: with both the card and the pack
    // reacting, you could not tell which of them produced a change, and for a
    // pack with no active/inactive split the toggle still moved something.
    //
    // The subject is held CONSTANT so that everything which changes is the
    // decoration's doing. Focus reaches the pack through the host's
    // surfaceFocused instead.

    implicitWidth: Kirigami.Units.gridUnit * 22
    implicitHeight: Kirigami.Units.gridUnit * 14

    // Content is sized in PROPORTIONS of the card, not in Kirigami.Units.
    //
    // This is the one place in the settings QML where that is the right call.
    // The same card is rendered at wildly different sizes — a browser thumbnail
    // a few gridUnits tall and a detail-dialog pane several times that — and it
    // is a picture OF a window rather than a real one, so it has to read
    // identically at both. Fixed unit sizes do not: at thumbnail scale a
    // gridUnit*2 title bar plus four rules plus an accent block cannot fit, the
    // Column overflows, and the overflow spills outside the card (that is what
    // put a stray accent block over the card's caption).
    //
    // Ratios are chosen to sum to less than 1 so the content always fits:
    // 0.2 title bar, then within the remaining 0.8 a 0.06 margin, four 0.055
    // rules, one 0.16 accent block, and 0.05 gaps.
    Item {
        anchors.fill: parent
        // Belt and braces: a caller that sizes the card absurdly small (or a
        // future ratio that stops summing correctly) must not be able to paint
        // outside the card and into the surrounding page again.
        clip: true

        // Title bar. Gives border and corner packs a strong horizontal edge to
        // sit against, and a saturated band for tint and duotone packs to
        // remap. Fixed at the active colours — see the focus note above.
        Rectangle {
            id: titleBar

            width: parent.width
            height: Math.max(2, parent.height * 0.2)
            color: Kirigami.Theme.highlightColor

            Text {
                anchors.left: parent.left
                anchors.leftMargin: titleBar.height * 0.4
                anchors.right: parent.right
                anchors.rightMargin: titleBar.height * 0.4
                anchors.verticalCenter: parent.verticalCenter
                text: root.title
                elide: Text.ElideRight
                // Scales with the bar so the caption stays proportionate. Below
                // a few pixels the glyphs are noise, so the text drops out
                // rather than smearing the title bar a pack is trying to show.
                visible: titleBar.height >= 10
                font.pixelSize: Math.max(4, titleBar.height * 0.5)
                color: Kirigami.Theme.highlightedTextColor
                font.bold: true
            }
        }

        Column {
            anchors.top: titleBar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: parent.height * 0.06
            spacing: parent.height * 0.05

            // Text rules: high-frequency detail. Blur and frosted packs are
            // only distinguishable from a plain tint when there is something
            // fine-grained to smear.
            Repeater {
                model: 4

                Rectangle {
                    required property int index

                    width: parent.width * (index % 2 === 0 ? 0.92 : 0.64)
                    height: Math.max(1, root.height * 0.055)
                    radius: height / 2
                    color: Kirigami.Theme.textColor
                    opacity: 0.35
                }
            }

            // Saturated accent block: duotone and tint packs remap colour,
            // which a grey card cannot show.
            Rectangle {
                width: parent.width * 0.45
                height: Math.max(2, root.height * 0.16)
                radius: Math.max(1, height * 0.15)
                color: Kirigami.Theme.positiveTextColor
            }
        }
    }
}
