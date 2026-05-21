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
    id: root

    property color backgroundColor: Kirigami.Theme.backgroundColor
    property color textColor: Kirigami.Theme.textColor
    property real containerRadius: Kirigami.Units.smallSpacing * 3
    default property alias contentData: frame.data

    QtObject {
        id: style

        readonly property real backgroundAlpha: 0.95
        readonly property real borderAlpha: 0.2
        readonly property real shadowAlpha: 0.5
        // Capture margin around the visible frame. The drop-shadow glow
        // blurs roughly this far past the frame; captureItem extends the
        // same distance on every side so the SurfaceAnimator FBO grab
        // includes the glow. Matches the OSD cards' osdCard margin.
        readonly property real glowMargin: Kirigami.Units.gridUnit * 1.25
    }

    // Shader-transition capture target. SurfaceAnimator walks the visual
    // tree for `shaderAnchor: true`; selector_update.cpp looks the same
    // Item up by objectName. Centred on root and larger than it by
    // glowMargin, so it owns the card AND its glow while the consumer
    // still treats the PopupFrame root as the bare card.
    Item {
        id: captureItem

        property bool shaderAnchor: true

        objectName: "shaderAnchor"
        anchors.centerIn: parent
        width: root.width + style.glowMargin * 2
        height: root.height + style.glowMargin * 2

        // Soft theme-tinted glow — same MultiEffect parameters as the
        // layout and navigation OSD cards (see LayoutOsdContent.qml) so
        // every popup overlay reads as the same surface family. The
        // "shadow" is tinted with the theme background rather than
        // black: a pale halo on light themes, a soft dark one on dark.
        MultiEffect {
            source: frame
            anchors.fill: frame
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
            anchors.margins: style.glowMargin
            color: Qt.rgba(root.backgroundColor.r, root.backgroundColor.g, root.backgroundColor.b, style.backgroundAlpha)
            radius: root.containerRadius
            border.color: Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, style.borderAlpha)
            border.width: 1
        }

    }

}
