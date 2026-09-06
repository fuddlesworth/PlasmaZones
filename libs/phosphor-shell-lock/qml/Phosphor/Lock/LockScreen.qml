// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Lock.LockScreen, one output's lock content (A3 §6).
//
// The layout you left, as an outline. On a void ground with the wallpaper
// desaturated and darkened beneath, every window of this screen's
// placement map is drawn at 1:1 as a static spectrum outline (LockLayout).
// The clock, the date eyebrow and the auth field sit in the LARGEST EMPTY
// REGION of that map (LockRegion.js), left-aligned to it, or centred on
// the screen when the map has no room. Bottom-left, the battery figure.
//
// Keyboard: typing goes to the field without clicking it. The item takes
// focus, and every key goes to the shared LockController, so whichever
// output the compositor gave keyboard focus feeds the one password.
//
// Choreography: on lock the outlines are already there (the compositor
// drew the release); the clock enters at 800 ms. On unlock the outlines
// fill 8 % to 100 % and the content dismisses over Motion.duration_dismiss.
// Nothing slides or scales.

import QtQuick
import QtQuick.Effects
import Phosphor.Theme
import Phosphor.Widgets
import "LockRegion.js" as Region

FocusScope {
    id: root

    // This output's PlacementMapScreen (or a fake with cells / workArea /
    // changed()).
    property var map: null
    // The shared LockController.
    property var controller: null
    // Absolute path of the wallpaper image, or "" for the plain void.
    property string wallpaperPath: ""
    // A UPowerHost (or anything with `displayDevice.percentage` and
    // `displayDevice.isPresent`), or null to show no battery figure.
    property var battery: null
    // The primary output announces the field to accessibility; the others
    // mirror it.
    property bool isPrimary: true

    // The region the content block was placed in, in this item's pixels;
    // width 0 when the block fell back to the screen centre.
    readonly property rect contentRegion: priv.region
    readonly property bool contentCentred: priv.centred
    readonly property alias outlineCount: layout.outlineCount
    readonly property alias clockText: clock.text
    readonly property alias blockX: block.x
    readonly property alias blockY: block.y

    // Void `#050916` (A3 consistency table, the full-screen ground; the
    // lock is the one surface that takes it at 100 %).
    readonly property color voidColor: "#050916"

    // The ink is pinned for the same reason the ground is. The lock surface
    // is a fixed dark field, so reading the ink from the palette would put
    // a light palette's dark `on_surface` on top of this near-black ground
    // and leave the clock unreadable. These are the dark palette's own
    // on_surface / on_surface_variant, so the usual appearance is unchanged.
    readonly property color inkColor: "#E6EDFF"
    readonly property color inkMutedColor: "#94A3B8"

    // Room the content block needs: the field's width plus the inset on
    // both sides, and the clock, date and field stacked, plus the inset.
    readonly property int blockInset: Tokens.spacing_xl
    readonly property int minRegionWidth: field.implicitWidth + 2 * root.blockInset
    readonly property int minRegionHeight: block.implicitHeight + 2 * root.blockInset

    property date now: new Date()

    focus: true
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Locked")

    Keys.onPressed: event => {
        if (root.controller && root.controller.handleKey(event))
            event.accepted = true;
    }

    QtObject {
        id: priv

        property rect region: Qt.rect(0, 0, 0, 0)
        property bool centred: true

        // The map's cells in this item's pixels, occupied ones only, then
        // the largest gap between them.
        function recompute(): void {
            if (!(root.width > 0) || !(root.height > 0))
                return;
            const wa = layout.workArea;
            const cells = layout.windows;
            const px = [];
            for (let i = 0; i < cells.length; ++i) {
                const c = cells[i];
                px.push({
                    "x": wa.x + Number(c.x) * wa.width,
                    "y": wa.y + Number(c.y) * wa.height,
                    "w": Number(c.w) * wa.width,
                    "h": Number(c.h) * wa.height
                });
            }
            const r = Region.largestEmptyRect(px, root.width, root.height);
            if (r && r.width >= root.minRegionWidth && r.height >= root.minRegionHeight) {
                priv.region = Qt.rect(r.x, r.y, r.width, r.height);
                priv.centred = false;
            } else {
                priv.region = Qt.rect(0, 0, 0, 0);
                priv.centred = true;
            }
        }
    }

    onWidthChanged: priv.recompute()
    onHeightChanged: priv.recompute()
    onMapChanged: priv.recompute()
    Component.onCompleted: {
        priv.recompute();
        root._sync();
    }

    Connections {
        target: root.map
        ignoreUnknownSignals: true
        function onChanged(): void {
            priv.recompute();
        }
        function onCellsChanged(): void {
            priv.recompute();
        }
    }

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: root.now = new Date()
    }

    Rectangle {
        anchors.fill: parent
        color: root.voidColor
    }

    // The wallpaper at 12 % saturation and 20 % brightness (A3 §6 b):
    // desaturated through MultiEffect, darkened by sitting at 20 % over
    // the void. No blur.
    Image {
        id: wallpaper

        anchors.fill: parent
        // encodeURI alone leaves `#` and `?`, which become a fragment and a
        // query: the wallpaper then silently fails to load on the lock screen.
        source: root.wallpaperPath.length > 0 ? "file://" + encodeURI(root.wallpaperPath).replace(/#/g, "%23").replace(/\?/g, "%3F") : ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: false
        visible: false
    }

    MultiEffect {
        anchors.fill: parent
        source: wallpaper
        visible: wallpaper.status === Image.Ready
        saturation: -0.88
        opacity: 0.2
    }

    LockLayout {
        id: layout

        anchors.fill: parent
        map: root.map
        fill: root._fill
        onCellClicked: (id, name) => {
            if (root.controller)
                root.controller.placeholderFor(name);
        }
        onWindowsChanged: priv.recompute()
    }

    // The surface pack on the clock block (A1 §2.4, `shell.phosphor.lock`:
    // the motes by default), set by the composition root.
    property Component decoration: null

    DecorationSlot {
        anchors.fill: parent
        component: root.decoration
        contentItem: block
        surfacePath: "shell.phosphor.lock"
    }

    // The content block: clock, date, field, left-aligned to the region.
    Column {
        id: block

        // The pack's capture item.
        property bool shaderAnchor: true

        readonly property real inset: root.blockInset

        x: priv.centred ? Math.round((root.width - block.width) / 2) : Math.round(priv.region.x + block.inset)
        y: priv.centred ? Math.round((root.height - block.height) / 2) : Math.round(priv.region.y + (priv.region.height - block.height) / 2)
        width: field.implicitWidth
        spacing: 0
        opacity: 0

        // 96 px numeric-tabular display type, the minute underline ticking
        // under the changed digit (05 §type).
        TabularText {
            id: clock

            text: Qt.formatTime(root.now, "HH:mm")
            color: root.inkColor
            font.family: Tokens.font_family_ui
            font.pixelSize: 96
            font.weight: Font.ExtraLight
            font.letterSpacing: -2
            tickOnChange: true
            t: Spectrum.tForX(block.x, root.width)
        }

        // The date eyebrow: 16 px uppercase, +1.5 px tracking.
        Text {
            id: date

            text: Qt.formatDate(root.now, "dddd d MMMM").toUpperCase()
            color: root.inkMutedColor
            font.family: Tokens.font_family_ui
            font.pixelSize: Tokens.font_size_title_m
            font.letterSpacing: 1.5
            topPadding: Tokens.spacing_s
        }

        Item {
            width: 1
            height: Tokens.spacing_xl
        }

        LockAuthField {
            id: field

            controller: root.controller
            // The field is one, announced once.
            Accessible.ignored: !root.isPrimary
        }
    }

    // Bottom-left eyebrow: the battery figure, 13 px numeric-tabular.
    TabularText {
        id: eyebrow

        readonly property var device: root.battery ? root.battery.displayDevice : null
        readonly property bool hasBattery: !!device && !!device.isPresent && Number.isFinite(device.percentage)

        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: Tokens.spacing_xl
        visible: hasBattery
        text: hasBattery ? Math.round(device.percentage) + "%" : ""
        color: root.inkMutedColor
        font.pixelSize: Tokens.font_size_body_m
        opacity: block.opacity
    }

    // Enter: the outlines are up at once; the clock at 800 ms. Dismiss:
    // the outlines fill in and the content goes.
    QtObject {
        id: motion

        readonly property bool showing: root.controller ? root.controller.locked && !root.controller.dismissing : true
        readonly property bool dismissing: root.controller ? root.controller.dismissing : false
    }

    // The outline fill: 0.08 while locked, driven to 1 by the dismiss.
    property real _fill: 0.08

    SequentialAnimation {
        id: enter

        PauseAnimation {
            duration: Motion.reducedMotion ? 0 : Motion.duration_extra_long_2
        }
        NumberAnimation {
            target: block
            property: "opacity"
            to: 1
            duration: Motion.duration_enter_content
            easing: Motion.reveal
        }
    }

    ParallelAnimation {
        id: dismiss

        NumberAnimation {
            target: root
            property: "_fill"
            to: 1
            duration: Motion.duration_release
            easing: Motion.release
        }
        NumberAnimation {
            target: block
            property: "opacity"
            to: 0
            duration: Motion.duration_dismiss
            easing: Motion.dismiss
        }
    }

    function _sync(): void {
        if (motion.dismissing) {
            enter.stop();
            dismiss.restart();
        } else if (motion.showing) {
            dismiss.stop();
            root._fill = 0.08;
            enter.restart();
        }
    }

    Connections {
        target: motion
        function onShowingChanged(): void {
            root._sync();
        }
        function onDismissingChanged(): void {
            root._sync();
        }
    }
}
