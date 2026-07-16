// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Effects
import org.kde.kirigami as Kirigami

/**
 * @brief Shared popup frame providing a consistent soft glow, background, and border.
 *
 * Used by ZoneSelectorWindow and LayoutPickerOverlay to share container chrome.
 * Children are injected via the default property alias into the internal frame.
 *
 * The frame and its glow live inside `captureItem`, which is the
 * SurfaceAnimator shader anchor (tagged `shaderAnchor: true` /
 * `objectName: "shaderAnchor"`). captureItem is larger than the visible
 * frame by `glowMargin` on every side, so a show / hide transition
 * captures the glow along with the card and animates them together —
 * without that margin the glow is clipped at the frame edge for the leg
 * and snaps back when the leg ends. Consumers size and position the
 * PopupFrame root as the bare card; the capture margin is internal, so
 * no consumer has to resize or re-anchor anything or tag the frame
 * itself.
 *
 * MouseArea is NOT included — each parent provides its own dismiss/absorb logic.
 */
Item {
    // Shader-transition capture target. SurfaceAnimator walks the visual
    // tree for `shaderAnchor: true`; selector_update.cpp looks the same
    // Item up by objectName. Centred on root and larger than it by
    // glowMargin, so it owns the card AND its glow while the consumer
    // still treats the PopupFrame root as the bare card.

    id: root

    property color backgroundColor: Kirigami.Theme.backgroundColor
    property color textColor: Kirigami.Theme.textColor
    property real containerRadius: Kirigami.Units.smallSpacing * 3
    /// Outward extent of `captureItem` past the visible card on every
    /// side, in logical pixels. The capture margin is internal to
    /// PopupFrame, but consumers anchored against their own parent's
    /// edges (e.g. ZoneSelectorContent's corner positions) MUST inset
    /// their anchor margins by at least this amount so `captureItem`'s
    /// glow ring is not pushed past the screen edge and clipped. Use
    /// `Math.max(consumerMargin, glowMargin)` at edge-anchored positions.
    readonly property real glowMargin: style.glowMargin
    default property alias contentData: frame.data

    QtObject {
        // Capture margin around the visible frame. The drop-shadow glow
        // blurs roughly this far past the frame; captureItem extends the
        // same distance on every side so the SurfaceAnimator FBO grab
        // includes the glow. Matches the OSD cards' osdCard margin.

        id: style

        readonly property real backgroundAlpha: 0.95
        readonly property real borderAlpha: 0.2
        readonly property real shadowAlpha: 0.5
        // Math.ceil rounds the gridUnit-derived value up to an integer
        // pixel on fractional-DPI outputs (e.g. 1.25 * 18 = 22.5 →
        // 23 px), so a consumer that pipes the published `root.glowMargin`
        // through Math.floor for an `int` property doesn't lose half a
        // pixel of clearance and silently re-introduce the corner-anchor
        // glow-clip artifact. The published value is integral by design.
        readonly property real glowMargin: Math.ceil(Kirigami.Units.gridUnit * 1.25)
    }

    // `shaderContentRect` tells SurfaceAnimator where the visible card
    // sits inside this oversized capture item (anchor-local coords). The
    // animator folds it into iAnchorRectInTexture so animation shaders
    // operate in card space — the glow margin lands outside their [0,1]
    // and generative effects (fire, incinerate) stay confined to the
    // card instead of spilling into the transparent margin.
    Item {
        // Soft theme-tinted glow — same MultiEffect parameters as the
        // layout and navigation OSD cards (see LayoutOsdContent.qml) so
        // every popup overlay reads as the same surface family. The
        // "shadow" is tinted with the theme background rather than
        // black: a pale halo on light themes, a soft dark one on dark.

        id: captureItem

        property bool shaderAnchor: true
        property rect shaderContentRect: Qt.rect(frame.x, frame.y, frame.width, frame.height)

        objectName: "shaderAnchor"
        anchors.centerIn: parent
        width: root.width + root.glowMargin * 2
        height: root.height + root.glowMargin * 2

        // Sizing: `anchors.fill: frame` keeps the effect rect matched to
        // the source's natural geometry so the card chrome (background,
        // rounded corners, 1px border) is rendered 1:1 with no scaling
        // ghost. Anchoring to `parent` (= captureItem) would stretch the
        // source texture to fill the larger glow-padded rect, producing
        // a fuzzy stretched-corner-radius copy underneath the real frame.
        // To extend the drawn SHADOW past the frame edges (the original
        // reason captureItem exists), set explicit `paddingRect`: the
        // four components are the per-side padding amounts (x = left,
        // y = top, width = right, height = bottom) added to the effect's
        // own bounds for shadow draw purposes. Adding glowMargin on each
        // side lets the shadow's blur and offset extend fully into the
        // captureItem ring without scaling the source. The +4 vertical
        // shadow offset stays comfortably within the bottom padding.
        // `autoPaddingEnabled: false` opts out of Qt's blur-derived
        // padding heuristic so the padding is exactly what we declare,
        // regardless of how shadowBlur / shadowVerticalOffset are tuned.
        MultiEffect {
            source: frame
            anchors.fill: frame
            autoPaddingEnabled: false
            paddingRect: Qt.rect(root.glowMargin, root.glowMargin, root.glowMargin, root.glowMargin)
            shadowEnabled: true
            shadowColor: Qt.rgba(root.backgroundColor.r, root.backgroundColor.g, root.backgroundColor.b, style.shadowAlpha)
            shadowBlur: 1
            shadowVerticalOffset: 4
            shadowHorizontalOffset: 0
        }

        Rectangle {
            id: frame

            // Inset by glowMargin so the frame is exactly the
            // consumer-set PopupFrame root size, centred in captureItem.
            anchors.fill: parent
            anchors.margins: root.glowMargin
            color: Qt.rgba(root.backgroundColor.r, root.backgroundColor.g, root.backgroundColor.b, style.backgroundAlpha)
            radius: root.containerRadius
            border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
            border.width: 1
        }
    }
}
