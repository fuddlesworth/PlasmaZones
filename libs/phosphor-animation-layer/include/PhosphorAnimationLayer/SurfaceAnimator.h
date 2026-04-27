// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimationLayer/phosphoranimationlayer_export.h>

#include <PhosphorLayer/ISurfaceAnimator.h>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>

namespace PhosphorAnimation {
class PhosphorProfileRegistry;
}

namespace PhosphorLayer {
class Role;
}

QT_BEGIN_NAMESPACE
class QQuickItem;
QT_END_NAMESPACE

namespace PhosphorAnimationLayer {

/**
 * @brief Concrete `PhosphorLayer::ISurfaceAnimator` driving show/hide via
 *        phosphor-animation Profiles.
 *
 * Phase 5 of the phosphor-animation roadmap. Replaces every overlay's
 * hand-rolled QML `ParallelAnimation { id: showAnimation }` block with a
 * single library-driven runtime that resolves curve + duration from
 * `PhosphorProfileRegistry`. Live profile reloads (drop a JSON, see it
 * apply on next show) flow through the registry's existing watcher path —
 * the animator just re-resolves on every `beginShow` / `beginHide`.
 *
 * ## Per-role configuration
 *
 * The animator carries a default `Config` plus a per-`Role` override map.
 * Consumers register configs at construction:
 *
 * @code
 *     SurfaceAnimator anim(registry);
 *     anim.registerConfigForRole(PzRoles::LayoutOsd,
 *         {.showProfile  = QStringLiteral("osd.show"),
 *          .showScaleProfile = QStringLiteral("osd.pop"),
 *          .showScaleFrom = 0.8,
 *          .hideProfile  = QStringLiteral("osd.hide"),
 *          .hideScaleTo  = 0.9});
 * @endcode
 *
 * Each surface's role is read from `surface->config().role` at dispatch
 * time. Surfaces whose role has no registered override use `defaultConfig`.
 *
 * ## Animations
 *
 * `beginShow` / `beginHide` drive the QQuickItem's `opacity` property
 * (always, 0↔1) and optionally its `scale` property (when a scale-profile
 * is configured). Both run in parallel against the same QtQuickClock so
 * frame timing aligns. Internally each property animation is a
 * `PhosphorAnimation::AnimatedValue<qreal>` that ticks via the per-window
 * `QtQuickClockManager` clock.
 *
 * ## Cancellation & supersession
 *
 * `cancel(surface)` stops in-flight animations for that surface only.
 * The Phase-5.1 dispatch in Surface calls `cancel` on every
 * Hidden→Shown / Shown→Hidden transition that supersedes a still-
 * running animation, so the SurfaceAnimator does not have to detect
 * overlap itself — the phosphor-layer side guarantees a clean cancel
 * point before each `beginShow` / `beginHide`.
 *
 * ## Lifetime
 *
 * Heap-allocated by the consumer (typically the daemon's OverlayService)
 * and passed to `SurfaceFactory::Deps::animator`. Must outlive every
 * Surface created from that factory; the destructor cancels any
 * still-tracked surface state but does NOT cascade-delete the surfaces
 * themselves.
 *
 * ## Thread safety
 *
 * GUI-thread only — every method touches QQuickItem state, and the
 * underlying `AnimatedValue<T>` runtime asserts the same. The ctor /
 * `registerConfigForRole` may run from any thread, but in practice they
 * always run during process / OverlayService construction.
 */
class PHOSPHORANIMATIONLAYER_EXPORT SurfaceAnimator : public PhosphorLayer::ISurfaceAnimator
{
public:
    /**
     * @brief Per-role profile + scale tuning bundle.
     *
     * `showProfile` / `hideProfile` are *Profile path strings* (e.g.
     * "osd.show", "panel.popup"). They're resolved live through the
     * registry on every dispatch so a settings-time profile reload
     * affects the next animation.
     *
     * `showScaleProfile` left empty disables the scale leg of the show
     * animation — opacity-only fade-in. Set non-empty to enable a parallel
     * scale animation from `showScaleFrom` → 1.0 driven by that profile's
     * curve / duration. The same convention applies to hide:
     * `hideScaleTo == 1.0` (the default) disables the hide-side scale
     * animation.
     *
     * Profile resolution failures (typo, missing JSON) fall back to the
     * library default Profile (150 ms OutCubic) — same fallback as
     * `PhosphorMotionAnimation` in the QML bindings.
     */
    struct Config
    {
        QString showProfile; ///< Path resolved via registry, e.g. "osd.show"
        QString hideProfile; ///< Path resolved via registry, e.g. "osd.hide"
        QString showScaleProfile; ///< Optional. Empty → no scale animation on show.
        QString hideScaleProfile; ///< Optional. Empty → no scale animation on hide.
        qreal showScaleFrom = 1.0; ///< Initial scale value when showScaleProfile is set.
        qreal hideScaleTo = 1.0; ///< Final scale value when hideScaleProfile is set.
    };

    /// Construct against an explicit registry with caller-supplied
    /// defaults. Used by tests that want a scoped registry, and by
    /// production code that wants explicit control. No default args —
    /// the unity-build paths confused the overload resolution between
    /// the registry-taking and singleton-taking ctors when either had a
    /// default Config parameter.
    explicit SurfaceAnimator(PhosphorAnimation::PhosphorProfileRegistry& registry, Config defaults);

    /// Default ctor: bind to the process-wide singleton with empty default
    /// config.
    SurfaceAnimator();

    /// Convenience: singleton registry + caller-supplied defaults.
    explicit SurfaceAnimator(Config defaults);

    ~SurfaceAnimator() override;
    SurfaceAnimator(const SurfaceAnimator&) = delete;
    SurfaceAnimator& operator=(const SurfaceAnimator&) = delete;

    /// Override the configuration for one Role. Roles inherit their
    /// `scopePrefix` as the lookup key (so two `Role` instances with the
    /// same prefix share the same config — matches how PlasmaZones treats
    /// `PzRoles::LayoutOsd` as a singleton across factory calls).
    void registerConfigForRole(const PhosphorLayer::Role& role, Config cfg);

    /// Read-only config lookup. Returns the registered config for @p role
    /// if one was set, otherwise `defaultConfig`.
    Config configForRole(const PhosphorLayer::Role& role) const;

    /// Mutable access to the default (unregistered-role fallback). Useful
    /// when the consumer wants to bind every overlay's default behaviour
    /// without enumerating roles.
    void setDefaultConfig(Config cfg);
    Config defaultConfig() const;

    /// @name ISurfaceAnimator
    /// @{
    void beginShow(PhosphorLayer::Surface* surface, QQuickItem* rootItem, CompletionCallback onComplete) override;
    void beginHide(PhosphorLayer::Surface* surface, QQuickItem* rootItem, CompletionCallback onComplete) override;
    void cancel(PhosphorLayer::Surface* surface) override;
    /// @}

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorAnimationLayer
