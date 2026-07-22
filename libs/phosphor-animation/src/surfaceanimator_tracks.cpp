// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "surfaceanimator_p.h"

#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/MotionSpec.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorRendering/ShaderEffect.h>

#include <PhosphorLayer/Surface.h>

#include <QDir>
#include <QFile>
#include <QQuickItem>
#include <private/qquickshadereffectsource_p.h>

#include <functional>
#include <memory>

namespace PhosphorAnimationLayer {

using namespace detail;

namespace {

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
    // Inheritance-aware resolve so a parent-node card edit at e.g.
    // `popup` propagates to every `popup.*.*` leg even
    // when the bundled per-leaf JSONs (popup.layoutPicker.show
    // .json etc.) hardcode their own duration. The previous direct
    // `registry.resolve(path)` call only matched the exact leaf —
    // bundled leaf entries silently shadowed the user's parent
    // override and the slider had no visible effect on legs whose
    // bundled JSON happened to ship.
    return registry.resolveWithInheritance(path);
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

} // namespace

/// Wind down the per-leg shader pieces, parking the
/// ShaderEffect/Source pair into m_pendingReuse[surface] so a
/// follow-up runLeg can reclaim them under the same effect id.
///
/// The shaderTime AV is leg-specific (its `from`/`to` are tied to
/// the leg's iTime direction) and goes to the graveyard regardless.
/// Sibling visibility is RESTORED on park — between legs the
/// surface is visually idle, and a follow-up reuse-attach hides
/// them again before its own first paint.
///
/// CRUCIALLY the parked items are made DORMANT — `setVisible(false)`
/// on the shaderItem stops its prepare()/render() each frame, and
/// `setLive(false)` on the QQuickShaderEffectSource stops its FBO
/// refresh. Without this the parked items keep consuming a Qt RHI
/// resource update batch per frame each (UBO upload + FBO render)
/// for the rest of the surface's lifetime — across rapid toggling
/// the 64-batch global pool exhausts within seconds even though
/// reuse "fixed" the per-leg ShaderEffect creation. The dormant
/// state contributes zero per-frame cost.
///
/// `setHideSource(false)` is also restored so the original anchor
/// renders normally during the idle phase between legs — without
/// it, the anchor stays suppressed from the scene render path
/// (because hideSource installs a QQuickItemLayer on the source)
/// and decorator siblings (drop shadows, badges) sample empty
/// content, making the post-leg static-Shown state look broken.
///
/// Without parking, every leg's shader pieces went to deleteLater
/// and the render thread couldn't drain them fast enough under
/// rapid show/hide toggling — same pool exhaustion via a different
/// mechanism. Parking + dormant state lets one ShaderEffect /
/// Source / ShaderNodeRhi tower live for the surface's lifetime
/// while contributing no per-frame cost between legs.
///
/// Surface destruction is handled by the destroyed-signal
/// connection installed in connectSurfaceCleanup — when surface
/// dies, the QPointers in m_pendingReuse auto-null and the entry
/// is dropped.
void SurfaceAnimator::Private::teardownShaderLeg(PhosphorLayer::Surface* surface, Track& track)
{
    if (track.shaderTime) {
        track.shaderTime->cancel();
        m_pendingDestroy.push_back(std::move(track.shaderTime));
    }
    // Restore previously-hidden sibling decorators FIRST. Reused
    // shader runs hide them again on next attach, so this just
    // covers the between-legs and post-final-leg cases.
    for (const QPointer<QQuickItem>& sibling : track.hiddenSiblings) {
        if (sibling) {
            sibling->setVisible(true);
        }
    }
    track.hiddenSiblings.clear();
    if (!track.shaderItem || track.shaderEffectId.isEmpty()) {
        // Nothing to park (non-shader track, or shader pieces
        // already moved out by an earlier reuse-claim).
        track.shaderItem = nullptr;
        track.shaderSource = nullptr;
        track.shaderAnchor.clear();
        track.foundExplicitAnchor = false;
        track.shaderEffectId.clear();
        return;
    }

    // If track.target's QPointer auto-nulled because the
    // QQuickItem was destroyed mid-flight, parking under
    // {surface, nullptr} keys the entry under a sentinel that
    // runLeg never queries (it always passes a non-null target).
    // Skip the park step: nothing useful to reuse, and burning
    // an unreclaimable slot in m_pendingReuse just leaks until
    // surface destruction. Tear the pieces down inline.
    if (!track.target) {
        PendingReuseShader transient;
        transient.shaderItem = std::move(track.shaderItem);
        transient.shaderSource = std::move(track.shaderSource);
        transient.shaderAnchor = track.shaderAnchor;
        destroyPendingReuseEntry(transient);
        track.shaderItem = nullptr;
        track.shaderSource = nullptr;
        track.shaderAnchor.clear();
        track.foundExplicitAnchor = false;
        track.shaderEffectId.clear();
        track.fboExtentKindPtr.reset();
        return;
    }
    // Park to m_pendingReuse[{surface, target}]. If a previous
    // pending entry exists for this (surface, target) pair (rare —
    // should only happen if a shader was parked but never
    // reclaimed), destroy it first to avoid leaking a stale
    // ShaderEffect.
    connectSurfaceCleanup(surface);
    PendingReuseShader& pending = m_pendingReuse[TrackKey{surface, track.target.data()}];
    if (pending.shaderItem) {
        destroyPendingReuseEntry(pending);
    }
    pending.shaderItem = std::move(track.shaderItem);
    pending.shaderSource = std::move(track.shaderSource);
    pending.shaderAnchor = track.shaderAnchor;
    pending.foundExplicitAnchor = track.foundExplicitAnchor;
    pending.shaderEffectId = std::move(track.shaderEffectId);
    pending.target = track.target;
    pending.fboExtentKindPtr = std::move(track.fboExtentKindPtr);

    // Make parked pieces dormant — see method docstring.
    if (pending.shaderItem) {
        pending.shaderItem->setVisible(false);
    }
    if (pending.shaderSource) {
        // setLive(false) freezes FBO refresh. setHideSource(false)
        // re-enables the anchor's normal scene render so the user
        // sees the anchor + decorators during the idle phase
        // exactly as they would without any shader.
        pending.shaderSource->setLive(false);
        pending.shaderSource->setHideSource(false);
    }

    track.shaderItem = nullptr;
    track.shaderSource = nullptr;
    track.shaderAnchor.clear();
    track.foundExplicitAnchor = false;
    track.shaderEffectId.clear();
    track.fboExtentKindPtr.reset();
}

/// Destroy a single pending-reuse entry's render-side pieces.
/// Disconnects the anchor signal hookups before deleteLater so
/// the scene-graph reflow window between deleteLater and dispatch
/// can't fire xChanged/yChanged/widthChanged/heightChanged on the
/// anchor into a queued-for-delete receiver.
void SurfaceAnimator::Private::destroyPendingReuseEntry(PendingReuseShader& pending)
{
    if (pending.shaderItem) {
        if (pending.shaderAnchor) {
            QObject::disconnect(pending.shaderAnchor.data(), nullptr, pending.shaderItem.data(), nullptr);
        }
        pending.shaderItem->deleteLater();
        pending.shaderItem = nullptr;
    }
    if (pending.shaderSource) {
        pending.shaderSource->deleteLater();
        pending.shaderSource = nullptr;
    }
    pending.shaderAnchor.clear();
    pending.foundExplicitAnchor = false;
    pending.shaderEffectId.clear();
    pending.target.clear();
    pending.fboExtentKindPtr.reset();
}

/// Drop the reuse stash for one surface — used when the surface is
/// destroyed, when the next leg uses a different effect / target,
/// or when a non-shader leg supersedes a shader leg.
/// Walks every (surface, *) entry — a surface may have multiple
/// targets parked simultaneously now that the unified shell hosts
/// independent items as siblings on one Surface.
void SurfaceAnimator::Private::destroyPendingReuseFor(PhosphorLayer::Surface* surface)
{
    for (auto it = m_pendingReuse.begin(); it != m_pendingReuse.end();) {
        if (it->first.surface == surface) {
            destroyPendingReuseEntry(it->second);
            it = m_pendingReuse.erase(it);
        } else {
            ++it;
        }
    }
}

/// Single-(surface, target) variant. Used by runLeg's null-target
/// path: target is null, so iterate every (surface, *) anyway.
/// Used by teardownShaderLeg-driven reuse claim: caller has
/// already moved fields out, so we just erase.
void SurfaceAnimator::Private::destroyPendingReuseForKey(const TrackKey& key)
{
    const auto it = m_pendingReuse.find(key);
    if (it == m_pendingReuse.end()) {
        return;
    }
    destroyPendingReuseEntry(it->second);
    m_pendingReuse.erase(it);
}

/// Wire surface->destroyed once per surface (per animator instance)
/// so we drop its reuse stash when it dies, and (defensively) any
/// still-tracked entry. Tracked in m_destroyedConnections — the
/// per-animator map ensures that a surface re-encountered by a
/// FRESH animator instance (e.g. test fixtures, daemon hot-restart
/// reusing a Surface*) gets its OWN connection rather than seeing a
/// dynamic-property flag set by a long-dead prior animator.
/// `contains()` is the gate; `insert()` ties the Connection lifetime
/// to the map entry so a future `disconnect` can be issued via
/// `QObject::disconnect(connection)` if we ever need explicit
/// teardown without surface destruction.
void SurfaceAnimator::Private::connectSurfaceCleanup(PhosphorLayer::Surface* surface)
{
    if (m_destroyedConnections.contains(surface)) {
        return;
    }
    // Cast is address-only-safe — destroyed fires from ~QObject
    // after ~Surface ran, so the value MUST NOT be dereferenced.
    // Using the QObject* form keeps the contract obvious.
    // Use m_driverTimer as the receiver context — it's a QObject
    // member of Private, so when this animator is destroyed
    // m_driverTimer dies first (member dtor order = reverse of
    // declaration; m_driverTimer is declared LAST) and the
    // connection auto-disconnects before m_pendingReuse tears
    // down. SurfaceAnimator itself isn't a QObject so we can't
    // use `this` as the receiver.
    QMetaObject::Connection conn =
        QObject::connect(surface, &QObject::destroyed, &m_driverTimer, [this](QObject* dying) {
            // Address-only cast — dying object is partially destroyed;
            // we only use the pointer value as a map key.
            auto* surf = static_cast<PhosphorLayer::Surface*>(dying);
            // Mutating m_destroyedConnections at the end of this
            // slot is safe because destroyPendingReuseFor's only
            // synchronous side-effect is deleteLater() on shader
            // pieces — Qt's deferred-delete mechanism queues the
            // destruction for the next event-loop iteration and
            // never re-enters connectSurfaceCleanup. If a future
            // edit ever makes destroyPendingReuseEntry synchronous
            // (e.g. a hard delete instead of deleteLater) and
            // re-enters this animator, this remove() must happen
            // BEFORE that path or be re-checked for invalidation.
            destroyPendingReuseFor(surf);
            // Also drop every still-tracked (surf, *) entry;
            // consumer's SurfaceAnimator::cancel may not have run
            // for surfaces that bypass the standard ~Surface cancel
            // path. Walk the map and erase all matching entries.
            for (auto trackIt = m_tracks.begin(); trackIt != m_tracks.end();) {
                if (trackIt->first.surface == surf) {
                    trackIt = m_tracks.erase(trackIt);
                } else {
                    ++trackIt;
                }
            }
            m_destroyedConnections.remove(surf);
        });
    m_destroyedConnections.insert(surface, conn);
}

/// Cancel a single (surface, target) leg. Called by runLeg's pre-
/// cancel (supersession of the SAME target) and by cancelAllForSurface.
/// AnimatedValue::cancel clears m_isAnimating without firing
/// spec.onComplete (cancel = non-completion termination per
/// ISurfaceAnimator). AVs are parked in m_pendingDestroy because a
/// re-entrant cancel from inside spec.onValueChanged must not
/// destroy *this mid-advance (AnimatedValue.h:547); graveyard
/// drains on next tickAll. Shader pieces are parked in
/// m_pendingReuse (see teardownShaderLeg) so they survive the
/// external Surface::show()/hide() pre-cancel and can be reclaimed
/// by the immediately-following beginShow/Hide.
void SurfaceAnimator::Private::cancelTrackingFor(PhosphorLayer::Surface* surface, QQuickItem* target)
{
    const auto it = m_tracks.find(TrackKey{surface, target});
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
    teardownShaderLeg(surface, it->second);
    m_tracks.erase(it);
}

/// Cancel every in-flight leg for the surface, regardless of target.
/// Used by the public ISurfaceAnimator::cancel(Surface*) contract,
/// which means "wipe all motion for this surface" — appropriate for
/// teardown paths and consumer-driven dismissals where every item
/// hosted by the surface should stop animating.
void SurfaceAnimator::Private::cancelAllForSurface(PhosphorLayer::Surface* surface)
{
    // Snapshot keys first — cancelTrackingFor erases from m_tracks,
    // and erase + iterate on the same unordered_map invalidates
    // iterators of the erased bucket.
    std::vector<QQuickItem*> targets;
    targets.reserve(m_tracks.size());
    for (const auto& [key, _] : m_tracks) {
        if (key.surface == surface) {
            targets.push_back(key.target);
        }
    }
    for (QQuickItem* target : targets) {
        cancelTrackingFor(surface, target);
    }
}

/// Run a show or hide leg. Used by beginShow/beginHide. Threads
/// the from→to opacity/scale pair, the profile pair, the optional
/// shader leg, and the caller's onComplete; fires onComplete once
/// every leg settles. Empty *ProfilePath = skip that leg.
void SurfaceAnimator::Private::runLeg(PhosphorLayer::Surface* surface, QQuickItem* target, qreal fromOpacity,
                                      qreal toOpacity, const QString& opacityProfilePath, qreal fromScale,
                                      qreal toScale, const QString& scaleProfilePath, const QString& shaderEffectId,
                                      const QString& shaderProfilePath, const QVariantMap& shaderParameters,
                                      ISurfaceAnimator::CompletionCallback onComplete)
{
    // Resolve shader up front so legCount is accurate.
    PhosphorAnimationShaders::AnimationShaderEffect resolvedShaderEff;
    QStringList animIncludePaths;
    if (!shaderEffectId.isEmpty() && m_shaderRegistry) {
        resolvedShaderEff = m_shaderRegistry->effect(shaderEffectId);
        // A compositor-only pack (desktop / geometry / move classes) is
        // authored against the kwin classic-GL dialect with no daemon
        // branch: attaching it would fail the strict SPIR-V bake at first
        // paint and stall the leg. This is reachable through a picker-legal
        // config — the picker is permissive on ambiguous rows (e.g. the
        // `global` root), so a geometry-only pack assigned there cascades to
        // every OSD/popup leg via resolveShaderWithDefault, which does no
        // applicability filtering. Routine, so log at debug level only —
        // drop the shader leg and let the opacity/scale legs run.
        if (PhosphorAnimationShaders::shaderEffectIsCompositorOnly(resolvedShaderEff)) {
            qCDebug(lcSurfaceAnimator) << "runLeg: shader effect" << shaderEffectId << "is compositor-only (appliesTo"
                                       << resolvedShaderEff.appliesTo << ") — skipping shader leg on daemon surface";
            resolvedShaderEff = {};
        }
        // Include paths (and the default-vert fallback) only matter when a
        // shader leg will actually attach — skip the per-leg filesystem
        // probes when the effect resolved invalid or was refused above.
        if (resolvedShaderEff.isValid()) {
            for (const QString& sp : m_shaderRegistry->searchPaths()) {
                const QString sharedDir = sp + QStringLiteral("/shared");
                if (QDir(sharedDir).exists()) {
                    animIncludePaths.append(sharedDir);
                    if (resolvedShaderEff.vertexShaderPath.isEmpty()) {
                        const QString sharedVert = sharedDir + QStringLiteral("/animation.vert");
                        if (QFile::exists(sharedVert)) {
                            resolvedShaderEff.vertexShaderPath = sharedVert;
                        }
                    }
                }
            }
        }
    }
    const bool hasShaderLeg = resolvedShaderEff.isValid();

    // Supersede BEFORE the null-target check — leaving a prior
    // entry behind on null-target strands its AV (custom
    // orchestrators bypass Surface::Impl::drive()'s pre-cancel).
    // cancelTrackingFor parks any in-flight shader pieces in
    // m_pendingReuse[{surface,target}] (see parkShaderForReuse) so
    // they survive the external Surface::show()/hide() pre-cancel
    // and can be reclaimed below if the new leg uses the same
    // effect. Null target means "supersede whatever was running on
    // any target of this surface" — fall back to surface-wide
    // cancel and drop every stash.
    if (!target) {
        cancelAllForSurface(surface);
        qCWarning(lcSurfaceAnimator) << "runLeg called with null target — onComplete fires synchronously";
        // Null-target dispatch is malformed and won't drive any
        // specific (surface, target) pair. The tickAll sweep will
        // GC stale pending entries that outlive their target's
        // QPointer; do NOT cascade-destroy every sibling slot's
        // parked shader on this surface. The unified-overlay-shell
        // pattern hosts multiple sibling slots per Surface, so
        // surface-wide destruction violates the per-(Surface, target)
        // keying contract.
        // Synchronous onComplete: same forbidden-ops contract as a
        // legCompleted-fired callback (SurfaceAnimator.h:79-90).
        if (onComplete) {
            onComplete();
        }
        return;
    }
    cancelTrackingFor(surface, target);

    // Reuse stashed shader pieces from the previous leg if the
    // effect id, target, anchor, and shader item all still match
    // and are alive. Under rapid show/hide toggling (zone selector
    // during a drag), creating a fresh QQuickShaderEffectSource
    // (with its own FBO) plus PhosphorRendering::ShaderEffect (with
    // its own QSGRenderNode + ShaderNodeRhi) on every leg outpaces
    // deleteLater + render-thread cleanup, accumulating render
    // nodes that each consume a Qt RHI resource update batch from
    // the 64-batch global pool — the journal eventually fills with
    // "Resource update batch pool exhausted (max is 64)" and the
    // compositor stalls. Reuse skips the create-then-destroy-then-
    // recreate churn entirely: one ShaderEffect lives for the
    // surface's lifetime (or until the effect id changes), and
    // successive legs just reset iTime and the shaderTime AV.
    QPointer<PhosphorRendering::ShaderEffect> reusedShaderItem;
    QPointer<QQuickShaderEffectSource> reusedShaderSource;
    QPointer<QQuickItem> reusedShaderAnchor;
    bool reusedFoundExplicit = false;
    std::shared_ptr<PhosphorAnimationShaders::AnimationShaderEffect::FboExtentKind> reusedFboExtentKindPtr;
    if (hasShaderLeg) {
        const auto pendIt = m_pendingReuse.find(TrackKey{surface, target});
        if (pendIt != m_pendingReuse.end()) {
            PendingReuseShader& pending = pendIt->second;
            // Re-validate anchor against the current QML scene.
            // The shell's per-mode Loader unloads + loads its content
            // on `mode` / `loaded` flips (e.g. layout-osd ↔ navigation-
            // osd, snap-assist re-instantiate), destroying the
            // previously-found shaderAnchor descendant. The cached
            // QPointer auto-nulls when the underlying QObject has
            // been destroyed — `pending.shaderAnchor.data() == nullptr`
            // in that case, so the equality compare against
            // `currentAnchor` (which is the live anchor or null)
            // correctly rejects reuse and forces a fresh attach. Even
            // when QPointer hasn't yet cleared (immediately after
            // deleteLater queues but before the event loop runs), the
            // pointer would refer to the previous Loader generation's
            // anchor, not the freshly-loaded one — also a mismatch
            // against `currentAnchor` (which is the *new* anchor) and
            // also correctly rejected. Reuse only when the cache
            // resolves to the same QQuickItem that the live tree's
            // shaderAnchor walk finds (or both fall back to target).
            QQuickItem* currentAnchor = findShaderAnchorRecursive(target);
            const bool anchorMatches = pending.foundExplicitAnchor
                ? (currentAnchor != nullptr && currentAnchor == pending.shaderAnchor.data())
                : (currentAnchor == nullptr && pending.shaderAnchor.data() == target);
            if (pending.shaderItem && pending.shaderAnchor && pending.shaderEffectId == shaderEffectId
                && pending.target.data() == target && anchorMatches) {
                reusedShaderItem = std::move(pending.shaderItem);
                reusedShaderSource = std::move(pending.shaderSource);
                reusedShaderAnchor = pending.shaderAnchor;
                reusedFoundExplicit = pending.foundExplicitAnchor;
                reusedFboExtentKindPtr = std::move(pending.fboExtentKindPtr);
                // Refresh the live fboExtentKind so the persistent
                // syncGeometry lambda (still connected from the
                // original attach) picks up the new metadata value
                // on the next anchor geometry signal. Without this
                // refresh, an in-place metadata.json hot-reload that
                // flips `fboExtent` between Anchor and Surface
                // leaves the lambda using the OLD kind when the
                // anchor next moves/resizes.
                if (reusedFboExtentKindPtr) {
                    *reusedFboExtentKindPtr = resolvedShaderEff.fboExtentKind;
                }
                m_pendingReuse.erase(pendIt);
            } else {
                // Mismatch (different effect, QML scene rebuild
                // swapped the anchor, or stale QPointers from item
                // destruction) — drop ONLY this {surface, target}
                // entry. Sibling slots on the same shell surface
                // (OSD, zone-selector, etc.) keep their parked
                // shaders intact; per-(Surface, target) keying is
                // the whole point of TrackKey.
                destroyPendingReuseEntry(pending);
                m_pendingReuse.erase(pendIt);
            }
        }
    } else {
        // Non-shader leg following a shader leg on THIS target —
        // drop ONLY this target's stash. Other slots on the same
        // surface may have valid parked shaders that should
        // survive (per-(Surface, target) keying contract).
        destroyPendingReuseForKey(TrackKey{surface, target});
    }

    PhosphorAnimation::IMotionClock* clock = &m_clock;

    // When a shader leg is active, it IS the transition — suppress
    // the scale leg so the built-in popin motion doesn't fight the
    // shader's own visual. Matches the Niri model where custom
    // shaders replace the default animation, not layer on top.
    const bool hasScaleLeg = !scaleProfilePath.isEmpty() && !hasShaderLeg;
    const int legCount = 1 + (hasScaleLeg ? 1 : 0) + (hasShaderLeg ? 1 : 0);

    // Install the bookkeeping slot BEFORE the synchronous pre-
    // state writes below. setOpacity / setScale fire QML changed
    // signals synchronously, and a consumer binding can re-enter
    // cancel(surface). With the slot installed first, that cancel
    // erases cleanly (no AVs to park yet) and the post-write
    // re-find below detects the erasure and bails.
    {
        Track& slot = m_tracks[TrackKey{surface, target}];
        slot.opacity.reset();
        slot.scale.reset();
        // Reset shader-leg state explicitly — a non-shader leg
        // following a shader leg (e.g. show w/ pixelate → hide w/o)
        // would otherwise leave stale anchor/shaderItem QPointers
        // hanging around since teardownShaderLeg only runs when the
        // prior leg had hasShaderLeg.
        slot.shaderAnchor.clear();
        slot.shaderItem = nullptr;
        slot.shaderSource = nullptr;
        slot.hiddenSiblings.clear();
        slot.foundExplicitAnchor = false;
        slot.shaderEffectId.clear();
        slot.fboExtentKindPtr.reset();
        slot.target = target;
        slot.onComplete = std::move(onComplete);
        slot.pendingLegs = legCount;
        slot.shaderExclusive = hasShaderLeg;
        slot.targetOpacity = toOpacity;
    }

    // When a shader leg is active, the shader IS the entire
    // visual transition (Niri model). The surface must be fully
    // visible while the shader runs — show snaps to 1.0 so the
    // shader's first frames aren't invisible; hide keeps the
    // current opacity (shader drives perceived fade-out), then
    // snaps to 0.0 on shader completion (see legCompleted below).
    //
    // Scale must ALSO snap to 1.0: the scale leg is suppressed
    // when a shader is active (`hasScaleLeg` gates on `!hasShaderLeg`),
    // and a previous non-shader hide that ran a scale-down leg
    // leaves `target->scale()` at hideScaleTo. Without snapping,
    // the QML root carries that residual scale into the shader
    // leg and the entire subtree — including our shaderItem and
    // any redirected MultiEffect — renders at the reduced scale.
    // The user sees a correctly-rendered shader at the wrong
    // visual size and a "pop to full size" at teardown when the
    // next non-shader cycle restores scale via its own scale leg.
    if (hasShaderLeg) {
        const bool isShow = (toOpacity > fromOpacity);
        target->setOpacity(isShow ? toOpacity : fromOpacity);
        target->setScale(1.0);
    } else {
        target->setOpacity(fromOpacity);
    }
    if (hasScaleLeg) {
        target->setScale(fromScale);
    }

    // Construct AVs atomically BEFORE start() on either — a
    // synchronous onValueChanged that re-enters cancel(surface)
    // would otherwise erase the slot between assignments. Re-find
    // here too in case the setOpacity/setScale chain above just
    // re-entered cancel() — bail per the cancellation contract.
    {
        auto it = m_tracks.find(TrackKey{surface, target});
        if (it == m_tracks.end()) {
            return;
        }
        if (!hasShaderLeg) {
            it->second.opacity = std::make_unique<PhosphorAnimation::AnimatedValue<qreal>>();
        }
        if (hasScaleLeg) {
            it->second.scale = std::make_unique<PhosphorAnimation::AnimatedValue<qreal>>();
        }
        if (hasShaderLeg) {
            const bool isShowLeg = (toOpacity > fromOpacity);
            if (reusedShaderItem) {
                // Reuse path: keep the existing ShaderEffect /
                // ShaderEffectSource / ShaderNodeRhi tower across
                // the leg boundary. The pieces were made dormant
                // by teardownShaderLeg's park step (setVisible
                // (false), setLive(false), setHideSource(false))
                // to avoid per-frame RHI batch consumption during
                // the idle phase. Wake them up here BEFORE setting
                // any leg-specific state so the SG sync that
                // follows runLeg picks them up live.
                if (reusedShaderSource) {
                    // Wake the source FBO + re-hide the anchor from
                    // direct scene render. The park step in
                    // `teardownShaderLeg` calls
                    // `setHideSource(false)` to restore the anchor's
                    // normal rendering during the idle phase, and
                    // that toggle tears down the source item's
                    // internal `QQuickItemLayer`. A bare
                    // `setHideSource(true)` here doesn't reliably
                    // rebuild that layer — the FBO comes back live
                    // but empty, so the shader samples a
                    // transparent uTexture0 throughout the leg and
                    // the user sees a "pop in" at completion when
                    // the post-leg `setOpacity(1.0)` restores the
                    // anchor's direct render.
                    //
                    // Force a clean layer reattach by clearing the
                    // source item first, then re-pointing it at
                    // the anchor. Qt rebuilds the layer + FBO from
                    // scratch, mirroring the fresh-attach setup at
                    // `attachShaderToAnchor`. The matching order
                    // (setLive, setHideSource, setSourceItem) keeps
                    // a single code-path's worth of state machine
                    // for the source.
                    reusedShaderSource->setSourceItem(nullptr);
                    reusedShaderSource->setLive(true);
                    reusedShaderSource->setHideSource(true);
                    reusedShaderSource->setSourceItem(reusedShaderAnchor.data());
                }
                reusedShaderItem->setVisible(true);
                // Re-apply per-effect static config so a metadata.json
                // hot-reload that changed multipass / wallpaper / depth
                // / vertex shader between legs propagates onto the
                // reused shader item. Idempotent on identity (the
                // ShaderEffect setters all no-op when the value is
                // unchanged), so the common no-hot-reload case costs
                // only a wallpaper-cache lookup.
                applyEffectStaticConfig(reusedShaderItem.data(), resolvedShaderEff, animIncludePaths);
                // Re-fire the geometry sync so a fboExtentKind
                // hot-reload between legs (refreshed into the
                // shared cell at the reuse-claim site above) takes
                // effect immediately on the new leg's first frame.
                // The persistent syncGeometry lambda only runs on
                // anchor geometry signals; for static popups
                // (snap-assist, OSD) those may never fire. Manually
                // invoking the same logic here closes that gap.
                syncShaderGeometryNow(reusedShaderAnchor.data(), reusedShaderItem.data(), reusedShaderSource.data(),
                                      reusedFboExtentKindPtr
                                          ? *reusedFboExtentKindPtr
                                          : PhosphorAnimationShaders::AnimationShaderEffect::FboExtentKind::Anchor);
                // Reset iTime to the new leg's start so the first
                // painted frame after the shaderTime AV starts
                // matches the AnimatedValue's intended `from` —
                // same defence as attachShaderToAnchor's setITime
                // block. Without this the parked iTime (terminal
                // value of the previous leg) leaks into the first
                // frame of the new leg.
                reusedShaderItem->setITime(isShowLeg ? qreal(0.0) : qreal(1.0));
                // Direction signal for asymmetric shaders. The reuse
                // path may swap a show leg into a hide leg (or vice
                // versa) on the same shader item, so re-push every
                // leg start.
                reusedShaderItem->setIsReversed(!isShowLeg);
                // Re-translate parameter overrides — shaderParameters
                // can differ between legs (per-event customisation).
                const QVariantMap translated =
                    PhosphorAnimationShaders::AnimationShaderRegistry::translateAnimationParams(resolvedShaderEff,
                                                                                                shaderParameters);
                if (!translated.isEmpty()) {
                    reusedShaderItem->setShaderParams(translated);
                }
                // Re-hide decorator siblings of the anchor — they
                // were restored by teardownShaderLeg between legs
                // so the static-Shown decoration was visible
                // during the idle phase, but the new leg's shader
                // needs them out of the way again. Same helper as
                // the fresh-attach path uses.
                QList<QPointer<QQuickItem>> hidden = hideAnchorSiblings(
                    reusedShaderAnchor.data(), reusedShaderItem.data(), reusedShaderSource.data(), reusedFoundExplicit);
                it->second.shaderItem = reusedShaderItem;
                it->second.shaderSource = reusedShaderSource;
                it->second.shaderAnchor = reusedShaderAnchor;
                it->second.foundExplicitAnchor = reusedFoundExplicit;
                it->second.hiddenSiblings = std::move(hidden);
                it->second.shaderEffectId = shaderEffectId;
                it->second.fboExtentKindPtr = std::move(reusedFboExtentKindPtr);
                it->second.shaderTime = std::make_unique<PhosphorAnimation::AnimatedValue<qreal>>();
                seedShaderUniformsAtAttach(it->second);
            } else {
                // Fresh attach: no prior compatible shader pair, or
                // the previous one was for a different effect id.
                // resolvedShaderEff already validated by hasShaderLeg.
                // isShowLeg lets attachShaderToAnchor seed iTime to
                // the leg's start value (0 for show, 1 for hide) so
                // the very first paint after the QQuickShaderEffect
                // is added to the scene matches the AnimatedValue's
                // intended start — see attachShaderToAnchor's
                // setITime block.
                ShaderAttachResult attached = attachShaderToAnchor(target, resolvedShaderEff, shaderEffectId,
                                                                   shaderParameters, isShowLeg, animIncludePaths);
                it->second.shaderItem = attached.shaderItem;
                it->second.shaderSource = attached.shaderSource;
                it->second.shaderAnchor = attached.shaderAnchor;
                it->second.foundExplicitAnchor = attached.foundExplicitAnchor;
                it->second.hiddenSiblings = std::move(attached.hiddenSiblings);
                it->second.shaderEffectId = shaderEffectId;
                it->second.fboExtentKindPtr = std::move(attached.fboExtentKindPtr);
                it->second.shaderTime = std::make_unique<PhosphorAnimation::AnimatedValue<qreal>>();
                seedShaderUniformsAtAttach(it->second);
            }
        }
    }

    // Capture-by-value of `surface` (raw pointer key, never deref'd)
    // and `this` (SurfaceAnimator outlives every Surface — daemon
    // owns both, dtor ordering enforces it) are safe inside the
    // callbacks. Re-find on every access between start()s so a
    // re-entrant cancel mid-flight is honoured.
    if (hasShaderLeg) {
        // Shader-exclusive mode: opacity was snapped to toOpacity
        // above. The opacity "leg" completes immediately — the
        // shader's iTime 0→1 drives the entire visual transition.
        legCompleted(surface, target);
    } else {
        auto it = m_tracks.find(TrackKey{surface, target});
        if (it == m_tracks.end() || !it->second.opacity) {
            return;
        }
        it->second.opacity->start(fromOpacity, toOpacity,
                                  buildSpec(
                                      resolveProfile(m_registry, opacityProfilePath), clock,
                                      /*onValueChanged=*/
                                      [this, surface, target](const qreal& v) {
                                          auto sit = m_tracks.find(TrackKey{surface, target});
                                          if (sit == m_tracks.end() || !sit->second.target) {
                                              return;
                                          }
                                          // Clamp: an overshooting curve's value is
                                          // unbounded, so a hide leg (1 -> 0) with a
                                          // bouncy spring drives this NEGATIVE (the
                                          // step response peaks at 1.163 for zeta=0.5,
                                          // so 1 - 1.163 = -0.163; at zeta=0.05 it is
                                          // -0.85). The sibling shader leg already
                                          // clamps for the same reason.
                                          sit->second.target->setOpacity(qBound(qreal(0.0), v, qreal(1.0)));
                                      },
                                      /*onComplete=*/
                                      [this, surface, target]() {
                                          legCompleted(surface, target);
                                      }));
    }

    if (hasScaleLeg) {
        auto it = m_tracks.find(TrackKey{surface, target});
        if (it == m_tracks.end() || !it->second.scale) {
            // The opacity leg's start() (or a synchronous callback)
            // cancelled the entry; let the cancellation stand.
            return;
        }
        it->second.scale->start(fromScale, toScale,
                                buildSpec(
                                    resolveProfile(m_registry, scaleProfilePath), clock,
                                    /*onValueChanged=*/
                                    [this, surface, target](const qreal& v) {
                                        auto sit = m_tracks.find(TrackKey{surface, target});
                                        if (sit == m_tracks.end() || !sit->second.target) {
                                            return;
                                        }
                                        sit->second.target->setScale(v);
                                    },
                                    /*onComplete=*/
                                    [this, surface, target]() {
                                        legCompleted(surface, target);
                                    }));
    }

    if (hasShaderLeg) {
        auto it = m_tracks.find(TrackKey{surface, target});
        if (it == m_tracks.end() || !it->second.shaderTime || !it->second.shaderItem) {
            return;
        }
        const QString shaderLegProfilePath = shaderProfilePath.isEmpty() ? opacityProfilePath : shaderProfilePath;
        if (shaderLegProfilePath.isEmpty()) {
            qCDebug(lcSurfaceAnimator)
                << "shader leg falling back to library defaults (150ms OutCubic) — both shader and opacity "
                   "profile paths are empty for surface"
                << surface;
        }
        // Run iTime backward on hide so shaders whose visual depends on
        // direction (pixelate dissipating, popin scaling up, slide
        // revealing, dissolve filling in) play in reverse for the hide
        // leg without each shader having to know the leg sign. iTime
        // semantics in the GLSL stay the same — iTime=0 is the
        // "transition start" state (pixelated / small / slid-out /
        // dissolved), iTime=1 is the "fully resolved" state (clear /
        // full size / fully revealed / opaque). On show we tween 0→1;
        // on hide we tween 1→0 so the surface visibly transitions
        // OUT through the same intermediate states.
        //
        // Symmetric envelopes (morph's `sin(iTime*pi)`, glitch's
        // sin-shaped peak) are direction-invariant by construction —
        // the peak still lands mid-leg either way.
        const bool isShowLegInner = (toOpacity > fromOpacity);
        const qreal shaderFrom = isShowLegInner ? qreal(0.0) : qreal(1.0);
        const qreal shaderTo = isShowLegInner ? qreal(1.0) : qreal(0.0);
        // No setITime here — attachShaderToAnchor (called with the
        // same isShowLeg flag) already seeded iTime to shaderFrom
        // before the QQuickShaderEffect could paint a frame.
        const PhosphorAnimation::Profile shaderLegProfile = resolveProfile(m_registry, shaderLegProfilePath);
        qCDebug(lcSurfaceAnimator).nospace()
            << "shader leg start: path=" << shaderLegProfilePath << " duration=" << shaderLegProfile.effectiveDuration()
            << "ms surface=" << surface;
        it->second.shaderTime->start(shaderFrom, shaderTo,
                                     buildSpec(
                                         shaderLegProfile, clock,
                                         /*onValueChanged=*/
                                         [this, surface, target](const qreal& v) {
                                             auto sit = m_tracks.find(TrackKey{surface, target});
                                             // shaderItem QPointer auto-nulls if the
                                             // ShaderEffect's parent (the anchor) was torn
                                             // down between ticks — bail rather than UAF.
                                             if (sit == m_tracks.end() || !sit->second.shaderItem) {
                                                 return;
                                             }
                                             // Clamp iTime to the [0, 1] contract before
                                             // forwarding. Underdamped curves (Spring with
                                             // zeta < 1, e.g. `bouncy`) overshoot the
                                             // target — show legs push the lerp above 1.0
                                             // and hide legs push it below 0.0 at each
                                             // oscillation peak. Shaders treat iTime
                                             // outside [0, 1] as undefined per
                                             // AnimationShaderContract; without clamping,
                                             // a hide leg flicks iTime negative which most
                                             // shaders render as "more than fully gone"
                                             // (transparent/invisible) before the spring
                                             // oscillates back to a slightly positive
                                             // value, producing a visible disappear→re-
                                             // render→disappear flicker. This is a
                                             // DELIBERATE divergence from the compositor,
                                             // which now lets an overshooting curve's iTime
                                             // leave [0,1] (see
                                             // ShaderInternal::clampProgressForCurve). The
                                             // daemon keeps the clamp: its surfaces are
                                             // QQuickItems whose hide legs would flicker on
                                             // an out-of-range value, and it has no
                                             // geometry bounce to stay consistent with. So
                                             // the same pack with the same curve bounces in
                                             // the compositor and is flat here, on purpose.
                                             // Geometry sync runs off the anchor's
                                             // widthChanged/heightChanged signals (see
                                             // syncGeometry); the per-tick callback only
                                             // threads the time value through.
                                             sit->second.shaderItem->setITime(qBound(qreal(0.0), v, qreal(1.0)));
                                         },
                                         /*onComplete=*/
                                         [this, surface, target]() {
                                             legCompleted(surface, target);
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
void SurfaceAnimator::Private::legCompleted(PhosphorLayer::Surface* surface, QQuickItem* target)
{
    const auto it = m_tracks.find(TrackKey{surface, target});
    if (it == m_tracks.end()) {
        return;
    }
    --it->second.pendingLegs;
    if (it->second.pendingLegs > 0) {
        return;
    }
    // Shader-exclusive hide: the surface stayed at full opacity while
    // the shader ran. Now snap to the terminal opacity (0.0 for hide)
    // so the surface actually disappears.
    if (it->second.shaderExclusive && it->second.target) {
        it->second.target->setOpacity(it->second.targetOpacity);
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
    teardownShaderLeg(surface, it->second);
    m_tracks.erase(it);
    if (onComplete) {
        onComplete();
    }
}

} // namespace PhosphorAnimationLayer
