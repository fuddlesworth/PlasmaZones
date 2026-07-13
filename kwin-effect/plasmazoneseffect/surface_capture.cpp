// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The composite fold's INPUT side, split out of surfacelayers.cpp (which owns the
// fold itself) to keep that TU under the 800-line limit.
//
// Two steps, in the order the fold runs them:
//   ensureSurfaceTargets  — (re)allocate the per-window GL targets the fold draws
//                           into, and invalidate exactly the caches an allocation
//                           invalidates.
//   captureWindowSurface  — the raw window capture, which is the single most
//                           expensive step of the whole fold (it re-enters KWin's
//                           draw chain) and the reason the capture cache exists.

#include "../plasmazoneseffect.h"

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

#include <QPoint>
#include <QRectF>
#include <QScopeGuard>
#include <QSize>

#include <algorithm> // std::max, unioning the two not-animating spans

#include <epoxy/gl.h>

namespace PlasmaZones {

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
    if (state.compositeSize != textureSize || !qFuzzyCompare(state.captureScaleKey, captureScale)
        || !state.compositeTex[0] || !state.compositeTex[1]) {
        bool allocFailed = false;
        for (size_t i = 0; i < state.compositeTex.size(); ++i) {
            auto& t = state.compositeTex[i];
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
            state.prefixPackCount = -1;
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
        state.chainBufferTex.clear();
        state.chainBufferTex.resize(chain.size());
        state.chainBufferFbo.clear();
        state.chainBufferFbo.resize(chain.size());
        for (int k = 0; k < chain.size(); ++k) {
            CompiledSurfacePack* const pk = compiledPackLazy(chain.at(k));
            if (!pk || !pk->shader || pk->bufferPasses.empty()) {
                continue;
            }
            const PhosphorSurfaceShaders::SurfaceShaderEffect eff = m_surfaceShaderRegistry.effect(chain.at(k));
            const qreal bufferScale =
                qBound(PhosphorSurfaceShaders::SurfaceShaderEffect::kMinBufferScale, eff.bufferScale,
                       PhosphorSurfaceShaders::SurfaceShaderEffect::kMaxBufferScale);
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
        state.prefixPackCount = -1;
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
                                             bool captureCacheable)
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
        // Capture RAW (opacity 1.0). Rule opacity is applied downstream
        // as a shader concern: the present passthrough modulates the
        // final composite (KWin-style uniform ghosting), unless a chain
        // pack declares handlesOpacity and applies uSurfaceOpacity to
        // its own content sample instead (frost). Dimming the capture
        // here would double-apply against either.
        captureData.setOpacity(1.0);
        const int captureMask = PAINT_WINDOW_TRANSFORMED | PAINT_WINDOW_TRANSLUCENT;
        KWin::effects->drawWindow(renderTarget, viewport, w, captureMask, KWin::Region::infinite(), captureData);
        KWin::GLFramebuffer::popFramebuffer();
    }
    resetCapture.dismiss();
    m_capturingSnapshot = false;
    state.captureValid = captureCacheable;
    state.captureInComposite = !intoCaptureTex;
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
// animates (it IS the thing being watched), and its capture is never cacheable.
SurfaceFoldPlan PlasmaZonesEffect::planSurfaceFold(KWin::EffectWindow* w, const QString& windowId,
                                                   const WindowDecoration& deco, const QStringList& chain,
                                                   SurfaceMultipassState& state,
                                                   const CompiledPackResolver& compiledPackLazy, bool inTransition)
{
    SurfaceFoldPlan plan;

    // Decorations.Performance: may this window's chain animate right now? A chain that
    // may not is folded against a FROZEN clock, which is what lets it be cached — see
    // packVariesPerFrame, and pausedAtSec / timeOffsetSec for why the freeze has to
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
    // phase. The seconds it was not animating are accumulated into timeOffsetSec and
    // subtracted out instead.
    //
    // TWO ways a window stops, and both must be accounted or the jump returns through
    // whichever was missed:
    const float sharedNow = surfaceShaderTimeSeconds();
    const qint64 nowMs = ShaderInternal::shaderClockNowMs();
    if (plan.mayAnimate) {
        // How long has this window NOT been animating? Two sensors, and they measure the
        // SAME interval from different ends — so take the larger, never the sum. Adding
        // them double-counts every span where both apply, which is the normal case, not
        // an exotic one: a gate-paused window is precisely a window nothing is driving to
        // repaint. Summing them subtracted each pause TWICE, so the clock walked backward
        // by the pause duration on every idle resume and every focus return, cumulatively,
        // until iTime went negative and anything under a sqrt() produced NaN.
        float notAnimatingSec = 0.0f;

        // GATED — Decorations.Performance paused it, and told us so. Measured from the
        // instant the pause began.
        if (state.pausedAtSec >= 0.0f) {
            notAnimatingSec = sharedNow - state.pausedAtSec;
            state.pausedAtSec = -1.0f;
        }

        // UNPAINTED — nothing stopped it, it simply was not drawn: minimized, on another
        // desktop, fully occluded. Nobody tells us that happened, so it is inferred from
        // the gap since the last fold. This is the far more common case, and it is why the
        // accounting cannot hang off the pause gate alone. The threshold is well clear of
        // any real frame interval and well under any gap a person would notice as a phase
        // jump. lastFoldMs is stamped on BOTH terminal paths of the fold, including the
        // cached-composite early return, so a window that IS being painted but is serving
        // from cache never looks unpainted here.
        constexpr qint64 kNotPaintedGapMs = 250;
        if (state.lastFoldMs >= 0 && nowMs - state.lastFoldMs > kNotPaintedGapMs) {
            notAnimatingSec = std::max(notAnimatingSec, static_cast<float>(nowMs - state.lastFoldMs) / 1000.0f);
        }
        state.timeOffsetSec += notAnimatingSec;
    } else if (state.pausedAtSec < 0.0f) {
        // ENTERING a gated pause. Account the unpainted gap FIRST, and by ADDING it: the
        // span [lastFold, pauseStart] is disjoint from the pause that starts now, so this
        // is the one place the two sensors do not overlap and max() would be wrong.
        //
        // Without this, a window that stopped being painted and only THEN got gated (the
        // session idles while it sits occluded or on another desktop; focus leaves a
        // window nothing was painting) lost the whole unpainted span, because the resume
        // only ever measures back to the pause. Its iTime jumped forward by exactly that
        // span — the phase pop this accounting exists to prevent, arriving through the
        // door nobody was watching.
        constexpr qint64 kNotPaintedGapMs = 250;
        if (state.lastFoldMs >= 0 && nowMs - state.lastFoldMs > kNotPaintedGapMs) {
            state.timeOffsetSec += static_cast<float>(nowMs - state.lastFoldMs) / 1000.0f;
        }
        state.pausedAtSec = sharedNow;
    }
    // While paused this is pinned to the instant the pause began, so the folds that still
    // have to run (the window's own content damaged, its focus ramp moving) reproduce the
    // frame it froze on instead of drifting.
    plan.foldTime = (plan.mayAnimate ? sharedNow : state.pausedAtSec) - state.timeOffsetSec;
    // A transition supplies its own restore shader and drives the window's geometry
    // frame by frame; don't trust a cached capture across it.
    //
    // NOT gated on foldablePacks. A chain in which every pack failed to compile folds
    // nothing — the capture goes straight into compositeTex[0] and is presented from
    // there — and its capture is as reusable as any other, because the window's own
    // damage is the only thing that can change it. Requiring a foldable pack here meant
    // such a window re-ran the full effects->drawWindow() re-entry, the single most
    // expensive step of the fold, on EVERY paint of any window overlapping it, forever,
    // to reproduce a composite identical to the undecorated window.
    plan.captureCacheable = !inTransition;
    if (!plan.captureCacheable) {
        state.captureValid = false;
    }

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
        if (draws && packVariesPerFrame(*pk, plan.mayAnimate)) {
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
    plan.foldCursor = chainReadsCursor ? foldCursorFor(w, state.canvasGeo, plan.mayAnimate) : kCursorOutside;
    const bool focusedNow = KWin::effects && w == KWin::effects->activeWindow();
    plan.foldFocus = chainReadsFocus ? advanceFocusFade(windowId, focusedNow) : 0.0f;
    plan.foldOpacity = static_cast<float>(resolvedWindowOpacity(w, deco));
    // Explicit epsilon, NOT qFuzzyCompare: both values settle at exactly 0.0, which
    // qFuzzyCompare is documented not to handle (it is a RELATIVE comparison, and
    // against zero its tolerance collapses to zero). It happens to fail SAFE here —
    // a false "moved" only over-invalidates — but relying on that is not a contract.
    // Both are clamped to 0..1, so an absolute epsilon is exactly right.
    constexpr float kFoldStateEpsilon = 0.0001f;
    const auto stateEqual = [](float a, float b) {
        return std::fabs(a - b) <= kFoldStateEpsilon;
    };
    const bool stateMoved = !stateEqual(state.foldedFocus, plan.foldFocus)
        || !stateEqual(state.foldedOpacity, plan.foldOpacity) || plan.foldCursor != state.foldedCursor;

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
    bool usePrefix = plan.captureCacheable && staticFoldable > 0 && staticFoldable < foldablePacks;
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
    plan.allStatic = plan.captureCacheable && foldablePacks > 0 && staticFoldable == foldablePacks;
    // Both caches sit downstream of the capture and of the folded state.
    if (!state.captureValid || stateMoved) {
        state.prefixValid = false;
        state.compositeValid = false;
    }
    if (!usePrefix || state.prefixPackCount != staticPrefix) {
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
