// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/SurfaceAnimator.h>

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/IMotionClock.h>

#include <QHash>
#include <QList>
#include <QLoggingCategory>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <QVector>

#include <chrono>
#include <memory>
#include <unordered_map>
#include <vector>

QT_BEGIN_NAMESPACE
class QQuickItem;
class QQuickShaderEffectSource;
QT_END_NAMESPACE

namespace PhosphorAnimation {
class PhosphorProfileRegistry;
}

namespace PhosphorAnimationShaders {
class AnimationShaderRegistry;
}

namespace PhosphorRendering {
class ShaderEffect;
}

namespace PhosphorLayer {
class Role;
class Surface;
}

namespace PhosphorAnimationLayer {

// Defined in surfaceanimator.cpp; declared here so every partition TU
// (surfaceanimator_shaderattach/tracks/tick.cpp) shares one category.
Q_DECLARE_LOGGING_CATEGORY(lcSurfaceAnimator)

/// Driver tick interval. SteadyClock::refreshRate MUST agree so
/// velocity-based curves (Spring) sample at the timer's cadence.
constexpr int kTickIntervalMs = 16;
constexpr qreal kRefreshRateHz = 1000.0 / kTickIntervalMs;

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

namespace detail {

/// Pieces produced by `attachShaderToAnchor`. The caller stashes
/// these on its in-flight Track; the helper stays free of the inner
/// Track type so a future caller can reuse it.
struct ShaderAttachResult
{
    PhosphorRendering::ShaderEffect* shaderItem = nullptr;
    QQuickShaderEffectSource* shaderSource = nullptr;
    QQuickItem* shaderAnchor = nullptr;
    bool foundExplicitAnchor = false;
    /// Anchor siblings whose `visible` we flipped to false for the leg.
    /// Restored on teardown. QPointer guards against the consumer's
    /// QML scene tearing down a sibling mid-flight.
    ///
    /// Decorator siblings (e.g. `MultiEffect { source: container }`)
    /// sample the anchor's layer FBO and render `source + shadow` in
    /// parallel with the shader leg — without hiding them the user
    /// sees a full-size copy of the anchor stacked behind the shader.
    /// We tried redirecting MultiEffect.source→shaderItem (so the
    /// decorator follows the shader's output and keeps drawing the
    /// shadow), but Qt 6's MultiEffect crashes inside its private
    /// QQuickShaderEffect's event handler when the source is a
    /// non-stock QQuickItem subclass. Hiding the siblings is the chosen
    /// tradeoff; the shadow-pop-in at teardown is the lesser evil.
    QList<QPointer<QQuickItem>> hiddenSiblings;
    /// Live fboExtentKind. Captured by the syncGeometry lambda by
    /// `shared_ptr` so a metadata edit that flips an effect from
    /// Anchor to Surface (or vice versa) takes effect on the next
    /// geometry signal without reattaching the shader.
    std::shared_ptr<PhosphorAnimationShaders::AnimationShaderEffect::FboExtentKind> fboExtentKindPtr;
};

/// Shader-attach machinery shared between the fresh-attach path
/// (`attachShaderToAnchor`) and `runLeg`'s reuse path. Given external
/// linkage in the `detail` namespace (following the `internal`
/// precedent above) so the definitions can live in
/// surfaceanimator_shaderattach.cpp while surfaceanimator_tracks.cpp
/// calls them. Doc comments live with the definitions.
QQuickItem* findShaderAnchorRecursive(QQuickItem* root);

QList<QPointer<QQuickItem>> hideAnchorSiblings(QQuickItem* shaderAnchor, QQuickItem* shaderItem,
                                               QQuickItem* shaderSource, bool foundExplicitAnchor);

void applyEffectStaticConfig(PhosphorRendering::ShaderEffect* shaderItem,
                             const PhosphorAnimationShaders::AnimationShaderEffect& effect,
                             const QStringList& shaderIncludePaths);

void syncShaderGeometryNow(QQuickItem* anchor, PhosphorRendering::ShaderEffect* shaderItem,
                           QQuickShaderEffectSource* shaderSource,
                           PhosphorAnimationShaders::AnimationShaderEffect::FboExtentKind extent);

ShaderAttachResult attachShaderToAnchor(QQuickItem* target,
                                        const PhosphorAnimationShaders::AnimationShaderEffect& effect,
                                        const QString& shaderEffectId, const QVariantMap& shaderParameters,
                                        bool isShowLeg, const QStringList& shaderIncludePaths);

} // namespace detail

// ══════════════════════════════════════════════════════════════════════════
// SurfaceAnimator::Private
// ══════════════════════════════════════════════════════════════════════════

class SurfaceAnimator::Private
{
public:
    /// Composite key for in-flight tracks. A single Surface can host
    /// multiple animated targets simultaneously (the unified overlay
    /// shell pattern: one per-screen wl_surface containing OSD,
    /// zone-selector, and modal items as siblings, each animated
    /// independently). Pre-shell, m_tracks was keyed on Surface*
    /// alone — adding a second target on the same surface would
    /// supersede the first via cancelTracking. Now tracks are keyed
    /// on the pair so concurrent legs on different items in the
    /// same surface coexist.
    struct TrackKey
    {
        PhosphorLayer::Surface* surface;
        QQuickItem* target;
        bool operator==(const TrackKey&) const = default;
    };
    struct TrackKeyHash
    {
        std::size_t operator()(const TrackKey& k) const noexcept
        {
            // Mix the two pointers — XOR alone collides on
            // (a, b) and (b, a). Shift the second half to break
            // that symmetry; std::hash<void*> on a sane stdlib is
            // identity for pointers, so the shift matters.
            return std::hash<const void*>()(k.surface) ^ (std::hash<const void*>()(k.target) << 1);
        }
    };

    /// Per-(surface, target) in-flight bookkeeping. unique_ptr<AnimatedValue>
    /// so slot.opacity.reset() can null the AV before installing a
    /// fresh one — AnimatedValue itself has no moved-from state.
    struct Track
    {
        QPointer<QQuickItem> target; ///< Auto-nulls if QML scene tears down mid-flight
        /// Where the shader effect is parented + sized. Equal to
        /// `target` unless an explicit `property bool shaderAnchor:
        /// true` was found by attachShaderToAnchor.
        QPointer<QQuickItem> shaderAnchor;
        bool foundExplicitAnchor = false;
        std::unique_ptr<PhosphorAnimation::AnimatedValue<qreal>> opacity;
        std::unique_ptr<PhosphorAnimation::AnimatedValue<qreal>> scale;
        std::unique_ptr<PhosphorAnimation::AnimatedValue<qreal>> shaderTime;
        QPointer<PhosphorRendering::ShaderEffect> shaderItem;
        QPointer<QQuickShaderEffectSource> shaderSource;
        /// Anchor siblings flipped to invisible for the leg. Restored on
        /// teardown. QPointer auto-nulls if the QML scene tears down mid-
        /// flight.
        QList<QPointer<QQuickItem>> hiddenSiblings;
        /// Effect id of the currently-attached shader item (empty when no
        /// shader leg is active). Used by runLeg to detect "same shader,
        /// can reuse the existing ShaderEffect/Source pair across legs"
        /// and skip the create-then-destroy-then-recreate churn that
        /// otherwise leaks render-thread RHI resources under rapid
        /// show/hide toggling (zone selector during a drag).
        QString shaderEffectId;
        /// Shared with the syncGeometry lambda so a metadata hot-reload
        /// that flips fboExtentKind (Anchor ↔ Surface) takes effect on
        /// the next geometry signal without reattaching.
        std::shared_ptr<PhosphorAnimationShaders::AnimationShaderEffect::FboExtentKind> fboExtentKindPtr;
        int pendingLegs = 0;
        bool shaderExclusive = false; ///< Shader replaces motion legs
        qreal targetOpacity = 1.0; ///< Snapped on completion when shaderExclusive
        /// Install stamp from m_nextTrackGeneration. runLeg's post-write
        /// re-find compares it so a slot REPLACED by a re-entrant
        /// beginShow/beginHide (not merely erased) is never adopted by
        /// the outer, superseded leg.
        quint64 generation = 0;
        ISurfaceAnimator::CompletionCallback onComplete;
        /// Per-leg `iFrame` counter pushed to the shader item by
        /// `pushDynamicShaderUniforms`. Resets to 0 on each fresh attach
        /// (matches overlay convention: `iFrame` starts at 0 when the
        /// shader first runs and increments per rendered frame).
        /// Post-incremented at push time so the first frame sees 0,
        /// the second sees 1, etc. Reset on reuse-attach as well —
        /// `iFrame` is per-leg, not per-shader-item-lifetime, so a
        /// reused shader item starts fresh at 0 on the next leg.
        int shaderFrameCount = 0;
    };

    /// Stash for shader pieces between legs. Populated by
    /// teardownShaderLeg, drained by runLeg's reuse path or by
    /// destroyPendingReuseFor. See teardownShaderLeg's comment for
    /// the rationale (avoids RHI resource batch pool exhaustion under
    /// rapid show/hide toggling).
    struct PendingReuseShader
    {
        QPointer<PhosphorRendering::ShaderEffect> shaderItem;
        QPointer<QQuickShaderEffectSource> shaderSource;
        QPointer<QQuickItem> shaderAnchor;
        bool foundExplicitAnchor = false;
        QString shaderEffectId;
        QPointer<QQuickItem> target;
        /// See Track::fboExtentKindPtr.
        std::shared_ptr<PhosphorAnimationShaders::AnimationShaderEffect::FboExtentKind> fboExtentKindPtr;
    };

    Private(PhosphorAnimation::PhosphorProfileRegistry& registry,
            PhosphorAnimationShaders::AnimationShaderRegistry* shaderRegistry, Config defaults);

    void teardownShaderLeg(PhosphorLayer::Surface* surface, Track& track);
    void destroyPendingReuseEntry(PendingReuseShader& pending);
    void destroyPendingReuseFor(PhosphorLayer::Surface* surface);
    void destroyPendingReuseForKey(const TrackKey& key);
    void connectSurfaceCleanup(PhosphorLayer::Surface* surface);
    void cancelTrackingFor(PhosphorLayer::Surface* surface, QQuickItem* target);
    void cancelAllForSurface(PhosphorLayer::Surface* surface);
    Config configFor(const PhosphorLayer::Role& role) const;
    void runLeg(PhosphorLayer::Surface* surface, QQuickItem* target, qreal fromOpacity, qreal toOpacity,
                const QString& opacityProfilePath, qreal fromScale, qreal toScale, const QString& scaleProfilePath,
                const QString& shaderEffectId, const QString& shaderProfilePath, const QVariantMap& shaderParameters,
                ISurfaceAnimator::CompletionCallback onComplete);
    void legCompleted(PhosphorLayer::Surface* surface, QQuickItem* target);
    void tickAll();
    void pushDynamicShaderUniforms(Track& track, qreal deltaSecs);
    void seedShaderUniformsAtAttach(Track& track);
    void ensureDriving();

    PhosphorAnimation::PhosphorProfileRegistry& m_registry;
    PhosphorAnimationShaders::AnimationShaderRegistry* m_shaderRegistry = nullptr;
    Config m_defaultConfig;
    /// Global animation enable. Mirrors `Settings::animationsEnabled`
    /// — flipped via `SurfaceAnimator::setEnabled`. When false,
    /// `beginShow`/`beginHide` snap to the target opacity and fire
    /// completion synchronously, skipping the opacity / scale / shader
    /// legs entirely. Default true so an animator built without
    /// explicit setEnabled honors the historic always-on behaviour.
    bool m_enabled = true;
    /// Latest CAVA / audio-spectrum sample fed by the consumer via
    /// `SurfaceAnimator::setAudioSpectrum`. Pushed verbatim to every
    /// active animation shader item per-tick (see
    /// `pushDynamicShaderUniforms`) and at attach time (see
    /// `seedShaderUniformsAtAttach`). Empty = no audio data yet.
    QVector<float> m_audioSpectrum;
    /// Steady-clock-ns timestamp of the last `tickAll` invocation.
    /// Used to compute real-time `iTimeDelta` for active shader items.
    /// Reset to 0 when the driver stops so the first tick after the
    /// next `ensureDriving()` reports 0 delta instead of the
    /// wall-clock gap accrued while idle.
    qint64 m_lastShaderTickNs = 0;
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
    std::unordered_map<TrackKey, Track, TrackKeyHash> m_tracks;
    /// Per-(surface, target) stash for ShaderEffect/Source pieces between
    /// legs. Populated by teardownShaderLeg, drained on reuse-claim in
    /// runLeg or on surface destruction (connectSurfaceCleanup wires
    /// the destroyed signal). Keeps one shader render-tower alive for
    /// the (surface, target) pair's lifetime instead of the
    /// create-then-destroy churn that used to leak Qt RHI resource
    /// update batches under rapid show/hide toggling.
    std::unordered_map<TrackKey, PendingReuseShader, TrackKeyHash> m_pendingReuse;
    /// Per-surface destroyed-signal connections, keyed by raw Surface*
    /// pointer. Replaces the earlier `p_surfaceAnimatorCleanupConnected`
    /// dynamic-property gate which leaked across animator instances —
    /// a fresh animator that re-encountered the same Surface would skip
    /// wiring its own cleanup, leaking its m_pendingReuse entries on
    /// surface destruction. Per-instance tracking ensures each animator
    /// installs (and on its own dtor disconnects via m_driverTimer's
    /// auto-disconnect) exactly one slot per surface. Map entries are
    /// removed when the surface dies (slot self-removes its own key).
    QHash<PhosphorLayer::Surface*, QMetaObject::Connection> m_destroyedConnections;
    /// Monotonic source for Track::generation install stamps (see the
    /// Track field's doc); never reset, starts at 1 so a default-
    /// constructed Track (generation 0) can never match a live leg.
    quint64 m_nextTrackGeneration = 1;
    /// Graveyard for AVs whose final tick fired legCompleted from
    /// inside their own spec.onComplete (AnimatedValue::advance()'s
    /// re-entrancy contract forbids destroying *this from a spec
    /// callback). Drained at the start
    /// of the next tickAll. MUST be declared after m_clock and
    /// before m_driverTimer so the destruction order is sound.
    std::vector<std::unique_ptr<PhosphorAnimation::AnimatedValue<qreal>>> m_pendingDestroy;
    /// Drives advance() at ~60Hz while any track is in flight.
    /// Declared LAST so the timer stops BEFORE m_tracks tears down on
    /// dtor — otherwise a final tick could fire after the tracks
    /// have been freed.
    QTimer m_driverTimer;
};

} // namespace PhosphorAnimationLayer
