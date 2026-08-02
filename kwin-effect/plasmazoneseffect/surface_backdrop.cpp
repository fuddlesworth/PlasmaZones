// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"

#include "shader_internal.h"
#include "surface_fold.h"
#include "types.h"
#include "window_query.h"

#include <core/region.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <effect/effectwindow.h>
#include <opengl/glframebuffer.h>
#include <opengl/gltexture.h>
#include <scene/item.h>

#include <QRectF>
#include <QSize>
#include <QVector4D>

#include <epoxy/gl.h>

// Backdrop capture for the surface decoration fold, split out of
// surfacelayers.cpp (which owns the fold itself).
namespace PlasmaZones {

// Blit the scene behind @p w into its backdrop texture (see the header).
// MUST run from paintWindow, while the live render target still holds only
// the content painted below this window.
void PlasmaZonesEffect::captureWindowBackdrop(const KWin::RenderTarget& renderTarget,
                                              const KWin::RenderViewport& viewport, KWin::EffectWindow* w,
                                              const WindowDecoration& wb, const KWin::Region& paintedDeviceRegion,
                                              const QRectF& animatedFrame)
{
    // Mirror renderSurfaceChainComposite's canvas math EXACTLY (padded
    // logical rect, capped capture scale, derived texture size) so uBackdrop
    // and the composite canvas are texel-aligned — a pack samples both with
    // the same uv (backdropTexel / surfaceTexel).
    const qreal pad = wb.outerPadding;
    QRectF windowRect = w->expandedGeometry();
    if (windowRect.isEmpty()) {
        windowRect = w->frameGeometry();
    }
    // The SAME canvas the fold builds, from the same helper — not a second derivation that
    // happens to agree. Texel alignment between the backdrop and the composite is what lets
    // a pack sample both with one uv, and a drift between two copies of this arithmetic
    // would misalign the frost silently.
    //
    // windowSurfaceScale(w), NOT viewport.scale(): the backdrop TEXTURE must match the
    // composite texture, and the composite is now built at the window's pinned scale. Handing
    // this output's scale here would size the backdrop for one output while the composite is
    // sized for another, and on a mixed-DPI straddle the two would disagree — exactly the
    // silent misalignment this shared helper exists to prevent. The blit below still reads
    // through the live `viewport`; only the destination canvas is pinned.
    const SurfaceCanvas canvas = surfaceCanvasFor(windowRect, pad, windowSurfaceScale(w));
    const QRectF logicalGeometry = canvas.logicalGeometry;
    const QSize textureSize = canvas.textureSize;
    if (textureSize.isEmpty()) {
        return;
    }
    const QString windowId = getWindowId(w);
    if (windowId.isEmpty()) {
        return; // no stable id — don't orphan a default-inserted entry under ""
    }
    SurfaceMultipassState& state = m_surfaceMultipass[windowId];
    // ── Multi-output accumulation ───────────────────────────────────────
    // paintWindow runs once per OUTPUT, and each output can only blit the
    // slice of the canvas its viewport covers. OUTPUTS HAVE INDEPENDENT
    // FRAME CLOCKS, so "same frame" is undecidable from the pinned clock —
    // keying a clear on it made the neighbour output (whose cycle always
    // looks like a new frame) wholesale-clear the texture and leave only
    // its overhang sliver, which the pixel probes showed as a transparent
    // backdrop exactly while animations kept both outputs cycling. So:
    // NEVER clear on capture (stale pixels outside the valid rect are
    // never sampled — backdropTexel clamps into it; the texture is cleared
    // once at allocation), and treat captures within a short window as one
    // accumulation generation: their dest slices overwrite in place and
    // the valid rect grows to the union. A stale generation restarts the
    // rect at this capture's slice.
    // Snapshot GL state BEFORE the realloc branch's clear (which sets the clear
    // colour to transparent): the guard must capture the pristine ambient state
    // so it hands blend/viewport/scissor/clear-colour back the way it found them
    // on EVERY exit path, mid scene-walk. Its scope covers the realloc-branch
    // clear and the blit.
    const ShaderInternal::ScopedGlState glStateGuard;
    // Disable scissor for the WHOLE capture: the init clear below and the blit
    // must not be clipped to whatever scissor box the scene walk left enabled.
    // A scissored init clear would leave a freshly-allocated texture's margin
    // undefined. The guard restores the ambient scissor on exit.
    glDisable(GL_SCISSOR_TEST);
    if (state.backdropSize != textureSize || !state.backdropTex) {
        state.backdropFbo.reset();
        state.backdropTex = KWin::GLTexture::allocate(GL_RGBA8, textureSize);
        if (!state.backdropTex) {
            state.backdropSize = QSize();
            return;
        }
        state.backdropTex->setFilter(GL_LINEAR);
        state.backdropTex->setWrapMode(GL_CLAMP_TO_EDGE);
        state.backdropSize = textureSize;
        state.backdropRect = QVector4D();
        // Fresh texture means no output has contributed to any accumulation generation yet, so
        // reset the generation tracker alongside the rect. Harmless today (backdropUsable gates
        // the union on the just-zeroed rect), but leaving stale output rects here would resurface
        // the never-expiring-generation bug if that guard ever stops keying on backdropRect.
        state.backdropGenerationOutputs.clear();
        // Wrap the texture once, here — the per-frame capture blit below reuses it.
        state.backdropFbo = std::make_unique<KWin::GLFramebuffer>(state.backdropTex.get());
        if (!state.backdropFbo->valid()) {
            state.backdropFbo.reset();
            state.backdropTex.reset();
            state.backdropSize = QSize();
            return;
        }
        // The only clear the texture ever gets: uncovered regions read
        // transparent until a capture lands there, and are never sampled
        // (backdropTexel clamps into the valid rect).
        KWin::GLFramebuffer::pushFramebuffer(state.backdropFbo.get());
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        KWin::GLFramebuffer::popFramebuffer();
    }
    // Where to READ the scene from. At rest that is the canvas rect itself;
    // while an animation draws the window elsewhere (animatedFrame valid),
    // map the canvas rect through the frame -> animatedFrame transform so
    // the pane shows the scene behind the MOVING quad. The canvas/texture
    // stays rest-rect sized either way (it must match the composite); a
    // moving source of a different size is scaled by the blit.
    QRectF sourceRect = logicalGeometry;
    if (animatedFrame.isValid()) {
        QRectF frame = w->frameGeometry();
        if (frame.isEmpty()) {
            frame = animatedFrame;
        }
        const qreal sx = animatedFrame.width() / qMax(frame.width(), 1.0);
        const qreal sy = animatedFrame.height() / qMax(frame.height(), 1.0);
        sourceRect = QRectF(animatedFrame.x() + (logicalGeometry.x() - frame.x()) * sx,
                            animatedFrame.y() + (logicalGeometry.y() - frame.y()) * sy, logicalGeometry.width() * sx,
                            logicalGeometry.height() * sy);
        if (sourceRect.isEmpty()) {
            sourceRect = logicalGeometry;
        }
    }
    // The source can hang off the output (a padded canvas at a screen edge,
    // a window half-dragged off, an animation passing the edge): blit only
    // the part the render target covers and record the valid sub-rect so
    // packs clamp samples into it rather than reading the cleared margin.
    const QRectF visibleCanvasF = sourceRect & viewport.renderRect();
    if (visibleCanvasF.isEmpty()) {
        return; // keep whatever another output captured
    }
    // ── Damage clip ─────────────────────────────────────────────────────
    // "The target holds exactly the content painted below this window" (the
    // header's contract) is only true INSIDE this frame's damage region.
    // KWin repaints partially (buffer age): outside the damage, the target
    // still holds the PREVIOUS presented frame — the finished composite,
    // including windows painted ABOVE this one. Blitting the whole canvas
    // fed that back into the backdrop, and the seam between fresh scene and
    // stale full frame landed exactly at the damage edge — user-visible as
    // a focused neighbour's padded-canvas boundary re-blurred inside this
    // window's frost. So clip the capture to what this pass actually
    // painted, and let the slices accumulate across frames exactly like the
    // multi-output slices always have: a region only enters the texture on
    // a frame it was genuinely repainted below this window, and anything
    // that changes behind the window necessarily produces damage there.
    const KWin::Rect deviceVisible = viewport.mapToDeviceCoordinatesAligned(KWin::RectF(visibleCanvasF));
    const KWin::Region paintedDevice = paintedDeviceRegion.intersected(deviceVisible);
    if (paintedDevice.isEmpty()) {
        return; // nothing repainted below us this pass — keep the prior capture
    }
    // Did the damage cover the WHOLE visible canvas? Shrink by one device px
    // before asking: the aligned (rounded-out) canvas edge can poke a
    // fraction of a pixel past a device-aligned damage rect that covers it
    // for every purpose that matters, and a false negative here starves the
    // generation restart below. (A canvas ≤2 device px across degenerates to
    // an empty probe and answers false — it also has nothing to restart.)
    const bool fullSlice = paintedDevice.contains(deviceVisible.adjusted(1, 1, -1, -1));
    // ── Generation restart ──────────────────────────────────────────────
    // The valid rect must eventually CONTRACT (a window dragged half off its
    // output must not keep claiming canvas it no longer captures — the
    // overhang would sample a frozen reflection), and no clock can decide
    // when: outputs have independent frame clocks, which is why the
    // wall-clock window this used to use never expired at all. So the rect
    // restarts when an output that already contributed a FULL slice blits a
    // full slice again — its next frame. Partial (damage-clipped) slices
    // always union and never touch the generation set: a sliver of damage is
    // neither a new frame's worth of canvas nor a full contribution, and
    // restarting on one would collapse the valid rect to the sliver. Full
    // slices are not rare — the ~30fps backdrop driver damages the whole
    // canvas (addRepaintFull + damagePaddedBand), and animations force full
    // repaints — so contraction still lands within a frame or two of the
    // window moving.
    const QRectF outputRect = viewport.renderRect();
    const bool restartGeneration = fullSlice && state.backdropGenerationOutputs.contains(outputRect);
    // The generation-set mutation is deferred to AFTER a successful blit,
    // below: clearing it here would leave the set emptied on the no-blit
    // early return, and the next frame would union with a stale rect it
    // should have restarted from.
    if (!state.backdropFbo) {
        state.backdropTex.reset();
        state.backdropSize = QSize();
        return;
    }
    KWin::GLFramebuffer& fbo = *state.backdropFbo;
    // No clear here — see the accumulation note above.
    const qreal texW = textureSize.width();
    const qreal texH = textureSize.height();
    // One blit per damage rect. The region's rects are what was actually
    // repainted; a single bounding-box blit would re-import the stale frame
    // through the gaps between them. Contained (inward-rounding) mapping so
    // a slice never claims a device pixel this pass did not repaint.
    QRectF destUnion;
    for (const KWin::Rect& deviceSlice : paintedDevice.rects()) {
        const KWin::Rect logicalSlice = viewport.mapFromDeviceCoordinatesContained(deviceSlice);
        const QRectF sliceF =
            QRectF(logicalSlice.x(), logicalSlice.y(), logicalSlice.width(), logicalSlice.height()) & visibleCanvasF;
        if (sliceF.isEmpty()) {
            continue;
        }
        const KWin::Rect source(qRound(sliceF.x()), qRound(sliceF.y()), qRound(sliceF.width()),
                                qRound(sliceF.height()));
        // Destination: the source's PROPORTIONAL position within sourceRect,
        // mapped onto the texture — reduces to the plain scaled copy at rest
        // (sourceRect == logicalGeometry) and scales a moving/resizing source
        // into the rest-sized canvas during animations.
        const QRectF destF((source.x() - sourceRect.x()) / sourceRect.width() * texW,
                           (source.y() - sourceRect.y()) / sourceRect.height() * texH,
                           source.width() / sourceRect.width() * texW, source.height() / sourceRect.height() * texH);
        const KWin::Rect destination(qRound(destF.x()), qRound(destF.y()), qRound(destF.width()),
                                     qRound(destF.height()));
        if (!fbo.blitFromRenderTarget(renderTarget, viewport, source, destination)) {
            continue;
        }
        destUnion = destUnion.isEmpty() ? destF : destUnion.united(destF);
    }
    if (destUnion.isEmpty()) {
        // No slice landed. Leave backdropRect alone, exactly as the empty-source path
        // above does. This runs once per OUTPUT, and a canvas straddling two of them
        // accumulates their slices into one texture — so zeroing the rect here would
        // throw away a sibling output's perfectly good capture from the same frame,
        // drop uHasBackdrop to 0, and collapse the frost to nothing for that frame. A
        // failed blit means THIS slice is missing, not that the others are invalid.
        return;
    }
    // Valid sub-rect in TOP-DOWN normalized coords — matches backdropTexel's
    // clamp space. Non-restart captures UNION with the slices already
    // accumulated (sibling outputs tiling the canvas, earlier damage slices
    // from this one). The bounding box can span texels no slice covered, but
    // those hold the previous capture of the same scene spot, not garbage —
    // the texture is only ever cleared at allocation.
    QVector4D destNorm(static_cast<float>(destUnion.x() / texW), static_cast<float>(destUnion.y() / texH),
                       static_cast<float>(destUnion.width() / texW), static_cast<float>(destUnion.height() / texH));
    if (!restartGeneration && backdropUsable(state)) {
        const float x0 = qMin(state.backdropRect.x(), destNorm.x());
        const float y0 = qMin(state.backdropRect.y(), destNorm.y());
        const float x1 = qMax(state.backdropRect.x() + state.backdropRect.z(), destNorm.x() + destNorm.z());
        const float y1 = qMax(state.backdropRect.y() + state.backdropRect.w(), destNorm.y() + destNorm.w());
        destNorm = QVector4D(x0, y0, x1 - x0, y1 - y0);
    }
    state.backdropRect = destNorm;
    // Commit the generation-set write now that a blit succeeded: a restart
    // clears first and re-adds this output; a first full slice from a new
    // output adds itself; a partial slice leaves the set untouched (see the
    // generation note above). The no-blit path skipped all of this.
    if (restartGeneration) {
        state.backdropGenerationOutputs.clear();
    }
    if (fullSlice && !state.backdropGenerationOutputs.contains(outputRect)) {
        state.backdropGenerationOutputs.append(outputRect);
    }
}

} // namespace PlasmaZones
