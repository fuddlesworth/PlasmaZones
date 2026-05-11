// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/Role.h>
#include <PhosphorLayer/phosphorlayer_export.h>

namespace PhosphorLayer {

/// Shell UI patterns — axis-2 vocabulary on top of the wlr-layer-shell
/// primitives in @ref Role.
///
/// A Pattern is a UI concept ("a panel reserves screen space", "a toast
/// appears in a corner") realised as a @ref Role bundle. Each Pattern is
/// orthogonal to:
///   - axis 1 (compositor primitive: @ref Layer, @ref Anchors, …) — those
///     live on @ref Role.
///   - axis 3 (application role: what the consumer uses it for) — those
///     live in consumer-side role files (e.g. `pz_roles.h`).
///
/// This namespace is the supported surface for consumers; the older
/// @ref Roles namespace is being phased out (see
/// `docs/surface-taxonomy-refactor-plan.md`).
namespace Patterns {

/// Screen edge a @ref Panel anchors to.
enum class Edge : int {
    Top = 0,
    Bottom = 1,
    Left = 2,
    Right = 3,
};

/// Screen corner a @ref Toast anchors to.
enum class Corner : int {
    TopLeft = 0,
    TopRight = 1,
    BottomLeft = 2,
    BottomRight = 3,
};

// ── Pattern presets (single-Role recipes) ──────────────────────────────

/// Wallpaper. Background layer, anchors all edges, exclusive-zone 0 so
/// panels and other layer surfaces render on top. No keyboard.
PHOSPHORLAYER_EXPORT extern const Role& Wallpaper;

/// Heads-up display. Overlay layer, anchors all edges, click-through,
/// no exclusive zone (drawn on top of panels). Typical for drag
/// indicators, zone highlights, focus rings.
PHOSPHORLAYER_EXPORT extern const Role& Hud;

/// Modal dialog. Top layer, no anchors (centred by the compositor),
/// exclusive keyboard grab. Typical for confirmation prompts, pickers.
PHOSPHORLAYER_EXPORT extern const Role& Modal;

/// Free-floating overlay. Overlay layer, no anchors, no keyboard.
/// Position is supplied by the consumer (e.g. shader previews, tear-off
/// windows).
PHOSPHORLAYER_EXPORT extern const Role& Floating;

// ── Pattern factories (parameterised) ──────────────────────────────────

/// Panel anchored to a screen @p edge, reserving space via exclusive
/// zone, keyboard-on-demand (clicking gives focus). One factory replaces
/// the four legacy `TopPanel` / `BottomPanel` / `LeftDock` / `RightDock`
/// presets. The returned @ref Role has the scope prefix
/// `"pl-{edge}-panel"`.
[[nodiscard]] PHOSPHORLAYER_EXPORT Role Panel(Edge edge);

/// Transient corner-anchored display. Click-through, no exclusive zone.
/// Typical for short-lived OSDs, snack-bars, success indicators. The
/// returned @ref Role has the scope prefix `"pl-{corner}-toast"`.
[[nodiscard]] PHOSPHORLAYER_EXPORT Role Toast(Corner corner);

} // namespace Patterns

} // namespace PhosphorLayer
