// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Power.PowerTile, one action in the session menu.
//
// A large icon-over-label target with its single-key shortcut shown beneath,
// per the power-menu mockup. Tiles are big and few rather than a dense list:
// these are consequential, infrequent actions, and the size is the point.
//
// `primary` marks the default action. In the mockup that is Lock, rendered
// filled rather than outlined, because it is the safest thing the menu can
// do and therefore the safest thing to have focused when it opens.

import QtQuick
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root

    required property string label
    required property string iconName
    /// The single-key shortcut, shown in the tile. The menu owns the actual
    /// key handling; this is only its display.
    required property string shortcut
    /// Accent for the glyph. Semantic per action (warning for restart, error
    /// for shut down), so the tile does not decide what it means.
    property color accent: Theme.on_surface_variant
    /// The default action: filled instead of outlined, and the tile the menu
    /// focuses on open.
    property bool primary: false

    signal activated

    /// The tile's own metrics, and the ONE definition of them. PowerMenu sizes
    /// its grid from these rather than repeating the numbers, so the two
    /// cannot drift. Component-local literals rather than Tokens entries
    /// because that is what the sibling atoms do (BarIconButton fixes its own
    /// 28x28); Tokens carries spacing, radius and type, not per-widget sizes.
    readonly property int tileWidth: 168
    readonly property int tallHeight: 116
    readonly property int shortHeight: 80

    implicitWidth: root.tileWidth
    implicitHeight: root.tallHeight

    activeFocusOnTab: true

    Accessible.role: Accessible.Button
    Accessible.name: root.label
    // The shortcut is genuinely useful to announce — it is how a keyboard
    // user is expected to drive this menu.
    Accessible.description: qsTr("Shortcut %1").arg(root.shortcut)
    Accessible.onPressAction: root.activated()

    // Pointer, keyboard, and assistive tech all reach `activated` through the
    // same path, so they cannot drift apart. Without these the tile could hold
    // focus and be tabbed to while Enter and Space did nothing, which is the
    // shape BarIconButton's own header warns about.
    Keys.onSpacePressed: event => root._activateFromKey(event)
    Keys.onReturnPressed: event => root._activateFromKey(event)
    Keys.onEnterPressed: event => root._activateFromKey(event)

    function _activateFromKey(event: var): void {
        // Autorepeat guard: these actions suspend or power off the machine, so
        // a held key must not fire twice.
        if (event.isAutoRepeat)
            return;
        ripple.start(root.width / 2, root.height / 2);
        root.activated();
    }

    Rectangle {
        id: surface

        anchors.fill: parent
        radius: Tokens.radius_l
        color: root.primary ? Theme.primary : Theme.surface_container_high
        border.width: root.primary ? 0 : 1
        border.color: Theme.outline_variant

        // The pointing-hand affordance the shell's other interactive
        // controls use. PhosphorRipple owns the state layer but not the
        // cursor; gated on enabled the same way BarIconButton gates its
        // handler, since Item.enabled does not suppress pointer handlers.
        HoverHandler {
            enabled: root.enabled
            cursorShape: Qt.PointingHandCursor
        }

        PhosphorRipple {
            id: ripple

            anchors.fill: parent
            radius: surface.radius
            rippleColor: root.primary ? Theme.on_primary : Theme.on_surface
            focused: root.activeFocus
            onTapped: root.activated()
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: Tokens.spacing_s

        Kirigami.Icon {
            source: root.iconName
            isMask: true
            width: 28
            height: 28
            color: root.primary ? Theme.on_primary : root.accent
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
            // Folded into the root's Accessible.name; QQuickText exposes
            // itself as its own StaticText node, so without this assistive
            // tech reads the composed name and then re-reads the fragment.
            Accessible.ignored: true
            text: root.label
            color: root.primary ? Theme.on_primary : Theme.on_surface
            font.pixelSize: Tokens.font_size_label_l
            font.weight: Tokens.font_weight_demibold
            font.family: Tokens.font_family
            anchors.horizontalCenter: parent.horizontalCenter
            // The tile is a fixed width, so a longer translation
            // ("Ruhezustand", "Sesión cerrada") would otherwise run past its
            // edges. Elide rather than wrap: two rows of differing tile
            // heights only line up if the label stays one line.
            // Off root.width, not the tileWidth constant: a layout that ever
            // compresses the tile must elide the label rather than overflow it.
            width: Math.max(0, Math.min(implicitWidth, root.width - Tokens.spacing_m * 2))
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            Accessible.ignored: true
            text: root.shortcut
            color: root.primary ? Theme.on_primary : Theme.on_surface_variant
            opacity: root.primary ? 0.75 : 1.0
            font.pixelSize: Tokens.font_size_label_s
            font.family: Tokens.font_family
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }
}
