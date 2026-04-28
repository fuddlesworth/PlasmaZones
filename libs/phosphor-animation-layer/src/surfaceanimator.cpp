// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimationLayer/SurfaceAnimator.h>

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/MotionSpec.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>

#include <PhosphorAnimationShaders/AnimationShaderRegistry.h>
#include <PhosphorRendering/ShaderEffect.h>

#include <PhosphorLayer/Role.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/SurfaceConfig.h>

#include <QHash>
#include <QLoggingCategory>
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <memory>
#include <unordered_map>

namespace PhosphorAnimationLayer {

namespace {
Q_LOGGING_CATEGORY(lcSurfaceAnimator, "phosphoranimationlayer.surfaceanimator")

/// Animation driver tick interval. AnimatedValue::advance() fires on this
/// cadence while any track is in flight; SteadyClock::refreshRate() must
/// agree (1000 / kTickIntervalMs) so velocity-based curves (Spring) sample
/// at the same rate the timer ticks.
constexpr int kTickIntervalMs = 16;
constexpr qreal kRefreshRateHz = 1000.0 / kTickIntervalMs;
} // namespace

namespace internal {
/// Steady-clock-backed IMotionClock. Independent of QQuickWindow rendering
/// (QtQuickClock ticks on `beforeRendering`, which doesn't fire on
/// offscreen QPA — the test platform that headless CI uses). Pairs with
/// the SurfaceAnimator's per-Private QTimer driver: the timer fires
/// AnimatedValue::advance() at ~60Hz and each advance reads `now()` from
/// here.
///
/// Lives in `internal` (not anonymous) namespace so the SurfaceAnimator::
/// Private member type has external linkage — Qt6 / GCC15 emits a
/// `-Wsubobject-linkage` warning otherwise.
class SteadyClock : public PhosphorAnimation::IMotionClock
{
public:
    std::chrono::nanoseconds now() const override
    {
        return std::chrono::steady_clock::now().time_since_epoch();
    }

    qreal refreshRate() const override
    {
        // Must match the QTimer driver cadence; AnimatedValue uses this
        // for velocity-based curve sampling (Spring). Both derived from
        // the single kTickIntervalMs constant in the anonymous namespace.
        return kRefreshRateHz;
    }

    /// requestFrame is a hint — the SurfaceAnimator's QTimer ticks
    /// unconditionally while any track is in flight, so this is a no-op.
    /// AnimatedValue calls requestFrame after every advance to keep the
    /// paint loop ticking; honouring it here would just duplicate the
    /// timer's wake-up.
    void requestFrame() override
    {
    }
};
} // namespace internal

namespace {

/// Resolve a Profile path through the registry, falling back to a library-
/// default Profile (durationOverride applies the library default duration via
/// the Profile's own resolution chain — `withDefaults` fills in unset fields).
///
/// An empty path is the documented "use library defaults" sentinel — silent
/// fallback. A non-empty path that fails to resolve is a typo or a missing
/// profile JSON; surface that as `qCWarning` so it's visible in the journal
/// without enabling debug categories. The QML-side
/// `scripts/check-animation-profiles.py` build-time lint catches typos in
/// QML references, but it can't inspect C++ string literals, so this runtime
/// warning is the backstop for `setupSurfaceAnimator`-style registrations.
PhosphorAnimation::Profile resolveProfile(PhosphorAnimation::PhosphorProfileRegistry& registry, const QString& path)
{
    if (path.isEmpty()) {
        return PhosphorAnimation::Profile{}.withDefaults();
    }
    if (auto p = registry.resolve(path)) {
        return p->withDefaults();
    }
    qCWarning(lcSurfaceAnimator).nospace() << "Profile path '" << path
                                           << "' did not resolve through registry — "
                                              "falling back to library defaults (150 ms OutCubic). "
                                              "Check the profile name for typos and that the "
                                              "corresponding JSON ships under data/profiles/.";
    return PhosphorAnimation::Profile{}.withDefaults();
}

/// Build a MotionSpec<qreal> from a Profile + clock + the two
/// AnimatedValue callbacks. Centralises the construction so beginShow /
/// beginHide read identically.
PhosphorAnimation::MotionSpec<qreal> buildSpec(const PhosphorAnimation::Profile& profile,
                                               PhosphorAnimation::IMotionClock* clock,
                                               std::function<void(const qreal&)> onValueChanged,
                                               std::function<void()> onComplete)
{
    PhosphorAnimation::MotionSpec<qreal> spec;
    spec.profile = profile;
    spec.clock = clock;
    spec.onValueChanged = std::move(onValueChanged);
    spec.onComplete = std::move(onComplete);
    return spec;
}

} // namespace

// ══════════════════════════════════════════════════════════════════════════
// SurfaceAnimator::Private
// ══════════════════════════════════════════════════════════════════════════

class SurfaceAnimator::Private
{
public:
    /// Per-surface in-flight bookkeeping. Each surface tracks at most one
    /// opacity AnimatedValue and one (optional) scale AnimatedValue at a
    /// time — beginShow/beginHide replace any prior pair via cancel(). The
    /// AnimatedValue<qreal>s are held via unique_ptr so the slot can be
    /// nulled (`slot.opacity.reset()`) before installing a fresh value,
    /// without forcing AnimatedValue itself to expose an empty / moved-from
    /// state. unordered_map already permits move-only mapped types; the
    /// indirection is solely for the reset-and-replace semantics
    /// `runLeg` relies on.
    struct Track
    {
        QPointer<QQuickItem> target; ///< Auto-nulls if QML scene tears down mid-flight
        std::unique_ptr<PhosphorAnimation::AnimatedValue<qreal>> opacity;
        std::unique_ptr<PhosphorAnimation::AnimatedValue<qreal>> scale;
        std::unique_ptr<PhosphorAnimation::AnimatedValue<qreal>> shaderTime;
        QPointer<PhosphorRendering::ShaderEffect> shaderItem; ///< Transient child, torn down on completion
        int pendingLegs = 0;
        ISurfaceAnimator::CompletionCallback onComplete;
    };

    Private(PhosphorAnimation::PhosphorProfileRegistry& registry,
            PhosphorAnimationShaders::AnimationShaderRegistry* shaderRegistry, Config defaults)
        : m_registry(registry)
        , m_shaderRegistry(shaderRegistry)
        , m_defaultConfig(std::move(defaults))
    {
        // Animation driver: while any track is in flight, fire on the
        // kTickIntervalMs cadence and call advance() on every AnimatedValue.
        // Stop the timer when m_tracks becomes empty — idle daemons don't
        // keep a 60 Hz wake-up scheduled on the GUI thread.
        m_driverTimer.setInterval(std::chrono::milliseconds(kTickIntervalMs));
        m_driverTimer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&m_driverTimer, &QTimer::timeout, [this]() {
            tickAll();
        });
    }

    /// Cancel any in-flight legs for a surface. Called both directly via
    /// SurfaceAnimator::cancel and implicitly when beginShow/beginHide
    /// supersedes a still-running animation on the same surface.
    ///
    /// AnimatedValue::cancel() clears m_isAnimating + m_isComplete
    /// without firing spec.onComplete (verified at AnimatedValue.h:464);
    /// this matches the ISurfaceAnimator::cancel contract — "the
    /// animator must call onComplete exactly once per
    /// beginShow/beginHide; cancel is an explicitly non-completion
    /// termination".
    ///
    /// ## Lifetime contract — defer destruction
    ///
    /// `cancelTracking` can be reached from inside an AnimatedValue's
    /// own `spec.onValueChanged` callback: a synchronous QML binding
    /// on the target's opacity/scale property fires consumer code that
    /// calls `cancel(surface)`. AnimatedValue.h:547 forbids destroying
    /// `*this` from within a spec callback — `advance()` continues to
    /// access `this` after the callback returns (e.g. the m_isComplete
    /// branch and the velocity guard). Destroying the AnimatedValue
    /// synchronously here would UAF.
    ///
    /// Mirror legCompleted's deferred-destroy: cancel the AnimatedValues
    /// (which sets m_isAnimating=false so any subsequent advance() bails
    /// before any further state mutation) and park them in
    /// m_pendingDestroy. The graveyard drains at the start of the next
    /// `tickAll` — by which point every advance() frame on the call
    /// stack has fully unwound, and destruction is safe.
    ///
    /// The track's `onComplete` is destroyed alongside the tracking entry
    /// (the unordered_map::erase below runs ~Track which destroys the
    /// `std::function`). Dropping the consumer's onComplete on the floor
    /// is the documented semantic for cancellation — cancel is the
    /// non-completion termination path; consumers that need a "settled"
    /// signal (whether by completion or cancellation) must wrap their
    /// callback themselves.
    void cancelTracking(PhosphorLayer::Surface* surface)
    {
        const auto it = m_tracks.find(surface);
        if (it == m_tracks.end()) {
            return;
        }
        // Cancel BEFORE moving so m_isAnimating=false propagates to any
        // advance() frame that might still be on the call stack — the
        // remaining post-callback work in advance() then bails on the
        // m_isAnimating guard rather than mutating cancelled state.
        if (it->second.opacity) {
            it->second.opacity->cancel();
            m_pendingDestroy.push_back(std::move(it->second.opacity));
        }
        if (it->second.scale) {
            it->second.scale->cancel();
            m_pendingDestroy.push_back(std::move(it->second.scale));
        }
        if (it->second.shaderTime) {
            it->second.shaderTime->cancel();
            m_pendingDestroy.push_back(std::move(it->second.shaderTime));
        }
        if (it->second.shaderItem) {
            delete it->second.shaderItem;
        }
        m_tracks.erase(it);
    }

    /// Look up the per-role config or fall back to default.
    ///
    /// Lookup is longest-prefix-match on `Role::scopePrefix` because
    /// PlasmaZones (and other consumers) derive per-surface roles via
    /// `Role::withScopePrefix("base-prefix-{screenId}-{gen}")` to give the
    /// compositor a unique wl_surface scope per instance, while registering
    /// configs against the *base* role (whose prefix is the unsuffixed
    /// "base-prefix"). A bare `find()` would always miss because the keys
    /// don't match exactly.
    ///
    /// Match boundary is `'-'` or end-of-string so a registered key
    /// `"plasmazones-layout"` doesn't accidentally match a surface scoped
    /// `"plasmazones-layout-picker-..."` if both `"plasmazones-layout"` and
    /// `"plasmazones-layout-picker"` are registered. Longest matching key
    /// wins.
    Config configFor(const PhosphorLayer::Role& role) const
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

    /// Run a show or hide leg. Used by beginShow/beginHide which thread
    /// in the from→to opacity/scale pair, the profile pair, and the
    /// caller's onComplete.
    ///
    /// @param surface     Surface key (used in the tracking map).
    /// @param target      QQuickItem whose `opacity` / `scale` we drive.
    /// @param fromOpacity Initial opacity. Set on the item before the
    ///                    first tick so the compositor sees a consistent
    ///                    starting frame.
    /// @param toOpacity   Target opacity.
    /// @param opacityProfilePath Profile to drive the opacity leg.
    /// @param fromScale   Initial scale (1.0 means "skip scale leg").
    /// @param toScale     Target scale.
    /// @param scaleProfilePath  Profile to drive the scale leg (empty
    ///                          means "skip scale leg").
    /// @param onComplete  Caller's completion callback. Fired when ALL
    ///                    legs settle.
    void runLeg(PhosphorLayer::Surface* surface, QQuickItem* target, qreal fromOpacity, qreal toOpacity,
                const QString& opacityProfilePath, qreal fromScale, qreal toScale, const QString& scaleProfilePath,
                const QString& shaderEffectId, ISurfaceAnimator::CompletionCallback onComplete)
    {
        // Supersede any in-flight animation on this surface BEFORE the
        // null-target check. If the caller hands us a null target while a
        // prior animation is still tracked, leaving that entry behind would
        // strand a unique_ptr<AnimatedValue> with a captured `surface` key
        // that no future call clears (the next runLeg with a non-null
        // target would re-cancel it, but a teardown sequence might not).
        // The phosphor-layer side does this too (Surface::Impl::drive()
        // calls animator().cancel before each beginShow / beginHide), but
        // a consumer calling beginShow directly without going through the
        // Surface dispatch (e.g. a custom transition orchestrator) still
        // gets clean supersession.
        cancelTracking(surface);

        if (!target) {
            qCWarning(lcSurfaceAnimator) << "runLeg called with null target — onComplete fires synchronously";
            // Re-entrancy contract for the synchronous-completion path:
            // the caller's onComplete runs on the same call stack as
            // beginShow/beginHide, so anything the SurfaceAnimator's
            // header forbids during a regular legCompleted-fired
            // callback (see SurfaceAnimator.h:79-90 — must not delete
            // the SurfaceAnimator) applies here too. Deleting the
            // SurfaceAnimator from this onComplete would tear down `d`
            // while runLeg's frame is still on the stack — `cancelTracking`
            // above already touched `d`, and a re-entrant beginShow that
            // the consumer's onComplete might dispatch is also unsafe.
            // Deleting the *Surface* whose runLeg this is remains safe
            // (no tracking entry was installed before this branch).
            if (onComplete) {
                onComplete();
            }
            return;
        }

        // Use the shared steady-clock — independent of QQuickWindow
        // rendering. QtQuickClock would tie the animation to
        // beforeRendering signals which don't fire on offscreen QPA
        // (the headless test platform); the SurfaceAnimator's per-tick
        // QTimer drives advance() at 60Hz instead.
        PhosphorAnimation::IMotionClock* clock = &m_clock;

        const bool hasScaleLeg = !scaleProfilePath.isEmpty();
        const bool hasShaderLeg = !shaderEffectId.isEmpty() && m_shaderRegistry;
        const int legCount = 1 + (hasScaleLeg ? 1 : 0) + (hasShaderLeg ? 1 : 0);

        // Install the bookkeeping slot BEFORE the synchronous pre-state
        // writes below. `target->setOpacity(fromOpacity)` /
        // `target->setScale(fromScale)` fire QML opacity-/scale-changed
        // signals synchronously, and a consumer binding observing them
        // can re-enter `cancel(surface)` from the QML side. Without the
        // slot installed, that cancel finds an empty m_tracks (we just
        // erased any prior entry above), silently does nothing, and we
        // then install a fresh slot the consumer wanted gone — losing
        // the cancel intent. With the slot installed first, cancelTracking
        // sees an AV-less entry, removes it cleanly (nothing to park in
        // m_pendingDestroy because we haven't constructed the
        // AnimatedValues yet), and the post-write re-find below detects
        // the erasure and bails.
        {
            Track& slot = m_tracks[surface];
            slot.opacity.reset();
            slot.scale.reset();
            slot.target = target;
            slot.onComplete = std::move(onComplete);
            slot.pendingLegs = legCount;
        }

        // Set the initial frame state synchronously so the compositor
        // doesn't paint a stale opacity for the one frame between
        // beginShow returning and the first AnimatedValue tick.
        target->setOpacity(fromOpacity);
        if (hasScaleLeg) {
            target->setScale(fromScale);
        }

        // Construct both AnimatedValues into the slot atomically BEFORE
        // calling start() on either. start() can synchronously fire
        // onValueChanged → target->setOpacity() → QML binding chain →
        // re-entrant cancel(surface), which would erase the slot. By
        // installing both unique_ptrs up front (instead of constructing
        // the scale leg AFTER opacity->start() returns) we avoid the
        // dangling-reference window that an interleaved teardown would
        // otherwise open between the two assignments.
        //
        // Re-find here in case the synchronous setOpacity/setScale chain
        // re-entered cancel(surface) and erased the slot. Honour that
        // cancellation by bailing — the consumer's onComplete was moved
        // into the now-erased slot and dropped per the cancellation
        // contract (cancel = non-completion termination, see
        // cancelTracking's docstring).
        {
            auto it = m_tracks.find(surface);
            if (it == m_tracks.end()) {
                return;
            }
            it->second.opacity = std::make_unique<PhosphorAnimation::AnimatedValue<qreal>>();
            if (hasScaleLeg) {
                it->second.scale = std::make_unique<PhosphorAnimation::AnimatedValue<qreal>>();
            }
            if (hasShaderLeg) {
                const auto eff = m_shaderRegistry->effect(shaderEffectId);
                if (eff.isValid()) {
                    auto* shaderItem = new PhosphorRendering::ShaderEffect(target);
                    shaderItem->setShaderSource(QUrl::fromLocalFile(eff.fragmentShaderPath));
                    shaderItem->setWidth(target->width());
                    shaderItem->setHeight(target->height());
                    shaderItem->setITime(0.0);
                    shaderItem->setIResolution(QSizeF(target->width(), target->height()));
                    shaderItem->setZ(1000);
                    it->second.shaderItem = shaderItem;
                    it->second.shaderTime = std::make_unique<PhosphorAnimation::AnimatedValue<qreal>>();
                }
            }
        }

        // Capture-by-value of `surface` is safe — it's a raw pointer key,
        // never dereferenced inside the callback. The QPointer<QQuickItem>
        // path checks the target before each setProperty so a torn-down
        // QQuickItem doesn't UAF. Self-pointer on `this` is safe because
        // the SurfaceAnimator's lifetime contract is "outlives every Surface
        // it animates" — the daemon owns the SurfaceAnimator and the
        // SurfaceFactory together, dtor ordering guarantees this.
        //
        // Re-find on every access between start() calls so we never hold a
        // dangling reference to the slot. If a re-entrant cancel erased the
        // entry mid-flight (see comment above), the second start() simply
        // bails — we can't drive a leg whose tracking entry no longer
        // exists. The first leg's start() may also see its own track
        // erased by a synchronous onComplete (zero-duration profile +
        // 1-leg case), which is harmless: the AnimatedValue lives in the
        // unique_ptr we just installed, and its destruction is owned by
        // legCompleted's `m_tracks.erase(it)` — we never re-touch the slot
        // after the start() call below.
        {
            auto it = m_tracks.find(surface);
            if (it == m_tracks.end() || !it->second.opacity) {
                // Nothing to start — runLeg races against re-entry; no-op.
                return;
            }
            it->second.opacity->start(fromOpacity, toOpacity,
                                      buildSpec(
                                          resolveProfile(m_registry, opacityProfilePath), clock,
                                          /*onValueChanged=*/
                                          [this, surface](const qreal& v) {
                                              auto sit = m_tracks.find(surface);
                                              if (sit == m_tracks.end() || !sit->second.target) {
                                                  return;
                                              }
                                              sit->second.target->setOpacity(v);
                                          },
                                          /*onComplete=*/
                                          [this, surface]() {
                                              legCompleted(surface);
                                          }));
        }

        if (hasScaleLeg) {
            auto it = m_tracks.find(surface);
            if (it == m_tracks.end() || !it->second.scale) {
                // The opacity leg's start() (or one of its synchronous
                // callbacks) cancelled the tracking entry. Nothing to
                // scale-leg into — bail and let the cancellation stand.
                return;
            }
            it->second.scale->start(fromScale, toScale,
                                    buildSpec(
                                        resolveProfile(m_registry, scaleProfilePath), clock,
                                        /*onValueChanged=*/
                                        [this, surface](const qreal& v) {
                                            auto sit = m_tracks.find(surface);
                                            if (sit == m_tracks.end() || !sit->second.target) {
                                                return;
                                            }
                                            sit->second.target->setScale(v);
                                        },
                                        /*onComplete=*/
                                        [this, surface]() {
                                            legCompleted(surface);
                                        }));
        }

        if (hasShaderLeg) {
            auto it = m_tracks.find(surface);
            if (it == m_tracks.end() || !it->second.shaderTime || !it->second.shaderItem) {
                return;
            }
            it->second.shaderTime->start(0.0, 1.0,
                                         buildSpec(
                                             resolveProfile(m_registry, opacityProfilePath), clock,
                                             /*onValueChanged=*/
                                             [this, surface](const qreal& v) {
                                                 auto sit = m_tracks.find(surface);
                                                 if (sit == m_tracks.end() || !sit->second.shaderItem) {
                                                     return;
                                                 }
                                                 sit->second.shaderItem->setITime(v);
                                             },
                                             /*onComplete=*/
                                             [this, surface]() {
                                                 auto sit = m_tracks.find(surface);
                                                 if (sit != m_tracks.end() && sit->second.shaderItem) {
                                                     delete sit->second.shaderItem;
                                                     sit->second.shaderItem = nullptr;
                                                 }
                                                 legCompleted(surface);
                                             }));
        }

        // Kick the driver so advance() starts firing on the next event-
        // loop tick. ensureDriving is idempotent — repeated beginShow /
        // beginHide calls on different surfaces don't pile up timers.
        // Skip if a re-entrant cancel already drained the map — no point
        // running the timer for nothing.
        if (!m_tracks.empty()) {
            ensureDriving();
        }
    }

    /// Decrement pendingLegs for @p surface; fire the consumer's
    /// onComplete and retire the tracking entry once both legs report
    /// done. Idempotent against a missing entry (cancel() may have
    /// raced through).
    ///
    /// ## Lifetime contract
    ///
    /// `legCompleted` runs from inside the spec.onComplete callback of
    /// the very AnimatedValue we'd be destroying — `AnimatedValue.h:547`
    /// is explicit that "spec callbacks must NOT cause *this to be
    /// destroyed while they run". Erasing the track here would tear
    /// down the AnimatedValue under its own `advance()` call stack.
    ///
    /// Defer destruction by parking both AnimatedValues in
    /// `m_pendingDestroy`. They become inert the moment the leg
    /// completes (`m_isAnimating=false`, `m_isComplete=true`); subsequent
    /// `advance()` calls on them are no-ops. The graveyard drains at
    /// the start of the next `tickAll` — by which point every advance()
    /// frame on the call stack has fully unwound, and destroying the
    /// AnimatedValue is safe.
    void legCompleted(PhosphorLayer::Surface* surface)
    {
        const auto it = m_tracks.find(surface);
        if (it == m_tracks.end()) {
            return;
        }
        --it->second.pendingLegs;
        if (it->second.pendingLegs > 0) {
            return;
        }
        // Move the callback + AnimatedValues out before erase so:
        //   1. m_tracks.erase doesn't drop the std::function we're
        //      about to invoke;
        //   2. m_tracks.erase doesn't destroy the AnimatedValue whose
        //      onComplete we're currently inside (deferred to graveyard).
        auto onComplete = std::move(it->second.onComplete);
        if (it->second.opacity) {
            m_pendingDestroy.push_back(std::move(it->second.opacity));
        }
        if (it->second.scale) {
            m_pendingDestroy.push_back(std::move(it->second.scale));
        }
        if (it->second.shaderTime) {
            m_pendingDestroy.push_back(std::move(it->second.shaderTime));
        }
        if (it->second.shaderItem) {
            delete it->second.shaderItem;
            it->second.shaderItem = nullptr;
        }
        m_tracks.erase(it);
        if (onComplete) {
            onComplete();
        }
    }

    /// Drive every active animation by one tick. Called from the
    /// 60Hz QTimer while m_tracks has entries. AnimatedValue::advance
    /// is a no-op once the value is complete; the legCompleted callback
    /// removes the track from m_tracks. Iterating by-key under a snapshot
    /// is safe against re-entrant erase from the completion callback.
    void tickAll()
    {
        // Drain the graveyard from the prior tick. AnimatedValues parked
        // here completed last tick inside their own spec.onComplete
        // callback (legCompleted's deferred-destroy contract). By now
        // the advance() call stack that fired that callback has fully
        // unwound, so destroying the AnimatedValues is safe.
        m_pendingDestroy.clear();

        // Snapshot the surfaces to advance; legCompleted may erase from
        // m_tracks during iteration.
        std::vector<PhosphorLayer::Surface*> surfaces;
        surfaces.reserve(m_tracks.size());
        for (auto& [s, _] : m_tracks) {
            surfaces.push_back(s);
        }
        for (auto* s : surfaces) {
            auto it = m_tracks.find(s);
            if (it == m_tracks.end()) {
                continue;
            }
            if (it->second.opacity) {
                it->second.opacity->advance();
            }
            // Re-find: opacity advance may have completed the leg and
            // erased the track via legCompleted.
            it = m_tracks.find(s);
            if (it == m_tracks.end()) {
                continue;
            }
            if (it->second.scale) {
                it->second.scale->advance();
            }
            it = m_tracks.find(s);
            if (it == m_tracks.end()) {
                continue;
            }
            if (it->second.shaderTime) {
                it->second.shaderTime->advance();
            }
        }
        // Stop the driver if every track completed during this tick.
        if (m_tracks.empty() && m_driverTimer.isActive()) {
            m_driverTimer.stop();
        }
    }

    /// Ensure the driver timer is running. Idempotent.
    void ensureDriving()
    {
        if (!m_driverTimer.isActive()) {
            m_driverTimer.start();
        }
    }

    PhosphorAnimation::PhosphorProfileRegistry& m_registry;
    PhosphorAnimationShaders::AnimationShaderRegistry* m_shaderRegistry = nullptr;
    Config m_defaultConfig;
    /// Keyed on `Role::scopePrefix` because two `Role` instances with
    /// the same prefix are semantically the same role.
    ///
    /// Lookup (`configFor`) is O(N) — a linear scan over registered keys
    /// computing longest-prefix-match against the surface's scopePrefix.
    /// Storage uses QHash for cheap insert / iterate, not for the lookup
    /// shape; the longest-prefix semantic precludes a hash hit. Fine for
    /// the handful of roles a typical consumer registers; if the
    /// registration count ever grows to hundreds, swap to a trie or
    /// precompute longest-prefix at registration time.
    QHash<QString, Config> m_configByRole;
    /// Single steady-clock-backed clock shared across every track. The
    /// AnimatedValue<T> contract holds it by non-owning pointer; this
    /// member MUST be declared BEFORE m_tracks so reverse-declaration
    /// destruction order destroys m_tracks (and the AnimatedValues in
    /// it) before m_clock. The explicit cancellation loop in
    /// ~SurfaceAnimator is defence-in-depth, but member-order alone is
    /// the load-bearing invariant — a future maintainer who deletes the
    /// loop must not introduce a UAF.
    internal::SteadyClock m_clock;
    /// Keyed on raw Surface pointer. Tracks only in-flight animations;
    /// a missing entry means "no active animation on this surface."
    /// std::unordered_map (not QHash) because Track contains a move-only
    /// `unique_ptr<AnimatedValue<qreal>>` and Qt6's QHash still requires
    /// copy-constructible value types.
    std::unordered_map<PhosphorLayer::Surface*, Track> m_tracks;
    /// Deferred-destroy graveyard for AnimatedValues whose final tick
    /// fired `legCompleted` from inside their own `spec.onComplete`.
    /// `AnimatedValue.h:547` forbids destroying *this from within a
    /// spec callback; legCompleted parks them here and tickAll drains
    /// the list at the start of the next tick (after the firing
    /// `advance()` has fully unwound). MUST be declared after m_clock
    /// (clock outlives the values) and before m_driverTimer (the timer
    /// drains them on tick — order matches m_tracks's invariant).
    std::vector<std::unique_ptr<PhosphorAnimation::AnimatedValue<qreal>>> m_pendingDestroy;
    /// Drives advance() at ~60Hz while any track is in flight. Declared
    /// LAST so reverse-declaration destruction stops the timer BEFORE
    /// m_tracks is destroyed — otherwise a final tick could fire after
    /// the tracks have been freed.
    QTimer m_driverTimer;
};

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

SurfaceAnimator::SurfaceAnimator()
    : SurfaceAnimator(PhosphorAnimation::PhosphorProfileRegistry::instance(), Config{})
{
}

SurfaceAnimator::SurfaceAnimator(Config defaults)
    : SurfaceAnimator(PhosphorAnimation::PhosphorProfileRegistry::instance(), std::move(defaults))
{
}

SurfaceAnimator::~SurfaceAnimator()
{
    // Stop the timer FIRST so no tick can race with the destruction work
    // below. cancelTracking parks AnimatedValues in m_pendingDestroy; if
    // the timer fires between the cancel loop and the explicit drain it
    // would call tickAll which itself drains m_pendingDestroy — harmless
    // but pointless work, and a future maintainer might worry about the
    // ordering. Stopping up front makes the dtor a single sequential
    // teardown.
    d->m_driverTimer.stop();

    // Move-out the track map and cancel each entry's AnimatedValues into
    // the graveyard. Move-out is cheaper than the prior `while (!empty)
    // { begin() }` loop (no per-iteration find / rehash) and preserves
    // the cancellation semantics: each entry's `onComplete` is dropped
    // (cancellation is a non-completion termination) and the AVs are
    // parked into m_pendingDestroy via cancelTracking.
    //
    // We iterate over the moved-out copy rather than calling
    // cancelTracking directly on m_tracks because cancelTracking does a
    // map find/erase; iterating an `unordered_map` while erasing requires
    // care. The move-out gives us a stable iteration target.
    auto leftovers = std::move(d->m_tracks);
    d->m_tracks.clear();
    for (auto& [_, track] : leftovers) {
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
        if (track.shaderItem) {
            delete track.shaderItem;
            track.shaderItem = nullptr;
        }
    }

    // Drain the graveyard explicitly. This is defence-in-depth on top of
    // member-declaration order — m_clock is declared before
    // m_pendingDestroy, so reverse-order destruction would still tear the
    // AnimatedValues down before m_clock disappears. Doing it here keeps
    // the invariant local to the dtor instead of buried in member order;
    // a future maintainer who reorders members won't accidentally turn
    // this into a UAF on shutdown.
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

void SurfaceAnimator::beginShow(PhosphorLayer::Surface* surface, QQuickItem* rootItem, CompletionCallback onComplete)
{
    if (!surface) {
        if (onComplete) {
            onComplete();
        }
        return;
    }
    const Config cfg = d->configFor(surface->config().role);

    // Supersession-aware starting state. If the previous animation left
    // the target mid-fade (opacity < 1 / scale < 1), pick up from there
    // so the user sees a continuous fade-up instead of a one-frame jump
    // back to the configured "from" value. If we're already at (or past)
    // the terminal state — fresh first show, no prior animation — fall
    // back to the configured starting values so we actually run an
    // animation rather than a no-op fade-from-1-to-1.
    //
    // Asymmetry caveat: scale supersession is one-sided. `liveScale > 1.0`
    // (e.g. a custom QML pinch effect that overshoots above 1) is treated
    // as "at or past the terminal state" and falls back to
    // `cfg.showScaleFrom` (typically < 1), causing a visible scale-down-
    // then-up. Opacity has no analogous edge case because `>1.0` opacity
    // doesn't render any differently than `1.0`. Production overlays
    // never push scale above 1 outside of an in-flight show, so this is
    // a documented limitation rather than a bug — surfaces that need
    // overshoot-from-above supersession should drive scale via their
    // own animator and skip `cfg.showScaleProfile`.
    const qreal liveOpacity = rootItem ? rootItem->opacity() : 1.0;
    const qreal fromOpacity = (liveOpacity < 1.0) ? liveOpacity : 0.0;

    const qreal toScale = 1.0;
    qreal fromScale = 1.0;
    if (!cfg.showScaleProfile.isEmpty()) {
        const qreal liveScale = rootItem ? rootItem->scale() : 1.0;
        fromScale = (liveScale < toScale) ? liveScale : cfg.showScaleFrom;
    }

    d->runLeg(surface, rootItem, fromOpacity, /*toOpacity=*/1.0, cfg.showProfile, fromScale, toScale,
              cfg.showScaleProfile, cfg.showShaderEffectId, std::move(onComplete));
}

void SurfaceAnimator::beginHide(PhosphorLayer::Surface* surface, QQuickItem* rootItem, CompletionCallback onComplete)
{
    if (!surface) {
        if (onComplete) {
            onComplete();
        }
        return;
    }
    const Config cfg = d->configFor(surface->config().role);
    // For the hide leg, the visible "from" is whatever opacity the item
    // currently has — typically 1.0 after a completed show, but could be
    // mid-fade if hide-while-showing supersedes. Read live so the new
    // animation starts from the visible state, not from a hardcoded 1.0.
    const qreal fromOpacity = rootItem ? rootItem->opacity() : 1.0;
    const qreal fromScale = (cfg.hideScaleProfile.isEmpty() || !rootItem) ? 1.0 : rootItem->scale();
    const qreal toScale = cfg.hideScaleProfile.isEmpty() ? 1.0 : cfg.hideScaleTo;
    d->runLeg(surface, rootItem, fromOpacity, /*toOpacity=*/0.0, cfg.hideProfile, fromScale, toScale,
              cfg.hideScaleProfile, cfg.hideShaderEffectId, std::move(onComplete));
}

void SurfaceAnimator::cancel(PhosphorLayer::Surface* surface)
{
    if (!surface) {
        return;
    }
    d->cancelTracking(surface);
}

} // namespace PhosphorAnimationLayer
