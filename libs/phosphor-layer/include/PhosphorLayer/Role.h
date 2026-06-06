// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/phosphorlayer_export.h>

#include <QFlags>
#include <QMargins>
#include <QObject>
#include <QString>

#include <cstdint>

namespace PhosphorLayer {

/// Meta-object context for namespace-level enums so QML and QMetaEnum can
/// stringify them. Registered in role.cpp.
Q_NAMESPACE_EXPORT(PHOSPHORLAYER_EXPORT)

// ── Protocol-level enums (match zwlr_layer_shell_v1 values) ────────────

/// Stacking layer (wlr-layer-shell protocol values).
enum class Layer : int {
    Background = 0, ///< Behind windows (wallpapers)
    Bottom = 1, ///< Above background, below windows (live wallpapers)
    Top = 2, ///< Above windows (panels, docks)
    Overlay = 3, ///< Above everything including fullscreen (HUDs, OSDs)
};
Q_ENUM_NS(Layer)

/// Edge anchors. Combine as flags (a panel anchors Top|Left|Right; a fullscreen
/// surface anchors all four; a centred modal anchors none).
enum class Anchor : std::uint32_t {
    None = 0,
    Top = 1U << 0,
    Bottom = 1U << 1,
    Left = 1U << 2,
    Right = 1U << 3,
};
Q_DECLARE_FLAGS(Anchors, Anchor)
Q_FLAG_NS(Anchors)

// Constructed via QFlags' initializer_list ctor so these expressions don't
// depend on the global-scope operator| from Q_DECLARE_OPERATORS_FOR_FLAGS,
// which is only declared at the bottom of this header.
inline constexpr Anchors AnchorNone = Anchors();
inline constexpr Anchors AnchorAll{Anchor::Top, Anchor::Bottom, Anchor::Left, Anchor::Right};

/// Keyboard focus policy (wlr-layer-shell v4 adds OnDemand).
enum class KeyboardInteractivity : int {
    None = 0, ///< Surface never receives keyboard focus (click-through)
    Exclusive = 1, ///< Surface takes exclusive keyboard focus (modals)
    OnDemand = 2, ///< Surface receives focus when clicked (panels)
};
Q_ENUM_NS(KeyboardInteractivity)

// ── Role: protocol-level configuration bundle ──────────────────────────

/**
 * @brief Value type describing a surface's protocol-level configuration.
 *
 * Role captures the wlr-layer-shell parameters that are immutable after
 * show() (layer, anchors, keyboard, scope) plus defaults for parameters
 * that are mutable (margins, exclusive zone). Consumers pick from the
 * PhosphorShellPatterns library (axis-2 UI-pattern recipes) or define
 * their own Role values directly.
 *
 * The fluent `withX()` modifiers return copies for composition:
 * @code
 *     inline const Role PhosphorOverlay =
 *         PhosphorShellPatterns::Hud().withScopePrefix(
 *             QStringLiteral("phosphor-overlay"));
 * @endcode
 */
struct PHOSPHORLAYER_EXPORT Role
{
    Layer layer = Layer::Overlay;
    Anchors anchors = AnchorNone;
    int exclusiveZone = -1; ///< -1 = ignore other surfaces' zones
    KeyboardInteractivity keyboard = KeyboardInteractivity::None;
    QMargins defaultMargins;
    QString scopePrefix; ///< Namespace for this role's surfaces (wl_surface scope)

    [[nodiscard]] Role withLayer(Layer l) const;
    [[nodiscard]] Role withAnchors(Anchors a) const;
    [[nodiscard]] Role withExclusiveZone(int z) const;
    [[nodiscard]] Role withKeyboard(KeyboardInteractivity k) const;
    [[nodiscard]] Role withMargins(QMargins m) const;
    [[nodiscard]] Role withScopePrefix(QString prefix) const;

    /// @brief True if this Role is a semantically valid wlr-layer-shell configuration.
    /// False for combinations the protocol rejects (e.g. Overlay layer with a
    /// positive exclusive zone, where Overlay ignores zones so a non-negative
    /// value is silently wasted) or that no compositor accepts (empty
    /// scopePrefix). The factory calls this and refuses to create malformed
    /// surfaces.
    [[nodiscard]] bool isValid() const;

    friend bool operator==(const Role& a, const Role& b) = default;
};

// The legacy `Roles` namespace was removed in phase 1 of the surface
// taxonomy refactor, and the axis-2 Patterns vocabulary was lifted into
// its own sibling library `phosphor-shell-patterns` in phase 3. New
// consumers should depend on PhosphorShellPatterns and use
// `PhosphorShellPatterns::{Hud, Modal, Wallpaper, Floating, Panel(Edge),
// Toast(Corner)}` rather than re-deriving Role values inline. See
// `docs/surface-taxonomy-refactor-plan.md` for the migration map.

} // namespace PhosphorLayer

Q_DECLARE_OPERATORS_FOR_FLAGS(PhosphorLayer::Anchors)
