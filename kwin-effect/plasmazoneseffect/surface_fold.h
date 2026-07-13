// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Helpers for the decoration composite fold (surfacelayers.cpp): GL target
// allocation, and the predicate that decides whether a pack's fold can be cached
// across frames. Split out to keep that TU under the 800-line limit.
//
// `inline`, not an anonymous namespace: the fold is compiled under a unity build, and
// a file-local definition that drifted into a second TU in the same chunk would
// collide (see drawFullscreenQuad's note in surfacelayers.cpp).

#include "types.h"

#include <core/output.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <opengl/glframebuffer.h>
#include <opengl/gltexture.h>

#include <QRectF>
#include <QSize>

#include <cmath>
#include <memory>

#include <epoxy/gl.h>

namespace PlasmaZones {

/// Absolute tolerance for the folded state scalars (focus, opacity).
///
/// ONE spelling, shared by the invalidator that decides the fold's state MOVED and by the
/// consumers that ask whether a folded value is at its resting point. They gate the same
/// machinery from opposite ends, so two different predicates let a value be "unmoved" to one
/// and "not resting" to the other. Absolute, not qFuzzyCompare: that is a RELATIVE compare
/// (~1e-12 here), which is the wrong question for a [0,1] scalar and is meaningless against
/// zero.
inline constexpr float kFoldStateEpsilon = 0.0001f;

inline bool foldStateEqual(float a, float b)
{
    return std::fabs(a - b) <= kFoldStateEpsilon;
}

/// Does @p state hold a backdrop a pack can actually sample?
///
/// ONE predicate. The fold asked "is the width positive" while the backdrop's own union
/// asked "are the width AND the height positive", so a slice that rounded to zero height
/// read as available to the fold, which then pushed uHasBackdrop = 1 with a zero-height
/// rect for a pack to divide by.
inline bool backdropUsable(const SurfaceMultipassState& state)
{
    return state.backdropTex && state.backdropRect.z() > 0.0f && state.backdropRect.w() > 0.0f;
}

/// The scale a window's decoration canvas is pinned to, the SAME on every output that paints
/// the window.
///
/// paintWindow runs once per output the window touches, and the viewport scale handed to each
/// call is THAT output's scale. A window straddling a 1x and a 2x output was therefore handed
/// a different scale on each of the two calls per frame, which changed both the texture size
/// and captureScale, tripped the realloc-and-recapture branch every single time, and re-ran
/// the fold's most expensive step — a full effects->drawWindow() re-entry — twice per frame,
/// forever. Every cache the window had was dead for exactly the mixed-DPI multi-monitor case.
///
/// Pin to the HIGHEST scale among the outputs the window's expanded rect intersects: one
/// canvas, one capture, one fold, and the lower-scale output's paint samples the cache.
/// Highest rather than the current output's, so the hi-DPI half of a straddle is rendered at
/// full resolution instead of upscaled from a low-resolution canvas. It cannot change with
/// which output is painting, so the two per-frame calls now agree and the caches hold.
///
/// Falls back to the window's own screen (then 1.0) when the intersection finds nothing — a
/// window mid-move can briefly report a rect touching no output at all.
inline qreal windowSurfaceScale(const KWin::EffectWindow* w)
{
    if (!w || !KWin::effects) {
        return 1.0;
    }
    QRectF rect = w->expandedGeometry();
    if (rect.isEmpty()) {
        rect = w->frameGeometry();
    }
    qreal best = 0.0;
    const auto outputs = KWin::effects->screens();
    for (const KWin::LogicalOutput* out : outputs) {
        if (out && rect.intersects(QRectF(out->geometry())) && out->scale() > best) {
            best = out->scale();
        }
    }
    if (best <= 0.0) {
        if (const KWin::LogicalOutput* const own = w->screen()) {
            best = own->scale();
        }
    }
    return best > 0.0 ? best : 1.0;
}

/// The rect a padded decoration actually covers: the window's expanded geometry (its
/// frame, if it reports none) grown by the chain's outer padding.
///
/// One definition, because the five callers that damage this band and the one that records
/// it must agree to the pixel. They were five copies of the same six lines, and a band
/// computed one way but damaged another leaves a stale glow behind — which is exactly the
/// bug that put the copy in the hover driver in the first place.
inline QRectF paddedBandRect(const KWin::EffectWindow* w, int outerPadding)
{
    if (!w) {
        return {};
    }
    QRectF padded = w->expandedGeometry();
    if (padded.isEmpty()) {
        padded = w->frameGeometry();
    }
    const qreal pad = outerPadding;
    return padded.adjusted(-pad, -pad, pad, pad);
}

/// Damage the whole padded band. A no-op for an unpadded chain, whose composite never
/// draws outside the window rect and is covered by addRepaintFull alone.
///
/// Guards the window too, matching windowSurfaceScale above: this is reached from teardown
/// paths, where a null window is the natural way for a future edit to arrive here.
inline void damagePaddedBand(const KWin::EffectWindow* w, int outerPadding)
{
    if (!w || outerPadding <= 0 || !KWin::effects) {
        return;
    }
    KWin::effects->addRepaint(KWin::RectF(paddedBandRect(w, outerPadding)));
}

/// Allocate a full-canvas RGBA8 target and the framebuffer that wraps it. False on
/// failure, with both left null.
inline bool allocSurfaceTarget(std::unique_ptr<KWin::GLTexture>& tex, std::unique_ptr<KWin::GLFramebuffer>& fbo,
                               const QSize& size)
{
    tex = KWin::GLTexture::allocate(GL_RGBA8, size);
    if (!tex) {
        fbo.reset();
        return false;
    }
    tex->setFilter(GL_LINEAR);
    tex->setWrapMode(GL_CLAMP_TO_EDGE);
    fbo = std::make_unique<KWin::GLFramebuffer>(tex.get());
    if (!fbo->valid()) {
        fbo.reset();
        tex.reset();
        return false;
    }
    return true;
}

/// The padded canvas a decorated window's composite (and its backdrop) is built on.
///
/// ONE definition, because the backdrop MUST be texel-aligned with the composite it is
/// sampled beside — surface_backdrop.cpp said so in a comment and then re-derived the same
/// arithmetic from scratch to achieve it. The failure mode of a drift between the two is a
/// silently misaligned frost, which nothing would catch.
///
/// The scale is capped so a very large window on a very high-DPI output cannot ask for a
/// texture past the driver's limit; past the cap the canvas is pinned on its long axis and
/// captureScale drops, which is why captureScale is a cache key in its own right.
struct SurfaceCanvas
{
    QRectF logicalGeometry; ///< the window's expanded rect, inflated by the chain's padding
    qreal captureScale = 1.0; ///< the output scale, reduced if the cap bites
    QSize textureSize; ///< the canvas in device px — empty means "nothing to draw"
};

inline SurfaceCanvas surfaceCanvasFor(const QRectF& expandedOrFrame, qreal outerPadding, qreal outputScale)
{
    constexpr qreal kMaxSurfaceDim = 8192.0;
    SurfaceCanvas canvas;
    canvas.logicalGeometry = expandedOrFrame;
    canvas.logicalGeometry.adjust(-outerPadding, -outerPadding, outerPadding, outerPadding);
    canvas.captureScale = outputScale;
    const qreal longestPx = qMax(canvas.logicalGeometry.width(), canvas.logicalGeometry.height()) * canvas.captureScale;
    if (longestPx > kMaxSurfaceDim) {
        canvas.captureScale *= kMaxSurfaceDim / longestPx;
    }
    canvas.textureSize = (canvas.logicalGeometry.size() * canvas.captureScale).toSize();
    return canvas;
}

/// Can this pack's fold be reused across frames?
///
/// The inputs a pack reads that are not fixed by (window size, parameter set) fall
/// into two very different groups, and conflating them is what made the caches miss
/// the case they exist for:
///
///   PER-FRAME — a different value on literally every frame, so a cached fold is
///   stale as soon as it is written and there is nothing to key on:
///       iTime            continuous seconds
///       audio spectrum   the live CAVA bars
///       uBackdrop        the scene behind the window (and the uHasBackdrop gate and
///                        uBackdropRect sub-rect that describe it, which move on their
///                        own as the window crosses outputs)
///
/// @p animating is what Decorations.Performance decides: false when this window's
/// chain is not allowed to animate right now (the session is idle, or the window is
/// unfocused and only the focused one may animate). Such a chain has its per-frame
/// inputs taken away — a STOPPED clock, SILENCE where the audio spectrum was, and no
/// cursor — so they stop being per-frame and the fold becomes cacheable, which is the
/// whole point. Without this the pause bought nothing under concurrent activity: KWin
/// paints a window whenever anything overlapping it damages, so a paused animated chain
/// re-folded on a neighbour's damage with a fresh live clock, paying the full fold cost
/// and lurching forward in jumps instead of holding still.
///
/// Note the asymmetry, because it is a real one and it is deliberate. The clock is
/// FROZEN (the pack keeps the phase it stopped at), but the audio is SILENCED rather
/// than frozen: freezing it would mean snapshotting the spectrum per window, and a
/// spectrum bar that holds one arbitrary beat forever is not obviously better than one
/// that settles. So an audio-reactive decoration settles to its silent look when it
/// pauses, and picks the beat back up when it resumes. The settle lands on the repaint
/// the pause itself issues, alongside the focus cross-fade, not at some later moment.
///
/// The BACKDROP is deliberately NOT frozen by @p animating. It changes with the scene
/// behind the window, which keeps moving whether or not this window may animate, and
/// its refold lands no damage on this window — so a frosted pane must keep re-folding
/// or it shows a stale reflection of a desktop that has moved on. (The idle gate does
/// freeze it, one level up: nothing is being looked at then, and a media player that
/// IS being looked at holds an idle inhibitor.)
///
///   STATE — constant between events, so a cached fold stays correct until the
///   state moves, and the state itself is the cache key:
///       uSurfaceFocused  focus (and the cross-fade ramp between the two states,
///                        which CLAMPS to exactly 0.0 / 1.0 at rest)
///       uSurfaceOpacity  the rule-resolved window opacity
///       iMouse           the cursor. STATE, not per-frame: it is constant between
///                        cursor MOVES, and a hover pack classed per-frame re-folded its
///                        whole chain at vsync forever with the pointer sitting still on
///                        another monitor — every other per-frame driver here settles
///                        (audio quiets, the focus ramp clamps, the backdrop rate-limits)
///                        and this one had no quiet condition at all
///
/// This function answers only the first group. Treating focus as per-frame is what
/// disqualified the DEFAULT border pack — data/surface/border/effect.frag mixes its
/// active and inactive colours on uSurfaceFocused — and with the flagship chain
/// ["border"] disqualified, both the prefix cache and the whole-composite cache were
/// dead code for the most common decorated window on the desktop.
///
/// The state group is keyed instead: SurfaceMultipassState::foldedFocus /
/// foldedOpacity record what the cached fold was baked with, and the fold refuses
/// the cache when they no longer match. A settled bordered window therefore folds
/// once and then holds; it re-folds across a focus ramp, which is exactly when its
/// border genuinely changes colour.
///
/// Everything else on CompiledSurfacePack (uSurfaceSize, uSurfaceScale, the frame
/// rect, custom params and colours) is fixed for a given size and parameter set, and
/// all of those already force a realloc or an explicit invalidation when they move.
///
/// Deliberately introspective, never keyed on a pack id: the compiled shader's
/// linked uniforms decide it, so a new pack is classified with no change here.
inline bool packVariesPerFrame(const PlasmaZones::CompiledSurfacePack& pk, bool animating, bool audioDriving)
{
    // The backdrop half. uHasBackdrop and uBackdropRect are as per-frame as uBackdrop
    // itself: the gate flips when the window leaves an output and the sub-rect is
    // recomputed by every capture, so a pack that reads either without sampling the
    // texture would otherwise be classified static, cached, and frozen on a stale rect.
    const auto readsBackdrop = [](int backdrop, int hasBackdrop, int backdropRect) {
        return backdrop >= 0 || hasBackdrop >= 0 || backdropRect >= 0;
    };
    // The half Decorations.Performance can freeze.
    //
    // @p audioDriving is audioReactiveDriving(): whether the spectrum is actually live. An
    // audio pack whose visualizer is OFF (or whose track has been quiet past the idle window)
    // is handed silence, so it does not vary per frame and its fold is cacheable — which is
    // exactly what windowSurfaceAnimates concludes when it stops driving such a window. This
    // predicate did not ask, so it kept classifying the pack as per-frame, compositeValid was
    // cleared on every fold, and the window re-folded its entire chain on any incidental
    // paint forever, for a composite that is byte-identical. The classifier and the driver
    // have to key on ONE expression; disagreeing about it is how this PR produced three
    // separate cache bugs already.
    const auto readsClock = [animating, audioDriving](int time, int audio) {
        return (animating && time >= 0) || (audioDriving && animating && audio >= 0);
    };

    if (readsBackdrop(pk.uBackdropLoc, pk.uHasBackdropLoc, pk.uBackdropRectLoc)
        || readsClock(pk.uTimeLoc, pk.iAudioSpectrumSizeLoc)) {
        return true;
    }
    // A main pass fed by a time-varying buffer pass is itself time-varying.
    for (const PlasmaZones::CompiledSurfaceBufferPass& bp : pk.bufferPasses) {
        if (readsBackdrop(bp.uBackdropLoc, bp.uHasBackdropLoc, bp.uBackdropRectLoc)
            || readsClock(bp.uTimeLoc, bp.iAudioSpectrumSizeLoc)) {
            return true;
        }
    }
    return false;
}
} // namespace PlasmaZones
