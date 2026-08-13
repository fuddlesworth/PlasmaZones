// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"
#include "compositor/compositorclock.h"
#include "shader_internal.h"
#include "surface_fold.h"
#include "shader_resolve.h"
#include "window_query.h"

#include <PhosphorAnimation/AnimationLimits.h>

#include <core/output.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>
#include <scene/item.h>
#include <scene/windowitem.h>

#include <QDate>
#include <QDateTime>
#include <QPointer>
#include <QScopeGuard>
#include <QTime>
#include <QVector2D>
#include <QVector4D>

#include <chrono>
#include <type_traits>

#include "compositor/windowanimator.h"

namespace PlasmaZones {

namespace {
// Snapshot texture sizing shared by the raw capture and the tab-swap seed.
// The snapshot is sampled by normalised uv, so downscaling via a reduced
// capture scale costs only resolution (no distortion) — the cap keeps the
// texture within GL limits and bounds the transient memory of an oversized
// window. Normal windows (≤ output size) never hit it; it's a guard against
// pathological geometry. An empty size means "give up on the cross-fade".
struct SnapshotExtent
{
    qreal scale = 1.0;
    QSize textureSize;
};

SnapshotExtent snapshotExtentFor(const QRectF& logicalGeometry, const KWin::LogicalOutput* screen)
{
    SnapshotExtent e;
    e.scale = screen ? screen->scale() : 1.0;
    constexpr qreal kMaxSnapshotDim = 8192.0;
    const qreal longestPx = qMax(logicalGeometry.width(), logicalGeometry.height()) * e.scale;
    if (longestPx > kMaxSnapshotDim) {
        e.scale *= kMaxSnapshotDim / longestPx;
    }
    e.textureSize = (logicalGeometry.size() * e.scale).toSize();
    return e;
}
} // namespace

void PlasmaZonesEffect::captureOldWindowSnapshot(ShaderTransition& transition, KWin::EffectWindow* window)
{
    // Mirrors KWin's OffscreenData::maybeRender (offscreeneffect.cpp): render the
    // window into an offscreen FBO sized to its expanded geometry × screen scale,
    // via effects->drawWindow. We temporarily bypass our morph shader so the
    // captured texture is the RAW old window content (the cross-fade source) —
    // drawing with the morph shader here would sample an unbound uOldWindow.
    //
    // FOREIGN SOURCE (the scrolling tab swap): the pixels come from the
    // outgoing tab rather than from @p window's own past. Everything below
    // reads through `src`, and the two cases differ only in the rect the
    // capture spans — see the logicalGeometry derivation.
    // A tab-swap leg whose source QPointer has gone NULL (QObject destruction,
    // stronger than isDeleted below) must abort like the deleted-source arm,
    // not fall through: the ternary below would silently rewrite `src` to the
    // transition's own window and the leg would capture an undecorated copy
    // of the ARRIVING tab as its "old" side — a decoration fade-in artifact
    // where the intended degradation is "no cross-fade at all".
    if (transition.tabSwap && !transition.snapshotSource) {
        qCWarning(lcEffect) << "tabSwap capture ABORTED: source window destroyed before the capture ran";
        transition.needsSnapshot = false;
        return;
    }
    KWin::EffectWindow* const src = transition.snapshotSource ? transition.snapshotSource.data() : window;
    // Foreign-source diagnostics, DEBUG level on the success path (the
    // category is opt-in; enable plasmazones.effect for a trace) with the
    // abort arms at warning where they belong — a failed capture is otherwise
    // silent by design: the shader falls back to iHasOldWindow == 0, which
    // collapses the old side to the live content and reads on screen as the
    // outgoing tab never having been there.
    const bool foreignSrc = transition.snapshotSource && src != window;
    if (foreignSrc) {
        qCDebug(lcEffect) << "tabSwap capture: src" << getWindowId(src) << "frame" << src->frameGeometry() << "expanded"
                          << src->expandedGeometry() << "-> into" << getWindowId(window) << "frame"
                          << window->frameGeometry();
    }
    if (src->isDeleted()) {
        if (foreignSrc) {
            qCWarning(lcEffect) << "tabSwap capture ABORTED: source window is deleted";
        }
        // The source closed between the install and this first paint frame.
        // Only reachable for a foreign source (the transition's own window is
        // alive by construction, it is being painted). The shader falls back
        // to iHasOldWindow == 0, which collapses the old side to the live
        // content — a plain appear-in-place, which is what a tab switch
        // degrades to anyway.
        transition.needsSnapshot = false;
        return;
    }
    const KWin::LogicalOutput* const screen = src->screen();
    // The rect the capture spans. For the ordinary case it is the window's own
    // expanded geometry.
    //
    // For a foreign source it is the SOURCE's own expanded rect, wherever the
    // source currently sits. Deriving it from the arriving window instead (its
    // expanded rect translated onto the source) was the first version, and it
    // only holds while the two agree about where the source is — which the
    // logs disproved: the arriving tab can still be at ITS park when this
    // runs, so the translation was computed between two park positions and
    // landed the capture rect nowhere near the source's composite.
    //
    // The residual cost of using the source's own rect is that the two clients
    // can pad their shadows differently (2020 vs 2038 logical px wide on the
    // pair this was debugged against), so the old side is sampled across the
    // arriving card with under a percent of horizontal stretch. That is
    // invisible, and it is the honest trade for a capture that always lands.
    // (For the ordinary case src IS window, so this expression covers both —
    // an earlier ternary here had identical arms and only implied a
    // distinction that does not exist.)
    const QRectF logicalGeometry = src->expandedGeometry();
    const auto [scale, textureSize] = snapshotExtentFor(logicalGeometry, screen);
    if (textureSize.isEmpty()) {
        if (foreignSrc) {
            qCWarning(lcEffect) << "tabSwap capture ABORTED: empty texture size from" << logicalGeometry << "scale"
                                << scale;
        }
        transition.needsSnapshot = false;
        return;
    }

    // Never leak the capture's GL state (blend/viewport/clear/active texture)
    // into the on-screen draw that follows in this same frame — see
    // ScopedGlState.
    const ShaderInternal::ScopedGlState glStateGuard;

    std::unique_ptr<KWin::GLTexture> tex = KWin::GLTexture::allocate(GL_RGBA8, textureSize);
    if (!tex) {
        // Allocation failed — give up on the cross-fade; the morph shader reads
        // a transparent uOldWindow and falls back to no cross-fade.
        transition.needsSnapshot = false;
        return;
    }
    tex->setFilter(GL_LINEAR);
    tex->setWrapMode(GL_CLAMP_TO_EDGE);

    KWin::GLFramebuffer fbo(tex.get());
    if (!fbo.valid()) {
        transition.needsSnapshot = false;
        return;
    }

    // Seed the snapshot from the decorated REST composite when one exists.
    // The raw capture below draws the window WITHOUT its decoration chain:
    // the fold never runs during a capture (m_capturingSnapshot short-
    // circuits paintWindow) and the decorated appearance lives only in the
    // multipass composite, never in the window item itself. A morph shader
    // cross-fades uOldWindow against the decorated live side, so a raw old
    // side visibly strips the decoration — most glaringly the frost/blur
    // pane — for the early part of every geometry morph. The multipass entry
    // still holds the last rest-path fold of the OLD appearance (the
    // deferred erase keeps it alive through a live transition); that frozen
    // frame is exactly what the morph should carry — the same reuse the
    // close path applies to deleted windows.
    //
    // Alignment: the shader samples uOldWindow through iAnchorRectInTexture,
    // the NEW frame's sub-rect of the NEW expanded rect, while the composite
    // canvas covers the OLD padded rect. Map frame→frame affinely (the old
    // frame's pixels land at the new frame's sub-rect; the surrounding
    // canvas scales along, so border strokes and glow bands just outside the
    // frame carry over), clip the source to the canvas, and shrink the dest
    // through the same map so the copied band lands where it belongs — the
    // rest of the snapshot stays cleared. Both textures come out of the same
    // RenderViewport draw path, and blitFromFramebuffer speaks top-down
    // rects, flipping both sides against their own heights internally.
    // Falls back to the raw capture when no composite exists (undecorated
    // windows, or the entry was flushed before the transition began).
    //
    // A FOREIGN source (the tab swap) SKIPS the seed entirely and takes the raw
    // capture below. The seed is the wrong source for it on the one axis that
    // matters most, ALPHA: a chain carrying a frost or blur pane samples the
    // backdrop and folds it INTO the window's pixels, so the composite is
    // opaque by construction. That is right for a morph, where the same window
    // is opaque on both sides of the leg and the alternative is watching its
    // pane blink out. It is wrong here — live-confirmed on a translucent
    // Kirigami pair — because the arriving tab is drawn live and translucent
    // while the captured outgoing one is not, so the swap starts opaque and
    // resolves to transparent, and the whole leg announces itself.
    //
    // The raw path draws the window item WITHOUT the chain fold, which
    // preserves the client's own alpha exactly. The cost is that the outgoing
    // side loses PlasmaZones' own decoration for the leg's duration, and that
    // cost is far smaller here than on a morph: both tabs sit in one column
    // rect wearing the same resolved decoration, so the arriving side's live
    // fold already draws the pane the outgoing side is missing, in the same
    // place, at the same size.
    if (src == window) {
        if (const auto mpIt = m_surfaceMultipass.find(getWindowId(src)); mpIt != m_surfaceMultipass.end()) {
            const SurfaceMultipassState& mp = mpIt->second;
            KWin::GLTexture* const comp = mp.compositeTex[mp.finalSlot].get();
            // Unconditionally the transition's own window here: the guard above
            // admits only src == window, so the foreign case never reaches this
            // map and no longer needs a term in it.
            const QRectF newFrame = src->frameGeometry();
            const QRectF oldFrame = transition.fromGeometry;
            if (comp && mp.canvasGeo.isValid() && !mp.canvasGeo.isEmpty() && oldFrame.width() > 0.0
                && oldFrame.height() > 0.0 && newFrame.width() > 0.0 && newFrame.height() > 0.0) {
                // T maps snapshot-space logical points into composite space:
                // T(p) = oldFrame.topLeft + (p - newFrame.topLeft) ⊙ s
                const qreal sx = oldFrame.width() / newFrame.width();
                const qreal sy = oldFrame.height() / newFrame.height();
                const QRectF srcLogical(oldFrame.x() + (logicalGeometry.x() - newFrame.x()) * sx,
                                        oldFrame.y() + (logicalGeometry.y() - newFrame.y()) * sy,
                                        logicalGeometry.width() * sx, logicalGeometry.height() * sy);
                const QRectF srcClipped = srcLogical & mp.canvasGeo;
                if (!srcClipped.isEmpty()) {
                    const QRectF dstLogical(newFrame.x() + (srcClipped.x() - oldFrame.x()) / sx,
                                            newFrame.y() + (srcClipped.y() - oldFrame.y()) / sy,
                                            srcClipped.width() / sx, srcClipped.height() / sy);
                    const qreal srcPxPerX = comp->width() / mp.canvasGeo.width();
                    const qreal srcPxPerY = comp->height() / mp.canvasGeo.height();
                    const qreal dstPxPerX = textureSize.width() / logicalGeometry.width();
                    const qreal dstPxPerY = textureSize.height() / logicalGeometry.height();
                    const QRect srcPx = QRectF((srcClipped.x() - mp.canvasGeo.x()) * srcPxPerX,
                                               (srcClipped.y() - mp.canvasGeo.y()) * srcPxPerY,
                                               srcClipped.width() * srcPxPerX, srcClipped.height() * srcPxPerY)
                                            .toRect();
                    const QRect dstPx = QRectF((dstLogical.x() - logicalGeometry.x()) * dstPxPerX,
                                               (dstLogical.y() - logicalGeometry.y()) * dstPxPerY,
                                               dstLogical.width() * dstPxPerX, dstLogical.height() * dstPxPerY)
                                            .toRect();
                    KWin::GLFramebuffer srcFbo(comp);
                    if (!srcPx.isEmpty() && !dstPx.isEmpty() && srcFbo.valid()) {
                        // The clear and the blit both honour scissor, and this
                        // runs mid scene-walk; the ScopedGlState guard above
                        // restores the enable state.
                        glDisable(GL_SCISSOR_TEST);
                        KWin::GLFramebuffer::pushFramebuffer(&fbo);
                        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                        glClear(GL_COLOR_BUFFER_BIT);
                        KWin::GLFramebuffer::popFramebuffer();
                        KWin::GLFramebuffer::pushFramebuffer(&srcFbo);
                        fbo.blitFromFramebuffer(KWin::Rect(srcPx.x(), srcPx.y(), srcPx.width(), srcPx.height()),
                                                KWin::Rect(dstPx.x(), dstPx.y(), dstPx.width(), dstPx.height()),
                                                GL_LINEAR);
                        KWin::GLFramebuffer::popFramebuffer();
                        transition.oldSnapshot = std::move(tex);
                        transition.needsSnapshot = false;
                        return;
                    }
                }
            }
        }
    }

    // FOREIGN-SOURCE fallback note: reaching the raw capture below for a tab
    // swap means seedTabSwapSnapshot found no composite at install time (an
    // undecorated source, or its entry was reaped at the park). The raw draw
    // is position-independent and preserves the client's own alpha, so it is
    // the right fallback — it merely lacks the decoration chain, which for an
    // undecorated source is nothing to lack.
    //
    // Bypass the source's own shader for the raw capture, restore it
    // afterwards. Resolved from the SOURCE's live transition rather than from
    // `transition.cached`: the two are the same object in the ordinary case,
    // but for a foreign source `transition` belongs to a different window and
    // installing its shader on the source would leave a foreign leg's program
    // attached to a window that is not running it.
    //
    // The RESTORE value cannot be read back from KWin (OffscreenEffect has no
    // shaderFor()), so it is reconstructed. A foreign source usually carries
    // NO transition, but a decorated one carries the decoration PRESENT
    // shader in its slot (reconcileDecorationShader installs it and stamps
    // shaderApplied) — restoring nullptr there left the outgoing tab
    // presenting its raw redirect texture until the next reconcile. Mirror
    // surfacelayers' capture-restore shape: transition shader first, then the
    // present shader for a shaderApplied decorated window, else nullptr (an
    // undecorated, unredirected source must stay unredirected). Deliberately
    // NOT reconcileDecorationShader() here: its live-transition arm cedes the
    // slot (shaderApplied = false, no setShader) for a source that does carry
    // a leg.
    const ShaderTransition* const srcSt = m_shaderManager.findTransition(src);
    KWin::GLShader* srcShader = srcSt && srcSt->cached ? srcSt->cached->shader.get() : nullptr;
    if (!srcShader) {
        const auto decoIt = m_windowDecorations.constFind(getWindowId(src));
        if (decoIt != m_windowDecorations.constEnd() && decoIt->shaderApplied) {
            srcShader = surfacePresentShader();
        }
    }
    setShader(src, nullptr);

    m_capturingSnapshot = true;
    // Guard the re-entrancy flag against a throw from the draw chain — a leaked
    // m_capturingSnapshot would corrupt every subsequent paint. Same pattern as
    // the surface-layer capture sites in surfacelayers.cpp.
    auto resetCapture = qScopeGuard([this] {
        m_capturingSnapshot = false;
    });
    {
        KWin::RenderTarget renderTarget(&fbo);
        KWin::RenderViewport viewport(logicalGeometry, scale, renderTarget, QPoint());
        KWin::GLFramebuffer::pushFramebuffer(&fbo);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        // Keep the window item renderable for the duration of the capture draw.
        // Load-bearing for a foreign source beyond the ordinary case: the
        // outgoing tab is parked off every output by now, so nothing else in
        // the frame is asking for it to be renderable.
        KWin::ItemEffect keepRenderable(src->windowItem());
        KWin::WindowPaintData captureData;
        captureData.setOpacity(1.0);
        const int captureMask = PAINT_WINDOW_TRANSFORMED | PAINT_WINDOW_TRANSLUCENT;
        // Route through effects->drawWindow (not OffscreenEffect::drawWindow) so
        // KWin's draw-chain iterator is advanced correctly — same rationale as
        // the on-screen draw paths. The re-entrant paintWindow short-circuits on
        // m_capturingSnapshot and draws the window plainly into this FBO.
        KWin::effects->drawWindow(renderTarget, viewport, src, captureMask, KWin::Region::infinite(), captureData);
        KWin::GLFramebuffer::popFramebuffer();
    }
    resetCapture.dismiss();
    m_capturingSnapshot = false;

    setShader(src, srcShader);

    if (foreignSrc) {
        // The raw draw ran at setOpacity(1.0), so nothing is baked in: record
        // the SOURCE's own resolved opacity for the iOldWindowOpacity push.
        // The decoration foldedOpacity is the same value the present path
        // dims by; an undecorated source has no entry and stays at 1.0.
        const auto decoIt = m_windowDecorations.constFind(getWindowId(src));
        transition.oldSnapshotOpacity = decoIt != m_windowDecorations.constEnd()
            ? static_cast<float>(qBound(0.0, decoIt->foldedOpacity, 1.0))
            : 1.0f;
        qCDebug(lcEffect) << "tabSwap capture OK, rect" << logicalGeometry << "tex" << textureSize << "scale" << scale
                          << "oldOpacity" << transition.oldSnapshotOpacity;
    }
    transition.oldSnapshot = std::move(tex);
    transition.needsSnapshot = false;
}

void PlasmaZonesEffect::seedTabSwapSnapshot(ShaderTransition& transition, KWin::EffectWindow* src,
                                            KWin::EffectWindow* window)
{
    // Capture the outgoing tab AT INSTALL TIME, from the decorated composite,
    // while that composite is still the PRE-SWITCH fold.
    //
    // Timing is the entire point of this function existing separately from
    // captureOldWindowSnapshot. The ordinary snapshot is taken lazily on the
    // leg's first paint frame, and by then the outgoing tab has been committed
    // to its park and re-folded there — so the composite the paint-time
    // capture reads carries the PARK's backdrop baked into any frost or blur
    // pane. Below the union of all outputs that backdrop is unwritten
    // garbage, and an old side wearing it flashes visibly before the
    // cross-fade resolves to the arriving tab's live, correctly-lit pane
    // (live-diagnosed on a translucent Kirigami pair). Here, inside the batch
    // slot, no paint has run since the switch: the entry still holds the fold
    // taken at the COLUMN, whose baked backdrop is the very backdrop the
    // arriving tab is about to be composited over. An opaque pane carrying
    // the right backdrop is visually identical to the live translucent one —
    // which is the same argument the morph seed rests on — so keeping the
    // decoration costs nothing.
    //
    // The map is the identity, and it is exact rather than convenient: the two
    // tabs of a tabbed column share ONE rect, the apply that just ran
    // committed the arriving window to it (moveResize updates frameGeometry
    // synchronously; the no-op-skip bail in applyWindowGeometry relies on the
    // same fact), and the composite was folded at that same rect. The capture
    // spans the ARRIVING window's expanded rect so the snapshot lives in the
    // coordinate system iAnchorRectInTexture describes; the clip against the
    // source's canvas trims the sliver where the two clients' shadow padding
    // disagrees, which stays cleared like every out-of-canvas band.
    //
    // Falls back to the lazy path (needsSnapshot, raw drawWindow at first
    // paint) when there is no usable composite: an undecorated source has no
    // entry, and a reaped one is already park-poisoned. The raw draw
    // preserves the client's own alpha and merely lacks the decoration chain,
    // which for an undecorated source is nothing to lack.
    const auto armFallback = [&] {
        transition.snapshotSource = src;
        transition.needsSnapshot = true;
    };
    if (!src || !window || src->isDeleted() || window->isDeleted()) {
        // Unreachable from the one caller (tiling.cpp guards all four terms),
        // kept as a fail-closed entry contract for any future caller — and
        // WARNED, unlike the arming arms below, precisely because a caller
        // that trips it has broken that contract and would otherwise learn
        // nothing (the shader's iHasOldWindow == 0 fallback hides it).
        // src != window is ALSO a caller-side precondition (tiling.cpp checks
        // it): a same-window call would blit the arriving window's own
        // composite as the "old" side.
        qCWarning(lcEffect) << "seedTabSwapSnapshot: refused a null or deleted window pair";
        return;
    }
    const auto mpIt = m_surfaceMultipass.find(getWindowId(src));
    if (mpIt == m_surfaceMultipass.end()) {
        armFallback();
        return;
    }
    const SurfaceMultipassState& mp = mpIt->second;
    KWin::GLTexture* const comp = mp.compositeTex[mp.finalSlot].get();
    if (!comp || !mp.canvasGeo.isValid() || mp.canvasGeo.isEmpty()) {
        armFallback();
        return;
    }
    // Outside any paint pass, so the context must be made current explicitly —
    // the decoration prewarm and the thumbnail capture set the precedent. A
    // blit is the only GL this path issues; the scene-walk hazards the lazy
    // capture's drawWindow carries do not arise.
    if (!ensureGlContextCurrent()) {
        armFallback();
        return;
    }
    const QRectF logicalGeometry = window->expandedGeometry();
    const auto [scale, textureSize] = snapshotExtentFor(logicalGeometry, window->screen());
    if (textureSize.isEmpty()) {
        armFallback();
        return;
    }
    // Constructed BEFORE the first framebuffer push, and load-bearing beyond
    // hygiene: popFramebuffer to an EMPTY stack binds FBO 0 without resetting
    // glViewport, so this guard's viewport restore is the only thing putting
    // it back for whatever paints next.
    const ShaderInternal::ScopedGlState glStateGuard;
    std::unique_ptr<KWin::GLTexture> tex = KWin::GLTexture::allocate(GL_RGBA8, textureSize);
    if (!tex) {
        armFallback();
        return;
    }
    tex->setFilter(GL_LINEAR);
    tex->setWrapMode(GL_CLAMP_TO_EDGE);
    KWin::GLFramebuffer fbo(tex.get());
    KWin::GLFramebuffer srcFbo(comp);
    if (!fbo.valid() || !srcFbo.valid()) {
        armFallback();
        return;
    }
    // Identity map: snapshot space IS canvas space up to the two canvases'
    // origins, so source and dest are the same logical rect expressed in each
    // texture's own pixels.
    const QRectF srcClipped = logicalGeometry & mp.canvasGeo;
    if (srcClipped.isEmpty()) {
        // The composite is NOT at the column after all (a stale entry from a
        // position this code did not predict). Trust the disagreement over
        // the theory and let the raw capture handle it.
        qCWarning(lcEffect) << "tabSwap seed: composite canvas" << mp.canvasGeo << "does not cover the column rect"
                            << logicalGeometry << "- falling back to the raw capture";
        armFallback();
        return;
    }
    const qreal srcPxPerX = comp->width() / mp.canvasGeo.width();
    const qreal srcPxPerY = comp->height() / mp.canvasGeo.height();
    const qreal dstPxPerX = textureSize.width() / logicalGeometry.width();
    const qreal dstPxPerY = textureSize.height() / logicalGeometry.height();
    const QRect srcPx =
        QRectF((srcClipped.x() - mp.canvasGeo.x()) * srcPxPerX, (srcClipped.y() - mp.canvasGeo.y()) * srcPxPerY,
               srcClipped.width() * srcPxPerX, srcClipped.height() * srcPxPerY)
            .toRect();
    const QRect dstPx =
        QRectF((srcClipped.x() - logicalGeometry.x()) * dstPxPerX, (srcClipped.y() - logicalGeometry.y()) * dstPxPerY,
               srcClipped.width() * dstPxPerX, srcClipped.height() * dstPxPerY)
            .toRect();
    if (srcPx.isEmpty() || dstPx.isEmpty()) {
        armFallback();
        return;
    }
    glDisable(GL_SCISSOR_TEST);
    KWin::GLFramebuffer::pushFramebuffer(&fbo);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    KWin::GLFramebuffer::popFramebuffer();
    KWin::GLFramebuffer::pushFramebuffer(&srcFbo);
    fbo.blitFromFramebuffer(KWin::Rect(srcPx.x(), srcPx.y(), srcPx.width(), srcPx.height()),
                            KWin::Rect(dstPx.x(), dstPx.y(), dstPx.width(), dstPx.height()), GL_LINEAR);
    KWin::GLFramebuffer::popFramebuffer();
    // expandedGeometry in the trace: the map above assumes it still holds the
    // pre-park column rect when the seed runs. If a Wayland commit ever lands
    // the park BEFORE this install, the mismatch is visible only here — the
    // blit itself would quietly seed a sliver (srcClipped stays non-empty for
    // most park positions).
    qCDebug(lcEffect) << "tabSwap seed OK from pre-park composite, canvas" << mp.canvasGeo << "src expanded"
                      << logicalGeometry << "src px" << srcPx << "dst px" << dstPx;
    transition.oldSnapshot = std::move(tex);
    transition.needsSnapshot = false;
    // The source is recorded even on the seeded path: teardown and diagnostics
    // key off it, and a later re-capture (never expected, but the field is a
    // contract) must not silently flip back to the transition's own window.
    transition.snapshotSource = src;
}

void PlasmaZonesEffect::apply(KWin::EffectWindow* window, int mask, KWin::WindowPaintData& data,
                              KWin::WindowQuadList& quads)
{
    Q_UNUSED(mask)
    Q_UNUSED(data)

    // During an old-content snapshot capture the window must be drawn with its
    // natural, undeformed quad (a raw copy). Leave `quads` untouched.
    if (m_capturingSnapshot) {
        return;
    }

    // Defensive: KWin may dispatch a paint to us for a window already
    // marked deleted (slot ordering vs. our windowDeleted handler). The
    // expandedGeometry / frameGeometry / screen accessors below would
    // deref freed Item state for a deleted window. Mirrors the same
    // guard endShaderTransition applies for the same race.
    // A CLOSING window is isDeleted() for its entire close animation (held
    // alive only by our close grab) — but its surface-extent quad rewrite MUST
    // still run: paintWindow feeds output-sized anchor uniforms and paints
    // with an infinite region for surface-extent transitions, so leaving the
    // natural window-sized quad in place mis-maps the sampled surface far past
    // the window (the close-animation overshoot). Live geometry accessors on a
    // ref-held Deleted window are already exercised every close frame by
    // paintWindow's anchor-uniform block, so reading them here is the same
    // safety class. Bail only for a deleted window with NO live transition
    // (nothing of ours paints it).
    if (!window || (window->isDeleted() && !m_shaderManager.findTransition(window))) {
        return;
    }
    // Only surface-extent transitions deform the quad list. Anchor-extent
    // transitions and plain redirected windows are drawn 1:1 over their
    // own geometry — leave their quads untouched. Non-const handle: the
    // handedness-cache block below mutates it on the first call, saving a
    // redundant `findTransition` lookup.
    auto* st = m_shaderManager.findTransition(window);

    // ── Padded decoration present ────────────────────────────────────────
    // A chain with an outer margin (WindowDecoration::outerPadding) composited
    // into a canvas LARGER than the redirect texture
    // (renderSurfaceChainComposite inflated the capture rect). Present it on
    // a matching padded quad so the margin band reaches the screen — the
    // same quad-rewrite mechanism the surface-extent transitions use (they
    // stretch to the whole output; this stretches by the padding). Gated on
    // no live transition: a transition owns the shader slot (shaderApplied
    // false) and its own quad handling wins.
    if (!st && !quads.isEmpty() && !m_windowDecorations.isEmpty()) {
        const auto bit = m_windowDecorations.find(getWindowId(window));
        if (bit != m_windowDecorations.end() && bit->shaderApplied) {
            QRectF textureGeo = window->expandedGeometry();
            if (textureGeo.isEmpty()) {
                textureGeo = window->frameGeometry();
            }
            if (textureGeo.isEmpty()) {
                return;
            }
            const qreal pad = bit->outerPadding;
            // The canvas the fold ACTUALLY produced, not a second derivation of it.
            // SurfaceMultipassState::canvasGeo is the single source for this quantity
            // (see its doc), and the layer-rect remap above already honours it: present
            // the composite on the rect it was composited into, or a window whose
            // geometry moved after the fold gets its decoration drawn against a canvas
            // the texture does not match. The live derivation stays as the fallback for
            // the fold's failure paths, which can erase the multipass entry.
            QRectF padded = textureGeo.adjusted(-pad, -pad, pad, pad);
            bool haveCanvas = false;
            // The scale the canvas was BUILT at, carried beside it. Re-deriving it here
            // with windowSurfaceScale would answer for the window's CURRENT outputs,
            // which is a different question than "what scale is this texture's grid" the
            // moment a window crosses outputs between the fold and the present.
            qreal presentScale = 0.0;
            if (const auto psIt = m_surfaceMultipass.find(getWindowId(window)); psIt != m_surfaceMultipass.end()) {
                if (psIt->second.canvasGeo.isValid()) {
                    padded = psIt->second.canvasGeo;
                    haveCanvas = true;
                }
                presentScale = psIt->second.captureScaleKey;
            }
            if (presentScale <= 0.0) {
                presentScale = windowSurfaceScale(window);
            }
            // UNPADDED windows rewrite too, not just padded ones: surfaceCanvasFor
            // device-aligns the canvas, so even at pad == 0 the composite covers the
            // ALIGNED rect, which can exceed the expanded rect by a sub-pixel band on
            // each side at fractional scale. Presenting that texture on KWin's natural
            // expanded-rect quad (uv 0..1) would squeeze the canvas into the smaller
            // rect — a whole-window sub-pixel resample, the exact blur the alignment
            // exists to remove (discussion #868). Only the no-canvas pad == 0 fallback
            // keeps the natural quad: there the composite was never built, so there is
            // nothing of ours to map.
            if (pad <= 0 && !haveCanvas) {
                return;
            }

            // quad-space <-> screen-space is a pure translation at 1:1
            // logical scale (see the surface-extent branch below for the
            // full rationale); derive the offset from the actual quads.
            double qLeft = quads.first().left();
            double qTop = quads.first().top();
            for (qsizetype i = 1; i < quads.size(); ++i) {
                qLeft = qMin(qLeft, quads[i].left());
                qTop = qMin(qTop, quads[i].top());
            }
            // Offset from the anchor the quad ACTUALLY carries, which is the expanded
            // rect SNAPPED to the device grid — not the raw expanded rect.
            // OffscreenEffect builds this quad from the redirect's visible rect, and the
            // scene rounds every vertex to a whole device pixel (RenderGeometry's default
            // VertexSnappingMode::Round), so qLeft names round(textureGeo.x * scale)
            // device pixels, not textureGeo.x * scale. Measuring the canvas delta against
            // the unsnapped origin therefore carried KWin's own rounding into the quad as
            // a sub-pixel error.
            //
            // At scale 1.5 that error is exactly half a device pixel, and a half pixel is
            // the one value the vertex rounding cannot absorb: the left edge sits at a
            // negative window-relative coordinate and the right at a positive one, so
            // round-half-AWAY-FROM-ZERO pushes them APART (-97.5 → -98, +1058.5 → +1059).
            // The quad came out one device pixel wider than its own texture and stretched
            // the whole composite through GL_LINEAR — every decorated window permanently
            // soft at 1.5x, while 1.25x and 1.48333x (whose fractions are never exactly
            // .5) stayed pixel-exact. That asymmetry is the fingerprint (discussion #868).
            //
            // Anchoring on the snapped origin puts both edges on whole device pixels — the
            // canvas is device-aligned by construction (surfaceCanvasFor) and the anchor
            // is now an integer device coordinate too — so the vertex rounding is a no-op
            // and the texture presents 1:1.
            //
            // Rounding the ABSOLUTE screen coordinate models what the scene does to the
            // window-RELATIVE one, and the two agree exactly while the window's own origin
            // lands on a device pixel, which is the case a zone layout produces. When it
            // does not, our edges inherit the window origin's fraction — the same fraction
            // KWin's own quads carry — so we track its rendering rather than adding error
            // of our own. What holds unconditionally is the WIDTH: both edges are an exact
            // integer number of device pixels apart, and rounding is translation-invariant
            // by integers everywhere except the .5 tie this anchor removes.
            const double anchorX = std::round(textureGeo.x() * presentScale) / presentScale;
            const double anchorY = std::round(textureGeo.y() * presentScale) / presentScale;
            const double ox = qLeft + (padded.x() - anchorX);
            const double oy = qTop + (padded.y() - anchorY);
            const double ow = padded.width();
            const double oh = padded.height();

            // Texcoord handedness: replicate from the quad KWin handed us
            // (not hardcoded) and cache per window — identical rationale to
            // the surface-extent transitions' handedness cache below.
            if (!bit->presentHandednessCached) {
                const KWin::WindowQuad& srcQuad = quads.first();
                int topIdx = 0, bottomIdx = 0, leftIdx = 0, rightIdx = 0;
                for (int i = 1; i < 4; ++i) {
                    if (srcQuad[i].y() < srcQuad[topIdx].y())
                        topIdx = i;
                    if (srcQuad[i].y() > srcQuad[bottomIdx].y())
                        bottomIdx = i;
                    if (srcQuad[i].x() < srcQuad[leftIdx].x())
                        leftIdx = i;
                    if (srcQuad[i].x() > srcQuad[rightIdx].x())
                        rightIdx = i;
                }
                bit->uAtLeft = (srcQuad[leftIdx].u() <= srcQuad[rightIdx].u()) ? 0.0 : 1.0;
                bit->uAtRight = 1.0 - bit->uAtLeft;
                bit->vAtTop = (srcQuad[topIdx].v() <= srcQuad[bottomIdx].v()) ? 0.0 : 1.0;
                bit->vAtBottom = 1.0 - bit->vAtTop;
                bit->presentHandednessCached = true;
            }

            KWin::WindowQuad paddedQuad;
            paddedQuad[0] = KWin::WindowVertex(ox, oy, bit->uAtLeft, bit->vAtTop);
            paddedQuad[1] = KWin::WindowVertex(ox + ow, oy, bit->uAtRight, bit->vAtTop);
            paddedQuad[2] = KWin::WindowVertex(ox + ow, oy + oh, bit->uAtRight, bit->vAtBottom);
            paddedQuad[3] = KWin::WindowVertex(ox, oy + oh, bit->uAtLeft, bit->vAtBottom);
            quads.clear();
            quads.append(paddedQuad);
            return;
        }
    }

    // ── Anchor-extent transition on a PADDED window ──────────────────────
    // The animation draws on the window's natural quad, which would clip the
    // decoration's outer margin (glow halo) for the whole animation. Grow the
    // quad by the padding with texcoords EXTENDED past the natural range —
    // NOT remapped to 0..1 — so uTexture0 keeps its 1:1 mapping and
    // surfaceColor's iLayerRectInTexture remap addresses the halo band in the
    // padded uSurfaceLayer. (Out-of-range uTexture0 samples only matter when
    // the layer is absent, where CLAMP smears the feathered-transparent
    // margin edge — invisible.)
    if (st && !st->surfaceExtent && !quads.isEmpty() && !m_windowDecorations.isEmpty()) {
        const auto bit = m_windowDecorations.find(getWindowId(window));
        if (bit != m_windowDecorations.end() && bit->outerPadding > 0) {
            QRectF textureGeo = window->expandedGeometry();
            if (textureGeo.isEmpty()) {
                textureGeo = window->frameGeometry();
            }
            if (textureGeo.isEmpty() || textureGeo.width() <= 0 || textureGeo.height() <= 0) {
                return;
            }
            const qreal pad = bit->outerPadding;

            double qLeft = quads.first().left();
            double qTop = quads.first().top();
            for (qsizetype i = 1; i < quads.size(); ++i) {
                qLeft = qMin(qLeft, quads[i].left());
                qTop = qMin(qTop, quads[i].top());
            }
            const double ox = qLeft - pad;
            const double oy = qTop - pad;
            const double ow = textureGeo.width() + 2.0 * pad;
            const double oh = textureGeo.height() + 2.0 * pad;

            // Same replicated-handedness cache the padded present uses.
            if (!bit->presentHandednessCached) {
                const KWin::WindowQuad& srcQuad = quads.first();
                int topIdx = 0, bottomIdx = 0, leftIdx = 0, rightIdx = 0;
                for (int i = 1; i < 4; ++i) {
                    if (srcQuad[i].y() < srcQuad[topIdx].y())
                        topIdx = i;
                    if (srcQuad[i].y() > srcQuad[bottomIdx].y())
                        bottomIdx = i;
                    if (srcQuad[i].x() < srcQuad[leftIdx].x())
                        leftIdx = i;
                    if (srcQuad[i].x() > srcQuad[rightIdx].x())
                        rightIdx = i;
                }
                bit->uAtLeft = (srcQuad[leftIdx].u() <= srcQuad[rightIdx].u()) ? 0.0 : 1.0;
                bit->uAtRight = 1.0 - bit->uAtLeft;
                bit->vAtTop = (srcQuad[topIdx].v() <= srcQuad[bottomIdx].v()) ? 0.0 : 1.0;
                bit->vAtBottom = 1.0 - bit->vAtTop;
                bit->presentHandednessCached = true;
            }

            // Extend the texcoord range by the padding fraction in each axis,
            // direction-aware so either handedness extends outward.
            const double padU = pad / textureGeo.width();
            const double padV = pad / textureGeo.height();
            const double uDir = bit->uAtRight - bit->uAtLeft; // ±1
            const double vDir = bit->vAtBottom - bit->vAtTop; // ±1
            const double uL = bit->uAtLeft - uDir * padU;
            const double uR = bit->uAtRight + uDir * padU;
            const double vT = bit->vAtTop - vDir * padV;
            const double vB = bit->vAtBottom + vDir * padV;

            KWin::WindowQuad paddedQuad;
            paddedQuad[0] = KWin::WindowVertex(ox, oy, uL, vT);
            paddedQuad[1] = KWin::WindowVertex(ox + ow, oy, uR, vT);
            paddedQuad[2] = KWin::WindowVertex(ox + ow, oy + oh, uR, vB);
            paddedQuad[3] = KWin::WindowVertex(ox, oy + oh, uL, vB);
            quads.clear();
            quads.append(paddedQuad);
            return;
        }
    }

    if (!st || !st->surfaceExtent || quads.isEmpty()) {
        return;
    }
    const auto* output = window->screen();
    if (!output) {
        return;
    }
    // The redirected texture KWin hands us covers the window's EXPANDED
    // geometry (frame + decoration + shadow), so the incoming quad — and
    // the quad-space <-> screen-space offset derived from it below — is
    // anchored to the expanded rect, not the frame. Pair it with the
    // expanded rect (the surface-extent anchor uniforms use the same
    // rect) so the output quad lands where KWin placed the texture.
    // expandedGeometry can be empty for a window with no decoration or
    // shadow extents; fall back to the frame there.
    QRectF textureGeo = window->expandedGeometry();
    if (textureGeo.isEmpty()) {
        textureGeo = window->frameGeometry();
    }
    const QRect outputGeo = output->geometry();
    if (textureGeo.isEmpty() || outputGeo.isEmpty()) {
        return;
    }

    // The incoming quads span the captured window in KWin's quad-list
    // coordinate space. Deriving the window's top-left from the actual
    // quads (rather than assuming an origin) keeps the mapping correct
    // whether KWin hands us window-local or screen-absolute quad
    // coordinates: quad-list space ↔ screen space is a pure translation
    // at 1:1 logical scale, so the same offset that maps the window also
    // maps the output.
    double qLeft = quads.first().left();
    double qTop = quads.first().top();
    for (qsizetype i = 1; i < quads.size(); ++i) {
        qLeft = qMin(qLeft, quads[i].left());
        qTop = qMin(qTop, quads[i].top());
    }

    // Window-relative grid deformation (e.g. the `flow` window-move
    // effect). Build an NxN grid over the window's DESTINATION frame rect
    // — the same rect pushed as iToRect — so the vertex shader can pull
    // trailing rows back toward iFromRect while the leading edge settles
    // first. Anchoring the grid to the window (not the output, as the
    // single-quad path below does) keeps the deformation resolution
    // constant regardless of how small a zone the window snaps into: every
    // cell lands on the window. Texcoords are emitted as plain card uv
    // (0..1, row 0 at the window's top); KWin Y-flips window-quad
    // texcoords on upload, so the flow vertex stage re-applies the
    // canonical `1.0 - texCoord.y` flip (same as the shared kwin vertex
    // stage) to recover card uv with y = 0 at the top. Displaced trailing
    // vertices reach past the destination rect toward iFromRect;
    // surface-extent draws with Region::infinite (see paintWindow), so
    // they are not clipped.
    if (st->gridSubdivisions > 0) {
        // Destination frame rect == iToRect for a geometry morph (the
        // window already jumped there via moveResize). A minimize-to-icon
        // leg (genie, phosphor-siphon) records NO morph destination by
        // design — its target is iIconRect, consumed in the pack's vertex
        // stage — so it takes the live-frameGeometry branch and the grid
        // sits on the window's resting rect.
        QRectF frameRect = st->toGeometry;
        if (!frameRect.isValid() || frameRect.isEmpty()) {
            frameRect = window->frameGeometry();
        }
        if (frameRect.isEmpty()) {
            return;
        }
        // COVER THE PADDED DECORATION CANVAS, not just the frame. An outer
        // ambience pack (glow / drop shadow / fireflies) paints its halo in
        // the margin OUTSIDE the frame, folded into the multipass composite
        // over a padded canvas. A grid confined to the frame samples only
        // texcoords [0,1] = the frame region of that canvas, so the halo was
        // clipped to the frame edge during the deform. Build the grid over
        // the recorded composite canvas instead, with texcoords kept
        // FRAME-relative — so cuv runs past [0,1] into the halo band, which
        // surfaceColor()'s iLayerRectInTexture remap (also frame-anchored)
        // resolves into the composite's margin. With no decoration the
        // canvas equals the frame and this reduces to the old behaviour.
        QRectF gridRect = frameRect;
        if (const auto lsIt = m_surfaceMultipass.find(getWindowId(window));
            lsIt != m_surfaceMultipass.end() && lsIt->second.canvasGeo.isValid() && !lsIt->second.canvasGeo.isEmpty()) {
            gridRect = lsIt->second.canvasGeo;
        }
        // quad-space <-> screen-space is a pure translation at 1:1 logical
        // scale; qLeft/qTop is the captured texture's top-left in quad
        // space and textureGeo its top-left in screen space.
        const double qOffX = qLeft - textureGeo.x();
        const double qOffY = qTop - textureGeo.y();
        const double fx = frameRect.x();
        const double fy = frameRect.y();
        const double fw = frameRect.width();
        const double fh = frameRect.height();
        const int n = st->gridSubdivisions;
        quads.clear();
        quads.reserve(n * n);
        for (int gy = 0; gy < n; ++gy) {
            const double sy0 = gridRect.y() + (static_cast<double>(gy) / n) * gridRect.height();
            const double sy1 = gridRect.y() + (static_cast<double>(gy + 1) / n) * gridRect.height();
            const double v0 = (sy0 - fy) / fh; // frame-relative: past [0,1] in the halo band
            const double v1 = (sy1 - fy) / fh;
            const double y0 = sy0 + qOffY;
            const double y1 = sy1 + qOffY;
            for (int gx = 0; gx < n; ++gx) {
                const double sx0 = gridRect.x() + (static_cast<double>(gx) / n) * gridRect.width();
                const double sx1 = gridRect.x() + (static_cast<double>(gx + 1) / n) * gridRect.width();
                const double u0 = (sx0 - fx) / fw;
                const double u1 = (sx1 - fx) / fw;
                const double x0 = sx0 + qOffX;
                const double x1 = sx1 + qOffX;
                KWin::WindowQuad cell;
                cell[0] = KWin::WindowVertex(x0, y0, u0, v0);
                cell[1] = KWin::WindowVertex(x1, y0, u1, v0);
                cell[2] = KWin::WindowVertex(x1, y1, u1, v1);
                cell[3] = KWin::WindowVertex(x0, y1, u0, v1);
                quads.append(cell);
            }
        }
        return;
    }

    const double ox = qLeft + (outputGeo.x() - textureGeo.x());
    const double oy = qTop + (outputGeo.y() - textureGeo.y());
    const double ow = outputGeo.width();
    const double oh = outputGeo.height();

    // Texcoord handedness: replicate it from the quad KWin handed us
    // rather than hardcoding. KWin's WindowQuad texcoord Y convention is
    // not part of the public contract; assuming it (texcoord.y = 1 at
    // the top) rendered every surface-extent transition upside down AND
    // animating from the wrong screen edge — bounce dropped UP from the
    // bottom — while the daemon path and the non-surface kwin shaders,
    // which use KWin's own window quad, stayed correct. The surface quad
    // below must carry the SAME (screen-position <-> texcoord)
    // handedness as that window quad so the shared kwin vertex stage's
    // `1.0 - texCoord.y` flip lands `vTexCoord` Y-down on this path
    // exactly as it does for the window's own quad. Window content is
    // axis-aligned, so u is linear in x and v in y — derive each axis's
    // sign from the source quad's extreme vertices.
    //
    // Cache the result on the transition: the handedness depends only on
    // KWin's quad convention, which doesn't shift mid-transition. Without
    // the cache, every surface-extent shader pays the 3-vertex search +
    // 4 comparisons per quad per frame for its entire lifetime. `st` is
    // already known non-null (the early `!st || !st->surfaceExtent` guard at
    // the top of apply() returned otherwise), so the cache population reuses
    // that handle instead of paying a second lookup.
    if (!st->handednessCached) {
        const KWin::WindowQuad& srcQuad = quads.first();
        int topIdx = 0, bottomIdx = 0, leftIdx = 0, rightIdx = 0;
        for (int i = 1; i < 4; ++i) {
            if (srcQuad[i].y() < srcQuad[topIdx].y())
                topIdx = i;
            if (srcQuad[i].y() > srcQuad[bottomIdx].y())
                bottomIdx = i;
            if (srcQuad[i].x() < srcQuad[leftIdx].x())
                leftIdx = i;
            if (srcQuad[i].x() > srcQuad[rightIdx].x())
                rightIdx = i;
        }
        // The surface quad spans the whole output, so its texcoords are the
        // full 0..1 range; only the handedness comes from the source quad.
        st->uAtLeft = (srcQuad[leftIdx].u() <= srcQuad[rightIdx].u()) ? 0.0 : 1.0;
        st->uAtRight = 1.0 - st->uAtLeft;
        st->vAtTop = (srcQuad[topIdx].v() <= srcQuad[bottomIdx].v()) ? 0.0 : 1.0;
        st->vAtBottom = 1.0 - st->vAtTop;
        st->handednessCached = true;
    }
    const double uAtLeft = st->uAtLeft;
    const double uAtRight = st->uAtRight;
    const double vAtTop = st->vAtTop;
    const double vAtBottom = st->vAtBottom;

    // One quad covering the whole output, clockwise from top-left (the
    // vertex order WindowQuad documents). The shared kwin vertex stage
    // flips texCoord to the Y-down vTexCoord the animation-shader
    // contract expects, so vTexCoord lands 0..1 over the output. The
    // window content stays in uTexture0 (the window-sized redirect FBO);
    // surface-extent shaders place it via iAnchorPosInFbo / anchorRemap.
    KWin::WindowQuad surfaceQuad;
    surfaceQuad[0] = KWin::WindowVertex(ox, oy, uAtLeft, vAtTop);
    surfaceQuad[1] = KWin::WindowVertex(ox + ow, oy, uAtRight, vAtTop);
    surfaceQuad[2] = KWin::WindowVertex(ox + ow, oy + oh, uAtRight, vAtBottom);
    surfaceQuad[3] = KWin::WindowVertex(ox, oy + oh, uAtLeft, vAtBottom);
    quads.clear();
    quads.append(surfaceQuad);
}

} // namespace PlasmaZones
