// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Widgets.PhosphorButton, M3 button in four variants.
//
// A token-driven button built from a rounded container + PhosphorRipple
// + label, rather than a restyled QtQuick.Controls Button, so every
// colour binds through the Theme singleton and retints live. Variants
// map to M3: Filled (primary action), Tonal (secondary emphasis),
// Outlined (medium emphasis), Text (low emphasis).
//
//   PhosphorButton {
//       text: i18n("Apply")
//       variant: PhosphorButton.Filled
//       onClicked: controller.apply()
//   }
//
// Set `enabled: false` for the disabled state; the container and content
// drop to the M3 disabled opacities from the StateLayer singleton.

import QtQuick
import Phosphor.Theme

Item {
    id: root

    enum Variant {
        Filled,
        Tonal,
        Outlined,
        Text
    }

    property string text: ""
    property int variant: PhosphorButton.Filled

    // Emitted on a completed tap when enabled.
    signal clicked

    implicitWidth: Math.max(64, label.implicitWidth + Tokens.spacing_xxxl)
    implicitHeight: 40

    Accessible.role: Accessible.Button
    Accessible.name: root.text
    Accessible.onPressAction: if (root.enabled)
        root.clicked()

    // Keyboard: Tab-focusable when enabled; Space / Enter / Return activate
    // it (with a ripple from the centre), matching the pointer path.
    activeFocusOnTab: enabled
    Keys.onSpacePressed: event => root._activateFromKey(event)
    Keys.onReturnPressed: event => root._activateFromKey(event)
    Keys.onEnterPressed: event => root._activateFromKey(event)

    function _activateFromKey(event) {
        if (event.isAutoRepeat || !root.enabled)
            return;
        ripple.start(root.width / 2, root.height / 2);
        root.clicked();
    }

    // True for the two variants that paint a filled container; false for
    // Outlined / Text, which sit on transparent.
    readonly property bool _hasContainer: variant === PhosphorButton.Filled || variant === PhosphorButton.Tonal

    // Resolved container fill. Disabled filled / tonal buttons drop to
    // the M3 disabled-container tint over on_surface; outlined / text
    // stay transparent.
    readonly property color _container: {
        if (!enabled)
            return _hasContainer ? StateLayer.disabledContainer(Theme.on_surface) : "transparent";
        switch (variant) {
        case PhosphorButton.Filled:
            return Theme.primary;
        case PhosphorButton.Tonal:
            return Theme.primary_container;
        default:
            return "transparent";
        }
    }

    // Resolved content (label + ripple) colour. Disabled drops to the M3
    // disabled-content opacity regardless of variant.
    readonly property color _content: {
        if (!enabled)
            return StateLayer.disabledContent(Theme.on_surface);
        switch (variant) {
        case PhosphorButton.Filled:
            return Theme.on_primary;
        case PhosphorButton.Tonal:
            return Theme.on_primary_container;
        default:
            return Theme.primary;
        }
    }

    Rectangle {
        id: bg

        anchors.fill: parent
        radius: height / 2
        color: root._container
        // Outlined keeps its outline when disabled, at the M3 disabled
        // opacity, rather than vanishing to a borderless invisible box.
        border.width: root.variant === PhosphorButton.Outlined ? 1 : 0
        border.color: root.enabled ? Theme.outline : StateLayer.disabledContainer(Theme.on_surface)

        Behavior on color {
            ColorAnimation {
                duration: Motion.duration_short_3
                easing: Motion.standard
            }
        }

        PhosphorRipple {
            id: ripple

            anchors.fill: parent
            radius: parent.radius
            interactive: root.enabled
            focused: root.activeFocus
            rippleColor: root._content
            onTapped: root.clicked()
        }
    }

    Text {
        id: label

        anchors.centerIn: parent
        text: root.text
        color: root._content
        font.pixelSize: Tokens.font_size_body_l
        font.weight: Tokens.font_weight_medium
        horizontalAlignment: Text.AlignHCenter

        Behavior on color {
            ColorAnimation {
                duration: Motion.duration_short_3
                easing: Motion.standard
            }
        }
    }
}
