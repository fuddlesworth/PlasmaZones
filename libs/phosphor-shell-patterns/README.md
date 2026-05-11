<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-shell-patterns

> UI-pattern recipes on top of `PhosphorLayer::Role`. The axis-2
> vocabulary between wlr-layer-shell wire primitives (axis 1, in
> phosphor-layer) and consumer-side application roles (axis 3, e.g.
> PZ's `PzRoles`).

## Responsibility

A `Role` in [`phosphor-layer`](../phosphor-layer/README.md) is a bundle
of wlr-layer-shell parameters: `Layer`, `Anchors`, exclusive zone,
keyboard interactivity, scope prefix. That's intentionally
domain-agnostic. Every Qt-based Wayland shell needs those.

`phosphor-shell-patterns` sits one level up. It names the *UI patterns*
a shell wants ("a wallpaper covers the background; a panel reserves an
edge; a modal grabs the keyboard; a toast appears in a corner") as
ready-to-use `Role` values, so consumers compose their public roles
from named recipes instead of re-deriving the layer/anchor/keyboard
combo each time.

Any Phosphor shell links this library to get the named patterns. PZ
today does, Phosphor-as-standalone tomorrow will.

## Vocabulary

| Pattern | Signature | Purpose |
|---------|-----------|---------|
| `Wallpaper()`           | `const Role&` | Background layer, all anchors, exclusive zone 0, kbd None |
| `Hud()`                 | `const Role&` | Overlay layer, all anchors, click-through, no exclusive zone |
| `Modal()`               | `const Role&` | Top layer, no anchors (compositor centres), exclusive kbd grab |
| `Floating()`            | `const Role&` | Overlay layer, no anchors, no kbd. Consumer positions via margins |
| `Panel(Edge)`           | `Role`        | Edge-anchored, reserves space via exclusive zone, kbd OnDemand |
| `Toast(Corner)`         | `Role`        | Corner-anchored, click-through, no exclusive zone |

`Edge` is `Top | Bottom | Left | Right`; `Corner` is `TopLeft |
TopRight | BottomLeft | BottomRight`.

The four fixed presets are exposed as accessor functions that return a
reference to a function-local static (Meyers singleton). First call
constructs the Role lazily; subsequent calls return the same object.
This makes the presets safe to use from any consumer's static
initializer regardless of dynamic-init order across translation units
or shared libraries.

The parameterised `Panel` and `Toast` functions return a fresh `Role`
per call so the scope prefix encodes the edge/corner variant.

## Typical use

```cpp
#include <PhosphorShellPatterns/Patterns.h>
#include <PhosphorLayer/Role.h>

namespace PSP = PhosphorShellPatterns;

// Compose an app-specific role from a pattern plus a scope prefix
// that identifies the consumer:
inline const PhosphorLayer::Role MyAppOverlay =
    PSP::Hud().withScopePrefix(QStringLiteral("myapp-overlay"));

// Panels and toasts take a variation parameter:
inline const PhosphorLayer::Role MyAppLeftRail =
    PSP::Panel(PSP::Edge::Left).withScopePrefix(QStringLiteral("myapp-left-rail"));

inline const PhosphorLayer::Role MyAppToast =
    PSP::Toast(PSP::Corner::BottomRight).withScopePrefix(QStringLiteral("myapp-toast"));
```

## Design notes

- **Axis separation is the whole point.** The split mirrors the three
  concerns surface taxonomy fuses today: protocol (axis 1, on `Role`),
  UI pattern (axis 2, here), app role (axis 3, consumer-side). A
  consumer that wants to override one axis without disturbing the
  others can target just that one. For example, swap `Hud` for
  `Wallpaper` on a particular consumer role without retyping every
  other field.
- **Patterns are open vocabulary.** Adding a new pattern (e.g. `Card`
  for in-place transient surfaces) costs one entry here and zero
  changes to phosphor-layer. The lib does not aspire to be exhaustive;
  it captures the recipes that are actually reused.
- **No shell-specific words.** "Panel" / "Toast" / "Modal" / "Hud"
  describe what something *is*, not which shell rendered it. Borrowed
  words like "popup" or "notification" come from specific shell
  vocabularies (Plasma) and are intentionally avoided.
- **Scope prefixes are unique per pattern variation.** Each preset and
  factory output carries a distinct scope prefix so compositors can
  namespace surfaces independently. The `test_patterns` suite pins
  this invariant.

## Dependencies

- `QtCore`, `QtGui` (for `QMargins`)
- [`phosphor-layer`](../phosphor-layer/README.md). The `Role` struct
  Patterns produces.

## See also

- [`phosphor-layer`](../phosphor-layer/README.md). wlr-layer-shell
  primitives (axis 1) this library composes.
- [`docs/surface-taxonomy-refactor-plan.md`](../../docs/surface-taxonomy-refactor-plan.md).
  The design split that produced this library.
