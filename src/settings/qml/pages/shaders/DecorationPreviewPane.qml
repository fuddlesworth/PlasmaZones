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
 * that uniform and NOTHING else — the stand-in card holds still across it, so
 * everything that moves is the pack's doing rather than the subject's.
 */
Item {
    id: root

    /// DecorationPreviewController, handed down by the detail dialog. This
    /// pane never touches the page controller itself — the dialog owns that
    /// relationship, which is why this file is not a shader-browser route
    /// file (see everyBridgeCallFromTheShaderBrowserIsReachable).
    required property QtObject previewController
    /// The browsed pack's id.
    required property string packId
    /// Live (transient) friendly parameter map from the dialog's editor.
    property var liveParams: ({})
    /// Gated by the dialog: false while the pane is hidden. Gates the chain's
    /// EXISTENCE, so it must track visibility only — never focus, which is
    /// what `animationsPaused` below is for.
    property bool active: false
    /// Dropped-focus freeze, forwarded through to the chain host: the preview
    /// keeps its composed frame and stops ticking, exactly what the zone
    /// pane's clock does when the app is not frontmost. Folding focus into
    /// `active` instead tore the whole chain down under a Plasma applet while
    /// the window was still fully visible.
    property bool animating: true

    /// Pack metadata, re-read only when the pack changes rather than per frame.
    readonly property var _info: (previewController && packId.length > 0) ? (previewController.packInfo(packId) || ({})) : ({})
    readonly property bool _isAudioPack: _info.audio === true
    readonly property bool _needsBackdrop: _info.needsBackdrop === true

    readonly property var _audioSpectrum: previewController ? previewController.audioSpectrum : []

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing
        spacing: Kirigami.Units.smallSpacing

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Kirigami.Units.smallSpacing
            // The same slot colour the browser cards frame their previews with,
            // so a pack looks the same in both and the wallpaper arriving is the
            // only thing that changes. The preview itself paints no ground of
            // its own (see DecorationChainPreview.groundColor), which is what
            // lets this show through while the wallpaper decodes.
            color: Kirigami.Theme.alternateBackgroundColor
            border.width: 1
            border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
            clip: true

            // Shared with the browser card's inline thumbnail, so the two can
            // never disagree about what a pack looks like. Fed the dialog's
            // live parameter map, which is what makes a padding param (glow
            // size, shadow spread) grow the transparent room as it is dragged
            // rather than only on reopen.
            DecorationChainPreview {
                id: chainPreview

                anchors.fill: parent
                anchors.margins: 1
                previewController: root.previewController
                packId: root.packId
                params: root.liveParams
                active: root.active
                animationsPaused: !root.animating
                focused: focusToggle.checked
                audioSpectrum: root._audioSpectrum
                cardTitle: i18nc("@title sample window in the decoration preview", "Sample Window")
            }

            // Covers the preview until the chain has actually compiled, the way
            // the zone pane's placeholder covers its renderer's load. Without it
            // the pane shows the plain undecorated card, then a gap while the
            // stages compile, and the decoration snaps in — the preview appears
            // to pop into place.
            //
            // COVERS rather than hides: the preview underneath has to keep
            // rendering to reach `ready` at all, because a ShaderEffectSource
            // whose chain is not visible is starved and never compiles. Opaque,
            // and the same colour as this frame, so it reads as the empty slot.
            PZCommon.ShaderPreviewPlaceholder {
                anchors.fill: parent
                anchors.margins: 1
                // Shown while the preview is still arriving, and kept up when a
                // stage failed to compile — that settles the chain, so `ready`
                // goes true with nothing worth showing underneath.
                visible: !chainPreview.ready || chainPreview.hasError
                text: chainPreview.hasError ? i18nc("@info:placeholder decoration preview", "This pack's shader did not compile.") : i18nc("@info:placeholder shader preview", "Preview unavailable")
                // Opaque, unlike the zone pane's: the preview underneath has to
                // keep RENDERING to reach `ready` at all, so this conceals it
                // rather than replacing it. Slot colour and rounding, so it
                // reads as an empty slot.
                backgroundColor: Kirigami.Theme.alternateBackgroundColor
                radius: Kirigami.Units.smallSpacing
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            CheckBox {
                id: focusToggle

                // Starts UNFOCUSED, matching the browser cards. That is the
                // state that shows what a focus-reactive pack does: focus-fade
                // is inert when focused, and the border family's inactive
                // colour never appears. Ticking it is the comparison.
                checked: false
                text: i18nc("@option:check decoration preview", "Focused")
            }

            Item {
                Layout.fillWidth: true
            }
        }

        // A needsBackdrop pack samples what is behind the window. The preview
        // shows it your wallpaper, which is the same stand-in a daemon surface
        // gets, so the note describes the approximation rather than claiming
        // there is no backdrop at all.
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root._needsBackdrop
            type: Kirigami.MessageType.Information
            text: i18nc("@info decoration preview limitation", "This pack samples whatever sits behind the window. The preview stands your wallpaper in for the real windows.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            // The PROPERTY, not the invokable of the same name: read as a call
            // this binding would have nothing to react to, and the notice would
            // stay as it was until the dialog was reopened.
            visible: root._isAudioPack && root.previewController !== null && !root.previewController.audioVisualizerEnabled
            type: Kirigami.MessageType.Information
            text: i18nc("@info decoration preview limitation", "This pack reacts to audio. Turn on the audio visualizer in Shaders settings to see it move.")
        }
    }
}
