// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/AnimationShaderContract.h> // kMaxCustomParams / kMaxCustomColors

#include <QHash> // std::hash<QString> specialization (used by the unordered_map key below)
#include <QString>
#include <QVariant> // QVariantMap
#include <QVector4D>

#include <array>
#include <memory>
#include <unordered_map>

namespace KWin {
class GLFramebuffer;
class GLShader;
class GLTexture;
class LogicalOutput;
class RenderTarget;
class RenderViewport;
class Region;
}

namespace PlasmaZones {

class PlasmaZonesEffect;

/// The scrolling strip's per-output shader pass: a velocity-driven
/// POST-PROCESS over the live scene while the strip's view spring is in
/// flight.
///
/// Unlike the desktop transitions (DesktopTransitionManager), this pass has
/// no from/to endpoint pair and no progress: wheel scrolling retargets the
/// view spring with PreserveVelocity on every batch (StripViewAnimator), so
/// there are no discrete legs for a crossfade to play. Instead, every frame
/// while the spring is live, the ordinary scene — columns already translated
/// by the spring's offset, parked columns relocated, the tab-indicator
/// surface riding along — is rendered into a per-output capture, and one
/// full-screen quad runs the assigned strip pack (data/animations/<id>,
/// appliesTo ["strip"], sampling uStrip via strip_transition.glsl) over it,
/// driven by offset/velocity uniforms (iStripMotion). The pack DECORATES the
/// motion; the spring still owns it, so the curve/duration settings on
/// `scrolling.view` behave identically with or without a pack.
///
/// The tiling batch path resolves the `scrolling.view` shader beside its
/// motion-profile resolve and calls notifyLeg() for every output it seeds a
/// view leg on (tilinghandler/tiling.cpp). Liveness is the SPRING's, not a
/// timer's: isRunningForOutput() consults StripViewAnimator, so the pass
/// starts on the first painted frame of a leg and stops on the settle frame
/// with no lifecycle of its own — settled entries are lazily reaped from
/// postPaintScreen (reapSettled). With no pack assigned notifyLeg() erases
/// the output's entry and every frame falls through to the plain translation.
///
/// The pass forces composition while live (PlasmaZonesEffect::
/// blocksDirectScanout) — a surface presented directly on a hardware plane
/// would bypass it entirely — and never runs under a live desktop transition,
/// which replaces the scene wholesale (paintScreen consults the desktop
/// manager first).
class StripTransitionManager
{
public:
    explicit StripTransitionManager(PlasmaZonesEffect* effect);
    ~StripTransitionManager();

    StripTransitionManager(const StripTransitionManager&) = delete;
    StripTransitionManager& operator=(const StripTransitionManager&) = delete;

    /// Arm (or refresh) the strip pass for @p output. Called from the tiling
    /// batch path every time a view leg is seeded, with the already resolved
    /// `scrolling.view` @p effectId and @p params (the profile's effective
    /// pack parameters). An EMPTY id erases any entry for the output — the
    /// user cleared the pack (or disabled animations) mid-flight, and the
    /// next frame falls through to the plain translation that was running
    /// inside the capture all along, visually seamless.
    ///
    /// Refreshing an ALREADY-ACTIVE output only updates effectId/params: the
    /// capture texture, iTime origin and velocity state persist. This is the
    /// retarget path — a wheel batch landing mid-leg must not restart the
    /// pass any more than it restarts the spring.
    void notifyLeg(KWin::LogicalOutput* output, const QString& effectId, const QVariantMap& params);

    /// True while any armed output's view spring is live. Feeds
    /// PlasmaZonesEffect::isActive() and blocksDirectScanout().
    bool isRunning() const;

    /// True while @p screen specifically is armed AND its view spring is
    /// live. prePaintScreen gates the per-output PAINT_SCREEN_TRANSFORMED
    /// mask on this; paintOutput repeats it as its entry check.
    bool isRunningForOutput(KWin::LogicalOutput* screen) const;

    /// Paint one output's strip pass. Returns true when the decorated scene
    /// was drawn (the caller must then SKIP the normal scene paint for this
    /// output); false when the output is not armed, its spring has settled,
    /// or the pack cannot run (compile/capture failure) — the caller then
    /// paints the normal scene, so the fallback is always the plain
    /// translation, never a black output.
    bool paintOutput(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport, int mask,
                     const KWin::Region& deviceRegion, KWin::LogicalOutput* screen);

    /// Erase entries whose view spring is no longer live, freeing their
    /// capture textures (GL-context guard). Called from postPaintScreen —
    /// the settle frame itself needs no repaint (paintOutput returns false
    /// and the normal scene paints in the SAME frame), so this is pure
    /// resource hygiene, and it also covers outputs that stopped painting
    /// mid-leg (DPMS) once StripViewAnimator's own clock reap clears their
    /// springs.
    void reapSettled();

    /// Drop every compiled strip shader so the next scroll recompiles against
    /// freshly reloaded pack source. Called from the AnimationShaderRegistry
    /// hot-reload handler, same site as the desktop and per-window caches.
    void invalidateShaderCache();

    /// Drop a removed output's entry (if any). Called from the effect's
    /// screenRemoved handler beside StripViewAnimator::forgetOutput — a
    /// disconnected LogicalOutput* left in m_active would dangle.
    void outputRemoved(KWin::LogicalOutput* screen);

    /// Drop all state and release GL resources (effect teardown / compositor
    /// reset).
    void reset();

private:
    struct OutputStripPass
    {
        QString effectId;
        // Resolved p_<name> values packed into the customParams[] /
        // customColors[] slot pools (metadata defaults merged with the
        // profile's overrides) — same translation as the desktop pass.
        std::array<QVector4D, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams> customParams{};
        std::array<QVector4D, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors> customColors{};
        // Persistent per-output capture target, reused across frames and
        // revalidated against the viewport's device size + the on-screen
        // target's internal format each paint (reallocated on mismatch).
        // Deliberately NOT allocated fresh per frame the way the desktop
        // captures are: this pass re-renders the scene into it every frame
        // for the whole leg, so churning an output-sized allocation per
        // frame would be pure waste.
        std::unique_ptr<KWin::GLTexture> captureTex;
        std::unique_ptr<KWin::GLFramebuffer> captureFbo;
        // iTime origin (pinned-clock ms of the first painted frame); -1 =
        // not yet painted. Persists across notifyLeg refreshes — iTime is
        // monotonic for the whole pass, never rewinding on a retarget.
        qint64 startTimeMs = -1;
        // Previous paint tick (pinned-clock ms) for the velocity dt; -1 = no
        // prior tick (first frame reports zero velocity).
        qint64 lastPaintTimeMs = -1;
        // Previous tick's view offset in LOGICAL px (offsetFor's space);
        // converted to device px only at upload.
        qreal lastOffsetPx = 0.0;
        // One-pole smoothed velocity, logical px/s. Smoothing (~30 ms tau)
        // keeps per-batch retargets from spiking a velocity-driven blur.
        qreal smoothedVelocity = 0.0;
        // Monotonic paint counter uploaded as iFrame, zero-based.
        int frameCount = 0;
    };

    /// An array of @p N uniform locations, every slot "unset" (-1).
    template<std::size_t N>
    static constexpr std::array<int, N> makeUnsetLocations()
    {
        std::array<int, N> locs{};
        for (int& loc : locs) {
            loc = -1;
        }
        return locs;
    }

    /// Compiled strip-pass shader + cached uniform locations. Keyed by
    /// effectId, mirroring the desktop and per-window caches.
    struct CompiledStripShader
    {
        std::unique_ptr<KWin::GLShader> shader; // null == compile failed (sentinel, don't retry)
        int uStripLoc = -1;
        int iTimeLoc = -1;
        int iResolutionLoc = -1;
        int iFrameLoc = -1;
        int iStripMotionLoc = -1;
        int iStripRectLoc = -1;
        // Filled with -1 rather than left value-initialised: `{}` would
        // default every slot to 0, a VALID location — see the identical note
        // on CompiledDesktopShader.
        std::array<int, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams> customParamsLoc =
            makeUnsetLocations<PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams>();
        std::array<int, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors> customColorsLoc =
            makeUnsetLocations<PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors>();
    };

    /// Compile (or fetch from cache) the strip-pass shader for @p effectId.
    /// NEVER null: returns the cache entry, whose `shader` is null when the
    /// id is unknown or compilation failed. Callers must check `->shader`.
    /// Implemented in striptransitionshader.cpp (the assembly part).
    CompiledStripShader* compiledShader(const QString& effectId);

    /// Make the compositor GL context current before freeing GL objects off
    /// the paint thread (same discipline as the desktop teardown part).
    void ensureGlContextCurrent();

    /// Erase one entry, freeing its GL resources. Caller ensures a current
    /// GL context (paintOutput is on the paint thread; the off-thread
    /// mutators call ensureGlContextCurrent first).
    void endOutput(KWin::LogicalOutput* screen);

    PlasmaZonesEffect* m_effect;
    // Move-only mapped values (unique_ptr GL handles) — std::unordered_map,
    // not QHash, which is copy-on-write and would instantiate a deleted copy
    // ctor.
    std::unordered_map<KWin::LogicalOutput*, OutputStripPass> m_active;
    std::unordered_map<QString, CompiledStripShader> m_shaderCache;
};

} // namespace PlasmaZones
