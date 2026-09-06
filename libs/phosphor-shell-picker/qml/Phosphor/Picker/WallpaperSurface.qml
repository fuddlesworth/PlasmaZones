// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Picker.WallpaperSurface, one output's wallpaper.
//
// The shell owns the wallpaper: a Background-layer surface per output
// that draws the image WallpaperService resolves for that output, so a
// hovered picker candidate paints under the windows and an Apply lands
// on the same surface. It lives in Phosphor.Picker because the picker is
// the only thing that previews it: the lib that changes the wallpaper
// owns the surface that shows it.
//
// A full-screen Background panel: Top edge, Fill alignment, thickness the
// screen's height (the same recipe the OSD overlay uses to reach every
// edge), exclusive zone 0 so panels and windows sit over it, no keyboard
// and an EMPTY input region, so every click goes through to the desktop.
//
// Two Image slots crossfade rather than hard-cut: the incoming path loads
// into the back slot, and only once it is Ready do the slots swap, the
// new front revealing over the old one releasing underneath. A path that
// fails to load is dropped and the front stays.
//
//   PerScreenPanels {
//       model: PhosphorShell.screens
//       delegate: WallpaperSurface { service: PhosphorShell.wallpaper }
//   }

import QtQuick
import Phosphor.Shell
import Phosphor.Theme

PanelWindow {
    id: surface

    // The wallpaper service (PhosphorShell.wallpaper in the shell, a fake
    // in tests): `effectivePath(screenName)` plus the
    // `effectivePathChanged(screenName)` signal, where "" means every
    // screen. Null draws the ground alone.
    property var service: null
    // Through PanelWindow.screen, which reads null once the output dies,
    // rather than a snapshot of the row.
    readonly property string screenName: surface.screen ? surface.screen.name : ""
    // The path the front slot shows, and the one loading behind it.
    readonly property string shownPath: priv.frontPath
    readonly property string pendingPath: priv.pendingPath

    edge: PanelWindow.Top
    alignment: PanelWindow.Fill
    // The screen's height, read at materialization like every panel
    // geometry. The fallback is only for an unmounted surface.
    thickness: surface.screen ? surface.screen.geometry.height : 1
    panelLayer: PanelWindow.LayerBackground
    exclusiveZoneEnabled: false
    keyboardFocus: PanelWindow.None
    inputRegion: []

    function sync(): void {
        priv.show(surface.service ? String(surface.service.effectivePath(surface.screenName)) : "");
    }

    onServiceChanged: sync()
    onScreenNameChanged: sync()
    Component.onCompleted: sync()

    Connections {
        target: surface.service

        function onEffectivePathChanged(screenName: string): void {
            if (screenName === "" || screenName === surface.screenName)
                surface.sync();
        }
    }

    QtObject {
        id: priv

        property Image front: slotA
        property Image back: slotB
        property string frontPath: ""
        property string pendingPath: ""

        // Wallpaper paths are filenames the user chose, so they routinely
        // carry spaces and occasionally `#` or `?`. Bare concatenation both
        // breaks the load and breaks `settle()`, which compares this against
        // the Image's own already-normalised `source` and would never match.
        function urlFor(path: string): string {
            return path === "" ? "" : "file://" + encodeURI(path).replace(/#/g, "%23").replace(/\?/g, "%3F");
        }

        // Ask for `path`. Same as the front: forget anything pending, so a
        // hover that left before its image loaded never swaps in late. The
        // back slot is cleared only when it was loading that request; with
        // nothing pending it holds the image still releasing underneath.
        function show(path: string): void {
            if (path === frontPath) {
                if (pendingPath !== "") {
                    pendingPath = "";
                    back.source = "";
                }
                return;
            }
            pendingPath = path;
            if (path === "") {
                back.source = "";
                swap();
                return;
            }
            const url = urlFor(path);
            if (String(back.source) === url && back.status === Image.Ready) {
                swap();
                return;
            }
            back.source = url;
        }

        // The back slot finished loading: swap when it is the image still
        // wanted, drop the request when it failed.
        function settle(slot: Image): void {
            if (slot !== back || pendingPath === "" || String(slot.source) !== urlFor(pendingPath))
                return;
            if (slot.status === Image.Ready) {
                swap();
            } else if (slot.status === Image.Error) {
                console.warn("WallpaperSurface: cannot load", pendingPath);
                pendingPath = "";
                slot.source = "";
            }
        }

        function swap(): void {
            frontPath = pendingPath;
            pendingPath = "";
            const previous = front;
            front = back;
            back = previous;
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.background
    }

    Image {
        id: slotA

        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        smooth: true
        // The incoming image on top, so the crossfade reads as the new
        // wallpaper revealing over the old.
        z: priv.front === slotA ? 1 : 0
        opacity: priv.front === slotA ? 1 : 0
        visible: opacity > 0
        onStatusChanged: priv.settle(slotA)

        Behavior on opacity {
            NumberAnimation {
                duration: Motion.reducedMotion ? 0 : Motion.duration_reveal
                easing: priv.front === slotA ? Motion.reveal : Motion.release
            }
        }
    }

    Image {
        id: slotB

        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        smooth: true
        // The incoming image on top, so the crossfade reads as the new
        // wallpaper revealing over the old.
        z: priv.front === slotB ? 1 : 0
        opacity: priv.front === slotB ? 1 : 0
        visible: opacity > 0
        onStatusChanged: priv.settle(slotB)

        Behavior on opacity {
            NumberAnimation {
                duration: Motion.reducedMotion ? 0 : Motion.duration_reveal
                easing: priv.front === slotB ? Motion.reveal : Motion.release
            }
        }
    }
}
