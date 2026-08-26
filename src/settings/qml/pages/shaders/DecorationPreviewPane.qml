// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as PZCommon

/**
 * @brief Live preview pane for a DECORATION (surface) pack.
 *
 * Sibling of the zone/overlay preview pane that lives inline in
 * ShaderBrowserDetailDialog. Kept in its own file rather than inlined beside
 * it: the two share no state, and folding a second pane into that dialog would
 * push it past the file-size ceiling.
 *
 * ## What it renders
 *
 * A DecorationPreviewCard (the stand-in window) decorated by the real
 * SurfaceDecoration chain host — the same component the daemon runs. It is not
 * a preview-shaped imitation of that host: the chain it feeds comes from
 * DecorationPreviewController, which composes stages through the same builder
 * OverlayService::applyDecoration uses. Everything that makes a decoration look
 * the way it does — the extended-FBO padding for outer effects, the left-to-
 * right stage fold, the multipass buffer set — therefore behaves here exactly
 * as it does on screen, because it IS the same code.
 *
 * ## Focus
 *
 * Packs that mix an active against an inactive appearance key on
 * `uSurfaceFocused`, which no other preview surface exposes. The toggle drives
 * both the shader uniform (through the host) and the card's own styling, so the
 * two read consistently.
 */
Item {
    id: root

    /// DecorationPreviewController, handed down by the detail dialog. This
    /// pane never touches the page controller itself — the dialog owns that
    /// relationship, which is why this file is not a shader-browser route
    /// file (see everyBridgeCallFromTheShaderBrowserIsReachable).
    required property var previewController
    /// The browsed pack's id.
    required property string packId
    /// Live (transient) friendly parameter map from the dialog's editor.
    property var liveParams: ({})
    /// Gated by the dialog: false while the app is backgrounded or the pane is
    /// hidden, so a closed dialog leaves no animated pack ticking.
    property bool active: false

    /// Pack metadata, re-read only when the pack changes rather than per frame.
    readonly property var _info: (previewController && packId.length > 0) ? (previewController.packInfo(packId) || ({})) : ({})
    readonly property bool _isAudioPack: _info.audio === true
    readonly property bool _needsBackdrop: _info.needsBackdrop === true

    /// The composed one-stage chain and its outer-margin request. Both
    /// recompute when the user edits a parameter, which is what makes a padding
    /// param (glow size, shadow spread) grow the preview's transparent room
    /// live rather than only on reopen.
    readonly property var _chain: (previewController && packId.length > 0 && active) ? (previewController.previewChain(packId, liveParams) || []) : []
    readonly property real _outerPad: (previewController && packId.length > 0 && active) ? previewController.previewOuterPadding(packId, liveParams) : 0

    readonly property var _audioSpectrum: previewController ? previewController.audioSpectrum : []

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing
        spacing: Kirigami.Units.smallSpacing

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Kirigami.Units.smallSpacing
            // Deliberately a flat neutral, not a theme colour: the pack
            // composites over this, and a tinted ground would misrepresent the
            // colours it produces. Mid-grey rather than the zone pane's black
            // so that a dark border and a bright glow are both legible.
            color: "#3a3a3a"
            border.width: 1
            border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
            clip: true

            // The decorated subject. Centred with room to spare so an outer
            // effect has somewhere to land: the chain host extends its canvas
            // bottom/right by _outerPad, and the card's own PopupFrame capture
            // ring provides the top/left halo room (the extension is trailing
            // by design — placement comes from the item's coordinates, the FBO
            // extension is offset inside it).
            PZCommon.DecorationPreviewCard {
                id: card

                anchors.centerIn: parent
                width: Math.min(parent.width - Kirigami.Units.gridUnit * 2 - root._outerPad, Kirigami.Units.gridUnit * 20)
                height: Math.min(parent.height - Kirigami.Units.gridUnit * 2 - root._outerPad, Kirigami.Units.gridUnit * 13)
                title: i18nc("@title sample window in the decoration preview", "Sample Window")
                focusedLook: focusToggle.checked
            }

            // The real chain host, fed the real composed chain.
            PZCommon.SurfaceDecoration {
                anchors.fill: parent
                contentItem: card
                decorationChain: root._chain
                decorationOuterPadding: root._outerPad
                audioSpectrum: root._audioSpectrum
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            CheckBox {
                id: focusToggle

                checked: true
                text: i18nc("@option:check decoration preview", "Focused")
                // Packs without an active/inactive split ignore uSurfaceFocused
                // entirely, so offering the toggle there would be a control that
                // visibly does nothing.
                enabled: true
            }

            Item {
                Layout.fillWidth: true
            }
        }

        // Honest note rather than a silently wrong preview: a needsBackdrop
        // pack samples the scene BEHIND the window, and a settings-app card has
        // no scene behind it. Such a pack takes its documented uHasBackdrop = 0
        // fallback here — the same limit the daemon overlay path has, for the
        // same reason.
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root._needsBackdrop
            type: Kirigami.MessageType.Information
            text: i18nc("@info decoration preview limitation", "This pack blurs or samples whatever sits behind the window. The preview has no desktop behind it, so it shows the pack's fallback appearance instead.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root._isAudioPack && root.previewController && !root.previewController.audioVisualizerEnabled()
            type: Kirigami.MessageType.Information
            text: i18nc("@info decoration preview limitation", "This pack reacts to audio. Turn on the audio visualizer in Shaders settings to see it move.")
        }
    }
}
