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
    // inline. One of the two must be set; the host warns on
    // Component.onCompleted if neither is provided (a misconfigured
    // host renders nothing, which is otherwise silent in QML). The
    // host takes parenting ownership of any Item assigned to
    // contentItem. Reassigning contentItem orphans the previous value
    // (parent set to null), not restored to its prior parent. Callers
    // handing in a pre-built Item should expect to give up its
    // parenting.
    //
    // Contract: the component's (or contentItem's) root MUST set
    // implicitWidth and implicitHeight. The contentLoader inside
    // contentFrame uses `anchors.fill: parent` (so the loaded item
    // sees a sized parent for anchors-based layouts), and contentFrame
    // sizes itself from the delegate's implicitWidth/implicitHeight.
    // A delegate without implicit sizes collapses contentFrame to 0x0
    // and the popout renders empty.
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
    // Detached popouts want transparent. The literal "transparent"
    // default is the sentinel for "no dim, no scrim" - the backdrop
    // Rectangle below treats this (alpha == 0) as "do not draw" and
    // skips its child MouseArea hit-tests unless dismissOnClickOutside
    // is also set. Keep the string literal: Qt's color converter
    // normalises "transparent" to a zero-alpha QColor, matching the
    // backdropShown check.
    property color backdropColor: "transparent"

    // Internal: duration token shared by the content frame's
    // opacity/scale Behaviors. Exposed as a property so the Behaviors
    // and any future timer/Animation referencing the content fade-out
    // bind a single source of truth.
    readonly property int contentAnimDuration: Motion.duration_medium_2
    // Internal: longest animation duration across the host's
    // Behaviors (content opacity/scale on contentAnimDuration, backdrop
    // opacity on duration_short_4). Single source of truth so the
    // dismiss timer and the Behaviors stay aligned through any token
    // retuning. Bound rather than computed at startup so a runtime
    // theme switch picks up the new value.
    //
    // Math.max keeps the duration_short_4 operand even though it is
    // currently the smaller of the two: it is a defensive ceiling for a
    // future token retune that flips the ordering (for example, if
    // Motion.duration_short_4 is ever bumped above
    // Motion.duration_medium_2 for a denser theme retune, or a
    // downstream theme lengthens the backdrop fade beyond the content
    // one). A reader tempted to "simplify" to `contentAnimDuration`
    // would remove that safety net.
    readonly property int closeDuration: Math.max(contentAnimDuration, Motion.duration_short_4)

    // Emitted once the close animation finishes. Transport callers
    // wire this to their bookkeeping so the IPopoutTransport's
    // dismissed callback fires only after the visual finishes. Fires
    // once per open-then-close cycle so transports that reuse a
    // single PopoutHost across many popouts get a fresh dismissed
    // for each.
    signal dismissed

    // Public dismiss helper for content. Content delegates that want
    // to close themselves (a Cancel button on a Modal, for example)
    // do not have a direct path to the host because they sit several
    // parent levels deep inside contentFrame. The transport injects
    // a reference to this host on the content so the content can call
    // _popoutHost.dismiss() rather than walk the parent chain.
    function dismiss() {
        open = false;
    }

    // Sizing is the transport's responsibility — the transport sets x,
    // y, width, and height on this Item. The root Item deliberately
    // does NOT anchors.fill its parent: an earlier revision did, which
    // conflicted with the documented contract above (the transport,
    // not the host, drives geometry) and produced double-bound
    // width/height when the transport assigned them after construction.
    // A host instantiated without an x/y/width/height assignment
    // collapses to 0x0; that is the transport's responsibility to
    // notice, not a host-side default.
    // Open going true resets the dismissed-fired latch AND records
    // that this host has begun a lifecycle. A re-used host can fire
    // dismissed again on the next close. A host destroyed without
    // ever being opened (transport pre-builds then discards) does
    // NOT fire dismissed, since there was no open-then-close cycle
    // to bookend; firing would double-decrement transport state.
    // Open going false starts the close-animation timer; the timer
    // emits dismissed after the longest Behavior duration settles.
    // Re-opening while a close is in flight cancels the pending close.
    onOpenChanged: {
        if (open) {
            dismissEmitter.stop();
            dismissLatch.dismissedFired = false;
            dismissLatch.everOpened = true;
        } else {
            // Known limitation: QTimer samples interval at start(), so
            // a Motion-token retune (theme switch mid-close) updates
            // dismissEmitter.interval via the binding below but does
            // NOT re-arm the already-running timer. The in-flight
            // close will complete on the pre-switch duration; the
            // next open-then-close cycle picks up the new tokens.
            // Acceptable because mid-close theme swaps are rare and
            // the visible Behavior animations interpolate independently
            // — only the dismissed-signal emission timing is affected.
            dismissEmitter.start();
        }
    }
    // contentItem reassignments after construction re-parent the new
    // item under contentFrame and detach the previous one.
    // Component.onCompleted covers the case where contentItem was
    // assigned before the host finished construction.
    onContentItemChanged: contentFrame.rebindContentItem()
    Component.onCompleted: {
        contentFrame.rebindContentItem();
        // Diagnostic: a host with neither contentItem nor
        // contentComponent renders an empty frame, which is silent in
        // QML and confusing in practice. Warn once at construction so
        // a missed transport assignment surfaces in the log rather
        // than as a missing UI.
        //
        // Deferred via a Timer (50ms) so transports that assign
        // contentComponent immediately after constructing the host (a
        // common pattern, since both properties default to null) get a
        // chance to settle before the warning fires. Firing the check
        // synchronously in Component.onCompleted produces false
        // positives any time the transport's assignment order is
        // construct-then-set-content rather than set-content-then-
        // construct.
        contentDiagnosticTimer.start();
    }
    // If the host is destroyed mid-cycle (open was set true but the
    // close-animation timer hasn't emitted dismissed yet), fire
    // dismissed here so the transport's bookkeeping never leaks a
    // handle. Skipped when the host never opened (no cycle to
    // bookend) and when the timer already fired (everOpened guards
    // the first case, dismissedFired guards the second).
    //
    // This is the last signal this host will ever emit: the QObject
    // is mid-destruction. Transport callers handling dismissed here
    // MUST NOT re-enter the host (calling dismiss(), reading
    // properties, rebinding contentItem, OR reading child items such
    // as contentFrame / _visibleDelegate / backdrop will operate on a
    // partially torn-down object - QML's child destruction order is
    // not guaranteed to outlive this handler). Treat the handler as
    // bookkeeping-only.
    //
    // The destruction-time emission is synchronous with the slot
    // dispatch: any handler reading host-side state (or state any
    // sibling QObject of the host owns whose teardown ordering is
    // entangled with the host's) is reading partially-torn-down
    // memory. Transports that connect to dismissed MUST ensure their
    // handler is strictly stateless w.r.t. the host - capture
    // everything the handler needs in the closure at connect time
    // (transport id, bookkeeping handle), and never dereference the
    // host or its children from inside the handler. The single signal
    // signature (no fromDestruction argument) is deliberate: branching
    // on the path inside the handler would tempt callers into the
    // exact re-entrance the warning above forbids.
    Component.onDestruction: {
        if (dismissLatch.everOpened && !dismissLatch.dismissedFired) {
            // Null-guard dismissEmitter: QML's child destruction order
            // is not guaranteed (the Timer below might already have
            // been torn down before this handler runs on the host),
            // and reading a child that has been destructed yields
            // null. Same null-read concern applies to dismissLatch
            // earlier in the condition, but the guard here is on the
            // method call that would crash if the child were gone.
            if (dismissEmitter)
                dismissEmitter.stop();
            dismissLatch.dismissedFired = true;
            root.dismissed();
        }
    }

    // Held inside a child QtObject so the latches aren't part of the
    // host's public surface. Consumers must NOT poke either field;
    // both are lifecycle bookkeeping managed inside this file.
    QtObject {
        id: dismissLatch

        // Tracks whether dismissed has already fired for the current
        // open cycle. The signal is edge-triggered per cycle: the
        // dismissEmitter timer fires it after the close animation, then
        // opening again resets the latch so the next close fires it
        // once more. Also guards against a double-fire from
        // Component.onDestruction running after the timer's emission.
        property bool dismissedFired: false
        // Set on the first open=true transition. Used by
        // Component.onDestruction to distinguish a destroy-without-
        // open from a destroy-mid-cycle so only the latter fires
        // dismissed.
        property bool everOpened: false
    }

    // Click-outside catcher. Sits behind the content. Swallows clicks
    // that miss the content and routes them to dismiss. Skipped
    // entirely for fully-transparent backdrops that also disable
    // click-outside dismiss (Detached popouts) so the host doesn't
    // pay for a draw call or a no-op hit-test region. When the
    // Rectangle is invisible, its child MouseArea is also hidden
    // (Qt skips hit-tests on invisible item subtrees), so the
    // detached-popout path pays nothing for hit-testing either.
    Rectangle {
        id: backdrop

        // backdropShown gates whether the backdrop conceptually exists
        // for this configuration (visible scrim and/or click-outside).
        // visible derives from this AND opacity > 0 so the Rectangle
        // stays drawn through the fade-out, instead of vanishing the
        // instant `open` flips to false and stealing the fade frames
        // from the Behavior below.
        //
        // The 0.01 threshold defends against trivial-alpha colors that
        // are visually invisible (e.g. "#01000000" with alpha 1/255 ≈
        // 0.0039) being mistaken for an intentional backdrop. Set
        // backdropColor: "transparent" (the literal) to disable the
        // backdrop entirely.
        readonly property bool backdropShown: root.backdropColor.a > 0.01 || root.dismissOnClickOutside

        anchors.fill: parent
        color: root.backdropColor
        opacity: root.open ? 1 : 0
        // Bind visible to discrete cycle state (open OR a pending
        // close-animation), not to opacity > 0. The opacity binding
        // re-fires every animation frame, and a `visible: opacity > 0`
        // dependency would re-evaluate visible on every step too.
        // dismissEmitter.running covers the fade-out window between
        // open=false and the close-animation completion.
        visible: backdropShown && (root.open || dismissEmitter.running)

        MouseArea {
            anchors.fill: parent
            // Gate hit-tests on the original boolean expression
            // (open + dismissOnClickOutside), NOT on the parent's
            // opacity-sensitive `visible`. A backdrop mid-fade-out has
            // visible=true but `open=false`, and accepting clicks then
            // would double-fire dismiss (or fire dismiss on an
            // already-dismissing popout). backdropShown is redundant
            // here: dismissOnClickOutside == true implies
            // backdropShown == true (see the readonly above), so the
            // two-term gate is equivalent and clearer.
            enabled: root.dismissOnClickOutside && root.open
            // Route via dismiss() rather than setting open=false
            // directly so any future logic added to dismiss() (focus
            // restore, transport callback, telemetry) applies to the
            // click-outside path too.
            onClicked: root.dismiss()
            Accessible.name: i18nc("@action:button", "Dismiss popout")
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
        //
        // Transient null window: when contentItem is reassigned from a
        // non-null value to null, contentLoader.active flips true only
        // after this binding re-evaluates, so for one binding tick
        // _visibleDelegate is null and contentFrame collapses to 0x0.
        // The same is true on initial construction before the Loader
        // instantiates. Both transients are masked by the opacity
        // Behavior (frame stays invisible while open=false); the next
        // binding evaluation inflates the frame before opacity reaches
        // 1. Eagerly preloading the Loader (active: contentComponent
        // !== null) would close the window but at the cost of building
        // a delegate the host may never display.
        //
        // The `??` (nullish coalescing) operator requires Qt 6.4+ in
        // QML's JS engine. The project pins QT_MIN_VERSION 6.6.0 (top-
        // level CMakeLists.txt), so the operator is safe to use here.
        readonly property Item _visibleDelegate: root.contentItem ?? contentLoader.item

        function rebindContentItem() {
            // Only orphan _lastBound if it is still parented under
            // contentFrame. A Loader-instantiated previous item
            // (contentComponent path) is owned by the Loader; setting
            // its parent to null here would detach a child the Loader
            // is about to destroy on its own, which is at best a
            // redundant write and at worst a transient double-parent
            // shuffle during the swap. Warn if we are dropping a
            // non-null prior bind without a replacement: a contentItem
            // -> null assignment silently orphans the existing item,
            // which is occasionally intentional (transport tear-down)
            // but more often a contract slip.
            if (_lastBound && _lastBound !== root.contentItem) {
                if (_lastBound.parent === contentFrame) {
                    _lastBound.parent = null;
                } else {
                    // _lastBound was tracked here as the bound item, but
                    // its parent has drifted away from contentFrame -
                    // some external code reparented it without going
                    // through the host. Warn parallel to the non-host-
                    // parent warn below so both directions of ownership
                    // drift surface in the log.
                    console.warn("PopoutHost.rebindContentItem: _lastBound parent drifted from contentFrame (parent=", _lastBound.parent, "); skipping detach to avoid stealing from a new owner");
                }
                if (!root.contentItem)
                    console.warn("PopoutHost.rebindContentItem: contentItem set to null while a previous item was bound; detaching without successor");
            }

            if (root.contentItem) {
                // Contract: the host takes parenting ownership of any
                // Item assigned to contentItem (see the contentItem
                // property docs above). Hand-in items are expected to
                // arrive parent-less or already parented under this
                // contentFrame from a previous rebind. A non-null
                // parent that isn't contentFrame indicates a caller
                // is stashing the item in another subtree (which we
                // will now silently steal), which is a contract break
                // and surfaces here as a console.warn rather than as
                // a mysterious detached UI elsewhere. QML lacks
                // assertions; this is the closest diagnostic available.
                if (root.contentItem.parent && root.contentItem.parent !== contentFrame)
                    console.warn("PopoutHost.rebindContentItem: contentItem already has a non-host parent; the host is taking ownership and detaching it from", root.contentItem.parent);

                root.contentItem.parent = contentFrame;
            }

            _lastBound = root.contentItem;
        }

        anchors.centerIn: parent
        // Bind to the visible delegate's intrinsic size. childrenRect
        // would include invisible children, and a preloaded but
        // hidden contentItem would inflate the frame.
        //
        // contentFrame can momentarily collapse to 0x0 during Loader
        // spin-up (between Loader.active flipping true and the
        // instantiated item reporting its implicit size). The opacity
        // Behavior masks this for the user (frame is invisible while
        // open=false), and the next binding evaluation - once the
        // delegate's implicitWidth/Height settle - inflates the frame
        // before opacity reaches 1. Holding open until implicitWidth
        // > 0 would require an extra state machine and trade one
        // hidden transient for another; the opacity-gated transient
        // is preferable.
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
            // preventStealing is inert in this context (no Flickable
            // or gesture filter is sitting between the content frame
            // and the backdrop), but kept as defensive future-proofing
            // against a transport wrapping the host in a Flickable
            // (e.g. for scrollable popouts) and inadvertently letting
            // a swipe gesture eat the hit-blocker's press.
            preventStealing: true
            acceptedButtons: Qt.LeftButton
            onPressed: mouse => {
                mouse.accepted = true;
            }
        }

        // Inline component instantiation when contentComponent is
        // set. Loader keeps the lifecycle simple. Pre-built
        // contentItem wins if both are provided. anchors.fill makes
        // the Loader fill contentFrame so the loaded item sees a
        // sized parent for any anchors-based layout the delegate uses
        // (an unanchored Loader collapses to 0x0 and the loaded item
        // inherits a zero-size parent until contentFrame's
        // implicitWidth/Height binding settles).
        Loader {
            id: contentLoader

            anchors.fill: parent
            active: root.contentItem === null && root.contentComponent !== null
            sourceComponent: root.contentComponent
        }

        Behavior on opacity {
            NumberAnimation {
                duration: root.contentAnimDuration
                easing: Motion.emphasized
            }
        }

        Behavior on scale {
            NumberAnimation {
                duration: root.contentAnimDuration
                easing: Motion.emphasized
            }
        }
    }

    // dismissed fires once the close animation has had time to
    // settle. The interval binds closeDuration, which tracks the same
    // Motion tokens (contentAnimDuration / duration_short_4) as the
    // host's Behaviors, so retuning the tokens (or swapping the
    // Behaviors to longer/shorter tokens) keeps them aligned without
    // a second edit.
    Timer {
        id: dismissEmitter

        // QTimer samples interval at start(); if Motion tokens retune
        // mid-close the binding updates this property but the armed
        // timer keeps its original interval until the next start().
        interval: root.closeDuration
        repeat: false
        onTriggered: {
            if (!dismissLatch.dismissedFired) {
                dismissLatch.dismissedFired = true;
                root.dismissed();
            }
        }
    }

    // Deferred no-content diagnostic. Fires once 50ms after
    // Component.onCompleted to give transports that assign
    // contentComponent post-construction a chance to settle. Without
    // the delay, every transport that uses the two-step pattern
    // (construct host, then assign contentComponent) would log a
    // spurious warning at startup.
    Timer {
        id: contentDiagnosticTimer

        interval: 50
        repeat: false
        onTriggered: {
            if (root.contentItem === null && root.contentComponent === null)
                console.warn("PopoutHost: neither contentItem nor contentComponent set; the host will render empty");
        }
    }
}
