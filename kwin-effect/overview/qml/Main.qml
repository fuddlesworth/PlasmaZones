// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The overview's per-screen root. One instance per output, created by
// QuickSceneEffect with `screenId` (the daemon's canonical id for this
// output) as an initial property. Everything on screen derives from two
// daemon payloads the effect exposes: the workspace map (which workspaces
// this screen owns, in slice order) and the overview model (where each
// workspace's windows are). The QML never moves a window itself; every
// mutation is a verb to the daemon and the next model repositions it.
//
// i18n: this file runs inside KWin's QML engine, whose default catalogue is
// kwin, so every string goes through i18nd("plasmazones", ...) here.

import QtQuick
import org.kde.kwin as KWinComponents

FocusScope {
    id: root
    focus: true

    required property string screenId

    readonly property QtObject effect: KWinComponents.SceneView.effect
    readonly property QtObject targetScreen: KWinComponents.SceneView.screen
    readonly property QtObject currentDesktop: KWinComponents.SceneView.currentDesktop

    // Gate the progress binding until the component is complete so the open
    // animation always runs from 0, even when the effect starts at factor 1
    // (a shortcut toggle).
    property bool organized: false

    // The animated open progress. The effect's partialActivationFactor is
    // the raw gesture value during a swipe and a 0/1 step on toggle; this
    // value follows it directly while a gesture is in progress and animates
    // toward it otherwise. A toggle mid-flight retargets the animation.
    property real progress: 0
    Behavior on progress {
        enabled: root.organized && !root.effect.gestureInProgress
        NumberAnimation {
            duration: root.effect.animationDuration
            easing.type: Easing.OutCubic
        }
    }
    Binding {
        target: root
        property: "progress"
        value: root.organized ? root.effect.partialActivationFactor : 0
    }

    // niri: zoom = 1 - p * (1 - zoomSetting).
    readonly property real zoom: 1 - root.progress * (1 - root.effect.zoom)
    readonly property real outputScale: root.targetScreen ? root.targetScreen.scale : 1

    // This screen's slice of the workspace map, in slice order.
    readonly property var slice: {
        const slices = root.effect.workspaceMap.slices;
        return (slices && slices[root.screenId]) ? slices[root.screenId] : [];
    }
    // Whether the map knows this screen at all (a known screen with no
    // workspace yet renders one placeholder cell).
    readonly property bool screenKnown: {
        const order = root.effect.workspaceMap.screenOrder;
        return Array.isArray(order) && order.indexOf(root.screenId) !== -1;
    }
    // This screen's overview model: {logicalSize, workspaces: {desktopId: {...}}}.
    readonly property var screenModel: {
        const screens = root.effect.overviewModel.screens;
        return (screens && screens[root.screenId]) ? screens[root.screenId] : null;
    }
    // Slice index of the workspace this output currently shows, resolved by
    // live desktop number (never by list position). The map's `current`
    // flag is not read: the compositor is the authority on what is shown.
    readonly property int currentIndex: {
        const number = root.currentDesktop ? root.currentDesktop.x11DesktopNumber : 0;
        for (let i = 0; i < root.slice.length; ++i) {
            if (root.slice[i].index === number) {
                return i;
            }
        }
        return 0;
    }

    // Round a logical coordinate to the output's physical pixel grid AFTER
    // zoom (niri #1467: rounding before zoom leaves 1px seams).
    function snap(value) {
        return Math.round(value * root.outputScale) / root.outputScale;
    }

    // The view texture has no alpha: paint the backdrop over the whole
    // output first.
    Rectangle {
        anchors.fill: parent
        color: root.effect.backdropColor
    }

    // One wallpaper per SCREEN, dimmed as the overview opens. Plasma's
    // desktop window is on every desktop of an output, so a per-cell
    // wallpaper would be N identical full-resolution renders. outputName is
    // the connector name, the one id that is NOT the canonical screen id.
    KWinComponents.DesktopBackground {
        id: wallpaper
        anchors.fill: parent
        outputName: root.targetScreen ? root.targetScreen.name : ""
        activity: KWinComponents.Workspace.currentActivity
        desktop: root.currentDesktop
        opacity: 1 - 0.55 * root.progress
    }

    WorkspaceColumn {
        id: column
        anchors.fill: parent
        root: root
    }

    DockLayer {
        anchors.fill: parent
        root: root
        opacity: 1 - root.progress
        visible: opacity > 0
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape) {
            root.effect.deactivate();
            event.accepted = true;
        }
    }

    Component.onCompleted: {
        root.organized = true;
    }
}
