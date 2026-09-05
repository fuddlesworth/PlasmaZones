// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "stripmotionsampler.h"

#include <PhosphorAnimation/AnimationShaderContract.h> // kMaxCustomParams / kMaxCustomColors
#include <PhosphorProtocol/ScrollAxisEnum.h>

#include <QHash> // std::hash<QString> specialization (used by the unordered_map key below)
#include <QString>
#include <QVariant> // QVariantMap
#include <QVector4D>

#include <array>
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
/// while the spring is live, the scene — columns already translated by the
/// spring's offset, parked columns relocated, the tab pills blitted at the
/// anchor's slot — is rendered into a per-output capture whose ALPHA is the
/// strip layer's coverage alone. The windows stacked BELOW the strip (the
/// desktop background, keep-below windows) paint into it first, so the
/// compositor's blur and translucency have their backdrop; at the band's
/// bottom edge paintWindow calls snapshotBelowCapture(), which copies that
/// below-strip content into a second texture (uBelow) and zeroes the
/// capture's alpha before the columns paint over it. Windows stacked ABOVE
/// the strip (OSDs, notifications, floats, panels) are skipped and
/// composited sharp over the pass, so an OSD popped mid-scroll is never
/// smeared; see PlasmaZonesEffect::m_stripCaptureBelowStrip /
/// m_stripCaptureAboveStrip and the trigger and record-and-return in
/// paintWindow that consume them. One full-screen quad then runs the
/// assigned strip pack (data/animations/<id>, appliesTo ["strip"], sampling
/// uStrip via strip_transition.glsl, which subtracts uBelow out of every
/// sample so the pack only ever displaces the strip layer, and whose entry
/// point re-composites the result over the UNDISPLACED uBelow), driven by
/// offset/velocity uniforms (iStripMotion). The wallpaper therefore never
/// moves with the columns. The pack DECORATES the motion; the spring still
/// owns it, so the curve/duration settings on `scrolling.view` behave
/// identically with or without a pack.
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
/// manager first). NOTE that preemption also FREEZES the strip visually: the
/// desktop blend reuses its two captures for its whole duration while the
/// view spring keeps integrating behind it, so the strip reappears at (or
/// near) its settled position when the blend ends. That is accepted — a
/// desktop switch replaces the whole scene, so animating the strip under it
/// would be invisible work — and the sampler's gap discipline keeps the
/// resume from spiking the velocity.
///
/// The spring is cut at a nonzero velocity (its settle band), so the pass
/// does not stop dead with it: a short SETTLE FADE (StripMotionSampler)
/// keeps painting with the last live velocity decaying to zero, which is
/// what makes velocity-driven packs land without a pop. The fade outlives
/// the spring, so liveness here is "spring live OR fade open".
class StripTransitionManager
{
public:
    explicit StripTransitionManager(PlasmaZonesEffect* effect);
    ~StripTransitionManager();

    StripTransitionManager(const StripTransitionManager&) = delete;
    StripTransitionManager& operator=(const StripTransitionManager&) = delete;

    /// Arm (or refresh) the strip pass for @p output. Called from the tiling
    /// batch path BEFORE the batch's applyBatchDelta for the same output
    /// (that ordering is load-bearing: it is what lets this method tell a
    /// fresh leg from a retarget — see below), with the already resolved
    /// `scrolling.view` @p effectId and @p params (the profile's effective
    /// pack parameters) and the batch's @p viewDelta. An EMPTY id erases
    /// any entry for the output — the user cleared the pack (or disabled
    /// animations) mid-flight, and the next frame falls through to the plain
    /// translation that was running inside the capture all along, visually
    /// seamless.
    ///
    /// Refresh semantics on an already-armed output:
    ///   • RETARGET (spring already live, same pack): capture texture,
    ///     accumulated iTime and velocity state all persist — a wheel batch
    ///     landing mid-leg must not restart the pass any more than it
    ///     restarts the spring. The sampler's offset baseline is shifted by
    ///     @p viewDelta so the committed step never reads as velocity.
    ///   • FRESH LEG (spring NOT live at call time — possible because this
    ///     runs before applyBatchDelta): the motion sampler resets, so a
    ///     pass that begins after a stale armed entry (animations toggled,
    ///     spring cleared outside the paint bracket) starts at iTime 0 with
    ///     no inherited velocity.
    ///   • PACK SWAP (different @p effectId): the sampler resets too — pack
    ///     B must not begin at pack A's accumulated clock and frame count.
    ///   • AXIS FLIP (@p axis differs from the animator's current axis for
    ///     this output): same arm as a pack swap. The spring may well be live
    ///     at call time, but applyBatchDelta is about to CANCEL it rather than
    ///     retarget it (an in-flight motion along the old axis has no velocity
    ///     worth keeping), so treating this as a retarget would compensate the
    ///     sampler's baseline against a measurement taken on the old axis and
    ///     then hand the pack a spurious velocity spike on the next sample.
    ///     The comparison is only meaningful because of the ordering contract
    ///     above: running BEFORE applyBatchDelta is what leaves
    ///     StripViewAnimator::axisFor() still reporting the PRE-batch axis.
    ///
    /// @p axis is the batch's strip axis for this output, the same value the
    /// caller passes to applyBatchDelta.
    void notifyLeg(KWin::LogicalOutput* output, const QString& effectId, const QVariantMap& params, int viewDelta,
                   PhosphorProtocol::ScrollAxis axis);

    /// True while any armed output's view spring is live OR its settle fade
    /// is open. Feeds PlasmaZonesEffect::isActive() and
    /// blocksDirectScanout().
    bool isRunning() const;

    /// True while @p screen specifically is armed AND (its view spring is
    /// live OR its settle fade is open). prePaintScreen gates the per-output
    /// PAINT_SCREEN_TRANSFORMED mask on this; paintOutput repeats it as its
    /// entry check.
    bool isRunningForOutput(KWin::LogicalOutput* screen) const;

    /// True while this manager holds the compositor's cursor hidden (see
    /// hideCursorForPass). Feeds PlasmaZonesEffect::isActive() SEPARATELY
    /// from isRunning(): on the settle path the hide is released only from
    /// the effect's paint hooks (paintOutput's settle frame, postPaintScreen's
    /// reap; the off-paint kill paths outputRemoved and reset release it
    /// themselves), and isRunning() goes false the instant the settle fade's
    /// window closes.
    /// When that happens between the last fade frame and the next frame's
    /// chain build, an isActive() built on isRunning() alone drops the
    /// effect from the chain with the cursor still hidden and no hook left
    /// to show it again — the pointer stays invisible until a later leg
    /// happens to run the release. This keeps the effect in the chain for
    /// the one extra frame (the last fade frame always damages the output)
    /// that runs updateCursorHiding.
    bool holdsCursorHide() const
    {
        return m_cursorHidden;
    }

    /// Paint one output's strip pass. Returns true when the decorated scene
    /// was drawn (the caller must then SKIP the normal scene paint for this
    /// output); false when the output is not armed, its spring has settled,
    /// or the pack cannot run (compile/capture failure) — the caller then
    /// paints the normal scene, so the fallback is always the plain
    /// translation, never a black output.
    bool paintOutput(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport, int mask,
                     const KWin::Region& deviceRegion, KWin::LogicalOutput* screen);

    /// Erase entries whose view spring is dead AND whose settle fade has
    /// closed, freeing their capture textures (GL-context guard). Called
    /// from postPaintScreen; pure resource hygiene (the frame that ends the
    /// fade already painted the normal scene). PAINT-DRIVEN ONLY: an output
    /// that stops painting mid-leg (DPMS) keeps its entry until it paints
    /// again or is torn down — its spring's clock freezes with it, the leg
    /// completes on wake, and the retained cost is one capture texture. The
    /// non-paint kill paths (animations toggled off, a screen leaving the
    /// scrolling set, output removal, teardown) each disarm at their own
    /// site rather than relying on this reap.
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
        // Capture target reused across the frames of ONE leg (plus its
        // settle fade), revalidated against the viewport's device size + the
        // on-screen target's internal format each paint (reallocated on
        // mismatch). Not allocated fresh per frame the way the desktop
        // captures are — this pass re-renders the scene into it every frame
        // — but it does NOT outlive the leg: reapSettled frees the entry at
        // settle, so a new burst of scrolling pays one allocation.
        std::unique_ptr<KWin::GLTexture> captureTex;
        std::unique_ptr<KWin::GLFramebuffer> captureFbo;
        // The below-strip snapshot (uBelow): a copy of the capture taken at
        // the strip band's bottom edge, before any column paints. Same
        // size and format as captureTex, allocated and freed with it.
        std::unique_ptr<KWin::GLTexture> belowTex;
        // Velocity estimation, painted-frame iTime accumulation, batch-jump
        // compensation and the settle fade, extracted into a plain-numbers
        // struct so the arithmetic is unit-testable without a compositor
        // (test_strip_motion_sampler). Persists across RETARGET refreshes;
        // reset on a fresh leg or a pack swap (see notifyLeg).
        StripMotionSampler motion;
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
        int uBelowLoc = -1;
        int iTimeLoc = -1;
        int iResolutionLoc = -1;
        int iFrameLoc = -1;
        int iStripMotionLoc = -1;
        int iStripAxisLoc = -1;
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

    /// The software cursor is part of the scene: KWin draws its overlay item
    /// (cursor, drag icon) at the end of every scene walk, INSIDE the
    /// paintScreen this pass nests to fill its capture. Left alone it lands
    /// in uStrip and the pack smears it with the columns, and since the pass
    /// replaces the output's paint nothing draws it sharp afterwards. So
    /// while a pass paints the output under the pointer the compositor's
    /// cursor is hidden (KWin then neither composites it nor puts it on a
    /// cursor plane) and the pass blits the cursor image itself as its last
    /// draw, above the composited above-strip windows.
    ///
    /// Hides when @p screen's pass is about to capture (its shader compiled
    /// and its capture target allocated, so the pass WILL replace this
    /// frame's paint) and the pointer is on it. A no-op when another effect
    /// already hides the cursor, so the pass never resurrects a cursor
    /// something else wanted gone.
    void hideCursorForPass(KWin::LogicalOutput* screen);
    /// Show the cursor again once no LIVE pass (spring live or fade open)
    /// covers the pointer: the leg settled, the output went away, or the
    /// pointer moved to an output with no pass. Runs from paintOutput's
    /// settle frame, so that frame's normal scene paints the cursor rather
    /// than blinking it for a frame, and from every postPaintScreen, so a
    /// pointer crossing to a quiet output gets its cursor back within a
    /// frame even though the hidden cursor damages nothing there.
    void updateCursorHiding();
    /// Render the scene's own cursor item into the pass, at the pointer.
    /// Only meaningful while hideCursorForPass has the cursor hidden: the
    /// item is drawn explicitly (the renderer only honours visibility on
    /// child items), so hiding it from KWin does not hide it from us.
    void drawCursor(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport);
    bool cursorOnOutput(KWin::LogicalOutput* screen) const;

    /// Draw @p windows sharp onto the current target, bottom to top, each
    /// through the effect's own paintWindow with m_directPaintCapture set.
    /// Used for the above-strip set after the pack's quad.
    void compositeSharp(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                        const QList<KWin::EffectWindow*>& windows);

public:
    /// Called by PlasmaZonesEffect::paintWindow, inside the capture walk,
    /// for the first window that is not below the strip band: copies the
    /// capture (below-strip content only at that point) into the pass's
    /// belowTex and zeroes the capture's alpha, so the columns that paint
    /// next leave the capture's alpha equal to the strip layer's coverage.
    /// Idempotent per capture through m_stripCaptureBelowSnapshotted; a
    /// no-op outside a capture. Requires the capture framebuffer to be the
    /// current one, which is true for the whole walk.
    void snapshotBelowCapture();

private:
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
    /// True while THIS manager holds a hideCursor() on the compositor (the
    /// call is refcounted, so the flag keeps show/hide balanced).
    bool m_cursorHidden = false;
};

} // namespace PlasmaZones
