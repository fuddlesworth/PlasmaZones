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
///       uBackdrop        the scene behind the window
///       iMouse           the cursor
///
///   STATE — constant between events, so a cached fold stays correct until the
///   state moves, and the state itself is the cache key:
///       uSurfaceFocused  focus (and the cross-fade ramp between the two states,
///                        which CLAMPS to exactly 0.0 / 1.0 at rest)
///       uSurfaceOpacity  the rule-resolved window opacity
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
inline bool packVariesPerFrame(const PlasmaZones::CompiledSurfacePack& pk)
{
    const auto passVaries = [](int time, int audio, int backdrop) {
        return time >= 0 || audio >= 0 || backdrop >= 0;
    };
    if (passVaries(pk.uTimeLoc, pk.iAudioSpectrumSizeLoc, pk.uBackdropLoc) || pk.iMouseLoc >= 0) {
        return true;
    }
    // A main pass fed by a time-varying buffer pass is itself time-varying.
    for (const PlasmaZones::CompiledSurfaceBufferPass& bp : pk.bufferPasses) {
        if (passVaries(bp.uTimeLoc, bp.iAudioSpectrumSizeLoc, bp.uBackdropLoc)) {
            return true;
        }
    }
    return false;
}
} // namespace PlasmaZones
