// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Service.Mpris 1.0
import Phosphor.Theme
import QtQuick
import QtQuick.Effects

// Media player capsule for the top panel. Shows album art (circular with
// progress ring), scrolling title, and prev/play/next controls. Left-click
// the capsule to open the MprisPopup detail panel; click the art circle
// to cycle players when multiple are running.
Item {
    // Middle-click = play/pause, scroll = volume.

    id: root

    property MprisPlayer currentPlayer: null
    readonly property bool hasPlayer: playerState.hasPlayer
    readonly property bool isPlaying: playerState.isPlaying
    readonly property real progress: playerState.progress
    readonly property alias stableArtUrl: playerState.stableArtUrl
    // Exposed so shell.qml can anchor the popup to us.
    property alias popupAnchor: artContainer

    // Named metrics keep magic-number sizing centralised. Mirrors the
    // pattern used by Theme tokens for colors: every consumer references
    // the named constant, so a future spacing-scale refactor can update
    // one site instead of grepping a dozen literals.
    readonly property int artDiameter: 26
    readonly property int playDiameter: 22
    readonly property int controlDiameter: 20
    readonly property int titleWidth: 120
    readonly property int titleScrollMargin: 10
    readonly property int capsuleSpacing: 6
    readonly property int controlSpacing: 2
    readonly property int controlGlyphSize: 9
    readonly property int fallbackGlyphSize: 10
    readonly property int titleFontSize: 11
    readonly property int progressStrokeWidth: 2
    readonly property int artSourcePixels: 80
    readonly property int controlRadius: 4
    readonly property int scrollPauseHeadMs: 2000
    readonly property int scrollPauseTailMs: 1500
    readonly property int scrollReturnMs: 400
    // Multiplier on titleText.implicitWidth driving the scroll-pass
    // duration; smaller = faster scroll. 25 ms/px yields a comfortable
    // reading pace for typical Catppuccin font sizes.
    readonly property int scrollSpeedMsPerPixel: 25

    signal popupRequested

    // ─── Player selection ────────────────────────────────────────────
    function selectPlayer() {
        let playing = null;
        let paused = null;
        for (let i = 0; i < mprisHost.playerCount; i++) {
            let p = mprisHost.playerAt(i);
            if (!p)
                continue;

            if (p.isPlaying) {
                playing = p;
                break;
            }
            if (p.playbackState === MprisPlayer.Paused && !paused)
                paused = p;
        }
        let next = playing || paused || (mprisHost.playerCount > 0 ? mprisHost.playerAt(0) : null);
        if (root.currentPlayer !== next)
            root.currentPlayer = next;
    }

    function cyclePlayer() {
        if (mprisHost.playerCount <= 1)
            return;

        let idx = 0;
        for (let i = 0; i < mprisHost.playerCount; i++) {
            if (mprisHost.playerAt(i) === currentPlayer) {
                idx = i;
                break;
            }
        }
        root.currentPlayer = mprisHost.playerAt((idx + 1) % mprisHost.playerCount);
    }

    visible: hasPlayer
    // The visibility-gated width branch matches the implicitHeight
    // discipline below but is also load-bearing for the parent Row's
    // layout: when this widget is hidden (no MPRIS players present),
    // Row reserves zero width so adjacent siblings collapse against
    // each other instead of stranding the widget's intrinsic slot.
    implicitWidth: visible ? capsule.implicitWidth : 0
    // Pin the widget's implicit height to its own content rather than
    // querying parent.height. The parent here is a Row, which sizes its
    // height from its child contents, so `parent.height` produced a
    // binding loop in the layout pass (parent height depends on this
    // widget's height depends on parent height) that QML resolved by
    // initialising at a 30 px sentinel and re-evaluating once layout
    // settled. Using capsule.implicitHeight breaks the loop cleanly:
    // the capsule sizes itself from its art-circle child, and the
    // widget inherits that.
    implicitHeight: capsule.implicitHeight
    Component.onCompleted: selectPlayer()

    // Shared MPRIS derived-state + flicker-free art URL (see
    // MprisPlayerState.qml). `sampling` is gated on visibility so the
    // QML `progress` binding goes dormant when the widget is hidden
    // (sampling=false short-circuits before `player.position` is read,
    // so the binding tracker drops it as a dependency). The C++
    // position timer keeps firing regardless; this just saves the
    // per-tick Math.min/Math.max in MprisPlayerState's `progress`
    // binding. The Canvas `requestPaint` scheduling cost is already
    // gated separately by `prog`'s `root.visible` short-circuit and
    // the `if (root.visible) requestPaint()` guard.
    MprisPlayerState {
        id: playerState

        player: root.currentPlayer
        sampling: root.visible
    }

    MprisHost {
        id: mprisHost
    }

    MprisPlayerModel {
        id: playerModel

        host: mprisHost
    }

    Connections {
        // playerCountChanged fires once per add and once per remove, so
        // a single handler covers both directions; the previous three-
        // handler form (onPlayerAdded / onPlayerRemoved /
        // onPlayerCountChanged) re-ran selectPlayer twice per change.
        function onPlayerCountChanged() {
            root.selectPlayer();
        }

        target: mprisHost
    }

    Connections {
        function onPlaybackStateChanged() {
            root.selectPlayer();
        }

        target: root.currentPlayer
        enabled: root.currentPlayer !== null
    }

    // ─── Capsule layout ──────────────────────────────────────────────
    Row {
        id: capsule

        anchors.verticalCenter: parent.verticalCenter
        spacing: root.capsuleSpacing

        // Album art with progress ring
        Item {
            id: artContainer

            width: root.artDiameter
            height: root.artDiameter
            anchors.verticalCenter: parent.verticalCenter

            // Background fill (visible while art loads / when no art).
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: Theme.surface_container
            }

            // Fallback glyph (sits underneath the masked art).
            Text {
                anchors.centerIn: parent
                text: "♪"
                color: Theme.on_surface_variant
                font.pixelSize: root.fallbackGlyphSize
                visible: !artImage.visible
            }

            // Circular mask source for MultiEffect. Lives off-screen
            // (visible: false + hideSource: true). White circle on
            // transparent background: only the circle composites
            // through to the visible scene.
            Item {
                id: artMaskShape

                width: artContainer.width
                height: artContainer.height
                visible: false
                layer.enabled: true
                layer.smooth: true

                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "white"
                }
            }

            // Album art masked to the circle via MultiEffect.maskSource
            // (QtQuick.Effects, Qt 6.5+). Qt6 has no built-in "rounded
            // clip": Rectangle.radius is paint-only, clip: true is
            // bounding-box only. MultiEffect is the canonical mask path.
            Image {
                id: artImage

                anchors.fill: parent
                source: root.stableArtUrl
                fillMode: Image.PreserveAspectCrop
                sourceSize: Qt.size(root.artSourcePixels, root.artSourcePixels)
                asynchronous: true
                cache: true
                visible: status === Image.Ready || (source !== "" && status === Image.Loading)
                layer.enabled: true
                layer.smooth: true

                layer.effect: MultiEffect {
                    maskEnabled: true
                    maskSource: artMaskShape
                    maskThresholdMin: 0.5
                    maskSpreadAtMin: 1
                }
            }

            // Progress ring drawn ON TOP of the art so the outer ring
            // stroke overlays the artwork edge.
            Canvas {
                id: progressRing

                // Only sample progress while the widget is visible and we
                // have a player. When there's no player or the widget is
                // hidden (occluded/no-player branch), keep `prog` at 0
                // and skip the per-second requestPaint cycle that would
                // otherwise re-rasterize the ring once per MPRIS position
                // tick regardless of visibility.
                //
                // The `root.visible && root.hasPlayer` guard here is
                // layered with MprisPlayerState's own sampling check
                // (which produces 0 when sampling=false=!root.visible)
                // and player-null check (root.progress already returns 0
                // when !hasPlayer). Both layers are intentional:
                // simplifying to `root.progress` would lose the local
                // guarantee that `prog` reads 0 during the brief layout
                // pass where `root.visible` transitions but
                // playerState.sampling has not yet propagated.
                property real prog: (root.visible && root.hasPlayer) ? root.progress : 0

                anchors.fill: parent
                z: 1
                onProgChanged: {
                    if (root.visible)
                        requestPaint();
                }
                // Force a clean repaint on the hide -> show transition.
                // The onProgChanged guard above suppresses repaints while
                // the widget is hidden, which left the ring showing the
                // last non-zero progress after a screen-lock cycle.
                onVisibleChanged: {
                    if (visible)
                        requestPaint();
                }

                // QML's auto-binding tracker doesn't see Theme.X reads
                // inside Canvas.onPaint (paint callbacks aren't a
                // bindable scope), so a runtime palette swap leaves the
                // ring stuck on the old `Theme.outline` / `Theme.primary`
                // colors until the next progress tick. Listen on the
                // palette directly and force a repaint when it changes.
                // Gated on root.visible to match the sister handlers
                // above: a wallpaper-driven palette swap shouldn't burn
                // a paint while the widget is hidden, and the
                // `onVisibleChanged: if (visible) requestPaint()` hook
                // already covers the hidden -> visible transition.
                Connections {
                    target: Theme.paletteStore
                    function onPaletteChanged() {
                        if (root.visible)
                            progressRing.requestPaint();
                    }
                }
                onPaint: {
                    let ctx = getContext("2d");
                    let cx = width / 2, cy = height / 2;
                    let r = Math.min(width, height) / 2 - 1;
                    ctx.reset();
                    ctx.beginPath();
                    ctx.arc(cx, cy, r, 0, 2 * Math.PI);
                    ctx.lineWidth = root.progressStrokeWidth;
                    ctx.strokeStyle = Qt.rgba(Theme.outline.r, Theme.outline.g, Theme.outline.b, 0.25);
                    ctx.stroke();
                    if (prog > 0) {
                        ctx.beginPath();
                        ctx.arc(cx, cy, r, -Math.PI / 2, -Math.PI / 2 + prog * 2 * Math.PI);
                        ctx.lineWidth = root.progressStrokeWidth;
                        ctx.strokeStyle = Theme.primary;
                        ctx.lineCap = "round";
                        ctx.stroke();
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Media player controls")
                onClicked: mouse => {
                    if (mouse.button === Qt.RightButton)
                        root.cyclePlayer();
                    else
                        root.popupRequested();
                }
            }
        }

        // Scrolling title; left-click opens popup
        Item {
            // Pin to artContainer.height rather than parent.height: the
            // capsule Row's height is content-derived, so referencing
            // parent.height converges only because artContainer fixes
            // the minimum at 26 px. A future change that drops the
            // fixed-size art child would reintroduce the binding-loop
            // hazard documented at the widget root.
            width: root.titleWidth
            height: artContainer.height
            anchors.verticalCenter: parent.verticalCenter
            clip: true

            MouseArea {
                anchors.fill: parent
                // hoverEnabled is required for cursorShape to take
                // effect: without it Qt only sets the cursor on
                // press, not on hover. The pointer-hand cue is the
                // primary affordance signaling that the title strip
                // opens the media popup.
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.popupRequested()
            }

            Text {
                id: titleText

                property bool needsScroll: implicitWidth > root.titleWidth

                y: (parent.height - height) / 2
                text: {
                    if (!root.hasPlayer)
                        return "";

                    let parts = [];
                    if (root.currentPlayer.trackArtist)
                        parts.push(root.currentPlayer.trackArtist);

                    if (root.currentPlayer.trackTitle)
                        parts.push(root.currentPlayer.trackTitle);

                    return parts.join(" · ") || root.currentPlayer.identity || "";
                }
                color: Theme.on_surface
                font.pixelSize: root.titleFontSize
                // Reset the scroll position whenever the text changes.
                // Without this, switching tracks mid-animation left the
                // previous track's NumberAnimation owning `x` until the
                // next animation cycle: the new track would scroll using
                // the old track's `to:` value for a cycle, producing a
                // visible offscreen jump on multi-track playback.
                onTextChanged: x = 0

                SequentialAnimation on x {
                    running: titleText.needsScroll && root.visible
                    loops: Animation.Infinite

                    PauseAnimation {
                        duration: root.scrollPauseHeadMs
                    }

                    NumberAnimation {
                        from: 0
                        to: -(titleText.implicitWidth - (root.titleWidth - root.titleScrollMargin))
                        duration: titleText.implicitWidth * root.scrollSpeedMsPerPixel
                        easing.type: Easing.Linear
                    }

                    PauseAnimation {
                        duration: root.scrollPauseTailMs
                    }

                    NumberAnimation {
                        from: -(titleText.implicitWidth - (root.titleWidth - root.titleScrollMargin))
                        to: 0
                        duration: root.scrollReturnMs
                        easing.type: Easing.OutQuad
                    }
                }
            }
        }

        // Controls
        Row {
            spacing: root.controlSpacing
            anchors.verticalCenter: parent.verticalCenter

            Rectangle {
                width: root.controlDiameter
                height: root.controlDiameter
                radius: root.controlRadius
                color: prevArea.containsMouse ? Theme.surface_container_high : "transparent"
                visible: root.hasPlayer && root.currentPlayer.canGoPrevious

                Text {
                    anchors.centerIn: parent
                    text: "⏮"
                    font.pixelSize: root.controlGlyphSize
                    color: Theme.on_surface
                }

                MouseArea {
                    id: prevArea

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Previous track")
                    onClicked: {
                        if (root.hasPlayer)
                            root.currentPlayer.previous();
                    }
                }
            }

            Rectangle {
                width: root.playDiameter
                height: root.playDiameter
                radius: width / 2
                color: playArea.containsMouse ? Theme.surface_container_high : Theme.surface_container

                Text {
                    anchors.centerIn: parent
                    text: root.isPlaying ? "⏸" : "▶"
                    font.pixelSize: root.controlGlyphSize
                    color: Theme.on_surface
                }

                MouseArea {
                    id: playArea

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: root.isPlaying ? qsTr("Pause") : qsTr("Play")
                    onClicked: {
                        if (root.hasPlayer)
                            root.currentPlayer.togglePlaying();
                    }
                }
            }

            Rectangle {
                width: root.controlDiameter
                height: root.controlDiameter
                radius: root.controlRadius
                color: nextArea.containsMouse ? Theme.surface_container_high : "transparent"
                visible: root.hasPlayer && root.currentPlayer.canGoNext

                Text {
                    anchors.centerIn: parent
                    text: "⏭"
                    font.pixelSize: root.controlGlyphSize
                    color: Theme.on_surface
                }

                MouseArea {
                    id: nextArea

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Next track")
                    onClicked: {
                        if (root.hasPlayer)
                            root.currentPlayer.next();
                    }
                }
            }
        }
    }

    // This MouseArea is anchors.fill: capsule and is declared AFTER
    // the per-element MouseAreas inside the Row, which puts it on top
    // of them in stacking order. Even though acceptedButtons filters
    // it to MiddleButton for click events, Qt's cursor-shape lookup
    // walks the topmost MouseArea regardless of acceptedButtons:
    // so without an explicit cursorShape here this MouseArea's
    // default Qt.ArrowCursor would override the PointingHandCursor
    // set on the title-strip MouseArea below it (the art-container
    // MouseArea wins because it lives INSIDE artContainer, a deeper
    // child, but the title strip's MouseArea is at the same level
    // as this overlay and loses the stack). Set the shape here so
    // the entire capsule reads as clickable.
    MouseArea {
        anchors.fill: capsule
        acceptedButtons: Qt.MiddleButton
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        propagateComposedEvents: true
        onClicked: {
            if (root.hasPlayer)
                root.currentPlayer.togglePlaying();
        }
        onWheel: wheel => {
            if (!root.hasPlayer)
                return;

            let delta = wheel.angleDelta.y > 0 ? 0.05 : -0.05;
            root.currentPlayer.volume = Math.max(0, Math.min(1, root.currentPlayer.volume + delta));
        }
    }
}
