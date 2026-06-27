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

namespace KWin {
class EffectWindow;
}

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

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

    /// Rebuild the effect-rule `RuleSet` from `m_ruleAnimationRules`
    /// — the rules from `rules.json` that carry any effect-consumed
    /// action (the `OverrideAnimation*` triple plus `SetOpacity`; see
    /// `PhosphorRules::ActionType::isEffectRuleAction`). Call after
    /// every mutation of that list. The bound `RuleEvaluator` picks up
    /// the new revision transparently and its match cache is invalidated.
    void rebuildAnimationRuleSet();

    /// Replace the set of `rules.json` rules that carry any
    /// effect-consumed action (`OverrideAnimation*` or `SetOpacity` —
    /// see `isEffectRuleAction`). The effect refreshes this on the
    /// `org.plasmazones.Rules.rulesChanged` D-Bus signal so a new
    /// effect rule authored in the settings UI fires without a restart.
    /// Triggers `rebuildAnimationRuleSet()` only when the list actually
    /// changes — a no-op rewrite keeps the evaluator's match cache warm.
    void setRuleAnimationRules(QList<PhosphorRules::Rule> rules);

    /// The evaluator bound to the effect-rule set. Resolution of the
    /// per-window cascade for every effect-consumed action (the
    /// `OverrideAnimation*` triple plus `SetOpacity`) routes through
    /// this evaluator.
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
    // Cache layout:
    //   `m_frameOpacityComputed.contains(w)` → "lookup already done this
    //                                           frame"; value is the
    //                                           resolved std::optional<qreal>
    //                                           (nullopt = no SetOpacity
    //                                           rule matched).
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
    // (`OverrideAnimation*` triple OR `SetOpacity` — see
    // `PhosphorRules::ActionType::isEffectRuleAction`). Refreshed
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

    // Cached "any enabled rule carries SetOpacity" predicate — recomputed in
    // rebuildAnimationRuleSet() so the per-frame opacity resolve can skip the
    // WindowQuery build entirely when no opacity rule exists. See hasOpacityRules().
    bool m_hasOpacityRules = false;

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
    QPointer<KWin::EffectWindow> m_lastFocusShaderWindow;
};

} // namespace PlasmaZones
