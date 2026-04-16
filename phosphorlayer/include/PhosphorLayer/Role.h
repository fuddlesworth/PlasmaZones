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
 * library-provided @ref Roles presets or define their own.
 *
 * The fluent `withX()` modifiers return copies for composition:
 * @code
 *     inline const Role PzOverlay =
 *         Roles::FullscreenOverlay.withScopePrefix(QStringLiteral("pz-overlay"));
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

    friend bool operator==(const Role& a, const Role& b) = default;
};

// ── Library-provided presets (open vocabulary — extend via `withX()`) ──

namespace Roles {

/// Fullscreen click-through HUD on the Overlay layer. Typical for drag
/// indicators, zone highlights, focus indicators.
PHOSPHORLAYER_EXPORT extern const Role FullscreenOverlay;

/// Horizontal panel anchored to the top edge, reserves space via exclusive
/// zone. Keyboard OnDemand — receives focus when clicked.
PHOSPHORLAYER_EXPORT extern const Role TopPanel;

/// Mirror of TopPanel anchored to the bottom edge.
PHOSPHORLAYER_EXPORT extern const Role BottomPanel;

/// Vertical dock anchored to the left edge.
PHOSPHORLAYER_EXPORT extern const Role LeftDock;

/// Mirror of LeftDock.
PHOSPHORLAYER_EXPORT extern const Role RightDock;

/// Centred modal with exclusive keyboard grab. Typical for settings
/// dialogs, confirmation prompts.
PHOSPHORLAYER_EXPORT extern const Role CenteredModal;

/// Corner-anchored transient display. Typical for notifications, OSDs.
/// Defaults to top-right; combine with `withAnchors()` for other corners.
PHOSPHORLAYER_EXPORT extern const Role CornerToast;

/// Background layer wallpaper. Reserves space (exclusive zone 0) so other
/// surfaces render on top.
PHOSPHORLAYER_EXPORT extern const Role Background;

/// Floating overlay with no anchors — client-positioned via margins or
/// compositor hints. Typical for shader previews, tear-off windows.
PHOSPHORLAYER_EXPORT extern const Role FloatingOverlay;

} // namespace Roles

} // namespace PhosphorLayer

Q_DECLARE_OPERATORS_FOR_FLAGS(PhosphorLayer::Anchors)
