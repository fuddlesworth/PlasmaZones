// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/Role.h>
#include <PhosphorLayer/phosphorlayer_export.h>

#include <QMargins>
#include <QSize>
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
 *
 * @note **Pre-1.0 ABI.** SurfaceConfig is a plain aggregate exposed across
 * the DSO boundary. Adding or reordering fields between releases is a
 * binary-incompatible change until the library reaches 1.0 (SOVERSION 0
 * signals this). Consumers that aggregate-init by position must rebuild
 * against each release; use named-member init for forward compatibility.
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

    /// Target screen. Nullptr means "let the factory resolve to the screen
    /// provider's primary()". Consumers that want focus-follows-surface can
    /// pass `IScreenProvider::focused()` here.
    QScreen* screen = nullptr;

    /// Injected into the surface's QML root context. Keys become context
    /// properties visible to the loaded QML as global identifiers.
    QVariantMap contextProperties;

    /// Applied to the QQuickWindow as dynamic properties (via QObject::
    /// setProperty) BEFORE the QML content loads. Lets the QML access
    /// them via the implicit `window.foo` binding. Distinct from
    /// @ref contextProperties: window properties are per-surface (won't
    /// leak between shared-engine surfaces) and are observable through
    /// Qt's property system (QVariant).
    QVariantMap windowProperties;

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

    // ── Lifecycle policy ────────────────────────────────────────────────
    /// When true, Surface::hide() does NOT call QQuickWindow::hide(); the
    /// surface stays Qt-visible and the visual "hidden" state is driven by
    /// the injected ISurfaceAnimator (typically by animating the root QML
    /// item's opacity to 0). The library still flips the underlying
    /// QWindow's `Qt::WindowTransparentForInput` flag during the hide so
    /// the still-mapped layer surface stops intercepting clicks at its
    /// screen position.
    ///
    /// Use this for overlays where reattaching the wl_surface (and its
    /// Vulkan swapchain) on every show/hide cycle is too expensive — the
    /// PlasmaZones layout picker, navigation OSD, and zone selector all
    /// fall into this category. The Phase-4 LayoutOsd "L3 v2" pattern
    /// pioneered this for OSDs; Phase 5 generalises it into the library.
    ///
    /// Default `false` preserves the original lifecycle: hide() unmaps the
    /// window immediately and a future show() pays the reattach cost.
    bool keepMappedOnHide = false;

    /// Initial size committed to the wl_surface during warm-up. Determines
    /// the size of the first Vulkan swapchain Qt allocates.
    ///
    /// When unset (default), warm-up sizes the wrapper QQuickWindow to the
    /// target screen's full geometry. That guarantees a non-zero buffer for
    /// partial-anchor layer surfaces (see the "non-zero size BEFORE
    /// completeCreate" invariant in surface.cpp::instantiateFromComponent),
    /// but it costs a full-screen swapchain (~25 MB at 4K × 3 buffers on
    /// NVIDIA) even when the eventual visible content is a small toast.
    ///
    /// When set, surface.cpp uses this as the warm-up geometry instead.
    /// Subsequent imperative setWidth/setHeight calls still resize the
    /// surface; this only governs the size of the first commit. Pass the
    /// largest size the surface is expected to grow to in practice — that
    /// way Qt avoids the "small swapchain → resize on first show" round
    /// trip on NVIDIA, where swapchain resize is destroy + recreate.
    ///
    /// Must be non-empty when set. Empty is treated as "use the default".
    std::optional<QSize> initialSize;

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
