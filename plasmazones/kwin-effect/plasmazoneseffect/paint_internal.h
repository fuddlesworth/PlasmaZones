// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/// @file paint_internal.h
/// Paint-pipeline internals shared across the split paintWindow translation
/// units (paint_pipeline.cpp, paint_shader_window.cpp, paint_capture.cpp).
///
/// Holds the progress SSOT (timeDrivenProgress) that both the backdrop-capture
/// predictor in paintWindow's tail and the shader-transition draw route through,
/// plus the small context struct that carries paintWindow's per-call state into
/// the extracted shader-transition branch. Both are header-inline (the effect
/// builds as a Unity/jumbo target where an anonymous-namespace copy per TU would
/// collide; an inline definition is safe in both jumbo and non-unity builds).

#include "shader_internal.h" // easeProgress, clampProgressForCurve
#include "transition_types.h" // ShaderTransition

#include <QtGlobal>

namespace KWin {
class RenderTarget;
class RenderViewport;
class Region;
class WindowPaintData;
}

namespace PlasmaZones {

/// Per-call state paintWindow hands to the extracted shader-transition branch
/// (paintShaderTransitionWindow). Holds references to the window-paint chain
/// arguments plus the per-frame clock pinned at the top of paintWindow, so the
/// extracted method observes exactly what the inline branch did.
struct PaintWindowContext
{
    const KWin::RenderTarget& renderTarget;
    const KWin::RenderViewport& viewport;
    KWin::EffectWindow* window;
    int mask;
    const KWin::Region& deviceRegion;
    KWin::WindowPaintData& data;
    qint64 frameNowMs;
};

/// Progress for a TIME-DRIVEN transition (`durationMs > 0`) at @p nowMs: linear
/// ratio → timing-curve ease → held-move pin → release down-ramp. @p active
/// reports whether paintWindow would paint the leg this frame.
///
/// Does NOT apply the `reverse` flip — callers do, because paintWindow shares
/// that final step with its animator-driven branch.
///
/// @p stepCurve owns the SINGLE per-frame integrator step for a stateful
/// (spring) curve. paintWindow passes true. The backdrop capture passes false
/// and reads the last stepped value (at most one frame stale), so it can predict
/// the drawn rect without double-stepping the integrator paintWindow owns.
///
/// Both callers route through this one function on purpose: the capture must
/// sample the scene where the quad is actually drawn, and a partial replica
/// drifts. During a held-move release, for instance, the draw ramps progress
/// back toward 0 while an un-ramped predictor sits pinned at 1 — the frost pane
/// then samples the live frame while the draw lerps toward the grab frame.
inline qreal timeDrivenProgress(ShaderTransition& st, qint64 nowMs, bool stepCurve, bool& active)
{
    active = false;
    if (st.durationMs <= 0) {
        return 0.0;
    }
    qreal progress = 0.0;
    const qint64 elapsed = nowMs - st.startTimeMs;
    if (elapsed >= 0 && elapsed <= st.durationMs) {
        // Ease the linear time progress through the per-event timing curve
        // (resolved global → "All" → node → rule at begin time), so a node's
        // curve shapes its shader iTime exactly as it shapes the animator-driven
        // branch. `lastPaintTimeMs` still holds the previous tick here — it is
        // advanced later, alongside iTimeDelta. Shared with the desktop switch;
        // see easeProgress for the dt cap and the stateful/stateless split.
        const qreal linear = qreal(elapsed) / qreal(st.durationMs);
        progress = ShaderInternal::easeProgress(st.progressCurve.get(), st.progressCurveState, st.lastPaintTimeMs,
                                                nowMs, linear, stepCurve);
        active = true;
    } else if ((st.holdUntilRelease || (st.meshSim.initialized && !st.meshSim.settled)) && elapsed > st.durationMs) {
        // HELD move/resize: the drag outlives the nominal duration by design.
        // Stay active with progress pinned at 1 — the motion uniforms
        // (iMoveVelocity / iMoveOffset / iMoveMesh), not the clock, drive the
        // shader from here, and a stateful curve deliberately stops stepping. The
        // pin is exact: reading the frozen CurveState instead would sit below 1
        // for an underdamped spring. After release (holdUntilRelease cleared) a
        // mesh-driven transition stays active until its lattice settles, so the
        // wobble rings out instead of being cut off at the release frame.
        progress = 1.0;
        active = true;
    }
    // NOTE: there is deliberately NO "completion frame" branch here for a
    // stateful curve. One was tried and was unreachable: it needed a paint at
    // `elapsed > durationMs`, but tryBeginShaderForEvent's teardown timer fires
    // AT durationMs (and Qt's coarse timer may fire up to 5% early), so the leg
    // is erased before such a frame can land. The residual it was meant to hide
    // is instead removed at the source — Spring's settling band is 0.5%, not the
    // 2% control-theory convention, so the integrator is already within half a
    // percent of 1.0 when the clock expires. See SettleBand in spring.cpp.
    // Held-move release leg (velocity/trail move packs): the release handler
    // stamps releaseStartMs instead of clearing the hold flag, and progress ramps
    // back toward 0 over the same durationMs — the shader plays iTime 1→0 after
    // release, so a dissolve-while-held pack (phosphor-vortex) rematerialises
    // instead of snapping at teardown. Subtracting from the base progress (not
    // from a hard 1.0) bounds a grab shorter than the nominal duration by its
    // grab-in value. The ramp is deliberately LINEAR even when the base was eased:
    // this leg is exclusive to window.movement.move, whose packs drive their
    // visuals from the motion uniforms, not from iTime. Mesh packs never stamp
    // this and are untouched. Runs BEFORE the caller's reverse flip, which is
    // correct — the stamp only exists on the held-move path, and window.move
    // installs with reverse == false.
    if (active && st.releaseStartMs >= 0) {
        const qreal down = qreal(nowMs - st.releaseStartMs) / qreal(st.durationMs);
        // Route through the ONE clamp policy, not a bare qBound. A held-move leg
        // whose grab was SHORTER than the nominal duration takes the eased branch
        // above, not the pin — so `progress` can legitimately carry an overshooting
        // curve's overshoot (1.08), and an unconditional qBound would snap it to 1.0
        // at the exact moment the button lets go. The lower bound matters equally:
        // an overshooting curve dips below 0, and a 0.0 floor kills that too.
        progress = ShaderInternal::clampProgressForCurve(progress - qMax(down, 0.0), st.progressCurve.get());
    }
    // Re-grab resume: the accrued down-ramp, decaying back to 0 over the same
    // durationMs. At the re-grab frame the offset equals the ramp the release
    // leg above had accrued, so the first resumed frame reproduces the last
    // released frame exactly — continuous by construction. Applied to the
    // PAINTED progress (not to the clock) so it is correct for a stateless
    // curve and a stateful spring alike: the spring's integrator keeps its own
    // value and this only lifts the offset off it. See the rationale on
    // ShaderTransition::regrabStartMs for why rewinding startTimeMs cannot work.
    if (active && st.regrabStartMs >= 0) {
        const qreal back = qreal(nowMs - st.regrabStartMs) / qreal(st.durationMs);
        const qreal offset = st.regrabDownOffset - qMax(back, 0.0);
        if (offset <= 0.0) {
            // Retire the exhausted offset only on the PAINT pass. `stepCurve` is the
            // "I own the per-frame mutation" flag, and the backdrop predictor passes
            // false precisely so it can read this function without side effects — but
            // this reset was outside that guard, so the predictor was clearing state
            // paintWindow's own call was about to read. Benign today (both share the
            // pinned frame clock, so both see the same expiry), and a free bug for
            // whoever next changes either arm.
            if (stepCurve) {
                st.regrabStartMs = -1;
                st.regrabDownOffset = 0.0;
            }
        } else {
            // Same clamp policy as the release ramp above — see there.
            progress = ShaderInternal::clampProgressForCurve(progress - offset, st.progressCurve.get());
        }
    }
    return progress;
}

} // namespace PlasmaZones
