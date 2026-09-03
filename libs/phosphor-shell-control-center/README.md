<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# PhosphorShellControlCenter

The `Phosphor.ControlCenter` tile surface: the quick-settings panel a
shell opens from the bar, holding toggles for network, bluetooth, audio,
brightness and the rest. Pure QML, themed through
[`phosphor-theme`](../phosphor-theme/README.md) and built on the
[`phosphor-shell-widgets`](../phosphor-shell-widgets/README.md) atoms.

Phase 4.4 deliverable per
[`docs/phosphor-shell-design/04-implementation-plan.md`](../../docs/phosphor-shell-design/04-implementation-plan.md).

## Responsibility

Own the tile grid, the drill-in detail view, and the routing between
them. The surface knows how a control tile *behaves*; it does not know
what any particular tile controls.

Tiles are supplied through a `provider` backed by a
`Registry<IControlCenterTileFactory>`, so this library links no
`phosphor-service-*` at all. A tile that needs NetworkManager is the
shell's to register, not this library's to know about. Per the factory
contract a null return means "unavailable in this environment" (no
service, no hardware) and is not an error.

## Presentation is the host's choice

Like `OSDHost` and `ToastHost`, `ControlCenter` renders into whatever
item it is parented to and owns no surface of its own. The shell decides
how it appears:

- composed into the bar's `BarCanvas` socket, so it grows out of the bar
  as one continuous painted shape (the connected-corner design), or
- parented into a standalone layer-shell popout opened through
  `PopoutController`.

Neither choice reaches into this module, so switching is a wiring change
in the shell rather than a rewrite here.

## Key types

| Component       | Role                                                                                          |
|-----------------|-----------------------------------------------------------------------------------------------|
| `ControlCenter` | Tile grid + detail routing. Materialises tiles from a provider, owns their teardown, and arbitrates the one-detail-view-at-a-time rule. |
| `Tile`          | Shared tile chrome: icon, label, live readout, filled/outlined active treatment, ripple, and an optional detail chevron. |
| `DetailPanel`   | The drill-in view: titled surface with a back affordance that slides over the grid. Content comes from the tile. |

## Tiles do not self-latch

`Tile.active` is driven by the service the tile binds to, and `toggled`
only *asks* the service to change. A tile that latched locally would show
a state the system never reached whenever a request failed or something
else changed it, so `active` follows the service's echo instead.

## Typical use

```qml
import Phosphor.ControlCenter

ControlCenter {
    anchors.fill: parent
    provider: controlCenterController   // Registry-backed, from the shell
    tileIds: ["network", "bluetooth", "audio", "brightness"]
    columns: 2
}
```

A tile is authored against the shared chrome:

```qml
Tile {
    iconName: "network-wireless"
    label: qsTr("Wi-Fi")
    sublabel: NetworkHost.activeSsid
    active: NetworkHost.wirelessEnabled
    hasDetail: true
    onToggled: NetworkHost.setWirelessEnabled(!active)
}
```

## Tests

`tests/` is a QtQuickTest harness covering the host's registry-agnostic
contract: tile materialisation order, a provider returning null, the
detail-view routing, and the rebuild path. The provider in those tests is
a plain QML object with `createTile(id, parent)`, which is exactly the
seam the shell fills with a registry-backed controller.
