<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-popout

> Centralised popout coordinator for Phosphor shells. Owns the single
> arbiter for popout lifetime, focus, and exclusivity policy across
> every transient surface in the shell.

## Responsibility

A desktop shell has many transient surfaces. Examples include the
control center, launcher, calendar, notification history, OSDs, and
tray menus. Each one needs a Wayland layer-shell surface, keyboard-
focus arbitration, screen affinity, exclusive-zone handling, a
dismiss-on-focus-loss behaviour, and open/close animations. Letting
each surface reinvent that machinery produces the "two popups
fighting over a Wayland grab" bug class. `phosphor-popout` is the
one place every popout request flows through so policy is resolved
consistently.

- **Single arbiter, multiple transports.** The arbitration state
  machine lives in `PopoutController`. The actual layer-shell surface
  creation is behind `IPopoutTransport`. A future `LayerPopoutTransport`
  will wire to `phosphor-layer::SurfaceFactory` once the shell binary
  needs popouts. The in-app demo transport ships today as the
  acceptance harness. Tests inject a fake transport and drive the
  controller as pure logic with no Wayland in sight.
- **Three exclusivity modes.** Cooperative is the default. One per
  scope. Opening a new one swaps the previous. Modal suppresses every
  Cooperative across every scope while it's open. Detached floats
  independent of both. The state machine treats them uniformly through
  one `open` entry point.
- **Stable handle vocabulary.** Every successful open returns an
  opaque handle. The handle is what `close` consumes. Logical
  identity is separate via `popoutId`. `toggle("control-center")`
  does the right thing regardless of which handle the existing
  instance has. The controller never leaks transport-specific
  identifiers to QML or to consumers further up the stack.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorPopout::PopoutRequest`     | Value type. Q_GADGET, registered as QML value type `popoutRequest`. Describes a popout to open: id, content, screen, anchor, exclusivity, scope, focus flags, props |
| `PhosphorPopout::ExclusiveMode`     | `Cooperative` / `Modal` / `Detached` arbitration modes. Q_ENUM_NS, reachable from QML as `ExclusiveMode.Cooperative` etc. |
| `PhosphorPopout::Anchor`            | Where on the screen the popout anchors. Values are `BarLeft`, `BarCenter`, `BarRight`, `ScreenCenter`, `AtPointer`, `Custom`. Q_ENUM_NS |
| `PhosphorPopout::IPopoutService`    | Abstract service. Methods are `open`, `close`, `toggle`, `isOpen`, `closeAll` |
| `PhosphorPopout::IPopoutTransport`  | Transport seam. Implementations create, tear down, and track layer-shell surfaces. Notifies the controller on self-dismiss |
| `PhosphorPopout::PopoutController`  | Concrete `IPopoutService`. Owns the arbitration state machine and delegates surface creation to an injected transport. Registered as a QML element but marked uncreatable. Instantiate from C++ and publish via `qmlRegisterSingletonInstance` |
| `Phosphor.Popout.PopoutHost` (QML)  | Transport-agnostic wrapper for popout content. Owns the open/close opacity-plus-scale animation, the backdrop dim, and the click-outside dismiss. Both transports instantiate this host wrapping the content delegate |

### `PopoutHost.qml` properties and signals

| Member | Direction | Purpose |
|--------|-----------|---------|
| `contentComponent: Component` | property | A QML Component the host instantiates inline via a Loader. Mutually exclusive with `contentItem` |
| `contentItem: Item`           | property | A pre-built Item the host reparents under its content frame. Wins over `contentComponent` if both are set |
| `open: bool`                  | property | Drives the show/hide animation. The transport sets this true after the surface is parented and false to begin teardown |
| `dismissOnClickOutside: bool` | property | When true, a click on the backdrop sets `open = false`. The transport wires this from `PopoutRequest.dismissOnFocusLoss` |
| `backdropColor: color`        | property | Background dim color. The transport binds a scrim for Modal, a lighter dim for Cooperative, transparent for Detached |
| `dismiss()`                   | function | Content delegates several levels deep can call this to close the popout without walking the parent chain. The transport injects a `_popoutHost` reference on the content, and the content calls `_popoutHost.dismiss()` |
| `dismissed()`                 | signal   | Emitted after the close animation finishes. Fires once per open-then-close cycle, so transports that reuse a single host across many popouts get a fresh dismissed each time. Also fires once on `Component.onDestruction` if the host had ever been opened and is torn down before the close-animation timer emits dismissed itself, so transport bookkeeping never leaks a handle when a popout is destroyed mid-cycle. A host that is constructed and destroyed without ever having `open` driven true does NOT fire dismissed, so transports that pre-build and discard surfaces don't see a phantom dismissed in that case. The transport's `dismissed` callback fires through here so the bookkeeping lines up with the visual |

## Typical use

**C++. Wire a controller to a transport and route a request.**

```cpp
#include <PhosphorPopout/PhosphorPopout.h>
#include <QObject>

using namespace PhosphorPopout;

MyPopoutTransport transport(...);                 // implements IPopoutTransport
PopoutController controller(&transport);          // transport must outlive the controller

QObject::connect(&controller, &PopoutController::popoutOpened,
                 [](const QString& id, const QString& handle) {
                     qDebug() << "opened" << id << "->" << handle;
                 });

QQmlComponent* controlCenterComponent = ...;      // caller owns; keep alive until popoutClosed

PopoutRequest req;
req.popoutId = QStringLiteral("control-center");
req.content = controlCenterComponent;
req.anchor = Anchor::BarRight;                    // BarLeft / BarCenter / BarRight / ScreenCenter
                                                  // / AtPointer / Custom. Custom uses req.customAnchor.
req.exclusive = ExclusiveMode::Cooperative;
const QString handle = controller.open(req);      // empty if rejected
```

**QML. Toggle from a button.** Register the controller via
`qmlRegisterSingletonInstance` in your engine setup so the whole UI
sees the same arbiter. The example below assumes the singleton is
registered as `Popouts`.

```qml
import QtQuick
import QtQuick.Controls
import Phosphor.Popout

ToolButton {
    text: i18nc("@action:button", "Toggle calendar")

    Component {
        id: calendarComponent
        Rectangle { implicitWidth: 320; implicitHeight: 220; color: "white" }
    }

    onClicked: {
        let req = popoutRequest();
        req.popoutId = "calendar";
        req.content = calendarComponent;
        req.anchor = Anchor.BarCenter;
        req.exclusive = ExclusiveMode.Cooperative;
        Popouts.toggle(req);
    }
}
```

The `popoutRequest` value type, `Anchor`, and `ExclusiveMode` enums
are all visible to QML through the registrations described in the
key-types table above. `popoutRequest()` is the value type's default
constructor. Field assignment uses regular property syntax.

## Arbitration

- **Cooperative per scope.** A second `Cooperative` request in the
  same scope closes the prior one before opening the new one.
  Different scopes are independent. The default scope is `"default"`.
  Shells that want one cooperative popout per output set
  `scope = "screen-DP-1"`.
- **Modal suppresses every Cooperative.** Opening a `Modal` request
  closes every Cooperative across every scope. While any Modal is
  open, new Cooperative requests are rejected. open returns empty
  string. Modals stack. A second Modal does not close the first.
  Cooperative popouts are NOT restored when the Modal closes. The
  user demanded the Modal. Reopening their prior popout would
  clobber whatever they focused after dismissing it.
- **Detached ignores arbitration.** Detached popouts open, stay open
  across cooperative-swap and modal-open events, and only close when
  explicitly closed or when their underlying surface dismisses
  itself.
- **Same-id collision.** A second `open` with a popoutId that is
  already open is rejected regardless of ExclusiveMode. Callers that
  want the new instance must close the old handle first.

## Dependencies

- `QtCore`, `QtGui`, `QtQml`, `QtQuick`. The C++ core links the
  first three. The QML module also links Quick.
- The Qt module `Phosphor.Theme`. PopoutHost.qml binds `Motion`
  durations and easings for its animation timing. The C++ core has
  no Phosphor link-time dependency.
- The QML module target (`PhosphorPopout::PhosphorPopoutQml`) is
  the in-tree linkable artefact. Out-of-tree consumption via
  `find_package(PhosphorPopout)` exposes the C++ core
  (`PhosphorPopout::PhosphorPopout`) only. Installed QML module
  deployment lands together with the layer-shell transport in a
  follow-up. In-tree consumers that link `PhosphorPopout::PhosphorPopoutQml`
  pick up the PhosphorTheme QML plugin transitively, so the runtime
  `import Phosphor.Theme` inside PopoutHost.qml resolves without
  extra wiring at the consumer side.

## See also

- `libs/phosphor-layer/`. The policy layer over `wlr-layer-shell`
  that the default transport will consume.
