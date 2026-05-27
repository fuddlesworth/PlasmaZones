// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Popout.PopoutHost, transport-agnostic wrapper for a popout's
// content. The transport (in-window for the demo, layer-shell for the
// shell) instantiates this host wrapping the content delegate. The
// host owns the open/close animation, click-outside dismiss detection,
// and content sizing.
// Designed to be parented into either a regular Item (in-window
// transports) or a layer-shell wrapper Item. The host does not assume
// a window of its own.

import Phosphor.Theme
import QtQuick

Item {
    id: root

    // The popout's content. Either a pre-built Item (which the host
    // reparents) or a Component the host instantiates inline. One of
    // the two must be set.
    property Component contentComponent: null
    property Item contentItem: null
    // Open state. Driving this from false to true plays the open
    // animation; back to false plays the close animation and emits
    // dismissed at the end. The transport drives this; consumers
    // listen to dismissed for the "surface gone" notification.
    property bool open: false
    // Whether a click on the dimmed backdrop outside the content area
    // dismisses the popout. Cooperative and modal popouts typically
    // want this on; detached popouts off.
    property bool dismissOnClickOutside: true
    // Background dim, modal-style. Cooperative popouts may want this
    // transparent; modal popouts opaque. Bind from the transport.
    property color backdropColor: "transparent"

    // Emitted once the close animation finishes. Transport callers
    // wire this to their bookkeeping so the IPopoutTransport's
    // dismissed callback fires only after the visual finishes.
    signal dismissed()

    // Anchoring is the transport's job (it sets x/y/width/height on
    // this Item). The host fills whatever container it's placed in by
    // default, which is convenient for full-screen modal popouts.
    anchors.fill: parent
    onOpenChanged: {
        if (!open)
            dismissEmitter.start();
        else
            dismissEmitter.stop();
    }

    // Click-outside catcher. Sits behind the content, swallows clicks
    // that miss the content and routes them to dismiss.
    Rectangle {
        id: backdrop

        anchors.fill: parent
        color: root.backdropColor
        opacity: root.open ? 1 : 0

        MouseArea {
            anchors.fill: parent
            enabled: root.dismissOnClickOutside && root.open
            onClicked: root.open = false
        }

        Behavior on opacity {
            NumberAnimation {
                duration: Motion.duration_short_4
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Motion.easing_emphasized
            }

        }

    }

    // Content frame. Owns the slide-and-fade animation. Sits in the
    // center of the host by default; the transport overrides x/y for
    // bar-anchored popouts.
    Item {
        id: contentFrame

        function rebindContentItem() {
            if (root.contentItem)
                root.contentItem.parent = contentFrame;

        }

        anchors.centerIn: parent
        width: childrenRect.width
        height: childrenRect.height
        opacity: root.open ? 1 : 0
        scale: root.open ? 1 : 0.96
        // Reparent a pre-built contentItem into the frame. The transport
        // owns the item's lifetime; the host re-parents whenever the
        // contentItem property changes. The transport typically sets it
        // AFTER Component.onCompleted has already run, so a one-shot
        // onCompleted handler would miss the assignment entirely.
        onParentChanged: rebindContentItem()
        Component.onCompleted: rebindContentItem()

        Connections {
            function onContentItemChanged() {
                contentFrame.rebindContentItem();
            }

            target: root
        }

        // Inline component instantiation when contentComponent is set.
        // Loader keeps the lifecycle simple. Pre-built contentItem
        // wins if both are provided.
        Loader {
            id: contentLoader

            active: root.contentItem === null && root.contentComponent !== null
            sourceComponent: root.contentComponent
            visible: active
        }

        Behavior on opacity {
            NumberAnimation {
                duration: Motion.duration_medium_2
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Motion.easing_emphasized
            }

        }

        Behavior on scale {
            NumberAnimation {
                duration: Motion.duration_medium_2
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Motion.easing_emphasized
            }

        }

    }

    // Dismissed signal fires after the close animation drains. We
    // sample the open property edge and emit when it goes false AND
    // the opacity animation has settled.
    Timer {
        id: dismissEmitter

        interval: Motion.duration_medium_2
        repeat: false
        onTriggered: root.dismissed()
    }

}
