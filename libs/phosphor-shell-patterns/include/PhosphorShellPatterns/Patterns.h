// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/Role.h>
#include <PhosphorShellPatterns/phosphorshellpatterns_export.h>

/// Shell UI patterns. The axis-2 vocabulary on top of the wlr-layer-shell
/// primitives in PhosphorLayer::Role.
///
/// A Pattern is a UI concept ("a panel reserves screen space", "a toast
/// appears in a corner") realised as a PhosphorLayer::Role bundle. Each
/// Pattern is orthogonal to:
///   - axis 1 (compositor primitive: Layer, Anchors, ...). Those live on
///     @ref PhosphorLayer::Role.
///   - axis 3 (application role: what the consumer uses it for). Those
///     live in consumer-side role files (e.g. Phosphor's `phosphor_roles.h`).
///
/// This library is the seam between protocol primitives and consumer
/// app-roles, so any Phosphor shell (Phosphor today, Phosphor-as-standalone
/// tomorrow) composes its public roles from these recipes without having
/// to know the wlr-layer-shell wire details.
///
/// The four fixed presets (Wallpaper, Hud, Modal, Floating) are exposed
/// as accessor functions returning `const Role&`. Each accessor wraps a
/// Meyers-style function-local static so the Role value is constructed
/// on first access regardless of dynamic-initialization order across
/// translation units. Consumers can safely take the result by reference
/// and store derived Roles in their own `inline const` globals.
namespace PhosphorShellPatterns {

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
[[nodiscard]] PHOSPHORSHELLPATTERNS_EXPORT const PhosphorLayer::Role& Wallpaper();

/// Heads-up display. Overlay layer, anchors all edges, click-through,
/// no exclusive zone (drawn on top of panels). Typical for drag
/// indicators, zone highlights, focus rings.
[[nodiscard]] PHOSPHORSHELLPATTERNS_EXPORT const PhosphorLayer::Role& Hud();

/// Modal dialog. Top layer, no anchors (centred by the compositor),
/// exclusive keyboard grab. Typical for confirmation prompts, pickers.
[[nodiscard]] PHOSPHORSHELLPATTERNS_EXPORT const PhosphorLayer::Role& Modal();

/// Free-floating overlay. Overlay layer, no anchors, no keyboard.
/// Position is supplied by the consumer (e.g. shader previews, tear-off
/// windows).
[[nodiscard]] PHOSPHORSHELLPATTERNS_EXPORT const PhosphorLayer::Role& Floating();

// ── Pattern factories (parameterised) ──────────────────────────────────

/// Panel anchored to a screen @p edge, reserving space via exclusive
/// zone, keyboard-on-demand (clicking gives focus). The returned
/// @ref PhosphorLayer::Role has the scope prefix `"pl-{edge}-panel"`.
[[nodiscard]] PHOSPHORSHELLPATTERNS_EXPORT PhosphorLayer::Role Panel(Edge edge);

/// Transient corner-anchored display. Click-through, no exclusive zone.
/// Typical for short-lived OSDs, snack-bars, success indicators. The
/// returned @ref PhosphorLayer::Role has the scope prefix
/// `"pl-{corner}-toast"`.
[[nodiscard]] PHOSPHORSHELLPATTERNS_EXPORT PhosphorLayer::Role Toast(Corner corner);

} // namespace PhosphorShellPatterns
