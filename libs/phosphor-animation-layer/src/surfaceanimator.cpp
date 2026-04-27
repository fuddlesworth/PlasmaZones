// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimationLayer/SurfaceAnimator.h>

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/MotionSpec.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>

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
PhosphorAnimation::Profile resolveProfile(PhosphorAnimation::PhosphorProfileRegistry& registry, const QString& path)
{
    if (path.isEmpty()) {
        return PhosphorAnimation::Profile{}.withDefaults();
    }
    if (auto p = registry.resolve(path)) {
        return p->withDefaults();
    }
    qCDebug(lcSurfaceAnimator).nospace() << "Profile path '" << path
                                         << "' did not resolve through registry — "
                                            "falling back to library defaults";
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
    /// AnimatedValue<qreal>s are heap-allocated because they're move-only
    /// AND the `onValueChanged` callback they hold needs to capture a
    /// stable pointer to *this* AnimatedValue for re-entry safety.
    struct Track
    {
        QPointer<QQuickItem> target; ///< Auto-nulls if QML scene tears down mid-flight
        std::unique_ptr<PhosphorAnimation::AnimatedValue<qreal>> opacity;
        std::unique_ptr<PhosphorAnimation::AnimatedValue<qreal>> scale;
        /// Number of legs still running. 1 for opacity-only; 2 when a
        /// scale leg is in flight too. `onComplete` fires when this hits
        /// 0 to ensure both legs finish before notifying the caller.
        int pendingLegs = 0;
        ISurfaceAnimator::CompletionCallback onComplete;
    };

    Private(PhosphorAnimation::PhosphorProfileRegistry& registry, Config defaults)
        : m_registry(registry)
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
    void cancelTracking(PhosphorLayer::Surface* surface)
    {
        const auto it = m_tracks.find(surface);
        if (it == m_tracks.end()) {
            return;
        }
        // AnimatedValue::cancel() clears m_isAnimating + m_isComplete
        // without firing spec.onComplete (verified at AnimatedValue.h:464);
        // this matches the ISurfaceAnimator::cancel contract — "the
        // animator must call onComplete exactly once per
        // beginShow/beginHide; cancel is an explicitly non-completion
        // termination".
        //
        // Move the AnimatedValues + the consumer's onComplete out of the
        // tracking entry BEFORE erasing, then cancel through the local
        // unique_ptrs. If a future change to AnimatedValue::cancel ever
        // regresses the contract and fires spec.onComplete synchronously,
        // legCompleted() would invalidate `it` mid-cancel — but we're
        // already past the iterator; the local pointers are stable.
        // Dropping the consumer's onComplete on the floor is the documented
        // semantic for cancellation (cancel is non-completion).
        auto opacity = std::move(it->second.opacity);
        auto scale = std::move(it->second.scale);
        m_tracks.erase(it);
        if (opacity) {
            opacity->cancel();
        }
        if (scale) {
            scale->cancel();
        }
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
                ISurfaceAnimator::CompletionCallback onComplete)
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

        // Set the initial frame state synchronously so the compositor
        // doesn't paint a stale opacity for the one frame between
        // beginShow returning and the first AnimatedValue tick.
        target->setOpacity(fromOpacity);
        if (!scaleProfilePath.isEmpty()) {
            target->setScale(fromScale);
        }

        const int legCount = scaleProfilePath.isEmpty() ? 1 : 2;

        // Insert/refresh FIRST so the leg-completion callbacks below find
        // the entry already present. AnimatedValue::start fires
        // onValueChanged synchronously on the first tick (same event-loop
        // turn) under some clock implementations; without the pre-insert
        // the lambda would race the map insert. cancelTracking() above
        // normally guarantees a fresh entry — but if a future refactor
        // ever leaves a stale entry behind, operator[] still gives us a
        // usable slot whose unique_ptrs we explicitly reset below before
        // installing fresh AnimatedValues. Belt-and-braces: a Q_ASSERT
        // here would be debug-only and silently corrupt release builds.
        Track& slot = m_tracks[surface];
        slot.opacity.reset();
        slot.scale.reset();
        slot.target = target;
        slot.onComplete = std::move(onComplete);
        slot.pendingLegs = legCount;

        // Capture-by-value of `surface` is safe — it's a raw pointer key,
        // never dereferenced inside the callback. The QPointer<QQuickItem>
        // path checks the target before each setProperty so a torn-down
        // QQuickItem doesn't UAF. Self-pointer on `this` is safe because
        // the SurfaceAnimator's lifetime contract is "outlives every Surface
        // it animates" — the daemon owns the SurfaceAnimator and the
        // SurfaceFactory together, dtor ordering guarantees this.
        slot.opacity = std::make_unique<PhosphorAnimation::AnimatedValue<qreal>>();
        slot.opacity->start(fromOpacity, toOpacity,
                            buildSpec(
                                resolveProfile(m_registry, opacityProfilePath), clock,
                                /*onValueChanged=*/
                                [this, surface](const qreal& v) {
                                    auto it = m_tracks.find(surface);
                                    if (it == m_tracks.end() || !it->second.target) {
                                        return;
                                    }
                                    it->second.target->setOpacity(v);
                                },
                                /*onComplete=*/
                                [this, surface]() {
                                    legCompleted(surface);
                                }));

        if (!scaleProfilePath.isEmpty()) {
            slot.scale = std::make_unique<PhosphorAnimation::AnimatedValue<qreal>>();
            slot.scale->start(fromScale, toScale,
                              buildSpec(
                                  resolveProfile(m_registry, scaleProfilePath), clock,
                                  /*onValueChanged=*/
                                  [this, surface](const qreal& v) {
                                      auto it = m_tracks.find(surface);
                                      if (it == m_tracks.end() || !it->second.target) {
                                          return;
                                      }
                                      it->second.target->setScale(v);
                                  },
                                  /*onComplete=*/
                                  [this, surface]() {
                                      legCompleted(surface);
                                  }));
        }

        // Kick the driver so advance() starts firing on the next event-
        // loop tick. ensureDriving is idempotent — repeated beginShow /
        // beginHide calls on different surfaces don't pile up timers.
        ensureDriving();
    }

    /// Decrement pendingLegs for @p surface; fire the consumer's
    /// onComplete and erase the tracking entry once both legs report
    /// done. Idempotent against a missing entry (cancel() may have
    /// raced through).
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
        // Move the callback out before erase so the call site can erase
        // the entry without invalidating the captured callback. Calling
        // a moved-from std::function is UB; empty after move. Same
        // pattern AnimationController uses for its completion hooks.
        auto onComplete = std::move(it->second.onComplete);
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
    Config m_defaultConfig;
    /// Keyed on `Role::scopePrefix` because two `Role` instances with
    /// the same prefix are semantically the same role; QHash<QString>
    /// gives O(1) lookup.
    QHash<QString, Config> m_configByRole;
    /// Keyed on raw Surface pointer. Tracks only in-flight animations;
    /// a missing entry means "no active animation on this surface."
    /// std::unordered_map (not QHash) because Track contains a move-only
    /// `unique_ptr<AnimatedValue<qreal>>` and Qt6's QHash still requires
    /// copy-constructible value types.
    std::unordered_map<PhosphorLayer::Surface*, Track> m_tracks;
    /// Single steady-clock-backed clock shared across every track. The
    /// AnimatedValue<T> contract holds it by non-owning pointer; this
    /// member outlives every AnimatedValue we hand the pointer to (it's
    /// destroyed after m_tracks at ~Private).
    internal::SteadyClock m_clock;
    /// Drives advance() at ~60Hz while any track is in flight.
    QTimer m_driverTimer;
};

// ══════════════════════════════════════════════════════════════════════════
// SurfaceAnimator
// ══════════════════════════════════════════════════════════════════════════

SurfaceAnimator::SurfaceAnimator(PhosphorAnimation::PhosphorProfileRegistry& registry, Config defaults)
    : d(std::make_unique<Private>(registry, std::move(defaults)))
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
    // Cancel any leftover entries — should be empty in correct lifecycle
    // (Surface dtor cancels), but a misordered teardown would leave entries
    // pointing at QQuickItems whose AnimatedValues fire onto deleted memory
    // on the next clock tick.
    while (!d->m_tracks.empty()) {
        auto it = d->m_tracks.begin();
        d->cancelTracking(it->first);
    }
    d->m_driverTimer.stop();
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
    const qreal liveOpacity = rootItem ? rootItem->opacity() : 1.0;
    const qreal fromOpacity = (liveOpacity < 1.0) ? liveOpacity : 0.0;

    const qreal toScale = 1.0;
    qreal fromScale = 1.0;
    if (!cfg.showScaleProfile.isEmpty()) {
        const qreal liveScale = rootItem ? rootItem->scale() : 1.0;
        fromScale = (liveScale < toScale) ? liveScale : cfg.showScaleFrom;
    }

    d->runLeg(surface, rootItem, fromOpacity, /*toOpacity=*/1.0, cfg.showProfile, fromScale, toScale,
              cfg.showScaleProfile, std::move(onComplete));
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
              cfg.hideScaleProfile, std::move(onComplete));
}

void SurfaceAnimator::cancel(PhosphorLayer::Surface* surface)
{
    if (!surface) {
        return;
    }
    d->cancelTracking(surface);
}

} // namespace PhosphorAnimationLayer
