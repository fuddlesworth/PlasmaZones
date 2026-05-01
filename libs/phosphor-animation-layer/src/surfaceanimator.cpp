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
#include <QStack>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <memory>
#include <unordered_map>

namespace PhosphorAnimationLayer {

namespace {
Q_LOGGING_CATEGORY(lcSurfaceAnimator, "phosphoranimationlayer.surfaceanimator")

/// Driver tick interval. SteadyClock::refreshRate MUST agree so
/// velocity-based curves (Spring) sample at the timer's cadence.
constexpr int kTickIntervalMs = 16;
constexpr qreal kRefreshRateHz = 1000.0 / kTickIntervalMs;
} // namespace

namespace internal {
/// Steady-clock IMotionClock. Independent of QQuickWindow rendering
/// so offscreen QPA (headless CI) ticks too. `internal` (not
/// anonymous) for external linkage — avoids -Wsubobject-linkage on
/// the SurfaceAnimator::Private member type.
class SteadyClock : public PhosphorAnimation::IMotionClock
{
public:
    std::chrono::nanoseconds now() const override
    {
        return std::chrono::steady_clock::now().time_since_epoch();
    }

    qreal refreshRate() const override
    {
        return kRefreshRateHz;
    }

    /// requestFrame is a hint — the SurfaceAnimator's QTimer already
    /// ticks unconditionally while any track is in flight. No-op.
    void requestFrame() override
    {
    }
};
} // namespace internal

namespace {

/// Iterative visual-tree search for `shaderAnchor: true` inside an
/// animator target. Walks `childItems()` because Window-rooted QML
/// QObject-parents its content to the Window (not contentItem), so
/// findChild from contentItem walks an empty subtree. QStack-iterative
/// to bound stack usage on deep trees. Returns the first match (prior
/// behaviour) but warns on duplicates.
QQuickItem* findShaderAnchorRecursive(QQuickItem* root)
{
    if (!root) {
        return nullptr;
    }
    QStack<QQuickItem*> stack;
    stack.push(root);
    QQuickItem* firstMatch = nullptr;
    bool warnedDuplicate = false;
    while (!stack.isEmpty()) {
        QQuickItem* item = stack.pop();
        if (!item) {
            continue;
        }
        if (item->property("shaderAnchor").toBool()) {
            if (!firstMatch) {
                firstMatch = item;
            } else if (!warnedDuplicate) {
                qCWarning(lcSurfaceAnimator) << "multiple shaderAnchor tags found under" << root
                                             << "— using first match" << firstMatch << "ignoring" << item;
                warnedDuplicate = true;
            }
        }
        const auto kids = item->childItems();
        for (QQuickItem* child : kids) {
            stack.push(child);
        }
    }
    return firstMatch;
}

/// Resolve a Profile path through the registry, falling back to a
/// library-default Profile. Empty path is the documented "use
/// defaults" sentinel; a non-empty unresolved path warns on the
/// journal — typo backstop for C++ profile registrations the QML-
/// side lint can't see.
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

/// Build a MotionSpec<qreal> — centralises construction so beginShow
/// / beginHide read identically.
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

/// Pieces produced by `attachShaderToAnchor`. The caller stashes
/// these on its in-flight Track; the helper stays free of the inner
/// Track type so a future caller can reuse it.
struct ShaderAttachResult
{
    PhosphorRendering::ShaderEffect* shaderItem = nullptr;
    QQuickItem* shaderAnchor = nullptr;
    bool foundExplicitAnchor = false;
};

/// Build the per-leg ShaderEffect: anchor resolution + layer-enable +
/// setSourceItem (gated on explicit anchors — layer-enabling the
/// fallback QQuickRootItem breaks scene-graph rendering) + parameter
/// translation + live geometry sync.
ShaderAttachResult attachShaderToAnchor(QQuickItem* target,
                                        const PhosphorAnimationShaders::AnimationShaderEffect& effect,
                                        const QString& shaderEffectId, const QVariantMap& shaderParameters)
{
    ShaderAttachResult out;
    QQuickItem* shaderAnchor = target;
    bool foundExplicitAnchor = false;
    if (QQuickItem* anchored = findShaderAnchorRecursive(target)) {
        shaderAnchor = anchored;
        foundExplicitAnchor = true;
    }

    if (foundExplicitAnchor) {
        // Two-step "layer" access — single-step setProperty(
        // "layer.enabled", true) creates a dynamic QObject property
        // that never reaches QQuickItemLayer.
        if (QObject* layer = shaderAnchor->property("layer").value<QObject*>()) {
            layer->setProperty("enabled", true);
        }
    }

    qCDebug(lcSurfaceAnimator).nospace() << "shader leg: effect=" << shaderEffectId
                                         << " path=" << effect.fragmentShaderPath << " anchor=" << shaderAnchor
                                         << " explicit=" << foundExplicitAnchor << " size=" << shaderAnchor->width()
                                         << "x" << shaderAnchor->height();

    auto* shaderItem = new PhosphorRendering::ShaderEffect(shaderAnchor);
    shaderItem->setShaderSource(QUrl::fromLocalFile(effect.fragmentShaderPath));
    shaderItem->setWidth(shaderAnchor->width());
    shaderItem->setHeight(shaderAnchor->height());
    shaderItem->setITime(0.0);
    shaderItem->setIResolution(QSizeF(shaderAnchor->width(), shaderAnchor->height()));

    // setSourceItem (binds anchor's layer texture as iChannel0) only
    // when we explicitly enabled layer — on the fallback path
    // setSourceItem would try to enable layer on the QQuickRootItem
    // and re-introduce the OSD-doesn't-render bug.
    if (foundExplicitAnchor) {
        shaderItem->setSourceItem(shaderAnchor);
    }

    // Translate friendly parameter ids to customParams<N>_<x|y|z|w>
    // slot keys. Always called so declared defaults populate the
    // slots — otherwise customParams reads the (-1,-1,-1,-1) sentinel.
    const QVariantMap translated =
        PhosphorAnimationShaders::AnimationShaderRegistry::translateAnimationParams(effect, shaderParameters);
    if (!translated.isEmpty()) {
        shaderItem->setShaderParams(translated);
    }

    // Z above the anchor's blit (layer.enabled doesn't suppress the
    // anchor's render — both are drawn, shader has to win depth).
    static constexpr qreal kShaderOverlayZ = 1000;
    shaderItem->setZ(kShaderOverlayZ);

    // Live geometry sync — the wayland surface may be 0x0 at
    // construction (anchor unresolved); the signal picks up the post-
    // configure pass. QPointer-capture the anchor so a Loader rebuild
    // that reparents-out-then-destroys doesn't fire widthChanged
    // against a tombstoned raw pointer before Qt's auto-disconnect.
    auto syncGeometry = [shaderItem, anchorPtr = QPointer<QQuickItem>(shaderAnchor)]() {
        if (!anchorPtr) {
            return;
        }
        shaderItem->setWidth(anchorPtr->width());
        shaderItem->setHeight(anchorPtr->height());
        shaderItem->setIResolution(QSizeF(anchorPtr->width(), anchorPtr->height()));
    };
    QObject::connect(shaderAnchor, &QQuickItem::widthChanged, shaderItem, syncGeometry);
    QObject::connect(shaderAnchor, &QQuickItem::heightChanged, shaderItem, syncGeometry);
    syncGeometry();

    out.shaderItem = shaderItem;
    out.shaderAnchor = shaderAnchor;
    out.foundExplicitAnchor = foundExplicitAnchor;
    return out;
}

} // namespace

// ══════════════════════════════════════════════════════════════════════════
// SurfaceAnimator::Private
// ══════════════════════════════════════════════════════════════════════════

class SurfaceAnimator::Private
{
public:
    /// Per-surface in-flight bookkeeping. unique_ptr<AnimatedValue>
    /// so slot.opacity.reset() can null the AV before installing a
    /// fresh one — AnimatedValue itself has no moved-from state.
    struct Track
    {
        QPointer<QQuickItem> target; ///< Auto-nulls if QML scene tears down mid-flight
        /// Where the shader effect is parented + sized. Equal to
        /// `target` unless an explicit `property bool shaderAnchor:
        /// true` was found by attachShaderToAnchor.
        QPointer<QQuickItem> shaderAnchor;
        /// True iff the anchor came from an explicit property tag.
        /// Gates `teardownShaderLeg`'s layer.enabled reset — fallback
        /// targets (QQuickRootItem) deliberately never had their layer
        /// enabled and must not have it touched at teardown either.
        bool foundExplicitAnchor = false;
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
        // Driver fires kTickIntervalMs cadence while any track is in
        // flight; tickAll stops the timer when m_tracks empties.
        m_driverTimer.setInterval(std::chrono::milliseconds(kTickIntervalMs));
        m_driverTimer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&m_driverTimer, &QTimer::timeout, [this]() {
            tickAll();
        });
    }

    /// Tear down the per-leg ShaderEffect. layer.enabled is restored
    /// only on explicit anchors — restoring on a fallback
    /// QQuickRootItem would re-introduce the OSD-doesn't-render bug.
    void teardownShaderLeg(Track& track)
    {
        if (track.shaderTime) {
            track.shaderTime->cancel();
            m_pendingDestroy.push_back(std::move(track.shaderTime));
        }
        if (track.shaderItem) {
            track.shaderItem->deleteLater();
            track.shaderItem = nullptr;
        }
        if (track.foundExplicitAnchor && track.shaderAnchor) {
            if (QObject* layer = track.shaderAnchor->property("layer").value<QObject*>()) {
                layer->setProperty("enabled", false);
            }
        }
        track.shaderAnchor.clear();
        track.foundExplicitAnchor = false;
    }

    /// Cancel any in-flight legs for a surface. Called directly and
    /// also implicitly on supersession. AnimatedValue::cancel clears
    /// m_isAnimating without firing spec.onComplete (cancel = non-
    /// completion termination per ISurfaceAnimator). AVs are parked in
    /// m_pendingDestroy because a re-entrant cancel from inside
    /// spec.onValueChanged must not destroy *this mid-advance
    /// (AnimatedValue.h:547); graveyard drains on next tickAll.
    void cancelTracking(PhosphorLayer::Surface* surface)
    {
        const auto it = m_tracks.find(surface);
        if (it == m_tracks.end()) {
            return;
        }
        // Cancel BEFORE moving so m_isAnimating=false propagates to
        // any advance() frame still on the stack — post-callback work
        // there then bails on the m_isAnimating guard rather than
        // mutating cancelled state.
        if (it->second.opacity) {
            it->second.opacity->cancel();
            m_pendingDestroy.push_back(std::move(it->second.opacity));
        }
        if (it->second.scale) {
            it->second.scale->cancel();
            m_pendingDestroy.push_back(std::move(it->second.scale));
        }
        teardownShaderLeg(it->second);
        m_tracks.erase(it);
    }

    /// Look up the per-role config (longest-prefix-match on
    /// scopePrefix, fall back to default). Consumers derive per-
    /// surface roles via withScopePrefix("base-{screenId}") and
    /// register configs against the unsuffixed base, so a bare find()
    /// would always miss. Match boundary is '-' or end-of-string so
    /// "plasmazones-layout" doesn't collide with "plasmazones-layout-
    /// picker-..." when both are registered.
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

    /// Run a show or hide leg. Used by beginShow/beginHide. Threads
    /// the from→to opacity/scale pair, the profile pair, the optional
    /// shader leg, and the caller's onComplete; fires onComplete once
    /// every leg settles. Empty *ProfilePath = skip that leg.
    void runLeg(PhosphorLayer::Surface* surface, QQuickItem* target, qreal fromOpacity, qreal toOpacity,
                const QString& opacityProfilePath, qreal fromScale, qreal toScale, const QString& scaleProfilePath,
                const QString& shaderEffectId, const QString& shaderProfilePath, const QVariantMap& shaderParameters,
                ISurfaceAnimator::CompletionCallback onComplete)
    {
        // Supersede BEFORE the null-target check — leaving a prior
        // entry behind on null-target strands its AV (custom
        // orchestrators bypass Surface::Impl::drive()'s pre-cancel).
        cancelTracking(surface);

        if (!target) {
            qCWarning(lcSurfaceAnimator) << "runLeg called with null target — onComplete fires synchronously";
            // Synchronous onComplete: same forbidden-ops contract as a
            // legCompleted-fired callback (SurfaceAnimator.h:79-90).
            if (onComplete) {
                onComplete();
            }
            return;
        }

        PhosphorAnimation::IMotionClock* clock = &m_clock;

        const bool hasScaleLeg = !scaleProfilePath.isEmpty();

        // Resolve up front so `legCount` counts only legs that will
        // actually start — a typo'd shaderEffectId that doesn't reach
        // start() would otherwise leave pendingLegs hanging mid-show.
        PhosphorAnimationShaders::AnimationShaderEffect resolvedShaderEff;
        if (!shaderEffectId.isEmpty() && m_shaderRegistry) {
            resolvedShaderEff = m_shaderRegistry->effect(shaderEffectId);
        }
        const bool hasShaderLeg = resolvedShaderEff.isValid();
        const int legCount = 1 + (hasScaleLeg ? 1 : 0) + (hasShaderLeg ? 1 : 0);

        // Install the bookkeeping slot BEFORE the synchronous pre-
        // state writes below. setOpacity / setScale fire QML changed
        // signals synchronously, and a consumer binding can re-enter
        // cancel(surface). With the slot installed first, that cancel
        // erases cleanly (no AVs to park yet) and the post-write
        // re-find below detects the erasure and bails.
        {
            Track& slot = m_tracks[surface];
            slot.opacity.reset();
            slot.scale.reset();
            // Reset shader-leg state explicitly — a non-shader leg
            // following a shader leg (e.g. show w/ pixelate → hide w/o)
            // would otherwise leave stale anchor/shaderItem QPointers
            // hanging around since teardownShaderLeg only runs when the
            // prior leg had hasShaderLeg.
            slot.shaderAnchor.clear();
            slot.shaderItem = nullptr;
            slot.foundExplicitAnchor = false;
            slot.target = target;
            slot.onComplete = std::move(onComplete);
            slot.pendingLegs = legCount;
        }

        // Initial frame synchronously — no stale opacity between
        // beginShow returning and the first AV tick.
        target->setOpacity(fromOpacity);
        if (hasScaleLeg) {
            target->setScale(fromScale);
        }

        // Construct AVs atomically BEFORE start() on either — a
        // synchronous onValueChanged that re-enters cancel(surface)
        // would otherwise erase the slot between assignments. Re-find
        // here too in case the setOpacity/setScale chain above just
        // re-entered cancel() — bail per the cancellation contract.
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
                // resolvedShaderEff already validated by hasShaderLeg.
                ShaderAttachResult attached =
                    attachShaderToAnchor(target, resolvedShaderEff, shaderEffectId, shaderParameters);
                it->second.shaderItem = attached.shaderItem;
                it->second.shaderAnchor = attached.shaderAnchor;
                it->second.foundExplicitAnchor = attached.foundExplicitAnchor;
                it->second.shaderTime = std::make_unique<PhosphorAnimation::AnimatedValue<qreal>>();
            }
        }

        // Capture-by-value of `surface` (raw pointer key, never deref'd)
        // and `this` (SurfaceAnimator outlives every Surface — daemon
        // owns both, dtor ordering enforces it) are safe inside the
        // callbacks. Re-find on every access between start()s so a
        // re-entrant cancel mid-flight is honoured.
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
                // The opacity leg's start() (or a synchronous callback)
                // cancelled the entry; let the cancellation stand.
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
            it->second.shaderTime->start(
                0.0, 1.0,
                buildSpec(
                    resolveProfile(m_registry, shaderProfilePath.isEmpty() ? opacityProfilePath : shaderProfilePath),
                    clock,
                    /*onValueChanged=*/
                    [this, surface](const qreal& v) {
                        auto sit = m_tracks.find(surface);
                        // shaderItem QPointer auto-nulls if the
                        // ShaderEffect's parent (the anchor) was torn
                        // down between ticks — bail rather than UAF.
                        if (sit == m_tracks.end() || !sit->second.shaderItem) {
                            return;
                        }
                        // Geometry sync runs off the anchor's
                        // widthChanged/heightChanged signals (see
                        // syncGeometry); the per-tick callback only
                        // threads the time value through.
                        sit->second.shaderItem->setITime(v);
                    },
                    /*onComplete=*/
                    [this, surface]() {
                        legCompleted(surface);
                    }));
        }

        // Kick the driver — ensureDriving is idempotent. Skip if a
        // re-entrant cancel already drained the map.
        if (!m_tracks.empty()) {
            ensureDriving();
        }
    }

    /// Decrement pendingLegs; on the final settle, fire the consumer's
    /// onComplete and retire the entry. Idempotent against a missing
    /// entry. AVs are parked in m_pendingDestroy (deferred destroy)
    /// because legCompleted runs from inside the spec.onComplete of
    /// the very AV — AnimatedValue.h:547 forbids destroying *this from
    /// within a spec callback.
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
        teardownShaderLeg(it->second);
        m_tracks.erase(it);
        if (onComplete) {
            onComplete();
        }
    }

    /// Drive every active animation by one tick. By-key snapshot
    /// iteration is safe against re-entrant erase from legCompleted.
    void tickAll()
    {
        // Drain the graveyard — AVs parked here last tick are now safe
        // to destroy (the spec.onComplete advance() frame has unwound).
        m_pendingDestroy.clear();

        // Snapshot — legCompleted may erase from m_tracks while iterating.
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
            // Re-find: advance may have completed + erased via legCompleted.
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
    /// Keyed on `Role::scopePrefix`. Lookup (configFor) is longest-
    /// prefix-match — a linear O(N) scan, which is fine for the handful
    /// of roles consumers register.
    QHash<QString, Config> m_configByRole;
    /// Steady-clock-backed clock shared across every track. MUST be
    /// declared BEFORE m_tracks so reverse-declaration destruction
    /// order tears down the AVs (held by non-owning pointer to this
    /// clock) before the clock disappears.
    internal::SteadyClock m_clock;
    /// Tracks only in-flight animations; missing entry means "no
    /// active animation". std::unordered_map (not QHash) because
    /// Track is move-only and QHash still requires copy-constructible
    /// values in Qt6.
    std::unordered_map<PhosphorLayer::Surface*, Track> m_tracks;
    /// Graveyard for AVs whose final tick fired legCompleted from
    /// inside their own spec.onComplete (AnimatedValue.h:547 forbids
    /// destroying *this from a spec callback). Drained at the start
    /// of the next tickAll. MUST be declared after m_clock and
    /// before m_driverTimer so the destruction order is sound.
    std::vector<std::unique_ptr<PhosphorAnimation::AnimatedValue<qreal>>> m_pendingDestroy;
    /// Drives advance() at ~60Hz while any track is in flight.
    /// Declared LAST so the timer stops BEFORE m_tracks tears down on
    /// dtor — otherwise a final tick could fire after the tracks
    /// have been freed.
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
        d->teardownShaderLeg(track);
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

void SurfaceAnimator::beginShow(PhosphorLayer::Surface* surface, QQuickItem* rootItem, CompletionCallback onComplete)
{
    if (!surface) {
        if (onComplete) {
            onComplete();
        }
        return;
    }
    const Config cfg = d->configFor(surface->config().role);

    // Supersession-aware starting state. If the previous animation
    // left the target mid-fade, pick up from there for a continuous
    // visual; otherwise (fresh first show / past terminal state) fall
    // back to the configured "from" so we actually run an animation
    // rather than a no-op fade-from-1-to-1. Scale supersession is
    // one-sided: liveScale > 1.0 (e.g. an external overshoot) falls
    // back to cfg.showScaleFrom — surfaces that need overshoot-from-
    // above must drive scale themselves and skip cfg.showScaleProfile.
    const qreal liveOpacity = rootItem ? rootItem->opacity() : 1.0;
    const qreal fromOpacity = (liveOpacity < 1.0) ? liveOpacity : 0.0;

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
    d->cancelTracking(surface);
}

} // namespace PhosphorAnimationLayer
