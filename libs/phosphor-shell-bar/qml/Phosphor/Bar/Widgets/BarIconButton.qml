// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.BarIconButton, a round icon-only bar button.
//
// Shared chrome for the bar's trailing buttons (control center,
// notifications, power): a round-painted button (the hit target itself is
// the square item bounds) carrying the shared
// PhosphorRipple state layer (hover / focus / pressed tints plus the press
// ripple), a themed icon, an optional unread badge, and an `activated`
// signal. Pointer, keyboard, and assistive-tech paths all route through
// one activation function, so their behaviour cannot drift apart. The
// keyboard path is dormant in the shipped bar, whose panel takes no
// keyboard focus; it lights up wherever a focused surface hosts one.
//
// Kirigami.Icon draws its own fallback glyph for a name the icon theme
// cannot resolve, so a missing icon never yields an invisible control.

import QtQuick
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root

    // Freedesktop icon name, resolved through the icon theme.
    property string iconName: ""
    // Accessible name for the control.
    property string label: ""
    // Unread/attention count; 0 hides the badge.
    property int badgeCount: 0

    // Emitted when the button is activated (pointer tap, Space / Enter, or
    // an assistive-tech press) while enabled. The seam the popout surfaces
    // (Phase 4.3/4.4/4.6) bind to.
    signal activated

    implicitWidth: 28
    implicitHeight: 28

    Accessible.role: Accessible.Button
    // Fold the unread count in so assistive tech announces it, not just the
    // label. badgeCount is 0 until a shared notification service feeds it.
    Accessible.name: root.badgeCount > 0 ? qsTr("%1, %2 unread").arg(root.label).arg(root.badgeCount) : root.label
    Accessible.onPressAction: root._activate()

    activeFocusOnTab: enabled
    Keys.onSpacePressed: event => root._activateFromKey(event)
    Keys.onReturnPressed: event => root._activateFromKey(event)
    Keys.onEnterPressed: event => root._activateFromKey(event)

    function _activate() {
        if (!root.enabled)
            return;
        root.activated();
    }

    function _activateFromKey(event) {
        // Check enabled BEFORE the ripple so a disabled button cannot flash
        // one, matching PhosphorPill's ordering.
        if (event.isAutoRepeat || !root.enabled)
            return;
        ripple.start(root.width / 2, root.height / 2);
        root._activate();
    }

    Rectangle {
        id: chip

        anchors.fill: parent
        radius: height / 2
        color: "transparent"

        // The pointing-hand affordance the bar's other interactive widgets
        // use. PhosphorRipple owns the state layer but not the cursor.
        // Gated explicitly: Item.enabled suppresses classic mouse and key
        // delivery but NOT pointer handlers, so an ungated handler would
        // promise a pointing hand over a dead control. PhosphorRipple gates
        // its own HoverHandler the same way.
        HoverHandler {
            enabled: root.enabled
            cursorShape: Qt.PointingHandCursor
        }

        Kirigami.Icon {
            anchors.centerIn: parent
            width: 18
            height: 18
            source: root.iconName
            // Tint to a single foreground colour so the glyph reads on the
            // dark capsule regardless of the theme's light/dark icon variant.
            // A disabled button dims its glyph, since the ripple's state
            // layer simply goes absent and would otherwise leave a disabled
            // button pixel-identical to an enabled one.
            isMask: true
            color: Theme.on_surface_variant
            // Dim through opacity rather than an alpha-bearing tint: no
            // other Kirigami.Icon in the tree passes alpha to `color` under
            // isMask, so relying on it honouring that alpha would be
            // unverified (the same trap as the decoration item that ignores
            // QColor alpha). Opacity is unambiguous.
            opacity: root.enabled ? 1 : StateLayer.disabled_content
        }

        // The shared state layer: hover / focus / pressed opacities and the
        // press ripple, with the same precedence every other Phosphor
        // control uses. Hand-rolling it here would let the bar's buttons
        // drift from the rest of the shell.
        PhosphorRipple {
            id: ripple

            anchors.fill: parent
            radius: parent.radius
            interactive: root.enabled
            focused: root.activeFocus
            rippleColor: Theme.on_surface
            onTapped: root._activate()
        }
    }

    Rectangle {
        id: badge

        // Dims with the button: a disabled control should not keep
        // signalling urgency at full strength.
        visible: root.badgeCount > 0
        opacity: root.enabled ? 1 : StateLayer.disabled_content
        anchors.right: parent.right
        anchors.top: parent.top
        width: Math.max(height, badgeLabel.implicitWidth + Tokens.spacing_xs)
        height: 14
        radius: height / 2
        color: Theme.error

        Text {
            id: badgeLabel

            anchors.centerIn: parent
            text: root.badgeCount > 99 ? "99+" : root.badgeCount.toString()
            // The count is already folded into the root's Accessible.name.
            // This has to sit on the Text, not the Rectangle: QQuickText is
            // the node that exposes itself as StaticText, and ignoring the
            // parent only hoists its children up a level rather than
            // removing them, so the count would still be announced twice.
            Accessible.ignored: true
            color: Theme.on_error
            font.pixelSize: Tokens.font_size_label_s
            font.weight: Tokens.font_weight_medium
            font.family: Tokens.font_family
        }
    }
}
