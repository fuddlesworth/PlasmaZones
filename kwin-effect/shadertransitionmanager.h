// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/AnimationAppRule.h>
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

    PhosphorAnimationShaders::AnimationAppRuleList& appRules()
    {
        return m_animationAppRules;
    }
    const PhosphorAnimationShaders::AnimationAppRuleList& appRules() const
    {
        return m_animationAppRules;
    }

    /// Rebuild the animation `WindowRuleSet` from the current `appRules()`.
    /// Call after every mutation of `m_animationAppRules` (the D-Bus load
    /// callback). The bridge converts the App Rule list into rule-engine
    /// form; the bound `RuleEvaluator` picks up the new revision transparently
    /// and its match cache is invalidated.
    void rebuildAnimationRuleSet();

    /// The evaluator bound to the animation rule set. Resolution of the
    /// animation App Rule cascade routes through this.
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
    /// `tryBeginShaderForEvent` resolves before applying the App Rule
    /// timing cascade. Refreshed on the dedicated `motionProfileTreeChanged`
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

    // ═══════════════════════════════════════════════════════════════════════════
    // Per-window State
    // ═══════════════════════════════════════════════════════════════════════════

    /// Access the in-flight shader transition map (for paintWindow).
    std::unordered_map<KWin::EffectWindow*, ShaderTransition>& shaderTransitions()
    {
        return m_shaderTransitions;
    }
    const std::unordered_map<KWin::EffectWindow*, ShaderTransition>& shaderTransitions() const
    {
        return m_shaderTransitions;
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
    PhosphorAnimationShaders::AnimationAppRuleList m_animationAppRules;
    PhosphorAnimation::ProfileTree m_motionProfileTree;

    // ═══════════════════════════════════════════════════════════════════════════
    // Window-rule view of the animation App Rules
    //
    // `m_animationRuleSet` is the bridge-converted form of `m_animationAppRules`,
    // rebuilt by `rebuildAnimationRuleSet()` on every App Rule D-Bus load.
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

    // ═══════════════════════════════════════════════════════════════════════════
    // Generation + Edge-detection
    // ═══════════════════════════════════════════════════════════════════════════
    quint64 m_shaderTransitionGenerationCounter = 0;
    QHash<KWin::EffectWindow*, bool> m_lastFullyMaximized;
    QPointer<KWin::EffectWindow> m_lastFocusShaderWindow;
};

} // namespace PlasmaZones
