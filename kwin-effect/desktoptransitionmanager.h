// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/AnimationShaderContract.h> // kMaxCustomParams

#include <QHash> // std::hash<QString> specialization (used by the unordered_map key below)
#include <QString>
#include <QVariant> // QVariantMap
#include <QVector4D>

#include <array>
#include <chrono>
#include <memory>
#include <unordered_map>

namespace KWin {
class EffectWindow;
class GLFramebuffer;
class GLShader;
class GLTexture;
class LogicalOutput;
class RenderTarget;
class RenderViewport;
class Region;
class VirtualDesktop;
}

namespace PlasmaZones {

class PlasmaZonesEffect;

/// Full-screen virtual-desktop switch transition.
///
/// Unlike the per-window shader transitions (ShaderTransitionManager, which runs
/// on the OffscreenEffect's redirected per-window quad), a desktop switch is a
/// full-screen TWO-texture blend: it captures the OUTGOING desktop and the
/// INCOMING desktop into per-output FBOs and cross-fades between them with a
/// `desktop.switch` transition shader (data/animations/<id>, appliesTo
/// ["desktop"], sampling uFromDesktop / uToDesktop via desktop_transition.glsl).
///
/// The effect owns one instance. On `desktopChanged` the effect resolves the
/// `desktop.switch` shader from the shader profile tree and calls begin(); if no
/// shader is assigned begin() is a no-op and KWin's normal switch proceeds. When
/// a transition is live the effect claims `setActiveFullScreenEffect(this)` so
/// KWin's built-in Slide bows out, forces a full-screen paint via the mask, and
/// routes paintScreen through paintOutput() which draws the blend instead of the
/// live scene until progress reaches 1.
class DesktopTransitionManager
{
public:
    explicit DesktopTransitionManager(PlasmaZonesEffect* effect);
    ~DesktopTransitionManager();

    DesktopTransitionManager(const DesktopTransitionManager&) = delete;
    DesktopTransitionManager& operator=(const DesktopTransitionManager&) = delete;

    /// Fallback switch duration (ms) when the motion profile tree carries no
    /// `desktop.switch` (or ancestor) duration override.
    static constexpr int DefaultDurationMs = 300;

    /// Start a transition. @p output is the switched output, or nullptr for a
    /// global (all-output) switch — in which case every output transitions from
    /// @p from to @p to. Resolves nothing itself: the caller passes the already
    /// resolved @p effectId (empty → no-op), @p params (the profile's effective
    /// pack parameters, translated into the customParams/customColors pools) and
    /// @p durationMs (the motion-tree resolved duration; <= 0 falls back to
    /// DefaultDurationMs). Capture is deferred to the first paintOutput() for each
    /// output, where a live GL context exists.
    void begin(KWin::VirtualDesktop* from, KWin::VirtualDesktop* to, KWin::LogicalOutput* output,
               const QString& effectId, const QVariantMap& params, int durationMs);

    /// True while any output has a live transition. Feeds PlasmaZonesEffect::isActive()
    /// so KWin keeps the effect in the paint chain.
    bool isRunning() const
    {
        return !m_active.empty();
    }

    /// True while @p screen specifically has a live transition. prePaintScreen gates
    /// the full-screen PAINT_SCREEN_TRANSFORMED mask on this so a per-output switch
    /// (Plasma 6.7 per-output desktops) doesn't force every NON-transitioning output
    /// through the transformed-paint path for the transition's duration.
    bool isRunningForOutput(KWin::LogicalOutput* screen) const
    {
        return m_active.find(screen) != m_active.end();
    }

    /// Paint one output's transition. Returns true when the blend was drawn (the
    /// caller must then SKIP the normal scene paint for this output); false when
    /// this output has no live transition (or it just settled) and the normal
    /// KWin scene paint should proceed. Performs the deferred capture on the
    /// first call for an output, advances progress, and tears the output's
    /// transition down when it completes.
    bool paintOutput(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport, int mask,
                     const KWin::Region& deviceRegion, KWin::LogicalOutput* screen);

    /// Schedule the next frame while any transition is live (called from
    /// postPaintScreen). No-op when idle.
    void scheduleRepaints() const;

    /// Drop every compiled desktop-transition shader so the next switch recompiles
    /// against freshly reloaded pack source. Called from the AnimationShaderRegistry
    /// hot-reload handler: desktop packs are served by the SAME registry as the
    /// per-window effects, so editing a `desktop.switch` pack must invalidate this
    /// cache too (the per-window ShaderTransitionManager cache clears in the same
    /// handler). Makes the compositor context current to free the GLShaders.
    void invalidateShaderCache();

    /// Drop any live transition whose OUTGOING desktop @p desktop was just removed
    /// from the pager. begin() stores a raw VirtualDesktop* that the deferred
    /// captureDesktop() consumes later; a removed desktop leaves that pointer
    /// dangling, so reap the entry (releasing the fullscreen claim when the last
    /// one goes). Wired to EffectsHandler::desktopRemoved.
    void desktopRemoved(KWin::VirtualDesktop* desktop);

    /// Drop a removed output's live transition (if any). Called from the effect's
    /// screenRemoved handler: a disconnected LogicalOutput* left in m_active would
    /// dangle — paintOutput()/scheduleRepaints() deref the key, and the fullscreen
    /// claim would never release once its output vanished. Releases the claim when
    /// the last transition goes.
    void outputRemoved(KWin::LogicalOutput* screen);

    /// Drop all live transitions and release GL resources (effect teardown /
    /// compositor reset). Clears the active-fullscreen-effect claim.
    void reset();

private:
    struct OutputTransition
    {
        KWin::VirtualDesktop* from = nullptr;
        std::unique_ptr<KWin::GLTexture> fromTex;
        std::unique_ptr<KWin::GLTexture> toTex;
        QString effectId;
        // Resolved p_<name> values packed into customParams[] slots (metadata
        // defaults merged with the profile's overrides). Same value for every
        // output in one begin(); copied per-output for the paint upload.
        std::array<QVector4D, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams> customParams{};
        // Resolved color params packed into customColors[] slots as normalised
        // rgba, in declaration order — the color pool the per-window path also
        // uploads (see shader_transitions.cpp). A `"type":"color"` pack param's
        // `p_<name>` define resolves to `customColors[N]`, so without this a
        // desktop pack's colors read transparent black.
        std::array<QVector4D, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors> customColors{};
        // Actual switch direction for direction-aware packs: wrap-corrected
        // desktop-grid delta in cells (.xy, +x = one column right, +y = one
        // row down) and the same delta normalized (.zw). Zeros when the grid
        // positions could not be resolved. Computed once in begin(), uploaded
        // as `iSwitchDelta` (declared in desktop_transition.glsl).
        QVector4D switchDelta;
        qint64 startTimeMs = 0;
        int durationMs = 0;
        // Monotonic paint counter uploaded as iFrame, so glitch-style desktop
        // packs get a per-frame stutter that is independent of the linear iTime
        // progress — matching the canonical animation contract's iFrame.
        int frameCount = 0;
        bool captured = false;
    };

    /// Compiled desktop-transition shader + cached uniform locations. Keyed by
    /// effectId, mirroring ShaderTransitionManager's per-effect cache.
    struct CompiledDesktopShader
    {
        std::unique_ptr<KWin::GLShader> shader; // null == compile failed (sentinel, don't retry)
        int iFromDesktopLoc = -1;
        int iToDesktopLoc = -1;
        int iTimeLoc = -1;
        int iResolutionLoc = -1;
        int iFrameLoc = -1;
        int iSwitchDeltaLoc = -1;
        // customParams[slot] / customColors[slot] uniform locations (-1 when the
        // shader omits the slot — the GLSL compiler prunes unreferenced ones).
        std::array<int, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams> customParamsLoc{};
        std::array<int, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors> customColorsLoc{};
    };

    /// Render every window on @p desktop that intersects @p screen into a fresh
    /// output-sized FBO, bottom-to-top in stacking order (wallpaper included —
    /// it is an on-all-desktops window). Used for the OUTGOING desktop, which is
    /// no longer current so its windows aren't in the live scene and must be
    /// reconstructed via drawWindow. Returns null on allocation failure.
    std::unique_ptr<KWin::GLTexture> captureDesktop(KWin::VirtualDesktop* desktop, KWin::LogicalOutput* screen);

    /// Capture the LIVE composited scene for @p screen into a fresh output-sized
    /// FBO via effects->paintScreen. Used for the INCOMING desktop: it is the
    /// current desktop after the switch, so its already-visible windows render
    /// black through drawWindow (they belong to the ongoing scene paint) — the
    /// scene composite is the correct, reliable source. Returns null on failure.
    std::unique_ptr<KWin::GLTexture> captureLiveScene(int mask, KWin::LogicalOutput* screen);

    /// Compile (or fetch from cache) the desktop-transition shader for @p effectId.
    /// Returns nullptr when the effect id is unknown or compilation failed.
    CompiledDesktopShader* compiledShader(const QString& effectId);

    /// Free an output's transition and, when the last one goes, release the
    /// active-fullscreen-effect claim.
    void endOutput(KWin::LogicalOutput* screen);

    /// Make the compositor GL context current so GLTexture/GLShader frees issue
    /// their glDelete* against a live context. Every off-paint-thread mutator that
    /// releases captured GL objects (begin's re-switch, output/desktop removal,
    /// shader-cache invalidation, reset) calls this first. A no-op at teardown
    /// (!effects), where the driver reclaims the objects regardless.
    void ensureGlContextCurrent();

    PlasmaZonesEffect* m_effect;
    // Move-only mapped values (unique_ptr GL handles) — std::unordered_map, not
    // QHash, which is copy-on-write and would instantiate a deleted copy ctor.
    std::unordered_map<KWin::LogicalOutput*, OutputTransition> m_active;
    std::unordered_map<QString, CompiledDesktopShader> m_shaderCache;
    bool m_fullScreenClaimed = false;
};

} // namespace PlasmaZones
