// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/ProfileTree.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>

#include <PhosphorRules/RuleEvaluator.h>
#include <PhosphorRules/RuleSet.h>

#include <opengl/glshader.h>
#include <opengl/gltexture.h>

#include <QHash>
#include <QLoggingCategory>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QSet>
#include <QSize>
#include <QString>
#include <QThreadPool>
#include <QVector4D>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>

#include "plasmazoneseffect/types.h"
#include "compositor/effectlogging.h"

namespace KWin {
class EffectWindow;
}

namespace PlasmaZones {

class PlasmaZonesEffect;

/**
 * @brief Manages the shader/texture/transition subsystem for PlasmaZonesEffect.
 *
 * Owns the animation shader registry, the shader profile tree, the user-texture
 * LRU cache, the compiled shader cache, and the per-window in-flight
 * ShaderTransition map. All GL-thread shader transition operations route
 * through this class.
 *
 * Holds a non-owning back-pointer to the owning PlasmaZonesEffect so it can
 * access the window animator, OffscreenEffect redirect API, and compositor
 * bridge. The effect declares `friend class ShaderTransitionManager;` for
 * the members that are not exposed via public accessors.
 */
class ShaderTransitionManager
{
public:
    explicit ShaderTransitionManager(PlasmaZonesEffect* effect);
    ~ShaderTransitionManager();

    // Non-copyable, non-movable (contains QThreadPool, pointers into effect)
    ShaderTransitionManager(const ShaderTransitionManager&) = delete;
    ShaderTransitionManager& operator=(const ShaderTransitionManager&) = delete;

    // ═══════════════════════════════════════════════════════════════════════════
    // Registry / Profile Tree
    // ═══════════════════════════════════════════════════════════════════════════

    PhosphorAnimationShaders::AnimationShaderRegistry& shaderRegistry()
    {
        return m_animationShaderRegistry;
    }
    const PhosphorAnimationShaders::AnimationShaderRegistry& shaderRegistry() const
    {
        return m_animationShaderRegistry;
    }

    PhosphorAnimationShaders::ShaderProfileTree& profileTree()
    {
        return m_shaderProfileTree;
    }
    const PhosphorAnimationShaders::ShaderProfileTree& profileTree() const
    {
        return m_shaderProfileTree;
    }

    /// Pre-write the maximize edge tracking for a maximize demote the
    /// effect itself authored (TilingHandler::demoteMaximizeForSnapPlacement):
    /// stamps the window not-fully-maximized so the demote's committed
    /// Wayland echo reads as no-edge in the maximize lambda instead of
    /// replaying a WindowMaximize morph over the snap-in leg, and drops any
    /// pending morph a just-prior genuine edge armed so the zone-rect commit
    /// cannot complete it through the geometry hook.
    void noteMaximizeDemotedForSnap(KWin::EffectWindow* w)
    {
        // Null-guarded like every sibling in this family. A null key would
        // insert an entry no sweep can ever reach, since every remover is
        // keyed by a live window.
        if (!w) {
            return;
        }
        m_lastFullyMaximized.insert(w, false);
        m_pendingMaximizeMorph.remove(w);
        // A demote is a SNAP placement, so it must not inherit a maximize leg
        // from an edge the user took a moment earlier: the window is being
        // dropped into a zone, and the marker below would hand the zone-rect
        // commit window.movement.maximize.
        m_maximizeEdgeAtMs.remove(w);
    }

    /// The fully-maximized state the maximize lambda last recorded for @p w.
    ///
    /// Exposed for the authorship-stamp gate in
    /// `TilingHandler::applyMaximizeSuppressed`, which must stamp exactly when
    /// the write it is about to make will reach noteMaximizeEdge. That lambda
    /// decides by comparing the incoming `horizontal && vertical` against THIS
    /// value, so the gate reads it too and the correspondence holds by
    /// construction rather than by argument — including where this value has
    /// been pre-written to swallow an echo (noteMaximizeDemotedForSnap), which
    /// makes the gate decline in the same breath.
    bool lastFullyMaximized(KWin::EffectWindow* w) const
    {
        return m_lastFullyMaximized.value(w, false);
    }

    /// Stamp @p w as having a maximize write the EFFECT ITSELF authored, so
    /// the edge that write produces is not mistaken for the user's own.
    ///
    /// Called from `TilingHandler::applyMaximizeSuppressed`, which every
    /// maximize write the effect makes routes through, BEFORE the
    /// `KWin::Window::maximize()` call — on X11 that call re-enters
    /// windowMaximizedStateChanged synchronously, so the stamp has to be in
    /// place by the time the handler runs. The one `KWin::Window::maximize()`
    /// left outside it is `KWinCompositorBridge::setMaximized`, an
    /// `ICompositorBridge` override with no caller; a future caller would have
    /// to route through the helper or stamp for itself.
    ///
    /// The suppression counter cannot carry this, and not only for the usual
    /// platform reason. noteMaximizeEdge is armed ABOVE the suppression skip in
    /// that lambda, so the counter never reaches it on either platform — and
    /// even a test moved under the skip would answer only on X11, since on
    /// Wayland the committed echo arrives a client round trip later with the
    /// counter back at 0, the same asymmetry `interceptMaximizeRequest`'s
    /// already-agrees arm exists to absorb. A stamp outlives the bracket, so it
    /// answers wherever the echo lands.
    ///
    /// Consumed by noteMaximizeEdge, so a stamp with no edge to take it back
    /// off is not merely untidy: inside the deadline it swallows the user's
    /// next genuine maximize, which is the failure the marker exists to fix.
    /// The write site is therefore gated on `lastFullyMaximized` — the same
    /// value the consumer's edge filter compares against — so it stamps if and
    /// only if the write it is about to make will reach that consumer. The
    /// deadline is the backstop under that gate, not the primary defence.
    void noteEffectAuthoredMaximizeWrite(KWin::EffectWindow* w);

    /// Arm the "this window just took a genuine maximize or restore edge"
    /// marker, from the windowMaximizedStateChanged hook.
    ///
    /// Armed for every genuine full-maximize flip THE USER caused — the
    /// maximize button, the titlebar double-click, a shortcut — on whichever
    /// of the several code paths ends up delivering the resulting geometry.
    /// It is armed before the scroll interception and the pending-morph split,
    /// so a maximize the scrolling engine answers is recorded the same as one
    /// KWin answers itself.
    ///
    /// Two exclusions, and both are about authorship rather than timing:
    ///
    ///  • A write the EFFECT authored, identified by the stamp
    ///    noteEffectAuthoredMaximizeWrite left at the write chokepoint. Every
    ///    such write already has an owner for its leg — the batch that made it,
    ///    which routes the geometry onto `window.movement.maximize` from its
    ///    own per-iteration flag — so arming here would only leave a marker
    ///    nothing needs, to be claimed by the next unrelated placement.
    ///  • The interactive drag-restore, excluded at the call site: KWin
    ///    unmaximizes a window when the user pulls its titlebar, and that
    ///    gesture's visuals belong to the held move pack.
    ///
    /// Returns whether this edge was the EFFECT'S OWN — i.e. a stamp was found
    /// and consumed, and no marker armed. The maximize lambda passes that
    /// answer down to `TilingHandler::interceptMaximizeRequest`, whose
    /// already-agrees arm must not record the echo of a write this effect made
    /// as though it were a user's press. This is the only place that
    /// distinction is available: the stamp is one-shot and is consumed here,
    /// before the interception runs.
    bool noteMaximizeEdge(KWin::EffectWindow* w);

    /// Whether @p w took a genuine maximize OR restore edge of its own within
    /// the freshness window, CONSUMING the marker either way.
    ///
    /// Both directions, and deliberately: `beginMaximizeShaderMorph` routes a
    /// restore onto `ProfilePaths::WindowMaximize` too, so the placement that
    /// answers an unmaximize belongs on the same leg as the one that answers a
    /// maximize.
    ///
    /// The tile batch asks this so the placement that lands as the direct
    /// consequence of that edge rides `window.movement.maximize` — the pack
    /// the user assigned to the event — instead of the snap leg, on every
    /// screen rather than only on a scrolling one whose own maximize-to-edges
    /// verb happened to author the bit.
    ///
    /// Consuming is what bounds the marker to ONE placement, and the batch
    /// consumes once per window per batch whether or not it ends up using the
    /// answer — an entry left armed by a batch that skipped its commit would
    /// otherwise be claimed by the next, unrelated placement. What the deadline
    /// bounds is the other direction: an edge that no batch ever answers must
    /// not sit waiting for one. Inside that window an unrelated placement can
    /// still claim the marker, which costs one mis-chosen leg and nothing else.
    bool takeRecentMaximizeEdge(KWin::EffectWindow* w);

    /// Rebuild the effect-rule `RuleSet` from `m_ruleAnimationRules`
    /// — the rules from `rules.json` that carry any effect-consumed
    /// action (admitted via `ActionRegistry::hasTag(type, Tag::Effect)`;
    /// the authoritative membership list is the descriptor tag
    /// assignments in ruleaction.cpp). Call after every mutation of that
    /// list. The bound `RuleEvaluator` picks up the new revision
    /// transparently and its match cache is invalidated.
    void rebuildAnimationRuleSet();

    /// Replace the set of `rules.json` rules that carry any
    /// effect-consumed action (admitted via
    /// `ActionRegistry::hasTag(type, Tag::Effect)`). The effect refreshes this on the
    /// `org.plasmazones.Rules.rulesChanged` D-Bus signal so a new
    /// effect rule authored in the settings UI fires without a restart.
    /// Triggers `rebuildAnimationRuleSet()` only when the list actually
    /// changes — a no-op rewrite keeps the evaluator's match cache warm.
    void setRuleAnimationRules(QList<PhosphorRules::Rule> rules);

    /// The evaluator bound to the effect-rule set. Resolution of the
    /// per-window cascade for every appearance/animation (`Tag::Effect`)
    /// action routes through this evaluator. Its terminal scope honours
    /// `ExcludeAnimations`, which is why the one-shot verdicts
    /// (`Tag::EffectVerdict`) route through `effectVerdictRuleEvaluator()`
    /// instead — see that accessor.
    const PhosphorRules::RuleEvaluator& animationRuleEvaluator() const
    {
        return m_animationRuleEvaluator;
    }

    /// The animation rule set itself — for the `!isEmpty()` fast path.
    const PhosphorRules::RuleSet& animationRuleSet() const
    {
        return m_animationRuleSet;
    }

    /// True when at least one enabled rule carries a `SetOpacity` action.
    /// The per-frame opacity resolve in `prePaintWindow`/`paintWindow` builds a
    /// `WindowQuery` (appId normalisation + screen/desktop/activity derivation)
    /// for every visible window — wasted work when the user's effect rules are
    /// all `OverrideAnimation*`/border/gap and never dim. Gate the resolve on
    /// this flag so the hot path costs two pointer reads in that common case.
    /// Recomputed by `rebuildAnimationRuleSet()` on every rule-set change.
    bool hasOpacityRules() const
    {
        return m_hasOpacityRules;
    }

    /// True when at least one enabled rule carries a `SetWindowLayer` action.
    /// Gates the per-window layer reconcile (`reconcileRuleWindowLayer`'s fast
    /// path) and the bulk-placement sweep in `invalidateAllRuleCaches` —
    /// without it, a session whose rules never touch the layer (opacity or
    /// border only) would still pay a cache-cold per-window rule resolution
    /// across the whole stacking order on every daemon loss / bringup re-seed.
    /// Recomputed by `rebuildAnimationRuleSet()` on every rule-set change.
    bool hasWindowLayerRules() const
    {
        return m_hasWindowLayerRules;
    }

    /// Rebuild the effect-VERDICT `RuleSet` from `m_effectVerdictRules` — the
    /// rules carrying any `Tag::EffectVerdict` action (OpenFullscreen /
    /// ScrollFactor). Separate from the animation/appearance set above because
    /// their evaluator honours `ExcludeAnimations` as a walk-stopper, and a
    /// verdict is not an animation: "no animations for this app" must not
    /// cancel the app's scroll multiplier or its open-fullscreen decision.
    /// Call after every mutation of that list.
    void rebuildEffectVerdictRuleSet();

    /// Replace the set of rules carrying any `Tag::EffectVerdict` action.
    /// Written from the same `getAllRules` pass that fills the animation set,
    /// so the two never drift. Triggers `rebuildEffectVerdictRuleSet()` only
    /// when the list actually changes.
    void setEffectVerdictRules(QList<PhosphorRules::Rule> rules);

    /// The evaluator bound to the effect-verdict set. Its terminal action
    /// scope is `{Exclude}` ONLY — see rebuildEffectVerdictRuleSet.
    const PhosphorRules::RuleEvaluator& effectVerdictRuleEvaluator() const
    {
        return m_effectVerdictRuleEvaluator;
    }

    /// The effect-verdict rule set itself — for the `!isEmpty()` fast path.
    const PhosphorRules::RuleSet& effectVerdictRuleSet() const
    {
        return m_effectVerdictRuleSet;
    }

    /// True when at least one enabled rule carries an `OpenFullscreen` action.
    /// Gates the open-time fullscreen reconcile (`applyRuleOpenFullscreen`'s
    /// fast path) so a session with no such rule pays two pointer reads per
    /// window open instead of a full WindowQuery build. Recomputed by
    /// `rebuildEffectVerdictRuleSet()` on every rule-set change.
    bool hasOpenFullscreenRules() const
    {
        return m_hasOpenFullscreenRules;
    }

    /// True when at least one enabled rule carries a `ScrollFactor` action.
    /// Gates the input filter's per-axis-event resolve so a session with no
    /// such rule pays two pointer reads per wheel tick instead of a rule
    /// resolution. Recomputed by `rebuildEffectVerdictRuleSet()` on every
    /// rule-set change.
    bool hasScrollFactorRules() const
    {
        return m_hasScrollFactorRules;
    }

    /// Per-event motion-profile tree mirrored from the daemon's
    /// PhosphorProfileRegistry over D-Bus (`motionProfileTree`). Holds
    /// the per-event base durations (window.open, window.close, …) that
    /// `tryBeginShaderForEvent` resolves before applying the per-window
    /// animation rule timing cascade (the `OverrideAnimationTiming` slot in
    /// `m_animationRuleSet`). Refreshed on the dedicated `motionProfileTreeChanged`
    /// D-Bus signal (live per-event edits) and on `settingsChanged` via
    /// `loadCachedSettings()`.
    PhosphorAnimation::ProfileTree& motionProfileTree()
    {
        return m_motionProfileTree;
    }
    const PhosphorAnimation::ProfileTree& motionProfileTree() const
    {
        return m_motionProfileTree;
    }

    // NOTE: Shader transition methods (beginShaderTransition, endShaderTransition,
    // tryBeginShaderForEvent, loadShaderRegistryFromDbus, loadShaderProfileFromDbus,
    // warmUserTextureAsync, evictLruTextureIfOverBound) are declared on
    // PlasmaZonesEffect and operate on m_shaderManager state via friend access.

    // ═══════════════════════════════════════════════════════════════════════════
    // Per-frame State (set by prePaintScreen, read by paintWindow)
    // ═══════════════════════════════════════════════════════════════════════════

    /// Frame-pinned shader clock. `prePaintScreen` samples
    /// `shaderClockNowMs()` once and stores it here; every `paintWindow`
    /// call within that compositor cycle reads this value instead of
    /// re-sampling steady_clock. Without this pin, KWin invoking
    /// `paintWindow` more than once per cycle (back-to-back paint cycles
    /// scheduled by our own `effects->addRepaint`, multi-output passes,
    /// etc.) would cause each call to compute a slightly different
    /// `progress` from a fresh `shaderClockNowMs()`, painting the
    /// surface-extent quad at a different position each call — visible
    /// as staggered ghost copies of the in-flight window.
    /// `-1` means "no cycle in progress; fall back to live read"
    /// (paintWindow happening before prePaintScreen on this effect
    /// instance, e.g. test paths).
    qint64 currentFrameClockMs() const
    {
        return m_currentFrameClockMs;
    }
    void setCurrentFrameClockMs(qint64 ms)
    {
        m_currentFrameClockMs = ms;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Per-window State
    // ═══════════════════════════════════════════════════════════════════════════

    /// Access the in-flight shader transition map. Raw map access is
    /// retained for the few hot-path iteration call sites
    /// (`paint_pipeline.cpp`'s prePaint/postPaint sweeps,
    /// `shader_transitions.cpp`'s `endShaderTransition` cleanup loop)
    /// that walk every entry. Single-entry call sites should use
    /// `hasTransition` / `findTransition` / `insertTransition` /
    /// `eraseTransition` / `empty()` below, which document intent and
    /// preserve the option of adding generation gating, instrumentation,
    /// or invariant assertions in the manager without touching every
    /// caller.
    std::unordered_map<KWin::EffectWindow*, ShaderTransition>& shaderTransitions()
    {
        return m_shaderTransitions;
    }
    const std::unordered_map<KWin::EffectWindow*, ShaderTransition>& shaderTransitions() const
    {
        return m_shaderTransitions;
    }

    /// True when no transitions are in flight. Hot-path single-test
    /// idiom used by every prePaint/postPaint check.
    bool empty() const
    {
        return m_shaderTransitions.empty();
    }
    /// Focused accessors. Each is a thin wrapper over the underlying
    /// `std::unordered_map` so call sites that don't need raw iterator
    /// access can express their intent at the manager API level.
    bool hasTransition(KWin::EffectWindow* window) const
    {
        return m_shaderTransitions.find(window) != m_shaderTransitions.end();
    }
    /// Returns nullptr when the window has no in-flight transition.
    /// The pointer is stable until that window's entry is erased or
    /// replaced — `std::unordered_map` guarantees that insertion and
    /// erasure do not invalidate references to OTHER entries (only the
    /// erased entry's references become dangling).
    ShaderTransition* findTransition(KWin::EffectWindow* window)
    {
        auto it = m_shaderTransitions.find(window);
        return it != m_shaderTransitions.end() ? &it->second : nullptr;
    }
    const ShaderTransition* findTransition(KWin::EffectWindow* window) const
    {
        auto it = m_shaderTransitions.find(window);
        return it != m_shaderTransitions.end() ? &it->second : nullptr;
    }
    /// Insert a transition for @p window, taking ownership of the moved
    /// payload. Returns the pointer to the inserted entry on success,
    /// or `nullptr` if an entry for @p window already existed (the
    /// moved-in payload is discarded in that case, and the caller MUST
    /// either `eraseTransition` first or handle the rollback explicitly
    /// — silently writing through the returned pointer would corrupt the
    /// pre-existing transition's grab state). Q_ASSERT covers debug;
    /// the runtime guard covers release, so the contract violation is
    /// loud in both build modes.
    ShaderTransition* insertTransition(KWin::EffectWindow* window, ShaderTransition&& transition)
    {
        auto result = m_shaderTransitions.emplace(window, std::move(transition));
        Q_ASSERT(result.second);
        if (!result.second) {
            // Forensic breadcrumb so a release-build duplicate-key event
            // surfaces in the journal instead of only showing up as the
            // caller's downstream rollback. Q_ASSERT above already covers
            // debug.
            qCWarning(lcEffect,
                      "ShaderTransitionManager::insertTransition: duplicate key for window %p — "
                      "caller failed to eraseTransition first",
                      static_cast<void*>(window));
            return nullptr;
        }
        return &result.first->second;
    }
    /// Returns true when an entry was actually erased.
    bool eraseTransition(KWin::EffectWindow* window)
    {
        return m_shaderTransitions.erase(window) > 0;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Per-frame SetOpacity cache
    //
    // prePaintWindow needs to know "is this window dimmed by a SetOpacity
    // rule?" to clear the opaque region; paintWindow then has to apply the
    // same opacity to the WindowPaintData. Without caching, both calls
    // walk the rule cascade (resolveWindowOpacity → highestPriorityMatch
    // → ResolvedActions assembly) per visible window per frame —
    // paying 2× the cost the cascade was designed for.
    //
    // Cache layout: ONE map, `m_frameOpacityCache`, where PRESENCE of the key
    // means "lookup already done this frame" and the value is the resolved
    // std::optional<qreal> (nullopt = no SetOpacity rule matched). The
    // presence/value split is what lets a no-match be cached as cheaply as a
    // match, which is the common case and the one the per-frame cost is paid
    // for.
    //
    // Lifetime: populated by prePaintWindow's resolve, consumed by
    // paintWindow's resolve, cleared at postPaintScreen so the next frame
    // re-resolves against any rule-set / metadata changes that landed
    // between frames. Per-frame, not per-revision: a metadata change
    // (windowClassChanged) invalidates the per-window cascade-cache via
    // RuleEvaluator::clearCache(), and the next frame's prePaintWindow
    // re-populates this map naturally.
    // ═══════════════════════════════════════════════════════════════════════════
    bool frameOpacityCached(KWin::EffectWindow* window) const
    {
        return m_frameOpacityCache.contains(window);
    }
    std::optional<qreal> cachedFrameOpacity(KWin::EffectWindow* window) const
    {
        return m_frameOpacityCache.value(window);
    }
    void cacheFrameOpacity(KWin::EffectWindow* window, std::optional<qreal> opacity)
    {
        m_frameOpacityCache.insert(window, opacity);
    }
    void clearFrameOpacityCache()
    {
        m_frameOpacityCache.clear();
    }
    /// The frame @p window held before its last maximize state change, or an
    /// invalid rect if none was captured. Read by the scroll batch to anchor a
    /// column-maximize toggle's departure: by the time the batch applies, the
    /// window has already been resized to KWin's maximize area, so its live
    /// frame is no longer where the motion should appear to start.
    QRectF preMaximizeFrame(KWin::EffectWindow* window) const
    {
        return m_preMaximizeFrame.value(window);
    }

private:
    friend class PlasmaZonesEffect;

    PlasmaZonesEffect* m_effect = nullptr;

    // ═══════════════════════════════════════════════════════════════════════════
    // Registry + Profile Tree
    // ═══════════════════════════════════════════════════════════════════════════
    PhosphorAnimationShaders::AnimationShaderRegistry m_animationShaderRegistry;
    PhosphorAnimationShaders::ShaderProfileTree m_shaderProfileTree;
    PhosphorAnimation::ProfileTree m_motionProfileTree;
    // Rules from rules.json that carry any effect-consumed action
    // (admitted via `ActionRegistry::hasTag(type, Tag::Effect)`;
    // authoritative list in ruleaction.cpp). Refreshed
    // from the daemon's org.plasmazones.Rules interface on every
    // `rulesChanged` signal; mirrored into `m_animationRuleSet` so the
    // bound RuleEvaluator picks up the new revision.
    QList<PhosphorRules::Rule> m_ruleAnimationRules;

    // ═══════════════════════════════════════════════════════════════════════════
    // Window-rule view of the animation rules
    //
    // `m_animationRuleSet` mirrors `m_ruleAnimationRules`, rebuilt by
    // `rebuildAnimationRuleSet()` on every D-Bus refresh.
    // `m_animationRuleEvaluator` binds a const reference to it — declaration
    // ORDER MATTERS: the rule set must outlive (and precede) the evaluator.
    // ═══════════════════════════════════════════════════════════════════════════
    PhosphorRules::RuleSet m_animationRuleSet;
    PhosphorRules::RuleEvaluator m_animationRuleEvaluator{m_animationRuleSet};

    // ═══════════════════════════════════════════════════════════════════════════
    // Effect-VERDICT view (Tag::EffectVerdict: OpenFullscreen / ScrollFactor)
    //
    // Its own set + evaluator rather than a slice of the animation one: the
    // evaluators differ in TERMINAL SCOPE, which is a per-evaluator property.
    // Same declaration-order requirement — the set must outlive the evaluator.
    // ═══════════════════════════════════════════════════════════════════════════
    QList<PhosphorRules::Rule> m_effectVerdictRules;
    PhosphorRules::RuleSet m_effectVerdictRuleSet;
    PhosphorRules::RuleEvaluator m_effectVerdictRuleEvaluator{m_effectVerdictRuleSet};

    // Cached "any enabled rule carries SetOpacity" predicate — recomputed in
    // rebuildAnimationRuleSet() so the per-frame opacity resolve can skip the
    // WindowQuery build entirely when no opacity rule exists. See hasOpacityRules().
    bool m_hasOpacityRules = false;
    // Same shape for SetWindowLayer. See hasWindowLayerRules().
    bool m_hasWindowLayerRules = false;
    // Same shape for OpenFullscreen, recomputed over the VERDICT list. See
    // hasOpenFullscreenRules().
    bool m_hasOpenFullscreenRules = false;
    // Same shape for ScrollFactor, over the verdict list. See hasScrollFactorRules().
    bool m_hasScrollFactorRules = false;

    // ═══════════════════════════════════════════════════════════════════════════
    // Texture Cache
    //
    // Declaration ORDER MATTERS — see comments in plasmazoneseffect.h (original).
    // m_textureCache declared first so it destructs last and outlives
    // m_shaderCache and m_shaderTransitions (which hold raw pointers into it).
    // ═══════════════════════════════════════════════════════════════════════════
    std::map<QString, CachedTexture> m_textureCache;

    QThreadPool m_textureLoaderPool;
    QSet<QString> m_textureLoadsInFlight;
    quint64 m_textureCacheGeneration = 0;
    quint64 m_textureCacheAccessTick = 0;
    static constexpr std::size_t kTextureCacheSoftBound = 32;

    // ═══════════════════════════════════════════════════════════════════════════
    // Shader Cache
    // ═══════════════════════════════════════════════════════════════════════════
    std::map<QString, CachedShader> m_shaderCache;

    // ═══════════════════════════════════════════════════════════════════════════
    // In-flight Transitions
    // ═══════════════════════════════════════════════════════════════════════════
    std::unordered_map<KWin::EffectWindow*, ShaderTransition> m_shaderTransitions;
    QSet<KWin::EffectWindow*> m_pendingShaderExpiryEnd;

    // ═══════════════════════════════════════════════════════════════════════════
    // Per-frame Cached State
    // ═══════════════════════════════════════════════════════════════════════════
    qint64 m_lastIDateRefreshMs = 0;
    QVector4D m_cachedIDate{};
    QPointF m_cachedCursorGlobal;
    qint64 m_currentFrameClockMs = -1;

    // Per-frame resolved SetOpacity values; cleared at postPaintScreen.
    // Hash presence = "computed this frame"; value = nullopt when no rule
    // matched. See the accessor block above for the per-frame contract.
    QHash<KWin::EffectWindow*, std::optional<qreal>> m_frameOpacityCache;

    // ═══════════════════════════════════════════════════════════════════════════
    // Generation + Edge-detection
    // ═══════════════════════════════════════════════════════════════════════════
    quint64 m_shaderTransitionGenerationCounter = 0;
    QHash<KWin::EffectWindow*, bool> m_lastFullyMaximized;
    // Frame rect captured at windowMaximizedStateAboutToChange — the rect the
    // window is LEAVING. KWin emits the about-to-change signal before any
    // geometry change, so this is the only place the maximize morph's
    // departure rect can be read. Latest-wins per window; erased with
    // m_lastFullyMaximized on windowDeleted.
    QHash<KWin::EffectWindow*, QRectF> m_preMaximizeFrame;
    // Maximize/restore morph whose state edge fired BEFORE the client
    // committed the new size. KWin does not guarantee the geometry has been
    // applied when windowMaximizedStateChanged is emitted: an occluded or
    // slow client leaves the frame at (or near) the departure rect — a
    // live trace on KWin 6.7.2 showed maximizedChanged arriving with only
    // the position applied and the size still pending the client's ack.
    // Installing the morph then would tween between two same-size rects
    // while the real resize lands as a raw snap mid-animation. Instead the
    // state edge arms this entry and the size-delivering
    // windowFrameGeometryChanged completes the install (window_connections.cpp),
    // so the animation starts exactly when the window visibly changes size.
    // Erased on completion, on windowDeleted, and on a stale-deadline check
    // at consumption time.
    struct PendingMaximizeMorph
    {
        QRectF departureFrame;
        qint64 armedAtMs = 0;
    };
    QHash<KWin::EffectWindow*, PendingMaximizeMorph> m_pendingMaximizeMorph;
    // Monotonic stamp of the last genuine maximize/restore edge, per window.
    // See noteMaximizeEdge / takeRecentMaximizeEdge above for the contract.
    // Raw-pointer-keyed like its siblings, so it is erased on windowDeleted
    // (lifecycle_wiring.cpp) both to stay bounded and so a reused address
    // cannot inherit a stale stamp.
    QHash<KWin::EffectWindow*, qint64> m_maximizeEdgeAtMs;
    // Monotonic stamp of a maximize write the EFFECT authored, per window,
    // written at the applyMaximizeSuppressed chokepoint and consumed by the
    // edge it produces. Same raw-pointer keying and the same windowDeleted
    // sweep as the marker it guards.
    //
    // ONE in-flight write per window, not a count: a second insert overwrites
    // rather than adds. Within a batch that is exhaustive — the monocle arm
    // writes once per iteration, and unmaximizeMonocleWindow and
    // releaseMaximizedToEdges are mutually exclusive with it — but that
    // reasoning is per-batch, and on Wayland a write and its echo are a client
    // round trip apart. Two batches inside one round trip could therefore leave
    // two writes outstanding against a single slot, and if KWin then delivered
    // both edges rather than coalescing them, the second would find no stamp
    // and arm the marker for a write the effect made. Unverified, because it
    // needs KWin's echo behaviour for a Full-then-Restore pair inside one round
    // trip, and bounded to one mis-chosen animation leg if it happens. A future
    // deliberate double-write must clear or count rather than rely on this.
    //
    // Do not "fix" it by clearing the stamp whenever the gate declines: the
    // demote path depends on both sides declining together, and a clear-on-
    // decline would break that symmetry to chase a case that may not exist.
    QHash<KWin::EffectWindow*, qint64> m_effectAuthoredMaximizeAtMs;
    QPointer<KWin::EffectWindow> m_lastFocusShaderWindow;
};

} // namespace PlasmaZones
