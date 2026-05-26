// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/ProfileTree.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>

#include <PhosphorWindowRule/RuleEvaluator.h>
#include <PhosphorWindowRule/WindowRuleSet.h>

#include <opengl/glshader.h>
#include <opengl/gltexture.h>

#include <QHash>
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
#include <unordered_map>

#include "plasmazoneseffect/types.h"

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

    /// Rebuild the animation `WindowRuleSet` from `m_windowRuleAnimationRules`
    /// — the rules from `windowrules.json` that carry an `OverrideAnimation*`
    /// action. Call after every mutation of that list. The bound
    /// `RuleEvaluator` picks up the new revision transparently and its match
    /// cache is invalidated.
    void rebuildAnimationRuleSet();

    /// Replace the set of `windowrules.json` rules that carry an
    /// `overrideAnimation*` action. The effect refreshes this on the
    /// `org.plasmazones.WindowRules.rulesChanged` D-Bus signal so a new
    /// animation rule authored in the settings UI fires without a restart.
    /// Triggers `rebuildAnimationRuleSet()` only when the list actually
    /// changes — a no-op rewrite keeps the evaluator's match cache warm.
    void setWindowRuleAnimationRules(QList<PhosphorWindowRule::WindowRule> rules);

    /// The evaluator bound to the animation rule set. Resolution of the
    /// per-window animation override cascade (WindowRules carrying any
    /// `OverrideAnimation*` action) routes through this evaluator.
    const PhosphorWindowRule::RuleEvaluator& animationRuleEvaluator() const
    {
        return m_animationRuleEvaluator;
    }

    /// The animation rule set itself — for the `!isEmpty()` fast path.
    const PhosphorWindowRule::WindowRuleSet& animationRuleSet() const
    {
        return m_animationRuleSet;
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

    /// Update the cached cursor position (call once per prePaintScreen).
    void setCachedCursorGlobal(const QPointF& pos)
    {
        m_cachedCursorGlobal = pos;
    }
    QPointF cachedCursorGlobal() const
    {
        return m_cachedCursorGlobal;
    }

    /// 1 Hz iDate cache (call once per prePaintScreen or when stale).
    qint64 lastIDateRefreshMs() const
    {
        return m_lastIDateRefreshMs;
    }
    void setLastIDateRefreshMs(qint64 ms)
    {
        m_lastIDateRefreshMs = ms;
    }
    QVector4D cachedIDate() const
    {
        return m_cachedIDate;
    }
    void setCachedIDate(const QVector4D& v)
    {
        m_cachedIDate = v;
    }

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

    /// Access the in-flight shader transition map (for paintWindow).
    ///
    /// @note The raw map exposure is a transitional shim. Prefer the
    /// focused accessors below (`hasTransition`, `findTransition`,
    /// `eraseTransition`) for new call sites — they preserve the
    /// option of adding generation gating, instrumentation, or
    /// invariant assertions in the manager without touching every
    /// caller. Existing direct uses live in `shader_transitions.cpp`
    /// (via `friend class PlasmaZonesEffect`) and migrate as those
    /// sites are touched.
    std::unordered_map<KWin::EffectWindow*, ShaderTransition>& shaderTransitions()
    {
        return m_shaderTransitions;
    }
    const std::unordered_map<KWin::EffectWindow*, ShaderTransition>& shaderTransitions() const
    {
        return m_shaderTransitions;
    }

    /// Focused accessors. Each is a thin wrapper over the underlying
    /// `std::unordered_map` so call sites that don't need raw iterator
    /// access can express their intent at the manager API level. New
    /// code should prefer these.
    bool hasTransition(KWin::EffectWindow* window) const
    {
        return m_shaderTransitions.find(window) != m_shaderTransitions.end();
    }
    /// Returns nullptr when the window has no in-flight transition.
    /// The pointer is stable until the next `eraseTransition` /
    /// `insertTransition` call for THAT window — the underlying
    /// `unordered_map` does not invalidate other entries' pointers
    /// on insertion or erasure.
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
    /// Returns true when an entry was actually erased.
    bool eraseTransition(KWin::EffectWindow* window)
    {
        return m_shaderTransitions.erase(window) > 0;
    }

    /// Windows with a pending deferred endShaderTransition queued.
    QSet<KWin::EffectWindow*>& pendingShaderExpiryEnd()
    {
        return m_pendingShaderExpiryEnd;
    }

    /// Per-window edge-detection for windowMaximizedStateChanged.
    QHash<KWin::EffectWindow*, bool>& lastFullyMaximized()
    {
        return m_lastFullyMaximized;
    }

    /// Last window we fired a window.focus shader on.
    QPointer<KWin::EffectWindow>& lastFocusShaderWindow()
    {
        return m_lastFocusShaderWindow;
    }

    /// Access the compiled shader cache (for paintWindow uniform upload).
    std::map<QString, CachedShader>& shaderCache()
    {
        return m_shaderCache;
    }
    const std::map<QString, CachedShader>& shaderCache() const
    {
        return m_shaderCache;
    }

    /// Access the texture cache (for paintWindow bind).
    std::map<QString, CachedTexture>& textureCache()
    {
        return m_textureCache;
    }
    const std::map<QString, CachedTexture>& textureCache() const
    {
        return m_textureCache;
    }

    /// Bump the texture cache generation (call on effectsChanged hot-reload).
    void bumpTextureCacheGeneration()
    {
        ++m_textureCacheGeneration;
    }
    quint64 textureCacheGeneration() const
    {
        return m_textureCacheGeneration;
    }

    /// Bump and read the access tick (for LRU tracking).
    quint64 nextAccessTick()
    {
        return ++m_textureCacheAccessTick;
    }

    /// Bump the shader transition generation counter.
    quint64 nextShaderTransitionGeneration()
    {
        return ++m_shaderTransitionGenerationCounter;
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
    // Rules from windowrules.json that carry an OverrideAnimation* action.
    // Refreshed from the daemon's org.plasmazones.WindowRules interface on
    // every `rulesChanged` signal; mirrored into `m_animationRuleSet` so the
    // bound RuleEvaluator picks up the new revision.
    QList<PhosphorWindowRule::WindowRule> m_windowRuleAnimationRules;

    // ═══════════════════════════════════════════════════════════════════════════
    // Window-rule view of the animation rules
    //
    // `m_animationRuleSet` mirrors `m_windowRuleAnimationRules`, rebuilt by
    // `rebuildAnimationRuleSet()` on every D-Bus refresh.
    // `m_animationRuleEvaluator` binds a const reference to it — declaration
    // ORDER MATTERS: the rule set must outlive (and precede) the evaluator.
    // ═══════════════════════════════════════════════════════════════════════════
    PhosphorWindowRule::WindowRuleSet m_animationRuleSet;
    PhosphorWindowRule::RuleEvaluator m_animationRuleEvaluator{m_animationRuleSet};

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

    // ═══════════════════════════════════════════════════════════════════════════
    // Generation + Edge-detection
    // ═══════════════════════════════════════════════════════════════════════════
    quint64 m_shaderTransitionGenerationCounter = 0;
    QHash<KWin::EffectWindow*, bool> m_lastFullyMaximized;
    QPointer<KWin::EffectWindow> m_lastFocusShaderWindow;
};

} // namespace PlasmaZones
