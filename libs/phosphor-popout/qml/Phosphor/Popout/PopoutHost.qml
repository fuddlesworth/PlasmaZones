// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Popout.PopoutHost. Transport-agnostic wrapper for a
// popout's content. The transport instantiates this host wrapping
// the content delegate. The in-window transport hosts the demo. A
// layer-shell transport hosts the shell. The host owns the
// open/close animation, click-outside dismiss detection, and content
// sizing. The host does not assume a window of its own. It can be
// parented into a regular Item or into a layer-shell wrapper Item.

import Phosphor.Theme
import QtQuick

Item {
    id: root

    // The popout's content. The host accepts either a pre-built Item
    // which the host reparents, or a Component the host instantiates
    // inline. One of the two must be set.
    property Component contentComponent: null
    property Item contentItem: null
    // Open state. Driving this from false to true plays the open
    // animation. Back to false plays the close animation and emits
    // dismissed at the end. The transport drives this. Consumers
    // listen to dismissed for the surface-gone notification.
    property bool open: false
    // Whether a click on the dimmed backdrop outside the content area
    // dismisses the popout. Cooperative and modal popouts typically
    // want this on. Detached popouts off.
    property bool dismissOnClickOutside: true
    // Background dim. The transport binds this. Cooperative popouts
    // may want a translucent dim. Modal popouts want an opaque scrim.
    // Detached popouts want transparent.
    property color backdropColor: "transparent"

    // Emitted once the close animation finishes. Transport callers
    // wire this to their bookkeeping so the IPopoutTransport's
    // dismissed callback fires only after the visual finishes.
    signal dismissed()

    // Public dismiss helper for content. Content delegates that want
    // to close themselves (a Cancel button on a Modal, for example)
    // do not have a direct path to the host because they sit several
    // parent levels deep inside contentFrame. The transport injects
    // a reference to this host on the content so the content can call
    // popoutHost.dismiss() rather than walk the parent chain.
    function dismiss() {
        open = false;
    }

    // Anchoring is the transport's responsibility. The transport sets
    // x, y, width, and height on this Item. By default the host fills
    // its parent container, which is convenient for full-screen modal
    // popouts. `parent ?? null` keeps the binding well-formed when
    // root is instantiated at top level with no parent yet.
    anchors.fill: parent ?? null
    onOpenChanged: {
        // Track the previously-bound contentItem so a swap (or a
        // clear) can detach the old one from contentFrame before the
        // new one parents in. Without this, switching contentItem
        // leaves both items parented to contentFrame and both stay
        // visible.
        if (!open)
            dismissEmitter.start();
        else
            dismissEmitter.stop();
    }
    onContentItemChanged: contentFrame.rebindContentItem()
    Component.onCompleted: {
        contentFrame.rebindContentItem();
        contentFrame._lastBound = contentItem;
    }
    // If the host is destroyed mid-close, ensure dismissed still
    // fires so the transport's bookkeeping does not leak the handle.
    // Component.onDestruction runs synchronously during teardown.
    Component.onDestruction: {
        if (!root.open && dismissEmitter.running) {
            dismissEmitter.stop();
            root.dismissed();
        }
    }

    // Click-outside catcher. Sits behind the content. Swallows clicks
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
                easing: Motion.emphasized
            }

        }

    }

    // Content frame. Owns the slide-and-fade animation. Centered in
    // the host by default. The transport overrides x and y for
    // bar-anchored popouts.
    Item {
        id: contentFrame

        // Tracks the contentItem currently parented under this
        // frame. Used by rebindContentItem to detach the previous
        // item when the property changes.
        property Item _lastBound: null

        function rebindContentItem() {
            if (_lastBound && _lastBound !== root.contentItem)
                _lastBound.parent = null;

            if (root.contentItem)
                root.contentItem.parent = contentFrame;

            _lastBound = root.contentItem;
        }

        anchors.centerIn: parent
        // Bind explicitly to the visible child's natural size instead
        // of childrenRect. childrenRect includes invisible children
        // and a preloaded but hidden contentItem would inflate the
        // frame.
        width: root.contentItem ? root.contentItem.implicitWidth : (contentLoader.item ? contentLoader.item.implicitWidth : 0)
        height: root.contentItem ? root.contentItem.implicitHeight : (contentLoader.item ? contentLoader.item.implicitHeight : 0)
        opacity: root.open ? 1 : 0
        scale: root.open ? 1 : 0.96

        // Hit-blocker. Without this, gaps inside the content area
        // (rounded-corner transparency, padding) propagate clicks
        // down to the backdrop's dismiss MouseArea. The blocker
        // accepts every click in the content region so only clicks
        // outside the content reach the backdrop.
        MouseArea {
            anchors.fill: parent
            preventStealing: true
            acceptedButtons: Qt.AllButtons
            onPressed: (mouse) => {
                return mouse.accepted = true;
            }
        }

        // Inline component instantiation when contentComponent is
        // set. Loader keeps the lifecycle simple. Pre-built
        // contentItem wins if both are provided.
        Loader {
            id: contentLoader

            active: root.contentItem === null && root.contentComponent !== null
            sourceComponent: root.contentComponent
        }

        Behavior on opacity {
            NumberAnimation {
                id: openOpacityAnim

                duration: Motion.duration_medium_2
                easing: Motion.emphasized
            }

        }

        Behavior on scale {
            NumberAnimation {
                duration: Motion.duration_medium_2
                easing: Motion.emphasized
            }

        }

    }

    // dismissed fires once the close animation has had time to
    // settle. The interval matches the longest Behavior duration
    // above (Motion.duration_medium_2). Binding the interval to the
    // same token keeps the two in sync. If the Behaviors switch to a
    // different duration token, update this binding too or extract
    // both into a shared property.
    Timer {
        id: dismissEmitter

        interval: Motion.duration_medium_2
        repeat: false
        onTriggered: root.dismissed()
    }

}
