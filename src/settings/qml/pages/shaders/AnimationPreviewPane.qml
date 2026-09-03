// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import PlasmaZones
import org.plasmazones.common as PZCommon

/**
 * @brief Live preview pane for an ANIMATION (transition) pack — every class.
 *
 * Third sibling of the zone/overlay pane (inline in
 * ShaderBrowserDetailDialog) and DecorationPreviewPane. The pack runs on an
 * AnimationShaderItem (the rendering library's ShaderEffect, the item every
 * production leg uses) configured through the production
 * applyEffectStaticConfig path by AnimationPreviewController. What the pane
 * stages around it depends on the pack's event class
 * (previewController.packInfo().eventClass):
 *
 *   appearance — the stand-in card, captured live into uTexture0, on a
 *     four-leg loop: open, minimize into a stand-in taskbar entry
 *     (iIconRect drives target-seeking packs), restore, close — each leg
 *     the exact iTime/isReversed drive SurfaceAnimator gives it.
 *   desktop — the pack's two endpoints stand in as composed scenes (a
 *     windows scene and the bare wallpaper, fed through the UBO branch's
 *     uFromDesktop/uToDesktop aliases); the clock sweeps iTime forward and
 *     back, which is literally the peek contract and reads as switching
 *     there and back on the switch event, with iSwitchDelta following.
 *   geometry — the card plus an old-content snapshot on a padded canvas
 *     riding the card (the kwin window-canvas framing); the clock replays
 *     a move from iFromRect to iToRect over the pack's vertex grid.
 *   move — a held drag: the card glides between the field's thirds on its
 *     padded canvas while the trail ring and the wobble spring lattice
 *     are simulated per frame.
 *   tab — the card as the arriving tab, the snapshot as the outgoing one,
 *     cross-faded forward on a loop.
 *   strip — the composed strip scene with a decaying scroll impulse driven
 *     into iStripMotion each frame; iTime free-runs (seconds), matching
 *     that pass's no-progress contract.
 */
Item {
    id: root

    /// AnimationPreviewController, handed down by the detail dialog.
    required property QtObject previewController
    /// The browsed pack's id.
    required property string packId
    /// Live (transient) friendly parameter map from the dialog's editor.
    property var liveParams: ({})
    /// Gated by the dialog: false while the pane is hidden. Gates the
    /// shader item's EXISTENCE; visibility only, never focus.
    property bool active: false
    /// Dropped-focus freeze: clocks stop, the preview holds its frame.
    property bool animating: true

    /// Invokable-call dependency tick — see DecorationPreviewPane._rev.
    readonly property int _rev: previewController ? previewController.previewRevision : 0

    /// Pack metadata, re-read when the pack changes or the registry revises.
    readonly property var _info: {
        void root._rev;
        return (previewController && packId.length > 0) ? (previewController.packInfo(packId) || ({})) : ({});
    }
    readonly property string _class: _info.eventClass || "appearance"
    readonly property bool _isAudioPack: _info.audio === true
    readonly property bool _previewable: _info.valid === true
    /// Whether the shader item rides the card as a PADDED CANVAS. On the
    /// kwin path the geometry / move classes run their grid over the
    /// window's own padded composite canvas, anchored to the window — NOT
    /// the whole output. Spanning the field here drew the sampled card
    /// stretched across the entire pane (wobble looked huge) and left the
    /// item static while the card glided (the drag read as nothing
    /// moving). The canvas mode mirrors kwin: item = card plus a margin
    /// ring for deflections and trail ghosts, moving with the card.
    readonly property bool _cardCanvas: _class === "geometry" || _class === "move"
    /// Whether the shader item spans the whole field: the screen-pass
    /// classes (one full-output quad on kwin), and appearance packs that
    /// declare fboExtent surface (fly-in's travel wants the whole
    /// stand-in surface, the daemon semantics).
    readonly property bool _fillsField: !_cardCanvas && (_info.fboExtentSurface === true || _class === "desktop" || _class === "strip")
    /// Whether a stand-in card is part of the subject at all.
    readonly property bool _hasCard: _class === "appearance" || _class === "geometry" || _class === "move" || _class === "tab"

    // Percent-encode a local file path for a file:// URL — same idiom as
    // ShaderBrowserDetailDialog._encodeFilePath.
    readonly property string _wallpaperUrl: {
        void root._rev;
        var p = previewController ? (previewController.wallpaperPath() || "") : "";
        return p.length > 0 ? "file://" + encodeURI(p).replace(/#/g, "%23").replace(/\?/g, "%3F") : "";
    }

    // Re-upload translated parameters as the dialog's editor moves them.
    onLiveParamsChanged: {
        if (previewLoader.item && previewLoader.item.configured)
            previewController.updatePreviewParams(previewLoader.item.shaderItem, packId, liveParams);
    }

    // A registry revision while the preview is open (a pack installed or
    // edited on disk) must rebuild the WHOLE subject, not just re-read
    // _info: the shader item's configuration is one-shot in its
    // Component.onCompleted, and a live item never rebakes an edited
    // source on its own. Bouncing the Loader tears the item down and
    // reconfigures from scratch — the fresh load keys the bake cache on
    // the edited files' new mtimes, so the preview shows the new sources.
    on_RevChanged: {
        if (previewLoader.active) {
            previewLoader.refreshHold = true;
            Qt.callLater(function () {
                previewLoader.refreshHold = false;
            });
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing
        spacing: Kirigami.Units.smallSpacing

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Kirigami.Units.smallSpacing
            color: Kirigami.Theme.alternateBackgroundColor
            border.width: 1
            border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
            clip: true

            // In a Loader so the capture + shader item only exist while the
            // dialog shows a previewable pack, and are torn down on close.
            Loader {
                id: previewLoader

                /// One-frame teardown pulse driven by root.on_RevChanged —
                /// folded into the `active` binding rather than written to
                /// `active` imperatively, which would sever the binding.
                property bool refreshHold: false

                anchors.fill: parent
                anchors.margins: 1
                active: root.active && root._previewable && !refreshHold
                visible: active

                sourceComponent: Item {
                    id: field

                    property alias shaderItem: shaderItem
                    property bool configured: false

                    /// The card's rect in field coordinates — the anchor the
                    /// spatial uniforms describe. For field-filling classes
                    /// with no card the field itself is the anchor.
                    readonly property rect cardRect: root._hasCard ? Qt.rect(cardHolder.x, cardHolder.y, cardHolder.width, cardHolder.height) : Qt.rect(0, 0, field.width, field.height)

                    /// True during the minimize/restore half of the
                    /// appearance loop — driven by the clock's
                    /// ScriptActions, never by a user control.
                    property bool iconPhase: false
                    /// Stand-in taskbar entry: a small rect at the
                    /// bottom-centre of the field, where a panel's task
                    /// entry would sit. Zero rect (the "no icon" contract
                    /// value) outside the minimize/restore legs, so the
                    /// open/close legs stay target-less.
                    readonly property rect iconRect: iconPhase ? Qt.rect(field.width / 2 - Kirigami.Units.gridUnit * 1.5, field.height - Kirigami.Units.gridUnit * 1.2, Kirigami.Units.gridUnit * 3, Kirigami.Units.gridUnit * 0.9) : Qt.rect(0, 0, 0, 0)

                    /// The margin ring around the card for the canvas
                    /// classes — the room wobble deflections and vortex
                    /// trail ghosts draw into, the preview's stand-in for
                    /// the kwin path's expanded-window composite canvas.
                    /// cardHolder IS the canvas: canvas-sized, card inset
                    /// by this on every side, captured whole so the anchor
                    /// sub-rect fold works exactly as kwin's padded capture.
                    /// Inner card width. The canvas classes take a smaller
                    /// card than the rest: their holder grows by the margin
                    /// ring, and the move glide needs travel room — the
                    /// shader item is a render node that does NOT respect
                    /// ancestor clipping, so everything it paints must
                    /// genuinely fit inside the field, not merely be
                    /// clipped at its edge.
                    readonly property real innerCardW: Math.min(field.width * (root._class === "move" ? 0.38 : (root._cardCanvas ? 0.5 : 0.6)), Kirigami.Units.gridUnit * (root._cardCanvas ? 16 : 22))
                    readonly property real canvasPad: root._cardCanvas ? Math.round(innerCardW * 0.3) : 0
                    /// The inner card's size (canvas minus the ring).
                    readonly property real innerW: cardHolder.width - 2 * canvasPad
                    readonly property real innerH: cardHolder.height - 2 * canvasPad

                    function syncGeometry() {
                        if (!configured)
                            return;
                        if (root._cardCanvas) {
                            // Canvas coords: the card sits at (pad, pad)
                            // inside its own captured canvas, and the
                            // canvas is the "surface" the spatial uniforms
                            // describe — the window-anchored framing the
                            // kwin grid gives these classes.
                            root.previewController.syncPreviewGeometry(shaderItem, true, field.canvasPad, field.canvasPad, field.innerW, field.innerH, cardHolder.width, cardHolder.height, true);
                            root.previewController.driveTransitionState(shaderItem, {
                                "fromRect": Qt.rect(field.canvasPad * 0.1, field.canvasPad * 0.1, field.innerW * 0.75, field.innerH * 0.75),
                                "toRect": Qt.rect(field.canvasPad, field.canvasPad, field.innerW, field.innerH)
                            });
                            return;
                        }
                        root.previewController.syncPreviewGeometry(shaderItem, root._fillsField, cardRect.x, cardRect.y, cardRect.width, cardRect.height, field.width, field.height);
                        // The per-class scalars that follow geometry rather
                        // than the clock.
                        if (root._class === "appearance")
                            root.previewController.driveTransitionState(shaderItem, {
                                "iconRect": field.iconRect
                            });
                        else if (root._class === "strip")
                            root.previewController.driveTransitionState(shaderItem, {
                                "stripRect": Qt.rect(0, 0, field.width, field.height),
                                "stripAxis": Qt.vector2d(1, 0)
                            });
                    }
                    onIconRectChanged: syncGeometry()
                    onWidthChanged: syncGeometry()
                    onHeightChanged: syncGeometry()

                    /// Hand the live card's Kirigami roles to the controller
                    /// (invalidates its composed-scene caches on change) and
                    /// upload the recomposed stand-in textures. Called from
                    /// the item's one-shot configure AND on a theme change,
                    /// so a light/dark switch while the dialog is open does
                    /// not leave the scene textures in the old theme.
                    function pushSceneTextures() {
                        root.previewController.setSceneColors(Kirigami.Theme.highlightColor, Kirigami.Theme.highlightedTextColor, Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.positiveTextColor);
                        root.previewController.bindClassTextures(shaderItem, root._class);
                    }
                    /// Change-detection key over every role the faux windows
                    /// paint with; a theme swap moves at least one of them.
                    readonly property string _themeKey: "" + Kirigami.Theme.highlightColor + Kirigami.Theme.highlightedTextColor + Kirigami.Theme.backgroundColor + Kirigami.Theme.textColor + Kirigami.Theme.positiveTextColor
                    on_ThemeKeyChanged: {
                        if (configured)
                            pushSceneTextures();
                    }

                    // The desktop the subject sits over. The screen-pass
                    // classes paint their own composed scenes through the
                    // shader, so the ground only shows for card classes.
                    Image {
                        anchors.fill: parent
                        source: root._wallpaperUrl
                        fillMode: Image.PreserveAspectCrop
                        sourceSize.width: Math.max(1, Math.round(width))
                        sourceSize.height: Math.max(1, Math.round(height))
                        asynchronous: true
                        visible: status === Image.Ready && root._hasCard
                    }

                    // The stand-in window, for the classes whose subject is a
                    // window. Captured below; hideSource keeps the raw card
                    // out of the normal render.
                    Item {
                        id: cardHolder

                        /// The INNER card width; for the canvas classes the
                        /// holder itself is canvas-sized (card plus the
                        /// margin ring on every side) with the card inset.
                        readonly property real _w: field.innerCardW
                        /// Move-class glide phase, -1..1, swept by the move
                        /// clock below. The card slides between the field's
                        /// left and right thirds; the simulation (trail +
                        /// wobble lattice) is driven off the resulting real
                        /// motion, exactly as a compositor drag would be.
                        property real glide: 0

                        visible: root._hasCard
                        // Glide amplitude from the actual free space, at
                        // 85% so InOutBack's turnaround overshoot (which
                        // peaks ~10% past the endpoints) still lands the
                        // whole CANVAS inside the field — the render node
                        // paints outside any QML clip, so the field edge is
                        // a hard wall, not a crop.
                        x: root._class === "move" ? Math.round((field.width - width) / 2 + glide * Math.max(0, (field.width - width) / 2) * 0.85) : Math.round((field.width - width) / 2)
                        y: Math.round((field.height - height) / 2)
                        width: Math.max(1, Math.round(_w + 2 * field.canvasPad))
                        height: Math.max(1, Math.round(_w * 14 / 22 + 2 * field.canvasPad))
                        onXChanged: field.syncGeometry()
                        onYChanged: field.syncGeometry()
                        onWidthChanged: field.syncGeometry()
                        onHeightChanged: field.syncGeometry()

                        PZCommon.DecorationPreviewCard {
                            anchors.fill: parent
                            // Inset by the margin ring for the canvas
                            // classes (zero everywhere else), so the
                            // captured texture is the card at (pad, pad)
                            // inside transparent room — the sub-rect the
                            // anchor fold describes.
                            anchors.margins: field.canvasPad
                            title: root._class === "tab" ? i18nc("@title arriving tab in the animation preview", "New Tab") : i18nc("@title sample window in the animation preview", "Sample Window")
                        }
                    }

                    // The stand-in taskbar entry the minimize collapses
                    // into. Drawn as real pixels (not just fed to the
                    // shader) so the funnel visibly lands ON something; a
                    // faint slot when the card is up, tinted while
                    // swallowed.
                    Rectangle {
                        visible: field.iconPhase && root._class === "appearance"
                        x: field.iconRect.x
                        y: field.iconRect.y
                        width: field.iconRect.width
                        height: field.iconRect.height
                        radius: Kirigami.Units.smallSpacing
                        color: Qt.alpha(Kirigami.Theme.highlightColor, 0.35)
                        border.width: 1
                        border.color: Kirigami.Theme.highlightColor
                    }

                    // Live capture of the card for uTexture0 — the same
                    // separate-pass FBO a real leg builds. Parked off-screen
                    // (a ShaderEffectSource is itself renderable and
                    // visible:false would starve the FBO). Only for card
                    // classes; the screen passes never sample uTexture0.
                    ShaderEffectSource {
                        id: capture

                        x: -1000000
                        y: -1000000
                        width: cardHolder.width
                        height: cardHolder.height
                        sourceItem: root._hasCard ? cardHolder : null
                        live: true
                        hideSource: root._hasCard
                    }

                    AnimationShaderItem {
                        id: shaderItem

                        // The canvas classes' holder is already canvas-sized
                        // (card + margin ring), so covering the holder IS
                        // the padded-canvas framing — no separate rect.
                        x: root._fillsField ? 0 : cardHolder.x
                        y: root._fillsField ? 0 : cardHolder.y
                        width: root._fillsField ? field.width : cardHolder.width
                        height: root._fillsField ? field.height : cardHolder.height
                        sourceItem: root._hasCard ? capture : null
                        audioSpectrum: root.previewController ? root.previewController.audioSpectrum : []
                        // The strip pass's iTime is seconds-since-active, not
                        // a progress sweep — let the item free-run it there.
                        playing: root._class === "strip" && root.animating
                        onWidthChanged: field.syncGeometry()
                        onHeightChanged: field.syncGeometry()

                        Component.onCompleted: {
                            field.configured = root.previewController.configurePreviewItem(shaderItem, root.packId, root.liveParams);
                            if (!field.configured)
                                return;
                            // The scene textures paint stand-in windows in
                            // the SAME Kirigami roles the live card uses —
                            // colours QPalette cannot supply (the accent
                            // has no palette role at all), so QML hands
                            // them over before the textures compose. AFTER
                            // configure: setShaderParams re-parses texture
                            // slots, so the stand-ins go on last.
                            field.pushSceneTextures();
                            if (root._class === "tab" || root._class === "geometry" || root._class === "move")
                                root.previewController.driveTransitionState(shaderItem, {
                                    "hasOldWindow": true,
                                    "oldWindowOpacity": 1.0
                                });
                            field.syncGeometry();
                        }
                    }

                    // ── Class clocks ────────────────────────────────────
                    // appearance: the full four-leg tour — open, close,
                    // then minimize into the stand-in taskbar entry and
                    // restore from it — with the per-leg iTime/isReversed
                    // drive a real transition gets (curves come from the
                    // user's motion profile; the preview plays a fixed
                    // neutral ease rather than blessing one profile). The
                    // icon target exists only for the minimize/restore
                    // half, so target-seeking packs funnel there and the
                    // open/close half stays target-less, and a pack that
                    // ignores the target simply plays its close twice.
                    SequentialAnimation {
                        running: field.configured && root.animating && root._class === "appearance"
                        loops: Animation.Infinite

                        // Open.
                        ScriptAction {
                            script: {
                                field.iconPhase = false;
                                shaderItem.isReversed = false;
                            }
                        }
                        NumberAnimation {
                            target: shaderItem
                            property: "iTime"
                            from: 0
                            to: 1
                            duration: 1200
                            easing.type: Easing.InOutQuad
                        }
                        PauseAnimation {
                            duration: 800
                        }
                        // Minimize into the taskbar entry.
                        ScriptAction {
                            script: {
                                field.iconPhase = true;
                                shaderItem.isReversed = true;
                            }
                        }
                        NumberAnimation {
                            target: shaderItem
                            property: "iTime"
                            from: 1
                            to: 0
                            duration: 1200
                            easing.type: Easing.InOutQuad
                        }
                        PauseAnimation {
                            duration: 700
                        }
                        // Restore from it.
                        ScriptAction {
                            script: shaderItem.isReversed = false
                        }
                        NumberAnimation {
                            target: shaderItem
                            property: "iTime"
                            from: 0
                            to: 1
                            duration: 1200
                            easing.type: Easing.InOutQuad
                        }
                        PauseAnimation {
                            duration: 700
                        }
                        // Close, target-less.
                        ScriptAction {
                            script: {
                                field.iconPhase = false;
                                shaderItem.isReversed = true;
                            }
                        }
                        NumberAnimation {
                            target: shaderItem
                            property: "iTime"
                            from: 1
                            to: 0
                            duration: 1200
                            easing.type: Easing.InOutQuad
                        }
                        PauseAnimation {
                            duration: 800
                        }
                    }

                    // desktop: there and back over the same endpoint pair —
                    // the peek contract verbatim (its show leg IS time run
                    // backwards), and a switch-and-return on the switch
                    // event. iSwitchDelta tracks the direction of travel so
                    // direction-following packs sweep the right way, and
                    // isReversed stays unbound like the real pass.
                    SequentialAnimation {
                        running: field.configured && root.animating && root._class === "desktop"
                        loops: Animation.Infinite

                        ScriptAction {
                            script: root.previewController.driveTransitionState(shaderItem, {
                                "switchDelta": Qt.vector4d(1, 0, 1, 0)
                            })
                        }
                        NumberAnimation {
                            target: shaderItem
                            property: "iTime"
                            from: 0
                            to: 1
                            duration: 1400
                            easing.type: Easing.InOutQuad
                        }
                        PauseAnimation {
                            duration: 900
                        }
                        ScriptAction {
                            script: root.previewController.driveTransitionState(shaderItem, {
                                "switchDelta": Qt.vector4d(-1, 0, -1, 0)
                            })
                        }
                        NumberAnimation {
                            target: shaderItem
                            property: "iTime"
                            from: 1
                            to: 0
                            duration: 1400
                            easing.type: Easing.InOutQuad
                        }
                        PauseAnimation {
                            duration: 900
                        }
                    }

                    // geometry / tab: one forward leg, hold, replay. A
                    // morph replays rather than reversing (windows do not
                    // un-move), and a tab switch always runs forward.
                    SequentialAnimation {
                        running: field.configured && root.animating && (root._class === "geometry" || root._class === "tab")
                        loops: Animation.Infinite

                        ScriptAction {
                            script: shaderItem.isReversed = false
                        }
                        NumberAnimation {
                            target: shaderItem
                            property: "iTime"
                            from: 0
                            to: 1
                            duration: 1200
                            easing.type: Easing.InOutQuad
                        }
                        PauseAnimation {
                            duration: 1100
                        }
                    }

                    // move: a held DRAG, not a leg — the card glides between
                    // the field's thirds and the per-frame simulation
                    // (driveMoveState: the 15 ms trail ring and the wobble
                    // spring lattice, the compositor's own integrator)
                    // follows the real motion, so vortex trails the glide
                    // and wobble jiggles through the turnarounds and settles
                    // in the rest pauses. iTime is held at the drag-start
                    // value a held transition paints at; the sim clock below
                    // is what advances.
                    SequentialAnimation {
                        running: field.configured && root.animating && root._class === "move"
                        loops: Animation.Infinite

                        ScriptAction {
                            // A held move paints at full leg progress; the
                            // motion character comes from the sim, not the
                            // clock, so pin iTime at the settled value.
                            script: shaderItem.iTime = 1
                        }
                        // InOutBack, not a gentle sine: the sharp
                        // acceleration into each leg and the overshoot at
                        // the turnarounds are what pump energy into the
                        // spring lattice — a smooth constant-ish glide
                        // barely excites it and wobble read as a rigid
                        // slide.
                        NumberAnimation {
                            target: cardHolder
                            property: "glide"
                            from: -1
                            to: 1
                            duration: 1100
                            easing.type: Easing.InOutBack
                        }
                        PauseAnimation {
                            duration: 650
                        }
                        NumberAnimation {
                            target: cardHolder
                            property: "glide"
                            from: 1
                            to: -1
                            duration: 1100
                            easing.type: Easing.InOutBack
                        }
                        PauseAnimation {
                            duration: 650
                        }
                    }
                    // The simulation tick. Runs through the pauses too —
                    // that is when the lattice visibly settles, which is
                    // half of what wobble IS. ~60 Hz like the zone pane's
                    // clock; the controller derives the trail cadence from
                    // the accumulated delta, not the tick rate.
                    Timer {
                        // Measured wall-clock delta, like the zone pane's
                        // clock: under load the timer fires late, and
                        // stepping the sim by the nominal interval would
                        // run the wobble/trail in slow motion relative to
                        // the on-screen glide. Clamped so a long stall (a
                        // suspended window) steps the spring stably.
                        property double lastTickMs: 0

                        running: field.configured && root.animating && root._class === "move"
                        interval: 16
                        repeat: true
                        onRunningChanged: lastTickMs = Date.now()
                        // The INNER card rect: the simulation models the
                        // window frame, not the padded canvas around it.
                        onTriggered: {
                            var now = Date.now();
                            var dt = Math.min(100, Math.max(1, now - lastTickMs));
                            lastTickMs = now;
                            root.previewController.driveMoveState(shaderItem, cardHolder.x + field.canvasPad, cardHolder.y + field.canvasPad, field.innerW, field.innerH, dt);
                        }
                    }

                    // strip: a scroll impulse decaying to rest, re-fired the
                    // other way — iStripMotion converging to zero is the
                    // identity contract every strip pack keys off. iTime
                    // free-runs via `playing` above.
                    property real stripPhase: 0
                    property real stripDir: 1
                    onStripPhaseChanged: {
                        if (root._class !== "strip" || !configured)
                            return;
                        // Offset decays exponentially from the impulse;
                        // velocity is its analytic derivative. Amplitude in
                        // field px, normalized lanes divided by the extent —
                        // the same lane pairing the manager pushes.
                        var A = field.width * 0.35 * stripDir;
                        var k = 5.0;
                        var off = A * Math.exp(-k * stripPhase);
                        var vel = -k * off;
                        root.previewController.driveTransitionState(shaderItem, {
                            "stripMotion": Qt.vector4d(off, vel, off / Math.max(field.width, 1), vel / Math.max(field.width, 1))
                        });
                    }
                    SequentialAnimation {
                        running: field.configured && root.animating && root._class === "strip"
                        loops: Animation.Infinite

                        NumberAnimation {
                            target: field
                            property: "stripPhase"
                            from: 0
                            to: 1.6
                            duration: 1600
                        }
                        ScriptAction {
                            script: field.stripDir = -field.stripDir
                        }
                    }
                }
            }

            // Covers the preview while the shader is still compiling, and
            // stays up when it failed. Opaque and slot-coloured — the item
            // underneath must keep rendering to compile.
            PZCommon.ShaderPreviewPlaceholder {
                anchors.fill: parent
                anchors.margins: 1
                visible: !root._previewable || !previewLoader.item || !previewLoader.item.configured || previewLoader.item.shaderItem.status === AnimationShaderItem.Error || previewLoader.item.shaderItem.status === AnimationShaderItem.Loading
                text: (previewLoader.item && previewLoader.item.configured && previewLoader.item.shaderItem.status === AnimationShaderItem.Error) ? i18nc("@info:placeholder animation preview", "This pack's shader did not compile.") : i18nc("@info:placeholder shader preview", "Preview unavailable")
                backgroundColor: Kirigami.Theme.alternateBackgroundColor
                radius: Kirigami.Units.smallSpacing
            }
        }

        // What the loop actually shows, per class — a pack can be assigned
        // to several events, and the preview stages exactly one subject.
        Label {
            Layout.fillWidth: true
            visible: root._previewable
            text: {
                switch (root._class) {
                case "desktop":
                    return i18nc("@info animation preview caption", "Previewing a desktop switch, there and back, on stand-in desktops.");
                case "geometry":
                    return i18nc("@info animation preview caption", "Previewing a window move on a sample window.");
                case "move":
                    return i18nc("@info animation preview caption", "Previewing a window being dragged back and forth.");
                case "tab":
                    return i18nc("@info animation preview caption", "Previewing a tab switch between two sample windows.");
                case "strip":
                    return i18nc("@info animation preview caption", "Previewing a scroll settling on a stand-in strip.");
                default:
                    return i18nc("@info animation preview caption", "Previewing open, minimize, restore and close on a sample window.");
                }
            }
            font: Kirigami.Theme.smallFont
            color: Kirigami.Theme.disabledTextColor
            wrapMode: Text.Wrap
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            // The PROPERTY, not the invokable of the same name — see
            // DecorationPreviewPane's twin notice.
            visible: root._isAudioPack && root.previewController !== null && !root.previewController.audioVisualizerEnabled
            type: Kirigami.MessageType.Information
            text: i18nc("@info animation preview limitation", "This pack reacts to audio. Turn on the audio visualizer in Shaders settings to see it move.")
        }
    }
}
