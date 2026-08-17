// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The composite fold's INPUT side, split out of surfacelayers.cpp (which owns the
// fold itself).
//
// Two steps, in the order the fold runs them:
//   ensureSurfaceTargets  — (re)allocate the per-window GL targets the fold draws
//                           into, and invalidate exactly the caches an allocation
//                           invalidates.
//   captureWindowSurface  — the raw window capture, which is the single most
//                           expensive step of the whole fold (it re-enters KWin's
//                           draw chain) and the reason the capture cache exists.

#include "plasmazoneseffect.h"

#include "shader_internal.h"
#include "surface_fold.h"
#include "types.h"

#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/gltexture.h>
#include <scene/item.h>
#include <scene/windowitem.h>

#include <PhosphorSurface/SurfaceShaderEffect.h>

#include <QByteArray>
#include <QLoggingCategory>
#include <QPoint>
#include <QRectF>
#include <QScopeGuard>
#include <QSize>

#include <algorithm> // std::max, unioning the two not-animating spans
#include <cmath> // std::floor / std::ceil, the shell content-rect scan bounds

#include <epoxy/gl.h>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

// (Re)allocate this window's composite / capture / per-pack buffer targets for the
// current size, scale and chain, and drop every cache an allocation makes stale.
//
// Returns false when an allocation FAILED, in which case the window's whole surface
// state has been erased and @p state is dangling — the caller must abandon the fold.
bool PlasmaZonesEffect::ensureSurfaceTargets(const QString& windowId, SurfaceMultipassState& state,
                                             const QStringList& chain, const QSize& textureSize, qreal captureScale,
                                             const CompiledPackResolver& compiledPackLazy)
{
    // (Re)allocate the composite ping-pong pair on a size change — or on a SCALE
    // change that the size does not reflect. Past the kMaxSurfaceDim cap the texture
    // is pinned to the cap on its long axis whatever the input scale, so a huge
    // window crossing between outputs of different scale keeps the same
    // compositeSize while uSurfaceScale (which packs multiply logical-px border
    // widths and radii by) moves under it.
    // Explicit epsilon, NOT qFuzzyCompare — the same rule planSurfaceFold spells out below
    // for the focus/opacity keys, and there is no reason for this file to hold two spellings
    // of it. captureScaleKey starts at exactly 0.0, and qFuzzyCompare is a RELATIVE
    // comparison whose tolerance collapses to zero against zero. It is safe here only by
    // accident (an empty textureSize returns before this, so captureScale is never 0), and
    // an accident is not a contract.
    constexpr qreal kScaleEpsilon = 1e-6;
    // `!state.captureTex` completes the defensive pair the fold's srcTex bind
    // relies on (mirroring the composite-pair terms beside it): the invariant
    // that captureTex is allocated whenever the pair is holds today by
    // construction, but a null capture texture reaching the bind would be a
    // null deref inside the compositor — the same reasoning the prefixTex
    // guard in surfacelayers.cpp spells out.
    if (state.compositeSize != textureSize || std::abs(state.captureScaleKey - captureScale) > kScaleEpsilon
        || !state.compositeTex[0] || !state.compositeTex[1] || !state.captureTex) {
        bool allocFailed = false;
        for (size_t i = 0; i < state.compositeTex.size(); ++i) {
            auto& t = state.compositeTex[i];
            // Drop the FRAMEBUFFER first. Reassigning the texture destroys the old one while
            // its framebuffer still wraps it — legal in GL (the attachment auto-detaches),
            // but it is the reverse of the order every sibling path uses (allocSurfaceTarget,
            // the backdrop realloc), and an object destroyed out from under its own wrapper
            // is not a habit worth keeping.
            state.compositeFbo[i].reset();
            t = KWin::GLTexture::allocate(GL_RGBA8, textureSize);
            if (!t) {
                allocFailed = true;
                break;
            }
            t->setFilter(GL_LINEAR);
            t->setWrapMode(GL_CLAMP_TO_EDGE);
            // Wrap each composite target once, here, rather than per pass per
            // frame in the fold below.
            auto fbo = std::make_unique<KWin::GLFramebuffer>(t.get());
            if (!fbo->valid()) {
                allocFailed = true;
                break;
            }
            state.compositeFbo[i] = std::move(fbo);
        }
        // The capture target lives alongside the ping-pong pair and is sized
        // identically; a stale one at the old size must never be presented, so the
        // realloc invalidates every cache keyed on it.
        //
        // The static-prefix target is NOT allocated here. It is only ever written
        // when a chain has a cacheable run followed by a per-frame pack, which the
        // most common chains do not — the default ["border"] has no per-frame pack at
        // all — so allocating it eagerly meant a full-canvas RGBA8 (a fifth of the
        // decoration's whole VRAM budget, ~8 MB on a 4K window) that was never
        // written and never read. It is allocated lazily below, once the fold knows
        // the chain actually needs it.
        if (!allocFailed) {
            state.captureValid = false;
            state.prefixValid = false;
            state.compositeValid = false;
            state.prefixChainEnd = -1;
            state.captureFbo.reset();
            state.prefixTex.reset();
            state.prefixFbo.reset();
            if (!allocSurfaceTarget(state.captureTex, state.captureFbo, textureSize)) {
                allocFailed = true;
            }
        }
        if (allocFailed) {
            // Drop the half-allocated state. Erase AFTER the loop has ended so
            // we never destroy the container mid-iteration (state is a reference
            // into the map being erased).
            qCWarning(lcEffect) << "Surface target allocation failed for" << windowId << "at" << textureSize
                                << "— dropping this window's decoration (out of VRAM?)";
            m_surfaceMultipass.erase(windowId);
            return false;
        }
        state.compositeSize = textureSize;
        state.captureScaleKey = captureScale;
        state.chainKey.clear(); // force the per-pack buffers to reallocate at the new size
    }

    // (Re)allocate the cached per-pack buffer textures when the chain or size
    // changes. chainBufferTex[k] holds one texture per pack k's buffer passes,
    // downscaled by that pack's bufferScale; a pack that fails to compile (or has
    // no buffers) leaves an empty inner vector and renders single-pass in the fold.
    if (state.chainKey != chain) {
        // Framebuffers before textures, for the reason given at the composite realloc above.
        state.chainBufferFbo.clear();
        state.chainBufferTex.clear();
        state.chainBufferTex.resize(chain.size());
        state.chainBufferFbo.resize(chain.size());
        for (int k = 0; k < chain.size(); ++k) {
            CompiledSurfacePack* const pk = compiledPackLazy(chain.at(k));
            if (!pk || !pk->shader || pk->bufferPasses.empty()) {
                continue;
            }
            const PhosphorSurfaceShaders::SurfaceShaderEffect eff = m_surfaceShaderRegistry.effect(chain.at(k));
            const qreal bufferScale = clampedBufferScale(eff.bufferScale);
            const QSize bufferSize(qMax(1, qRound(textureSize.width() * bufferScale)),
                                   qMax(1, qRound(textureSize.height() * bufferScale)));
            auto& bufs = state.chainBufferTex[k];
            auto& fbos = state.chainBufferFbo[k];
            bufs.reserve(pk->bufferPasses.size());
            fbos.reserve(pk->bufferPasses.size());
            for (size_t i = 0; i < pk->bufferPasses.size(); ++i) {
                std::unique_ptr<KWin::GLTexture> bt = KWin::GLTexture::allocate(GL_RGBA8, bufferSize);
                if (!bt) {
                    // Pack k degrades to no buffers. The fold's main pass then
                    // binds the transparent fallback to every iChannel the pack
                    // still declares, so they genuinely sample 0 — an unset
                    // sampler2D would otherwise read unit 0, i.e. the running
                    // composite.
                    bufs.clear();
                    fbos.clear(); // the framebuffers pooled beside them go too
                    break;
                }
                bt->setFilter(GL_LINEAR);
                bt->setWrapMode(GL_CLAMP_TO_EDGE);
                // Wrap the buffer target once here; the fold reuses it every frame.
                // Keep bufs/fbos strictly in lockstep — the fold indexes both by i.
                auto bfbo = std::make_unique<KWin::GLFramebuffer>(bt.get());
                if (!bfbo->valid()) {
                    bufs.clear();
                    fbos.clear();
                    break;
                }
                bufs.push_back(std::move(bt));
                fbos.push_back(std::move(bfbo));
            }
        }
        // A different chain folds to a different composite, so neither the whole-
        // chain cache nor the static-prefix cache survives it.
        state.compositeValid = false;
        state.prefixValid = false;
        state.prefixChainEnd = -1;
        state.chainKey = chain;
        // The prefix TEXTURE goes too (the size-change branch above releases it as well;
        // that branch clears chainKey, so this one always follows it). The new
        // chain may not want one at all (["border","glow"] → ["border"]), and holding a
        // full-canvas RGBA8 nothing will ever write again is ~8 MB per 4K window. The fold
        // deliberately does NOT release it when usePrefix merely goes false, because that
        // flips with the animation gate and would realloc on every focus change — but a
        // chain change is rare and is already rebuilding everything.
        state.prefixTex.reset();
        state.prefixFbo.reset();
    }
    return true;
}

// Capture the raw window surface into the target the fold will read as uTexture0.
//
// THE expensive step: KWin::effects->drawWindow() re-enters the entire draw chain for
// this window. Its only input is the window's own content, so the fold caches the
// result and re-runs this only when the window actually damages — which is what the
// whole capture cache is for. @p intoCaptureTex is false only for the degenerate chain
// where no pack compiled, which folds nothing and presents the capture directly out of
// compositeTex[0].
void PlasmaZonesEffect::captureWindowSurface(KWin::EffectWindow* w, SurfaceMultipassState& state,
                                             const QRectF& logicalGeometry, qreal captureScale, bool intoCaptureTex,
                                             qreal captureOpacity)
{
    KWin::GLFramebuffer& fbo = intoCaptureTex ? *state.captureFbo : *state.compositeFbo[0];
    setShader(w, nullptr);
    m_capturingSnapshot = true;
    // Guard the re-entrancy flag against a throw from the draw chain — a
    // leaked m_capturingSnapshot would corrupt every subsequent paint.
    // Same pattern as renderSurfaceChain.
    auto resetCapture = qScopeGuard([this] {
        m_capturingSnapshot = false;
    });
    {
        KWin::RenderTarget renderTarget(&fbo);
        KWin::RenderViewport viewport(logicalGeometry, captureScale, renderTarget, QPoint());
        KWin::GLFramebuffer::pushFramebuffer(&fbo);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        KWin::ItemEffect keepRenderable(w->windowItem());
        KWin::WindowPaintData captureData;
        // Almost always RAW (1.0): opacity is a downstream pack-param concern in the
        // layer-backed model (the opacity-tint layer's folded param, or a pack's own
        // contentOpacity). The caller passes < 1.0 only on the ONE fail-safe path — an
        // opacity-baking chain whose opacity-tint pack failed to compile — where nothing
        // else would apply the window's resolved opacity. See the call site.
        captureData.setOpacity(captureOpacity);
        const int captureMask = PAINT_WINDOW_TRANSFORMED | PAINT_WINDOW_TRANSLUCENT;
        KWin::effects->drawWindow(renderTarget, viewport, w, captureMask, KWin::Region::infinite(), captureData);
        KWin::GLFramebuffer::popFramebuffer();
    }
    resetCapture.dismiss();
    m_capturingSnapshot = false;
    state.captureValid = true;
    state.captureInComposite = !intoCaptureTex;
    // The frame-relative offset the shell content scan must measure against
    // (see the field doc): the viewport above maps logicalGeometry onto the
    // FBO, so the frame's texels sit at exactly this offset (times scale)
    // inside the capture — and unlike either absolute origin, the OFFSET is
    // move-invariant, because the canvas is derived from the window's own
    // geometry and moves with it.
    state.captureFrameOffset = w->frameGeometry().topLeft() - logicalGeometry.topLeft();
}

void PlasmaZonesEffect::updateShellContentRect(KWin::EffectWindow* w, SurfaceMultipassState& state, qreal captureScale)
{
    const QRectF frame = w->frameGeometry();
    if (frame.isEmpty() || captureScale <= 0.0) {
        return;
    }
    // Throttle the readback: a panel damages often (clock ticks, tray
    // animation) but its visible shape changes rarely, and glReadPixels is a
    // pipeline stall. A frame RESIZE bypasses the throttle — the stored rect
    // is invalid for the new size (consumers check shellContentFrameSize) and
    // must be replaced on the first capture at that size. The caller runs this
    // on EVERY paint of a shell surface (not only on a fresh capture — see
    // renderSurfaceChainComposite), so a shape change at an unchanged frame
    // size self-heals within one interval of the panel's next paint. A panel
    // that is never painted at all keeps the old rect, which is fine: nothing
    // is on screen to disagree with it.
    constexpr qint64 kRescanIntervalMs = 1000;
    const qint64 nowMs = ShaderInternal::shaderClockNowMs();
    if (state.shellContentFrameSize == frame.size() && state.shellContentScanMs >= 0
        && nowMs - state.shellContentScanMs < kRescanIntervalMs) {
        return;
    }

    KWin::GLFramebuffer* fbo = state.captureInComposite ? state.compositeFbo[0].get() : state.captureFbo.get();
    if (!fbo || !fbo->valid()) {
        return;
    }
    const QSize texSize = fbo->size();

    // The frame subrect inside the canvas texture, top-down device px. FLOOR
    // the origin / CEIL the extent so a fractional-scale frame edge lands
    // inside the scan rather than outside it, then clamp to the texture. The
    // exact (pre-floor) origins are kept so the stored rect can carry the
    // sub-device-pixel residue at fractional scales — bounds are measured
    // from the FLOORED origin, and dropping the fraction shifted the
    // substituted frame up to one device px up/left of the visible body.
    //
    // Measured via the frame offset stamped AT CAPTURE TIME (see the field
    // doc): the scan can run on a still-valid capture after a pure move, and
    // the frame-relative offset is the move-invariant quantity — pairing the
    // live frame with a capture-time canvas origin (or the reverse) would err
    // by the whole move delta, while the offset is exact for the texels the
    // buffer actually holds.
    const qreal fxExact = state.captureFrameOffset.x() * captureScale;
    const qreal fyExact = state.captureFrameOffset.y() * captureScale;
    const int fx = std::clamp(static_cast<int>(std::floor(fxExact)), 0, texSize.width());
    const int fy = std::clamp(static_cast<int>(std::floor(fyExact)), 0, texSize.height());
    const int fw = std::clamp(static_cast<int>(std::ceil(frame.width() * captureScale)), 0, texSize.width() - fx);
    const int fh = std::clamp(static_cast<int>(std::ceil(frame.height() * captureScale)), 0, texSize.height() - fy);
    if (fw <= 0 || fh <= 0) {
        return;
    }

    // GL's framebuffer origin is bottom-left (same flip snapassistthumbnail-
    // capture.cpp applies to its toImage()), so the read starts at the frame
    // rect's BOTTOM edge and buffer row r is top-down row fh - 1 - r.
    // m_shellScanScratch is the reusable staging buffer — this runs on the
    // paint path, and a fresh ~1 MB allocation per rescan is exactly the
    // per-frame-allocation class this file avoids. QByteArray retains its
    // capacity across resize(), so once the buffer has grown to the largest
    // scanned panel no further allocation happens.
    m_shellScanScratch.resize(static_cast<qsizetype>(fw) * fh * 4);
    // The row indexing below assumes tightly packed rows. RGBA8 rows are
    // 4-byte multiples so any PACK_ALIGNMENT up to 4 is tight, but the global
    // pixel-store state is shared with KWin and every other loaded effect —
    // pin PACK_ROW_LENGTH/ALIGNMENT for the read and restore what was there.
    GLint prevAlignment = 4;
    GLint prevRowLength = 0;
    glGetIntegerv(GL_PACK_ALIGNMENT, &prevAlignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &prevRowLength);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    KWin::GLFramebuffer::pushFramebuffer(fbo);
    glReadPixels(fx, texSize.height() - fy - fh, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, m_shellScanScratch.data());
    KWin::GLFramebuffer::popFramebuffer();
    glPixelStorei(GL_PACK_ALIGNMENT, prevAlignment);
    glPixelStorei(GL_PACK_ROW_LENGTH, prevRowLength);

    // Bound the texels that read as the surface's BODY, not its shadow. A
    // floating panel keeps its float gap and drop shadow INSIDE the frame
    // rect (plasmashell draws them in its own window), so a fixed low floor
    // bounded the shadow's soft skirt and the decoration wrapped the full
    // floating extent instead of the visible bar (live-measured: the skirt
    // sits around 15-25% alpha while a Panel Colorizer body is 85%+ with
    // opaque widgets on top). The floor is therefore RELATIVE — a fraction of
    // the strongest alpha actually present — so it lands above any soft
    // shadow while still admitting a deliberately translucent body, whose
    // widgets carry the maximum up anyway. kAlphaFloorMin keeps a
    // near-invisible surface from bounding its own noise.
    //
    // KNOWN LIMIT of the relative floor: it holds only while the body's peak
    // alpha stays well above the shadow skirt. A panel styled fully
    // translucent with no opaque widgets on it (peak alpha under ~72% of 255)
    // puts the floor back inside the measured 15-25% skirt band, and the
    // bounds then admit the shadow again. Accepted: such a panel has no crisp
    // body edge to hug in the first place, and the fallback is the full
    // frame, not a wrong crop of the body.
    //
    // Two passes over the buffer by necessity, not oversight: the floor is a
    // fraction of the maximum, so the bounds pass cannot start until the max
    // pass has finished.
    const auto* data = reinterpret_cast<const unsigned char*>(m_shellScanScratch.constData());
    unsigned char maxAlpha = 0;
    const qsizetype pixelCount = static_cast<qsizetype>(fw) * fh;
    for (qsizetype i = 0; i < pixelCount; ++i) {
        maxAlpha = std::max(maxAlpha, data[i * 4 + 3]);
    }
    constexpr int kAlphaFloorMin = 8;
    constexpr int kAlphaFloorPercentOfMax = 35;
    const int alphaFloor = std::max(kAlphaFloorMin, (static_cast<int>(maxAlpha) * kAlphaFloorPercentOfMax) / 100);
    int minX = fw, maxX = -1, minRow = fh, maxRow = -1;
    for (int r = 0; r < fh; ++r) {
        const unsigned char* row = data + static_cast<qsizetype>(r) * fw * 4;
        for (int x = 0; x < fw; ++x) {
            if (row[x * 4 + 3] < alphaFloor) {
                continue;
            }
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minRow = std::min(minRow, r);
            maxRow = std::max(maxRow, r);
        }
    }

    state.shellContentFrameSize = frame.size();
    state.shellContentScanMs = nowMs;
    const QRectF previousRect = state.shellContentRect;
    if (maxX < 0) {
        // Nothing visible at all — leave the rect empty; consumers fall back
        // to the full frame rather than collapsing the decoration to a point.
        state.shellContentRect = QRectF();
    } else {
        // Buffer rows are bottom-up (row 0 is the frame's bottom edge), so the
        // top-down top is fh - 1 - maxRow and the top-down bottom is
        // fh - 1 - minRow, both inclusive. Bounds are measured from the
        // FLOORED scan origin; subtracting the origin's dropped fraction
        // (fxExact - fx, in [0,1) device px) re-expresses them relative to the
        // frame's true fractional origin, which is what the consumer
        // re-anchors against (pushBorderUniforms translates by
        // frame.topLeft()).
        const int topDown = fh - 1 - maxRow;
        const int bottomDown = fh - 1 - minRow;
        state.shellContentRect =
            QRectF((minX - (fxExact - fx)) / captureScale, (topDown - (fyExact - fy)) / captureScale,
                   (maxX - minX + 1) / captureScale, (bottomDown - topDown + 1) / captureScale);
    }
    // The rect is a direct shader input (pushBorderUniforms) but is NOT part
    // of the fold plan's key set, so a moved rect must drop the cached
    // prefix/composite itself — the fold has no other way to notice. Without
    // this the update is consumed only by the accident that the sole original
    // call site ran right after a fresh capture (which had already cleared
    // both flags); the hoisted per-paint call site relies on this.
    if (state.shellContentRect != previousRect) {
        state.prefixValid = false;
        state.prefixChainEnd = -1;
        state.compositeValid = false;
    }
}

// Decide what a fold can REUSE before it does any work.
//
// Every cache decision the fold makes lives here and nowhere else: whether the chain may
// animate right now, what clock it therefore runs on, how much of its head is cacheable,
// and whether the state it was last folded with has moved. The fold then only executes
// the plan. Keeping the decision in one place is what stops its parts from drifting out
// of agreement with each other, which they have done more than once.
//
// @p inTransition: a live shader transition owns the window's shader slot. It always
// animates (it IS the thing being watched). Its capture stays cacheable — see the
// invalidation rationale at the fold-time block below.
SurfaceFoldPlan PlasmaZonesEffect::planSurfaceFold(KWin::EffectWindow* w, const QString& windowId,
                                                   const WindowDecoration& deco, const QStringList& chain,
                                                   SurfaceMultipassState& state,
                                                   const CompiledPackResolver& compiledPackLazy, bool inTransition)
{
    SurfaceFoldPlan plan;

    // Decorations.Performance: may this window's chain animate right now? A chain that
    // may not is folded against a FROZEN clock, which is what lets it be cached — see
    // packVariesPerFrame, and pausedAtMs / timeOffsetMs for why the freeze has to
    // reach the uniform and not just the repaint driver.
    //
    // A window under a live shader TRANSITION always animates: the transition is the
    // thing being watched, it drives the window's geometry frame by frame, and it is
    // the one caller that supplies its own restore shader.
    plan.mayAnimate = inTransition || decorationMayAnimate(w);
    // The window's own clock, which STOPS whenever the window is not animating and
    // RESUMES where it stopped. Not the shared clock: that one runs on regardless, so a
    // window that stopped for ten minutes would resume by jumping its iTime ten minutes
    // forward in a single frame, and every periodic pack would pop to an unrelated
    // phase. The time it was not animating is accumulated into timeOffsetMs and
    // subtracted out instead.
    //
    // TWO ways a window stops, and both must be accounted or the jump returns through
    // whichever was missed:
    const qint64 nowMs = ShaderInternal::shaderClockNowMs();
    const qint64 sharedNowMs = surfaceShaderTimeMs();
    // Well clear of any real frame interval, and well under any gap a person would notice
    // as a phase jump.
    constexpr qint64 kNotPaintedGapMs = 250;
    if (plan.mayAnimate) {
        // How long has this window NOT been animating? Two sensors, and on THIS path they
        // measure the same interval from different ends — both end at now — so take the
        // larger, never the sum. Adding them double-counts every span where both apply,
        // which is the normal case, not an exotic one: a gate-paused window is precisely a
        // window nothing is driving to repaint.
        qint64 notAnimatingMs = 0;

        // GATED — Decorations.Performance paused it, and told us so.
        if (state.pausedAtMs >= 0) {
            notAnimatingMs = sharedNowMs - state.pausedAtMs;
            state.pausedAtMs = -1;
        }

        // UNPAINTED — nothing stopped it, it simply was not drawn: minimized, on another
        // desktop, fully occluded. Nobody tells us that happened, so it is inferred from
        // the gap since the last fold. lastFoldMs is stamped on BOTH terminal paths of the
        // fold, including the cached-composite early return, so a window that IS being
        // painted but is serving from cache never looks unpainted here.
        if (state.lastFoldMs >= 0 && nowMs - state.lastFoldMs > kNotPaintedGapMs) {
            notAnimatingMs = std::max(notAnimatingMs, nowMs - state.lastFoldMs);
        }
        state.timeOffsetMs += notAnimatingMs;
    } else if (state.pausedAtMs < 0) {
        // ENTERING a gated pause. Account the unpainted gap FIRST, and by ADDING it: the
        // span [lastFold, pauseStart] is disjoint from the pause that starts now, so this is
        // the one place the two sensors do not overlap and max() would be wrong. Without it,
        // a window that stopped being painted and only THEN got gated lost the whole
        // unpainted span, and its iTime jumped forward by exactly that much on resume.
        if (state.lastFoldMs >= 0 && nowMs - state.lastFoldMs > kNotPaintedGapMs) {
            state.timeOffsetMs += nowMs - state.lastFoldMs;
        }
        state.pausedAtMs = sharedNowMs;
    }
    // While paused this is pinned to the instant the pause began, so the folds that still
    // have to run (the window's own content damaged, its focus ramp moving) reproduce the
    // frame it froze on instead of drifting.
    const qint64 ownClockMs = (plan.mayAnimate ? sharedNowMs : state.pausedAtMs) - state.timeOffsetMs;
    plan.foldTime = static_cast<float>(static_cast<double>(ownClockMs) / 1000.0);
    // The capture is cacheable THROUGH a live transition. This used to be
    // `!inTransition` ("the transition drives the window's geometry frame by frame;
    // don't trust a cached capture across it"), which re-ran the full
    // effects->drawWindow() re-entry — the single most expensive step of the fold —
    // plus the whole chain re-fold, per decorated window, per frame, for every
    // animation. But the two ways a capture can actually go stale are both covered by
    // machinery that stays live throughout a transition:
    //   • a geometry/scale move changes canvasGeo → textureSize/captureScale →
    //     ensureSurfaceTargets reallocates and clears captureValid;
    //   • real client damage fires the windowDamaged connection (decorations.cpp),
    //     which clears captureValid; the per-frame transition drivers in
    //     postPaintScreen damage at SCREEN level and never trip it.
    // The transition's own motion (WindowAnimator transform, vertex-stage sweep) is
    // applied downstream of the composite and is never baked into the capture, so it
    // cannot stale it. One-shot window-level repaints (transition install, and
    // animation start via onAnimationStarted's unscoped addRepaintFull) still
    // invalidate once each — that costs one re-capture per edge, not one per
    // frame. (The mesh settle-edge repaint is selfRepaintScope'd and does not
    // invalidate.) The BACKDROP is a third input with no invalidator here, and
    // deliberately so: the backdrop is not part of the CAPTURE (it is sampled
    // by the fold each refold), and a chain that links a backdrop uniform is
    // classified per-frame by packVariesPerFrame regardless of pause state, so
    // its composite is never served from this cache. Two known residuals at
    // fractional output scale, accepted pending a live check: a pure MOVE
    // during a held drag keeps the capture's sub-pixel rasterization phase
    // from when it was taken (snapping is relaxed for the duration, pulling
    // the same direction), and the capture's edge texels blend with the
    // transparent canvas under a bufferScale < 1 downsample.

    // How many packs in the chain actually compiled and therefore draw? A pack that
    // failed to compile folds nothing, so it cannot make the composite time-varying and
    // it cannot be the reason a cache is refused. compiledPackLazy is memoised, so this
    // pre-pass costs a hash lookup per pack and everything below re-reads it free.
    int foldablePacks = 0;
    for (int k = 0; k < chain.size(); ++k) {
        const CompiledSurfacePack* const pk = compiledPackLazy(chain.at(k));
        if (pk && pk->shader && k < static_cast<int>(state.chainBufferTex.size())) {
            ++foldablePacks;
        }
    }

    // How many packs at the head of the chain are cacheable? Their fold is a pure
    // function of the capture and the folded STATE (focus / opacity), so it can be
    // cached across frames while the per-frame packs behind them keep re-folding.
    //
    // This is a leading RUN, not a set: once a per-frame pack has folded, its output
    // differs every frame, so every pack downstream is fed a different input every
    // frame no matter how simple it is. Stop at the first pack that varies per frame.
    //
    // A pack that FAILED to compile does not stop the run. The fold below skips it
    // (it draws nothing, so it cannot make the composite time-varying), and stopping
    // here instead meant one broken pack mid-chain disabled the whole-composite cache
    // for the entire chain permanently — the trailing static packs re-folded on every
    // paint even though nothing about them had moved.
    //
    // Hence THREE counters, and the distinction between them matters: `staticPrefix`
    // is a chain INDEX (where the run ends), `staticFoldable` is how many packs inside
    // it actually draw, and `lastStaticDraw` is the last of those. Comparing an index
    // against the foldable COUNT is what produced the bug above.
    int staticPrefix = 0;
    int staticFoldable = 0;
    int lastStaticDraw = -1;
    while (staticPrefix < chain.size()) {
        const CompiledSurfacePack* const pk = compiledPackLazy(chain.at(staticPrefix));
        const bool draws = pk && pk->shader && staticPrefix < static_cast<int>(state.chainBufferTex.size());
        if (draws && packVariesPerFrame(*pk, plan.mayAnimate, audioReactiveDriving())) {
            break;
        }
        if (draws) {
            ++staticFoldable;
            lastStaticDraw = staticPrefix;
        }
        ++staticPrefix;
    }

    // The STATE the fold is about to bake in. Focus and rule-opacity are constant
    // between events (the ramp clamps to exactly 0/1 at its ends), so they are cache
    // keys rather than disqualifiers — which is what lets the default `border` chain
    // cache at all, since that pack mixes its colours on uSurfaceFocused.
    //
    // advanceFocusFade reads the PINNED per-frame clock, so calling it here and again
    // from pushBorderUniforms within the same fold is an exact no-op the second time:
    // the ramp still advances at most once per frame.
    //
    // The ramp is advanced ONLY for a chain that some pack actually reads focus in
    // (uFocusedLoc >= 0), matching pushBorderUniforms. advanceFocusFade CREATES the
    // m_focusFade entry, and an entry mid-ramp forces per-frame repaints and clears
    // the composite cache every frame of the fade — so advancing it for a chain that
    // never samples uSurfaceFocused would drag focus-blind windows into a full re-fold
    // of a composite that cannot change.
    bool chainReadsFocus = false;
    bool chainReadsCursor = false;
    for (int k = 0; k < chain.size(); ++k) {
        const CompiledSurfacePack* const pk = compiledPackLazy(chain.at(k));
        if (!pk || !pk->shader) {
            continue;
        }
        chainReadsFocus = chainReadsFocus || pk->uFocusedLoc >= 0;
        chainReadsCursor = chainReadsCursor || pk->iMouseLoc >= 0;
    }
    // The cursor is a cache KEY for a hover chain, exactly as focus and opacity are.
    //
    // Keyed on the value the SHADER actually receives, not the raw global position.
    // pushBorderUniforms resolves the cursor to an "outside" sentinel whenever it is not
    // over this window's canvas, so keying on the global position would make every hover
    // window on the desktop re-fold its entire chain every time the pointer moved
    // ANYWHERE — each one to reproduce the identical outside sentinel it already had.
    // A paused chain reads it as absent too, matching what pushBorderUniforms pushes.
    // Keyed on the GATE, not on plan.mayAnimate. A live transition widens mayAnimate to
    // true (the transition is the thing being watched, and its clock and audio must run),
    // but the CURSOR must not be widened with it: a paused window would then bake the live
    // pointer into its key on every frame of the animation, and when the transition ends
    // nothing repaints it — the driver skips a paused window and so does the hover wake-up
    // — so the highlight stays frozen wherever the pointer happened to be, indefinitely.
    // windowSurfaceAnimates re-evaluates this same expression, so the two must key on the
    // same gate on EVERY path, transitions included.
    plan.foldCursor = chainReadsCursor
        ? foldCursorFor(w, state.canvasGeo, decorationMayAnimate(w), m_shaderManager.m_cachedCursorGlobal)
        : kCursorOutside;
    // Shell surfaces count as FOCUSED: a panel is a dock and never becomes
    // KWin's active window, so the raw test pinned uSurfaceFocused at 0 for
    // its whole life and a focus-mixing pack (border's active/inactive
    // colours) only ever showed its inactive appearance there. A panel has no
    // unfocused state to represent — it is always "in use" while visible —
    // and an applet popup is effectively focused for its whole open lifetime,
    // so both kinds pin the flag high. Mirrored in pushBorderUniforms.
    const bool focusedNow = deco.isShellSurface || (KWin::effects && w == KWin::effects->activeWindow());
    if (!chainReadsFocus) {
        // Terminate a STRANDED ramp. advanceFocusFade is this map's only
        // advancer, and it is unreachable for a chain with no compiled
        // focus-reading pack — so if the chain stopped reading focus with a
        // fade mid-flight (a chain edit, or the focus pack's compile failing
        // on a hot-reload), the entry would sit strictly between 0 and 1
        // forever, focusRampInFlight would keep windowSurfaceAnimates true
        // unconditionally, and the repaint driver would bypass the
        // Decorations.Performance gate at vsync rate with nothing able to
        // stop it. The driver's own repaint reaches this fold, so the drop
        // lands on the first frame of the runaway; a focus-reading pack
        // returning later re-seeds the -1 snap sentinel, matching the
        // undecorated→decorated scrub in updateWindowDecoration.
        m_focusFade.remove(windowId);
    }
    plan.foldFocus = chainReadsFocus ? advanceFocusFade(windowId, focusedNow) : 0.0f;
    // The effective opacity, folded into the opacity-tint pack param and carried on the
    // decoration. It is a fold cache key: a change re-folds, and on the fail-safe path (where
    // the opacity is baked into the CAPTURE) the guard just below also re-captures.
    plan.foldOpacity = static_cast<float>(deco.foldedOpacity);
    // Explicit epsilon, NOT qFuzzyCompare: both values settle at exactly 0.0, which
    // qFuzzyCompare is documented not to handle (it is a RELATIVE comparison, and
    // against zero its tolerance collapses to zero). It happens to fail SAFE here —
    // a false "moved" only over-invalidates — but relying on that is not a contract.
    // Both are clamped to 0..1, so an absolute epsilon is exactly right.
    //
    // foldStateEqual (surface_fold.h), not a local copy: the opacity fail-safe in
    // surfacelayers.cpp asks the other half of this question ("is the folded opacity at
    // rest"), and the two must not disagree about what "the same" means.
    const bool stateMoved = !foldStateEqual(state.foldedFocus, plan.foldFocus)
        || !foldStateEqual(state.foldedOpacity, plan.foldOpacity) || plan.foldCursor != state.foldedCursor;

    // The opacity fail-safe (surfacelayers.cpp) bakes the window's opacity INTO the capture
    // when an opacity-baking chain's opacity-tint pack failed to compile. On that path an
    // opacity move needs a RE-CAPTURE, not just the re-fold `stateMoved` triggers below: the
    // dimmed pixels live in the capture texture, and re-folding over the still-old-dim
    // capture would render the previous opacity forever (until unrelated content damage). The
    // layer-backed path captures raw and is correctly served by the re-fold alone, so gate on
    // the same missing-pack condition the fail-safe itself uses rather than re-capturing on
    // every opacity move.
    if (deco.chainBakesOpacity && !foldStateEqual(state.foldedOpacity, plan.foldOpacity)) {
        const CompiledSurfacePack* const otPack = compiledPackLazy(QStringLiteral("opacity-tint"));
        if (!otPack || !otPack->shader) {
            state.captureValid = false;
        }
    }

    // Where does the capture BELONG this fold? Two homes: compositeTex[0] for a chain
    // with no compilable pack (nothing folds, so the capture IS the composite), captureTex
    // otherwise. This is a cache DECISION and so it lives here with the others — deciding
    // it in the fold, after the invalidations below had already run, meant a flip
    // re-captured but left compositeValid set, and the early return then served the stale
    // composite and threw the fresh capture away.
    plan.captureInComposite = foldablePacks == 0;
    if (state.captureInComposite != plan.captureInComposite) {
        state.captureValid = false; // it is sitting in the other texture
    }

    // The PREFIX cache pays only when something per-frame follows the cacheable run:
    // it exists so those packs can fold over a run that does not need re-folding.
    bool usePrefix = staticFoldable > 0 && staticFoldable < foldablePacks;
    // Allocate its target lazily, and only for a chain that will actually use it. A
    // chain with no per-frame pack (the default ["border"]) never writes it, so an
    // eager allocation was a full-canvas RGBA8 held for nothing. Release it again if
    // the chain changes to a shape that no longer needs it.
    if (usePrefix) {
        if (!state.prefixTex && !allocSurfaceTarget(state.prefixTex, state.prefixFbo, state.compositeSize)) {
            // Out of VRAM for the optional cache: fold the chain the long way rather
            // than failing the whole paint.
            usePrefix = false;
        }
    }
    // NOTE the missing `else`. The prefix texture is NOT released just because this fold
    // has no use for it. usePrefix flips with the animation gate — a chain like
    // ["border", "glow"] needs the prefix while it animates and does not while it is
    // paused — so releasing on the flip meant a full-canvas RGBA8 allocation and a
    // framebuffer gen/check on every single focus change of every such window, roughly
    // 8 MB of churn on a 4K window, on the most frequent interaction there is. The
    // texture is keyed on size and chain and is freed with the rest of the surface state
    // when either moves, or when the window goes away; holding it across a pause costs
    // memory we were about to reallocate anyway.
    // When NOTHING in the chain varies per frame there is no such split — the whole
    // composite is a pure function of (capture, state), so it is cached entire.
    // NO `foldablePacks > 0` term. A chain in which nothing compiles folds nothing: the
    // capture goes straight into compositeTex[0] and is presented from there, so the
    // composite is a pure function of the capture and is every bit as cacheable as an
    // all-static chain — the capture-cacheability rationale two dozen lines up argues
    // exactly that.
    // Requiring at least one foldable pack meant such a chain never took the cached-composite
    // early return, so it cleared backdropRepaintPending on every fold, so a needsBackdrop
    // driver re-armed every 33ms forever, plus a full-canvas backdrop blit per paint, for a
    // decoration that draws nothing at all. `staticFoldable == foldablePacks` is trivially
    // true when both are zero, which is the right answer.
    plan.allStatic = staticFoldable == foldablePacks;
    // Both caches sit downstream of the capture and of the folded state.
    if (!state.captureValid || stateMoved) {
        state.prefixValid = false;
        state.compositeValid = false;
    }
    if (!usePrefix || state.prefixChainEnd != staticPrefix) {
        state.prefixValid = false;
    }
    if (!plan.allStatic) {
        state.compositeValid = false;
    }

    plan.foldablePacks = foldablePacks;
    plan.staticPrefix = staticPrefix;
    plan.lastStaticDraw = lastStaticDraw;
    plan.usePrefix = usePrefix;
    return plan;
}

} // namespace PlasmaZones
