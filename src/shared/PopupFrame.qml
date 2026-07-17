// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

/**
 * @brief Shared popup card body and shader-transition anchor.
 *
 * Supplies the opaque card body and the SurfaceAnimator shader anchor for the
 * OSD and popup cards. Children are injected via the default property alias
 * into the internal frame.
 *
 * Chrome (border, rounded corners, glow, shadow) is NOT drawn here — it is
 * owned entirely by the surface-decoration pipeline (SurfaceDecoration.qml +
 * the data/surface/* packs), which captures this card and re-renders it with
 * the resolved decoration. PopupFrame just supplies a square-cornered opaque
 * body for that capture: the border pack clips it to the resolved corner
 * radius and draws the border, the shadow pack draws the halo. A card whose
 * decoration was cleared shows the bare opaque body with no chrome.
 *
 * The body lives inside `captureItem`, the SurfaceAnimator shader anchor
 * (tagged `shaderAnchor: true` / `objectName: "shaderAnchor"`, which
 * selector_update.cpp also looks up by objectName). captureItem is larger than
 * the visible card by `captureMargin` on every side, giving show / hide shader
 * transitions capture runway around the card. Consumers size and position the
 * PopupFrame root as the bare card; the capture margin is internal, so no
 * consumer has to resize or re-anchor anything.
 *
 * MouseArea is NOT included — each parent provides its own dismiss/absorb logic.
 */
Item {
    id: root

    property color backgroundColor: Kirigami.Theme.backgroundColor
    /// Outward extent of `captureItem` past the visible card on every side, in
    /// logical pixels — capture runway for show / hide shader transitions. The
    /// margin is internal to PopupFrame, but consumers anchored against their
    /// own parent's edges (e.g. ZoneSelectorContent's corner positions) MUST
    /// inset their anchor margins by at least this amount so the capture ring
    /// (and any decoration halo drawn into it) is not pushed past the screen
    /// edge and clipped. Use `Math.max(consumerMargin, captureMargin)`.
    readonly property real captureMargin: style.captureMargin
    default property alias contentData: frame.data

    QtObject {
        id: style

        // Math.ceil rounds the gridUnit-derived value up to an integer pixel on
        // fractional-DPI outputs (e.g. 1.25 * 18 = 22.5 → 23 px), so a consumer
        // that pipes the published `root.captureMargin` through Math.floor for
        // an `int` property doesn't lose half a pixel of clearance. The
        // published value is integral by design. ZoneSelectorContent.qml's
        // `_captureMargin` clones this expression for its corner anchor insets.
        readonly property real captureMargin: Math.ceil(Kirigami.Units.gridUnit * 1.25)
    }

    // `shaderContentRect` tells SurfaceAnimator where the visible card sits
    // inside this oversized capture item (anchor-local coords). The animator
    // folds it into iAnchorRectInTexture so animation shaders operate in card
    // space — the capture margin lands outside their [0,1] and generative
    // effects (fire, incinerate) stay confined to the card.
    Item {
        id: captureItem

        property bool shaderAnchor: true
        property rect shaderContentRect: Qt.rect(frame.x, frame.y, frame.width, frame.height)

        objectName: "shaderAnchor"
        anchors.centerIn: parent
        width: root.width + root.captureMargin * 2
        height: root.height + root.captureMargin * 2

        // Opaque, square-cornered card body. The body must be fully opaque — it
        // is drawn onto a transparent layer-shell surface (surface.cpp clears to
        // Qt::transparent), so any alpha below 1.0 lets the desktop bleed
        // through. It is left square so the decoration's border pack, which
        // clips the captured card to its own rounded rect, is the sole owner of
        // the corner radius — a rounded body under a tighter pack clip leaves a
        // transparent notch at the corners.
        Rectangle {
            id: frameChrome

            anchors.fill: parent
            anchors.margins: root.captureMargin
            color: Qt.rgba(root.backgroundColor.r, root.backgroundColor.g, root.backgroundColor.b, 1.0)
        }

        // Content container for injected children (via the default property
        // alias). Transparent; drawn on top of the body and captured together
        // with it by the surface decoration.
        Item {
            id: frame

            // Inset by captureMargin so the frame is exactly the consumer-set
            // PopupFrame root size, centred in captureItem.
            anchors.fill: parent
            anchors.margins: root.captureMargin
        }
    }
}
