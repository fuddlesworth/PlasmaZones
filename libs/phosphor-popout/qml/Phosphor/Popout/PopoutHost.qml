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
    // Tracks whether dismissed has already fired for the current
    // open cycle. The signal is edge-triggered per cycle: the timer
    // fires it after the close animation, then opening again resets
    // the latch so the next close fires it once more. The flag also
    // guards against a double fire from Component.onDestruction
    // running after the timer's own emission.

    id: root

    // The popout's content. The host accepts either a pre-built Item
    // which the host reparents, or a Component the host instantiates
    // inline. One of the two must be set. The host takes parenting
    // ownership of any Item assigned to contentItem. Reassigning
    // contentItem orphans the previous value (parent set to null),
    // not restored to its prior parent. Callers handing in a pre-built
    // Item should expect to give up its parenting.
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
    // dismissed callback fires only after the visual finishes. Fires
    // once per open-then-close cycle so transports that reuse a
    // single PopoutHost across many popouts get a fresh dismissed
    // for each.
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
    // popouts. Qt treats `anchors.fill: parent` as a no-op while the
    // Item has no parent yet, so a top-level instantiation with no
    // parent assignment is safe without explicit null-guarding.
    anchors.fill: parent
    // Open going true resets the dismiss latch so a re-used host can
    // fire dismissed again on the next close. Open going false starts
    // the close-animation timer; the timer emits dismissed after the
    // longest Behavior duration settles. Re-opening while a close is
    // in flight cancels the pending close.
    onOpenChanged: {
        if (open) {
            dismissEmitter.stop();
            _internal.dismissedFired = false;
        } else {
            dismissEmitter.start();
        }
    }
    // contentItem reassignments after construction re-parent the new
    // item under contentFrame and detach the previous one.
    // Component.onCompleted covers the case where contentItem was
    // assigned before the host finished construction.
    onContentItemChanged: contentFrame.rebindContentItem()
    Component.onCompleted: contentFrame.rebindContentItem()
    // If the host is destroyed before the close-animation timer
    // emits dismissed, emit dismissed here so the transport's
    // bookkeeping never leaks a handle. This covers a mid-close
    // teardown or a transport that destroys the host while open is
    // still true. The fired flag guards against a double fire when
    // the timer also pumps during teardown.
    Component.onDestruction: {
        if (!_internal.dismissedFired) {
            dismissEmitter.stop();
            _internal.dismissedFired = true;
            root.dismissed();
        }
    }

    // Held inside a child QtObject so the latch isn't part of the
    // host's public surface. Consumers must NOT poke
    // _internal.dismissedFired; the lifecycle is managed inside this
    // file.
    QtObject {
        id: _internal

        property bool dismissedFired: false
    }

    // Click-outside catcher. Sits behind the content. Swallows clicks
    // that miss the content and routes them to dismiss. Skipped
    // entirely for fully-transparent backdrops that also disable
    // click-outside dismiss (Detached popouts) so the host doesn't
    // pay for a draw call or a no-op hit-test region.
    Rectangle {
        id: backdrop

        anchors.fill: parent
        color: root.backdropColor
        opacity: root.open ? 1 : 0
        visible: root.backdropColor.a > 0 || root.dismissOnClickOutside

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
        // Visible delegate: either the caller-provided contentItem,
        // or the Loader's instantiation of contentComponent. Read by
        // the size bindings below so the frame matches the delegate's
        // intrinsic size without re-evaluating the resolution twice.
        readonly property Item _visibleDelegate: root.contentItem ?? contentLoader.item

        function rebindContentItem() {
            if (_lastBound && _lastBound !== root.contentItem)
                _lastBound.parent = null;

            if (root.contentItem)
                root.contentItem.parent = contentFrame;

            _lastBound = root.contentItem;
        }

        anchors.centerIn: parent
        // Bind to the visible delegate's intrinsic size. childrenRect
        // would include invisible children, and a preloaded but
        // hidden contentItem would inflate the frame.
        width: _visibleDelegate ? _visibleDelegate.implicitWidth : 0
        height: _visibleDelegate ? _visibleDelegate.implicitHeight : 0
        opacity: root.open ? 1 : 0
        scale: root.open ? 1 : 0.96

        // Hit-blocker. Without this, gaps inside the content area
        // (rounded-corner transparency, padding) propagate clicks
        // down to the backdrop's dismiss MouseArea. The blocker
        // accepts left-button presses (the click-outside dismiss
        // path's trigger) so only out-of-content left clicks reach
        // the backdrop. Other buttons (right-click for context menus)
        // propagate through to inner MouseAreas in the content.
        MouseArea {
            anchors.fill: parent
            preventStealing: true
            acceptedButtons: Qt.LeftButton
            onPressed: (mouse) => {
                mouse.accepted = true;
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

        // Interval matches the longest Behavior duration above.
        // Content opacity and scale animate over
        // Motion.duration_medium_2. Backdrop opacity uses
        // duration_short_4 which is shorter. Computing the max
        // keeps the timer aligned even if the backdrop duration is
        // ever retuned above the content's.
        interval: Math.max(Motion.duration_medium_2, Motion.duration_short_4)
        repeat: false
        onTriggered: {
            if (!_internal.dismissedFired) {
                _internal.dismissedFired = true;
                root.dismissed();
            }
        }
    }

}
