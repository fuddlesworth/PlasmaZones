// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/Role.h>
#include <PhosphorLayer/phosphorlayer_export.h>

#include <QMargins>
#include <QString>
#include <QUrl>
#include <QVariantMap>

#include <memory>
#include <optional>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QQuickItem;
class QScreen;
QT_END_NAMESPACE

namespace PhosphorLayer {

/**
 * @brief Immutable per-surface configuration passed to SurfaceFactory::create().
 *
 * Aggregates a Role (protocol-level defaults) with per-instance data: content
 * source, target screen, per-instance overrides of role defaults, optional
 * shared QML engine.
 *
 * Reconfiguration = destroy + recreate. This matches wlr-layer-shell, which
 * forbids most post-show property changes (layer, anchors, output, scope
 * are all immutable per protocol).
 *
 * Exactly one of @ref contentUrl / @ref contentItem must be non-empty.
 */
struct PHOSPHORLAYER_EXPORT SurfaceConfig
{
    /// Protocol-level defaults. Required.
    Role role;

    /// QML root component URL (`qrc:/…` or `file://…`). Mutually exclusive
    /// with @ref contentItem.
    QUrl contentUrl;

    /// Pre-built QQuickItem. Ownership is transferred to the Surface at
    /// construction. Mutually exclusive with @ref contentUrl.
    /// Note: unique_ptr is intentionally non-copyable — SurfaceConfig
    /// containing a contentItem is move-only.
    std::unique_ptr<QQuickItem> contentItem;

    /// Target screen. Nullptr means "let the factory resolve from the
    /// affinity" (typically primary).
    QScreen* screen = nullptr;

    /// Injected into the surface's QML root context. Keys become context
    /// properties visible to the loaded QML.
    QVariantMap contextProperties;

    /// Opt-in shared QQmlEngine. Nullptr (default) → Surface owns its own
    /// engine for full isolation. Non-null → Surface uses the provided
    /// engine; caller retains ownership.
    QQmlEngine* sharedEngine = nullptr;

    // ── Per-instance overrides ──────────────────────────────────────────
    // nullopt = use the role's default. All are applied at surface-creation
    // time; post-show mutation of these fields has no effect (SurfaceConfig
    // is stored const on the Surface).

    std::optional<Layer> layerOverride;
    std::optional<Anchors> anchorsOverride;
    std::optional<int> exclusiveZoneOverride;
    std::optional<KeyboardInteractivity> keyboardOverride;
    std::optional<QMargins> marginsOverride;

    /// Logged in state transitions. Defaults to Role::scopePrefix when empty.
    QString debugName;

    // ── Resolution helpers ──────────────────────────────────────────────

    [[nodiscard]] Layer effectiveLayer() const noexcept
    {
        return layerOverride.value_or(role.layer);
    }
    [[nodiscard]] Anchors effectiveAnchors() const noexcept
    {
        return anchorsOverride.value_or(role.anchors);
    }
    [[nodiscard]] int effectiveExclusiveZone() const noexcept
    {
        return exclusiveZoneOverride.value_or(role.exclusiveZone);
    }
    [[nodiscard]] KeyboardInteractivity effectiveKeyboard() const noexcept
    {
        return keyboardOverride.value_or(role.keyboard);
    }
    [[nodiscard]] QMargins effectiveMargins() const noexcept
    {
        return marginsOverride.value_or(role.defaultMargins);
    }
    [[nodiscard]] QString effectiveDebugName() const
    {
        return debugName.isEmpty() ? role.scopePrefix : debugName;
    }

    // Special members defined out-of-line (in surface.cpp) so QQuickItem
    // need only be forward-declared here — the concrete ~unique_ptr<QQuickItem>
    // call sites live where QQuickItem is complete.
    SurfaceConfig();
    ~SurfaceConfig();
    SurfaceConfig(const SurfaceConfig&) = delete;
    SurfaceConfig& operator=(const SurfaceConfig&) = delete;
    SurfaceConfig(SurfaceConfig&&) noexcept;
    SurfaceConfig& operator=(SurfaceConfig&&) noexcept;
};

} // namespace PhosphorLayer
