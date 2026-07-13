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

#include <opengl/glframebuffer.h>
#include <opengl/gltexture.h>

#include <QSize>

#include <memory>

#include <epoxy/gl.h>

namespace PlasmaZones {

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
inline bool packVariesPerFrame(const PlasmaZones::CompiledSurfacePack& pk, bool animating)
{
    // The backdrop half. uHasBackdrop and uBackdropRect are as per-frame as uBackdrop
    // itself: the gate flips when the window leaves an output and the sub-rect is
    // recomputed by every capture, so a pack that reads either without sampling the
    // texture would otherwise be classified static, cached, and frozen on a stale rect.
    const auto readsBackdrop = [](int backdrop, int hasBackdrop, int backdropRect) {
        return backdrop >= 0 || hasBackdrop >= 0 || backdropRect >= 0;
    };
    // The half Decorations.Performance can freeze.
    const auto readsClock = [animating](int time, int audio) {
        return animating && (time >= 0 || audio >= 0);
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
