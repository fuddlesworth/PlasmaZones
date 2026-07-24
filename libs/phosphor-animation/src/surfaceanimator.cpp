// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "surfaceanimator_p.h"

#include <PhosphorLayer/Role.h>
#include <PhosphorRendering/ShaderEffect.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/SurfaceConfig.h>

#include <QQuickItem>

#include <chrono>
#include <cstddef>
#include <memory>

namespace PhosphorAnimationLayer {

Q_LOGGING_CATEGORY(lcSurfaceAnimator, "phosphoranimationlayer.surfaceanimator")

// ══════════════════════════════════════════════════════════════════════════
// SurfaceAnimator::Private
// ══════════════════════════════════════════════════════════════════════════

SurfaceAnimator::Private::Private(PhosphorAnimation::PhosphorProfileRegistry& registry,
                                  PhosphorAnimationShaders::AnimationShaderRegistry* shaderRegistry, Config defaults)
    : m_registry(registry)
    , m_shaderRegistry(shaderRegistry)
    , m_defaultConfig(std::move(defaults))
{
    // Pin the member-destruction-order invariant connectSurfaceCleanup
    // depends on. m_driverTimer is the receiver context for every
    // per-surface destroyed-signal lambda we install; those lambdas
    // touch m_pendingReuse and m_tracks. Member dtor order is reverse
    // of declaration order, so m_driverTimer MUST be declared after
    // m_pendingReuse and m_tracks (= destroyed FIRST, severing
    // connections before the maps it accesses tear down). Without
    // this assertion a future maintainer reordering members for
    // readability could silently introduce a UAF on shutdown.
    //
    // `Private` is non-standard-layout (QObject-derived members,
    // std::function callbacks, mixed access specifiers) which makes
    // `offsetof` "conditionally-supported" per [class.mem]/27, so
    // GCC raises -Winvalid-offsetof. Every mainstream compiler
    // (GCC, Clang, MSVC) actually computes the right value here —
    // the warning is a portability hint, not a correctness bug.
    // Suppress locally; the static_assert is what we want, and any
    // compiler that DIDN'T support offsetof on this type would
    // simply fail to compile rather than miscompare.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(Private, m_driverTimer) > offsetof(Private, m_pendingReuse),
                  "m_driverTimer must be declared AFTER m_pendingReuse so connectSurfaceCleanup's "
                  "destroyed-lambda is auto-disconnected before the map it touches dies.");
    static_assert(offsetof(Private, m_driverTimer) > offsetof(Private, m_tracks),
                  "m_driverTimer must be declared AFTER m_tracks so connectSurfaceCleanup's "
                  "destroyed-lambda is auto-disconnected before the map it touches dies.");
#pragma GCC diagnostic pop

    // Driver fires kTickIntervalMs cadence while any track is in
    // flight; tickAll stops the timer when m_tracks empties.
    m_driverTimer.setInterval(std::chrono::milliseconds(kTickIntervalMs));
    m_driverTimer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&m_driverTimer, &QTimer::timeout, [this]() {
        tickAll();
    });
}

/// Look up the per-role config (longest-prefix-match on
/// scopePrefix, fall back to default). Consumers derive per-
/// surface roles via withScopePrefix("base-{screenId}") and
/// register configs against the unsuffixed base, so a bare find()
/// would always miss. Match boundary is '-' or end-of-string so
/// "phosphor-layout" doesn't collide with "phosphor-layout-
/// picker-..." when both are registered.
SurfaceAnimator::Config SurfaceAnimator::Private::configFor(const PhosphorLayer::Role& role) const
{
    const QString& target = role.scopePrefix;
    int bestLen = -1;
    Config bestCfg = m_defaultConfig;
    for (auto it = m_configByRole.constBegin(); it != m_configByRole.constEnd(); ++it) {
        const QString& key = it.key();
        if (key.isEmpty() || !target.startsWith(key)) {
            continue;
        }
        if (target.size() != key.size() && target.at(key.size()) != QLatin1Char('-')) {
            continue;
        }
        if (key.size() > bestLen) {
            bestLen = key.size();
            bestCfg = it.value();
        }
    }
    return bestCfg;
}

// ══════════════════════════════════════════════════════════════════════════
// SurfaceAnimator
// ══════════════════════════════════════════════════════════════════════════

SurfaceAnimator::SurfaceAnimator(PhosphorAnimation::PhosphorProfileRegistry& registry, Config defaults)
    : SurfaceAnimator(registry, nullptr, std::move(defaults))
{
}

SurfaceAnimator::SurfaceAnimator(PhosphorAnimation::PhosphorProfileRegistry& registry,
                                 PhosphorAnimationShaders::AnimationShaderRegistry* shaderRegistry, Config defaults)
    : d(std::make_unique<Private>(registry, shaderRegistry, std::move(defaults)))
{
}

SurfaceAnimator::SurfaceAnimator(PhosphorAnimation::PhosphorProfileRegistry& registry)
    : SurfaceAnimator(registry, Config{})
{
}

SurfaceAnimator::~SurfaceAnimator()
{
    // Stop the timer FIRST so no tick races with the teardown below.
    d->m_driverTimer.stop();

    // Move-out for stable iteration (cancelTracking erases from
    // m_tracks; iterating + erase on unordered_map is fragile).
    // Each onComplete is dropped per the cancellation contract.
    //
    // Skip the park-then-drain dance teardownShaderLeg would do (move
    // shader pieces to m_pendingReuse, then immediately drain it on
    // the next loop): the animator is dying, no future runLeg will
    // reclaim, and parking would also re-fire connectSurfaceCleanup
    // for surfaces whose Qt destroyed-signal connection is already
    // tracked in m_destroyedConnections. Build a transient
    // PendingReuseShader on the stack and route through
    // destroyPendingReuseEntry directly so the deleteLater path is
    // identical to the normal teardown.
    auto leftovers = std::move(d->m_tracks);
    d->m_tracks.clear();
    for (auto& [key, track] : leftovers) {
        (void)key;
        if (track.opacity) {
            track.opacity->cancel();
            d->m_pendingDestroy.push_back(std::move(track.opacity));
        }
        if (track.scale) {
            track.scale->cancel();
            d->m_pendingDestroy.push_back(std::move(track.scale));
        }
        if (track.shaderTime) {
            track.shaderTime->cancel();
            d->m_pendingDestroy.push_back(std::move(track.shaderTime));
        }
        for (const QPointer<QQuickItem>& sibling : track.hiddenSiblings) {
            if (sibling) {
                sibling->setVisible(true);
            }
        }
        if (track.shaderItem) {
            Private::PendingReuseShader transient;
            transient.shaderItem = std::move(track.shaderItem);
            transient.shaderSource = std::move(track.shaderSource);
            transient.shaderAnchor = track.shaderAnchor;
            d->destroyPendingReuseEntry(transient);
        }
    }

    // Drain the reuse stash. Animator is dying; no future runLeg will
    // reclaim. destroyPendingReuseEntry calls deleteLater on each
    // shader item / source so QObject's deferred-delete event-loop
    // catches them rather than leaking past dtor.
    auto reuseLeftovers = std::move(d->m_pendingReuse);
    d->m_pendingReuse.clear();
    for (auto& [_, pending] : reuseLeftovers) {
        d->destroyPendingReuseEntry(pending);
    }

    // Drain the graveyard explicitly — defence-in-depth on top of
    // member-declaration order. Keeps the invariant local to the dtor
    // so a future maintainer who reorders members can't UAF on
    // shutdown.
    d->m_pendingDestroy.clear();
}

void SurfaceAnimator::registerConfigForRole(const PhosphorLayer::Role& role, Config cfg)
{
    d->m_configByRole.insert(role.scopePrefix, std::move(cfg));
}

SurfaceAnimator::Config SurfaceAnimator::configForRole(const PhosphorLayer::Role& role) const
{
    return d->configFor(role);
}

void SurfaceAnimator::setDefaultConfig(Config cfg)
{
    d->m_defaultConfig = std::move(cfg);
}

SurfaceAnimator::Config SurfaceAnimator::defaultConfig() const
{
    return d->m_defaultConfig;
}

void SurfaceAnimator::setAnimationShaderRegistry(PhosphorAnimationShaders::AnimationShaderRegistry* registry)
{
    d->m_shaderRegistry = registry;
}

void SurfaceAnimator::setAudioSpectrum(const QVector<float>& spectrum)
{
    // Cache for newly-attached shader items (seedShaderUniformsAtAttach
    // reads this field) AND push immediately to every active item so
    // the next paint frame picks it up — same eager-dispatch contract
    // as OverlayService::onAudioSpectrumUpdated → writeQmlProperty
    // for the overlay path.
    d->m_audioSpectrum = spectrum;
    for (auto& [_, track] : d->m_tracks) {
        if (track.shaderItem) {
            track.shaderItem->setAudioSpectrum(spectrum);
        }
    }
}

void SurfaceAnimator::beginShow(PhosphorLayer::Surface* surface, QQuickItem* rootItem, CompletionCallback onComplete)
{
    if (!surface) {
        if (onComplete) {
            onComplete();
        }
        return;
    }
    // Global animations toggle off — snap directly to the visible end
    // state and fire completion synchronously. Same contract as the
    // kwin-effect's `m_windowAnimator->isEnabled()` gate; both runtimes
    // honour `Settings::animationsEnabled` identically. Cancel any
    // in-flight track first so a rapid toggle mid-animation can't leave
    // the surface stuck at intermediate opacity.
    if (!d->m_enabled) {
        if (!rootItem) {
            qCWarning(lcSurfaceAnimator)
                << "beginShow on null rootItem with gate off; surface visibility unchanged but onComplete will fire";
        }
        // Salvage any in-flight previous-track onComplete BEFORE
        // cancelTracking drops it. The bare cancellation contract
        // (legCompleted-only firing) is correct for `cancel()` calls,
        // but the gate-off short-circuit here also fires the NEW
        // caller's onComplete synchronously below — leaving the
        // previous caller's callback unfired strands them while a
        // sibling caller gets serviced.
        //
        // Dispatch order: the salvaged previous-track onComplete fires
        // SYNCHRONOUSLY before cancelTracking, then cancelTracking
        // erases the track, then the new caller's onComplete runs
        // synchronously below. Natural completion order (oldest-first);
        // consumers can safely call back into SurfaceAnimator from
        // either callback since neither runs from a dtor — re-entry
        // touches a fully-constructed `*d`. cancelTracking itself is
        // synchronous and does not re-touch `m_tracks` after erasing
        // the entry, so the salvaged callback's potential re-entry
        // (which may mutate `m_tracks`) is bounded: the callback runs
        // AFTER cancelTracking has already erased our `surface` entry,
        // so any new entry the callback inserts is its own to manage.
        if (const auto it = d->m_tracks.find(Private::TrackKey{surface, rootItem}); it != d->m_tracks.end()) {
            if (auto prevOnComplete = std::move(it->second.onComplete)) {
                auto cb = std::move(prevOnComplete);
                d->cancelTrackingFor(surface, rootItem);
                cb();
            } else {
                d->cancelTrackingFor(surface, rootItem);
            }
        } else {
            d->cancelTrackingFor(surface, rootItem);
        }
        if (rootItem) {
            rootItem->setOpacity(1.0);
            rootItem->setScale(1.0);
        }
        if (onComplete) {
            onComplete();
        }
        return;
    }
    const Config cfg = d->configFor(surface->config().role);

    // Always start from fully transparent. Picking up from a mid-hide
    // opacity value causes a ghost frame: the surface is visible at
    // partial opacity with stale geometry while the compositor processes
    // the new layer-shell configure. Starting from 0.0 is visually
    // indistinguishable for the typical 150ms animation and eliminates
    // the flash on rapid re-show (e.g. layout switch during OSD fade-out).
    const qreal fromOpacity = 0.0;

    const qreal toScale = 1.0;
    qreal fromScale = 1.0;
    if (!cfg.showScaleProfile.isEmpty()) {
        const qreal liveScale = rootItem ? rootItem->scale() : 1.0;
        fromScale = (liveScale < toScale) ? liveScale : cfg.showScaleFrom;
    }

    d->runLeg(surface, rootItem, fromOpacity, /*toOpacity=*/1.0, cfg.showProfile, fromScale, toScale,
              cfg.showScaleProfile, cfg.showShaderEffectId, cfg.showShaderProfile, cfg.showShaderParameters,
              std::move(onComplete));
}

void SurfaceAnimator::beginHide(PhosphorLayer::Surface* surface, QQuickItem* rootItem, CompletionCallback onComplete)
{
    if (!surface) {
        if (onComplete) {
            onComplete();
        }
        return;
    }
    if (!d->m_enabled) {
        if (!rootItem) {
            qCWarning(lcSurfaceAnimator)
                << "beginHide on null rootItem with gate off; surface visibility unchanged but onComplete will fire";
        }
        // Salvage previous-track onComplete before cancelTracking drops it.
        // See beginShow's matching block for the rationale (gate-off path
        // fires the new caller's onComplete synchronously below — silently
        // dropping the in-flight caller's callback is the bug, not the
        // cancellation contract).
        //
        // Dispatch order: the salvaged previous-track onComplete fires
        // SYNCHRONOUSLY before cancelTracking, then cancelTracking
        // erases the track, then the new caller's onComplete runs
        // synchronously below. Natural completion order (oldest-first);
        // consumers can safely call back into SurfaceAnimator from
        // either callback since neither runs from a dtor — re-entry
        // touches a fully-constructed `*d`. cancelTracking is fully
        // synchronous and does not re-touch `m_tracks` after erasing
        // the entry, so the salvaged callback's potential re-entry
        // (which may mutate `m_tracks`) is bounded: the callback runs
        // AFTER cancelTracking has already erased our `surface` entry,
        // so any new entry the callback inserts is its own to manage.
        if (const auto it = d->m_tracks.find(Private::TrackKey{surface, rootItem}); it != d->m_tracks.end()) {
            if (auto prevOnComplete = std::move(it->second.onComplete)) {
                auto cb = std::move(prevOnComplete);
                d->cancelTrackingFor(surface, rootItem);
                cb();
            } else {
                d->cancelTrackingFor(surface, rootItem);
            }
        } else {
            d->cancelTrackingFor(surface, rootItem);
        }
        if (rootItem) {
            rootItem->setOpacity(0.0);
            rootItem->setScale(1.0);
        }
        if (onComplete) {
            onComplete();
        }
        return;
    }
    const Config cfg = d->configFor(surface->config().role);
    // Read live opacity so hide-while-showing supersession picks up
    // from the current visible state, not from a hardcoded 1.0.
    const qreal fromOpacity = rootItem ? rootItem->opacity() : 1.0;
    const qreal fromScale = (cfg.hideScaleProfile.isEmpty() || !rootItem) ? 1.0 : rootItem->scale();
    const qreal toScale = cfg.hideScaleProfile.isEmpty() ? 1.0 : cfg.hideScaleTo;
    d->runLeg(surface, rootItem, fromOpacity, /*toOpacity=*/0.0, cfg.hideProfile, fromScale, toScale,
              cfg.hideScaleProfile, cfg.hideShaderEffectId, cfg.hideShaderProfile, cfg.hideShaderParameters,
              std::move(onComplete));
}

void SurfaceAnimator::cancel(PhosphorLayer::Surface* surface)
{
    if (!surface) {
        return;
    }
    d->cancelAllForSurface(surface);
}

void SurfaceAnimator::beginShow(PhosphorLayer::Surface* surface, QQuickItem* rootItem,
                                const PhosphorLayer::Role& configRole, CompletionCallback onComplete)
{
    // Same contract as the no-arg beginShow but the config is resolved
    // from `configRole` instead of `surface->config().role`. Used by
    // the unified PassiveOverlayShell pattern where the surface's role
    // is the shell's (PassiveShell) and per-content motion/shader
    // configs would otherwise homogenise across content types.
    if (!surface) {
        if (onComplete) {
            onComplete();
        }
        return;
    }
    if (!d->m_enabled) {
        if (!rootItem) {
            qCWarning(lcSurfaceAnimator)
                << "beginShow on null rootItem with gate off; surface visibility unchanged but onComplete will fire";
        }
        if (const auto it = d->m_tracks.find(Private::TrackKey{surface, rootItem}); it != d->m_tracks.end()) {
            if (auto prevOnComplete = std::move(it->second.onComplete)) {
                auto cb = std::move(prevOnComplete);
                d->cancelTrackingFor(surface, rootItem);
                cb();
            } else {
                d->cancelTrackingFor(surface, rootItem);
            }
        } else {
            d->cancelTrackingFor(surface, rootItem);
        }
        if (rootItem) {
            rootItem->setOpacity(1.0);
            rootItem->setScale(1.0);
        }
        if (onComplete) {
            onComplete();
        }
        return;
    }
    const Config cfg = d->configFor(configRole);
    const qreal fromOpacity = 0.0;
    const qreal toScale = 1.0;
    qreal fromScale = 1.0;
    if (!cfg.showScaleProfile.isEmpty()) {
        const qreal liveScale = rootItem ? rootItem->scale() : 1.0;
        fromScale = (liveScale < toScale) ? liveScale : cfg.showScaleFrom;
    }
    d->runLeg(surface, rootItem, fromOpacity, /*toOpacity=*/1.0, cfg.showProfile, fromScale, toScale,
              cfg.showScaleProfile, cfg.showShaderEffectId, cfg.showShaderProfile, cfg.showShaderParameters,
              std::move(onComplete));
}

void SurfaceAnimator::beginHide(PhosphorLayer::Surface* surface, QQuickItem* rootItem,
                                const PhosphorLayer::Role& configRole, CompletionCallback onComplete)
{
    if (!surface) {
        if (onComplete) {
            onComplete();
        }
        return;
    }
    if (!d->m_enabled) {
        if (!rootItem) {
            qCWarning(lcSurfaceAnimator)
                << "beginHide on null rootItem with gate off; surface visibility unchanged but onComplete will fire";
        }
        if (const auto it = d->m_tracks.find(Private::TrackKey{surface, rootItem}); it != d->m_tracks.end()) {
            if (auto prevOnComplete = std::move(it->second.onComplete)) {
                auto cb = std::move(prevOnComplete);
                d->cancelTrackingFor(surface, rootItem);
                cb();
            } else {
                d->cancelTrackingFor(surface, rootItem);
            }
        } else {
            d->cancelTrackingFor(surface, rootItem);
        }
        if (rootItem) {
            rootItem->setOpacity(0.0);
            rootItem->setScale(1.0);
        }
        if (onComplete) {
            onComplete();
        }
        return;
    }
    const Config cfg = d->configFor(configRole);
    const qreal fromOpacity = rootItem ? rootItem->opacity() : 1.0;
    const qreal fromScale = (cfg.hideScaleProfile.isEmpty() || !rootItem) ? 1.0 : rootItem->scale();
    const qreal toScale = cfg.hideScaleProfile.isEmpty() ? 1.0 : cfg.hideScaleTo;
    d->runLeg(surface, rootItem, fromOpacity, /*toOpacity=*/0.0, cfg.hideProfile, fromScale, toScale,
              cfg.hideScaleProfile, cfg.hideShaderEffectId, cfg.hideShaderProfile, cfg.hideShaderParameters,
              std::move(onComplete));
}

void SurfaceAnimator::setEnabled(bool enabled)
{
    // Skip the assignment when the gate value hasn't changed — settings
    // sweeps re-push every gate on every save, and there's no point
    // touching the member for a no-op write.
    if (d->m_enabled == enabled) {
        return;
    }
    d->m_enabled = enabled;
    // Gate semantics are "next dispatch", not "kill in progress" — the
    // toggle itself does not touch any tracks. A track that is currently
    // mid-leg will continue to its natural `legCompleted` (firing its
    // stored onComplete normally) ONLY if no subsequent
    // `beginShow` / `beginHide` arrives in the meantime. If a new
    // dispatch does arrive while the gate is off, that dispatch
    // short-circuits via the gate-off branch in beginShow/beginHide,
    // which calls `cancelTracking` on the in-flight track. Per the
    // cancellation contract documented on `cancelTracking`,
    // legCompleted does not fire on cancellation, so the in-flight
    // track's onComplete would be dropped — the gate-off branch
    // therefore salvages that callback and dispatches it synchronously
    // (after cancelTracking has already erased the entry) before
    // running the new caller's onComplete, so callers are not stranded
    // when their in-flight animation is preempted by a gate-off
    // dispatch.
    //
    // Parity with kwin-effect: `WindowAnimator::setEnabled` (see
    // kwin-effect/windowanimator.cpp) is also a pure flag flip; it does
    // not actively cancel in-flight window animations on the compositor
    // path. Both runtimes therefore share "next dispatch" semantics for
    // the global animation gate.
}

bool SurfaceAnimator::isEnabled() const
{
    return d->m_enabled;
}

} // namespace PhosphorAnimationLayer
