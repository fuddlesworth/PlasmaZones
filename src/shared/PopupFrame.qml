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
        // includes the glow. The OSD cards (LayoutOsdContent.qml,
        // NavigationOsdContent.qml) get this margin by embedding PopupFrame
        // itself, so there is no separate value to keep in sync there —
        // but src/ui/ZoneSelectorContent.qml:76 clones the glowMargin
        // expression for its corner anchor insets and must track changes.

        id: style

        readonly property real backgroundAlpha: 0.95
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
        // Soft theme-tinted glow. PopupFrame is the single source of this
        // chrome — the layout and navigation OSD cards get it by embedding
        // PopupFrame (LayoutOsdContent.qml / NavigationOsdContent.qml), so
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

        // Card chrome (background, rounded corners, 1px border). This is
        // the MultiEffect source and is hidden (`visible: false`): the
        // effect paints it exactly once together with its shadow. Feeding
        // a VISIBLE item (or worse, one containing the injected content)
        // into the effect paints it twice — once in the offscreen effect
        // texture and once live — compounding the 0.95-alpha background
        // to ~0.9975 and paying an extra offscreen pass per popup. The
        // injected content lives in `frame` below and is painted live
        // exactly once, on top of the effect output.
        Rectangle {
            id: frameChrome

            anchors.fill: parent
            anchors.margins: root.glowMargin
            visible: false
            color: Qt.rgba(root.backgroundColor.r, root.backgroundColor.g, root.backgroundColor.b, style.backgroundAlpha)
            radius: root.containerRadius
            border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
            border.width: 1
        }

        // Sizing: `anchors.fill: frameChrome` keeps the effect rect
        // matched to the source's natural geometry so the card chrome is
        // rendered 1:1 with no scaling ghost. Anchoring to `parent`
        // (= captureItem) would stretch the source texture to fill the
        // larger glow-padded rect, producing a fuzzy
        // stretched-corner-radius copy underneath the real frame.
        // To extend the drawn SHADOW past the frame edges (the original
        // reason captureItem exists), set explicit `paddingRect`: the
        // four components are the per-side padding amounts (x = left,
        // y = top, width = right, height = bottom) added to the effect's
        // own bounds for shadow draw purposes. Adding glowMargin on each
        // side lets the shadow's blur and offset extend fully into the
        // captureItem ring without scaling the source.
        // `autoPaddingEnabled: false` opts out of Qt's blur-derived
        // padding heuristic so the padding is exactly what we declare,
        // regardless of how shadowBlur / shadowVerticalOffset are tuned.
        //
        // Shadow budget — keep these four values in sync:
        //   glowMargin >= blurMax * shadowBlur + shadowVerticalOffset
        // The blur tail hard-truncates at the captureItem edge if the
        // extent exceeds glowMargin (the artifact the margin exists to
        // prevent). Qt's default blurMax of 32 with shadowBlur 1 and
        // offset 4 gives a 36px extent against a ~23px margin, so blurMax
        // is pinned to glowMargin - shadowVerticalOffset (≈19) and the
        // falloff reaches zero smoothly inside the budget instead.
        // src/ui/ZoneSelectorContent.qml:76 clones the glowMargin
        // expression; keep it in sync when changing the budget.
        MultiEffect {
            source: frameChrome
            anchors.fill: frameChrome
            autoPaddingEnabled: false
            paddingRect: Qt.rect(root.glowMargin, root.glowMargin, root.glowMargin, root.glowMargin)
            shadowEnabled: true
            shadowColor: Qt.rgba(root.backgroundColor.r, root.backgroundColor.g, root.backgroundColor.b, style.shadowAlpha)
            shadowBlur: 1
            shadowVerticalOffset: 4
            shadowHorizontalOffset: 0
            blurMax: Math.max(0, Math.floor(root.glowMargin - shadowVerticalOffset))
        }

        // Content container for injected children (via the default
        // property alias). Transparent and painted live exactly once, on
        // top of the effect output — it deliberately holds no chrome so
        // nothing here is double-composited by the MultiEffect above.
        Item {
            id: frame

            // Inset by glowMargin so the frame is exactly the
            // consumer-set PopupFrame root size, centred in captureItem.
            anchors.fill: parent
            anchors.margins: root.glowMargin
        }
    }
}
