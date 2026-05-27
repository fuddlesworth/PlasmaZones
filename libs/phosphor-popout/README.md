<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-popout

> Centralised popout coordinator for Phosphor shells. Owns the single
> arbiter for popout lifetime, focus, and exclusivity policy across
> every transient surface in the shell.

## Responsibility

A desktop shell has many transient surfaces: control center, launcher,
calendar, notification history, OSDs, tray menus. Each one needs a
Wayland layer-shell surface, keyboard-focus arbitration, screen
affinity, exclusive-zone handling, dismiss-on-focus-loss behaviour, and
open/close animations. Letting each surface reinvent that machinery
produces the "two popups fighting over a Wayland grab" bug class.
`phosphor-popout` is the one place every popout request flows through
so policy is resolved consistently.

- **Single arbiter, multiple transports.** The arbitration state machine
  lives in `PopoutController`. The actual layer-shell surface creation
  is behind `IPopoutTransport`. Production wires the transport to
  `phosphor-layer::SurfaceFactory`. Tests inject a fake transport and
  drive the controller as pure logic with no Wayland in sight.
- **Three exclusivity modes.** `Cooperative` is the default: one per
  scope, opening a new one swaps the previous. `Modal` suppresses every
  Cooperative across every scope while it's open. `Detached` floats
  independent of both. The state machine treats them uniformly through
  one `open()` entry point.
- **Stable handle vocabulary.** Every successful open returns an opaque
  handle. The handle is what `close()` consumes. Logical identity is
  separate, via `popoutId`, so `toggle("control-center")` does the right
  thing regardless of which handle the existing instance has. The
  controller never leaks transport-specific identifiers to QML or to
  consumers further up the stack.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorPopout::PopoutRequest`     | Value type. Describes a popout to open: id, content, screen, anchor, exclusivity, scope, focus flags, props |
| `PhosphorPopout::ExclusiveMode`     | `Cooperative` / `Modal` / `Detached` arbitration modes |
| `PhosphorPopout::Anchor`            | Where on the screen the popout anchors: `BarLeft`, `BarCenter`, `BarRight`, `ScreenCenter`, `AtPointer`, `Custom` |
| `PhosphorPopout::IPopoutService`    | Abstract service. Methods are `open()`, `close()`, `toggle()`, `isOpen()`, `closeAll()` |
| `PhosphorPopout::IPopoutTransport`  | Transport seam. Implementations create / tear down / track layer-shell surfaces. Notifies the controller on self-dismiss |
| `PhosphorPopout::PopoutController`  | Concrete `IPopoutService`. Owns the arbitration state machine and delegates surface creation to an injected transport |

## Typical use

**C++: wire a controller to a transport and route a request.**

```cpp
#include <PhosphorPopout/PhosphorPopout.h>

using namespace PhosphorPopout;

MyLayerShellTransport transport(...);             // implements IPopoutTransport
PopoutController controller(&transport);

QObject::connect(&controller, &PopoutController::popoutOpened,
                 [](const QString& id, const QString& handle) {
                     qDebug() << "opened" << id << "->" << handle;
                 });

PopoutRequest req;
req.popoutId = QStringLiteral("control-center");
req.content = controlCenterComponent;
req.anchor = Anchor::BarRight;
req.exclusive = ExclusiveMode::Cooperative;
const QString handle = controller.open(req);     // empty if rejected
```

**QML: toggle from a button.** `PopoutController` is registered as a
QML element. Bind a shared instance via `qmlRegisterSingletonInstance`
in your engine setup so the whole UI sees the same arbiter.

```qml
import Phosphor.Popout

ToolButton {
    text: qsTr("Toggle calendar")
    onClicked: popouts.toggle(calendarRequest)

    PopoutRequest {
        id: calendarRequest
        popoutId: "calendar"
        content: calendarComponent
        anchor: PopoutController.BarCenter
        exclusive: PopoutController.Cooperative
    }
}
```

## Arbitration

- **Cooperative per scope.** A second `Cooperative` request in the same
  scope closes the prior one before opening the new one. Different
  scopes are independent. The default scope is `"default"`. Shells that
  want one cooperative popout per output set `scope = "screen-DP-1"`.
- **Modal suppresses every Cooperative.** Opening a `Modal` request
  closes every Cooperative across every scope. While any Modal is open,
  new Cooperative requests are rejected (open returns empty string).
  Modals stack: a second Modal does not close the first. Cooperative
  popouts are NOT restored when the Modal closes; the user demanded
  the Modal, and reopening their prior popout would clobber whatever
  they focused after dismissing it.
- **Detached ignores arbitration.** Detached popouts open, stay open
  across cooperative-swap and modal-open events, and only close when
  explicitly closed or when their underlying surface dismisses itself.

## Dependencies

- `QtCore`, `QtGui`, `QtQml`. Zero Phosphor deps. This is a leaf
  library at the policy layer. The default layer-shell transport lives
  in a separate target so consumers that only need the arbitration
  logic don't pull the Wayland stack in.

## See also

- `libs/phosphor-layer/`, the policy layer over `wlr-layer-shell` that
  the default transport will consume.
- `docs/phosphor-shell-design/03-component-map.md`, the section
  *PopoutService, central popout coordinator* describes the design
  context this library implements.
