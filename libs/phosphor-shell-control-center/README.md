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
`Registry<IControlCenterTileFactory>`. Per the factory contract a null
return means "unavailable in this environment" (no service, no hardware)
and is not an error.

The built-in tiles ship here, and they import `Phosphor.Service.*`
modules, but the library still **links** no service: those modules are
registered by the host process before it loads any QML (each service
exposes a hand-rolled `registerQmlTypes()`; see `src/shell/main.cpp`), so
they resolve at runtime. That is the same arrangement the bar's
service-bound widgets use. A host that registers only some services gets
working tiles for those and inert ones for the rest.

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

The choice does decide whether the keyboard works, though. In the bar
socket this surface has no window of its own, and the bar asks for no
keyboard interactivity (`keyboardFocus: PanelWindow.None`, which is
ordinary panel behaviour), so the compositor routes no key to it: the
tiles' Tab order, the detail panel's Escape and every key handler in this
module are inert there. All of it works in a standalone popout. Pointer
interaction is the same either way, and nothing here is reachable only by
keyboard. See the control-center section of
`docs/phosphor-shell-design/04-implementation-plan.md` for what closing
that would cost.

## Key types

| Component       | Role                                                                                          |
|-----------------|-----------------------------------------------------------------------------------------------|
| `ControlCenter` | Tile grid + detail routing. Materialises tiles from a provider, owns their teardown, applies each tile's layout hint, and arbitrates the one-detail-view-at-a-time rule. |
| `Tile`          | Chrome for a **toggle**: icon, label, live readout, filled/outlined active treatment, ripple, and an optional detail chevron. Occupies one cell. |
| `SliderTile`    | Chrome for a **range** (volume, brightness): the same surface with a `PhosphorSlider` and a tappable icon for mute. Spans the full grid width. |
| `DetailPanel`   | The drill-in view: titled surface with a back affordance that slides over the grid. Content comes from the tile. |

### Built-in tiles

| Tile | Binds to | Shape |
|---|---|---|
| `NetworkTile`    | `NetworkHost.wirelessEnabled` (the radio, never NM's global switch) | toggle |
| `BluetoothTile`  | the first `BluetoothAdapter.powered`                                | toggle |
| `IdleTile`       | an injected `IdleService`, holding one inhibition cookie            | toggle |
| `AudioTile`      | the default `PwNode` sink's volume and mute                         | slider |
| `BrightnessTile` | the first Display-kind `BrightnessDevice`                           | slider |

Layout is the host's job: a tile declares `spansRow` and `ControlCenter`
turns that into a column span, so a tile never has to know how many
columns the grid has.

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
    id: root

    // NetworkHost is registered as a creatable type, not a singleton, so
    // the tile declares its own.
    NetworkHost {
        id: host
    }

    iconName: host.wirelessEnabled ? "network-wireless" : "network-wireless-disconnected"
    label: qsTr("Wi-Fi")
    sublabel: host.connectivity === NetworkHost.Full ? qsTr("Connected") : qsTr("Not connected")
    active: host.wirelessEnabled
    // The chevron appears because there is a detail view behind it.
    // `hasDetail` is derived from this, not set.
    detailTitle: qsTr("Wi-Fi")
    detailContent: Component {
        NetworkList {
            host: host
        }
    }
    // No local latch: write the property and let the service's own change
    // signal drive `active` back.
    onToggled: host.wirelessEnabled = !host.wirelessEnabled
}
```

## Tests

`tests/` is a QtQuickTest harness covering the host's registry-agnostic
contract: tile materialisation order, a provider returning null, the
detail-view routing, and the rebuild path. The provider in those tests is
a plain QML object with `createTile(id, parent)`, which is exactly the
seam the shell fills with a registry-backed controller.

## Dependencies

- Qt6 >= 6.6 Core / Gui / Qml / Quick and KF6 Kirigami, for the tile icons.
  This module is QML-only: it builds no C++ library of its own.
- `phosphor-theme` (`Phosphor.Theme`) for tokens, Motion and the state-layer
  opacities; `phosphor-shell-widgets` (`Phosphor.Widgets`) for the slider and
  the ripple. In-tree builds link their QML plugins automatically.
- No service dependency. The tiles bind whichever `Phosphor.Service.*` types
  their host has registered, so this module never links them itself.

## Status

Phase 4.4: in the tree. The host, the shared tile chrome, the slider variant,
the detail panel and five built-in tiles are present, with a QtQuickTest
suite over the host's registry-agnostic contract.

Built only with `-DBUILD_PHOSPHOR_SHELL=ON`, which is off by default.
The acceptance demo is `examples/phosphor-control-center-demo/`, a plain
window that hosts the grid over the real services, so a tile whose service
is missing on the machine reports itself unavailable rather than vanishing.

One thing is deliberately unfinished. `DetailPanel` has a content path, and
a tile supplies its own view through `detailTitle` and `detailContent`, but
none of the five built-ins authors one yet. `hasDetail` is derived from
whether that content exists, so a tile without it shows no chevron rather
than a chevron leading to a blank panel.

The other open item is not this module's to close: the shipped mount is the
bar's socket, whose surface requests no keyboard interactivity, so the tile
and panel key handlers are inert there. See "Presentation is the host's
choice" above.

