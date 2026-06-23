<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-identity

> Stable, cross-process string formats for the IDs that flow through the
> Phosphor stack: window IDs, screen IDs, and virtual-screen IDs.

## Responsibility

Wayland doesn't expose a reliable numeric window ID the way X11 does, and
each compositor surfaces "which window" differently. KWin scripts use
`internalId`, the daemon's compositor-bridge D-Bus interface carries
string IDs over the wire, and application QObjects own `QWindow` pointers.

`phosphor-identity` owns the wire formats for those identities. It is an
INTERFACE library where every helper is `inline` in the public header, so
all consumers link the same definitions without an extra `.so`.

The three identity formats it owns:

- **`WindowId`** — composite `"appId|instanceId"` strings. Helpers to
  build, parse, and pattern-match against the canonical form.
- **`ScreenId`** — EDID-derived stable identifier with connector-name
  fallback when no EDID is available. Compositor-portable.
- **`VirtualScreenId`** — `"<physicalId>/vs:<index>"` for the
  user-defined sub-regions of a physical monitor.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorIdentity::WindowId`        | Helpers for the canonical `appId|instanceId` window-id format |
| `PhosphorIdentity::ScreenId`        | EDID parsing + screen-id construction, cached across TUs |
| `PhosphorIdentity::VirtualScreenId` | `<physicalId>/vs:<index>` make / parse / detect helpers |

## Typical use

```cpp
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorIdentity/VirtualScreenId.h>

using namespace PhosphorIdentity;

// Build the wire format from its parts
QString id = WindowId::buildCompositeId(QStringLiteral("firefox"),
                                        QStringLiteral("Navigator"));

// Parse back out
QString app  = WindowId::extractAppId(id);       // "firefox"
QString inst = WindowId::extractInstanceId(id);  // "Navigator"

// Virtual screens
QString vs = VirtualScreenId::make(physicalId, /*index*/ 1);
if (VirtualScreenId::isVirtual(screenId)) {
    QString phys = VirtualScreenId::extractPhysicalId(screenId);
}
```

## Design notes

- **INTERFACE library.** No `.so` is shipped. Every function is `inline`
  in the public header. Cross-process consumers all share one definition
  by language rule, and nobody links a third copy.
- **Header-only by design.** The function-local static caches in
  `ScreenId.h` are guaranteed unique across translation units by the
  C++17 inline-function-static rule.
- **Wire format owns the spelling.** Every consumer uses these helpers
  rather than hand-rolling the format. To change the format, change it
  here once.

## Dependencies

- `QtCore` only. Zero Phosphor dependencies. This is the foundation
  layer everything else builds on.
