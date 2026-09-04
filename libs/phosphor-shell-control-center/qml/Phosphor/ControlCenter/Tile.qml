// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.ControlCenter.Tile, the shared chrome for one control tile.
//
// A control tile is a toggle with a readout and, optionally, a detail
// view: Network shows the connected SSID and expands to a network list,
// Audio shows the sink volume and expands to a device picker. This
// component owns the chrome and the interaction contract; the concrete
// tiles (NetworkTile, AudioTile, ...) fill in `iconName`, `label`,
// `sublabel`, `active`, and whatever detail content they carry.
//
//   Tile {
//       // NetworkHost is a creatable type, not a singleton.
//       NetworkHost { id: host }
//       iconName: "network-wireless"
//       label: qsTr("Wi-Fi")
//       sublabel: host.connectivity === NetworkHost.Full ? qsTr("Connected") : qsTr("Not connected")
//       active: host.wirelessEnabled
//       onToggled: host.wirelessEnabled = !host.wirelessEnabled
//       detailTitle: qsTr("Wi-Fi")
//       detailContent: Component { NetworkList { host: host } }
//   }
//
// The `active` state is the tile's own truth, driven by the service it
// binds to. Tiles are deliberately NOT self-latching: `toggled` asks the
// service to change, and `active` follows when the service echoes the new
// state back. A tile that latched locally would show a state the system
// never reached whenever a request failed or was overridden elsewhere.

// Kirigami.Icon draws its own fallback glyph for a name the icon theme
// cannot resolve, so a missing icon never yields an invisible tile. Same
// reasoning as BarIconButton.

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root

    // Icon glyph name, resolved by the host's icon theme.
    property string iconName: ""
    // Primary line: what this tile controls ("Wi-Fi", "Bluetooth").
    property string label: ""
    // Secondary line: the live readout ("HomeNet", "Off", "62%"). Empty
    // collapses the line rather than reserving blank space, so a tile with
    // nothing to report stays visually simple instead of looking broken.
    property string sublabel: ""
    // Whether the thing this tile controls is currently on. Drives the
    // filled/outlined treatment.
    property bool active: false
    // Set false when the underlying service is missing or the hardware is
    // absent. An unavailable tile stays visible but inert, so the grid does
    // not reflow as services come and go (a tile that vanished mid-session
    // would move every tile after it under the user's cursor).
    property bool available: true
    // The detail view this tile drills into: a title for the panel header
    // and a Component for its body. The host reads both when the chevron is
    // pressed, so the tile owns what its detail view contains without
    // knowing how the panel is presented.
    property string detailTitle: ""
    property Component detailContent: null
    // Whether this tile has a detail view worth expanding to. Drives the
    // chevron affordance.
    //
    // Derived, not set: a tile that offers the chevron without supplying
    // content sends the user to a blank panel they have to back out of, so
    // the affordance appears exactly when there is something behind it. A
    // tile can still force it off while its content is unavailable.
    property bool detailEnabled: true
    readonly property bool hasDetail: root.detailEnabled && root.detailContent !== null
    // Layout hint read by ControlCenter. A toggle occupies one cell; the
    // continuous SliderTile overrides this to take the full row.
    readonly property bool spansRow: false

    // The primary action: the user asked to flip this control.
    signal toggled
    // The user asked for this tile's detail view.
    signal detailRequested

    implicitWidth: 160
    // A FLOOR, not a fixed size. 72 is the design height, but the label and
    // readout are text: at a larger font scale the content outgrows it and
    // the tile would clip, taking the grid's row height with it.
    implicitHeight: Math.max(72, content.implicitHeight + 2 * Tokens.spacing_m)
    // An unavailable tile reads as dimmed rather than absent. Item.enabled
    // does not suppress pointer handlers, so every handler below is gated
    // on `available` explicitly.
    opacity: root.available ? 1 : StateLayer.disabled_content

    // Deliberately NOT gated on `available`. Dropping out of the tab order
    // when a service disappears would shift focus order under the user,
    // which is the instability the fixed-position layout above exists to
    // avoid. The tile stays reachable and announceable; `_activate` and
    // `_activateFromKey` already refuse when it is unavailable.
    activeFocusOnTab: true

    Accessible.role: Accessible.Button
    Accessible.name: root.label
    // Availability is part of what a screen reader must convey: without it
    // an unavailable tile announces identically to a working one.
    Accessible.description: root.available ? root.sublabel : (root.sublabel.length > 0 ? qsTr("%1 — unavailable").arg(root.sublabel) : qsTr("Unavailable"))
    Accessible.onPressAction: root._activate()

    Keys.onSpacePressed: event => root._activateFromKey(event)
    Keys.onReturnPressed: event => root._activateFromKey(event)
    Keys.onEnterPressed: event => root._activateFromKey(event)

    function _activate(): void {
        if (root.available)
            root.toggled();
    }

    function _activateFromKey(event: var): void {
        // Autorepeat guard: a held key must not flip a control twice.
        // Matches PowerTile's guard, for the same reason.
        if (event.isAutoRepeat)
            return;
        if (!root.available)
            return;
        ripple.start(root.width / 2, root.height / 2);
        root.toggled();
        // Accept, so a handled activation does not also propagate to an
        // ancestor Keys handler and fire twice.
        event.accepted = true;
    }

    Rectangle {
        id: surface

        anchors.fill: parent
        radius: Tokens.radius_l
        // Filled when on, outlined when off: the M3 treatment for a
        // toggle, and it survives a palette swap because both states are
        // token-derived rather than a tint of one another.
        color: root.active ? Theme.primary : Theme.surface_container_high
        border.width: root.active ? 0 : 1
        border.color: Theme.outline_variant

        readonly property color contentColor: root.active ? Theme.on_primary : Theme.on_surface

        Behavior on color {
            ColorAnimation {
                duration: Motion.duration_short_3
                easing: Motion.standard
            }
        }

        HoverHandler {
            enabled: root.available
            cursorShape: Qt.PointingHandCursor
        }

        RowLayout {
            id: content

            anchors.fill: parent
            anchors.margins: Tokens.spacing_m
            // Reserve the chevron's lane so a long readout elides before it
            // runs under the affordance instead of colliding with it.
            anchors.rightMargin: root.hasDetail ? Tokens.spacing_m + chevron.width + Tokens.spacing_s : Tokens.spacing_m
            spacing: Tokens.spacing_m

            Kirigami.Icon {
                source: root.iconName
                color: surface.contentColor
                implicitWidth: 22
                implicitHeight: 22
                Layout.alignment: Qt.AlignVCenter
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Text {
                    // Folded into the root's Accessible.name already, so
                    // without this assistive tech reads the composed name
                    // and then re-reads this fragment. Same reason as the
                    // bar's Clock.
                    Accessible.ignored: true
                    text: root.label
                    color: surface.contentColor
                    font.family: Tokens.font_family
                    font.pixelSize: Tokens.font_size_label_l
                    font.weight: Tokens.font_weight_medium
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    Accessible.ignored: true
                    text: root.sublabel
                    color: surface.contentColor
                    opacity: StateLayer.secondary_content
                    font.family: Tokens.font_family
                    font.pixelSize: Tokens.font_size_body_s
                    elide: Text.ElideRight
                    visible: root.sublabel !== ""
                    Layout.fillWidth: true
                }
            }
        }

        // The whole tile face toggles. The detail chevron sits above it in
        // the stacking order and claims its own presses, so a tap on the
        // chevron expands rather than flipping the control.
        PhosphorRipple {
            id: ripple

            anchors.fill: parent
            radius: surface.radius
            rippleColor: surface.contentColor
            interactive: root.available
            focused: root.activeFocus
            onTapped: root.toggled()
        }

        // The glyph is 16px but the target is 28, matching DetailPanel's back
        // button. A 16px hit area is below any usable pointer minimum, and
        // the two are the same class of navigation affordance.
        Item {
            id: chevron

            anchors.right: parent.right
            anchors.rightMargin: Tokens.spacing_s
            anchors.verticalCenter: parent.verticalCenter
            width: 28
            height: 28
            visible: root.hasDetail

            Kirigami.Icon {
                anchors.centerIn: parent
                width: 16
                height: 16
                // Mirrored under a right-to-left layout, where "forward" is
                // the other way; anchors mirror on their own, a glyph does not.
                source: root.LayoutMirroring.enabled ? "go-previous-symbolic" : "go-next-symbolic"
                color: surface.contentColor
            }

            HoverHandler {
                enabled: root.available && root.hasDetail
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                enabled: root.available && root.hasDetail
                // Exclusive, so a tap here does not ALSO reach the ripple's
                // TapHandler underneath and flip the control the user meant
                // to drill into. Both default to a passive DragThreshold
                // grab, which excludes nobody.
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: root.detailRequested()
            }
        }
    }
}
