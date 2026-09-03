// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "striptransitionmanager.h"

#include "compositor/stripviewanimator.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "plasmazoneseffect/shader_internal.h"
#include "shadertransitionmanager.h"
#include "transitionpasshelpers.h"

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/ProfilePaths.h>

#include <core/output.h>
#include <core/region.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/gltexture.h>

#include <scene/itemrenderer.h>
#include <scene/windowitem.h>
#include <scene/workspacescene.h>

#include <QPoint>
#include <QRectF>
#include <QScopeGuard>
#include <QSize>
#include <QVector2D>

#include <cmath>

// The drive part of StripTransitionManager: arm from the tiling batch path,
// capture the live scene, run the pack over it. The pack-source → GLShader
// assembly lives in striptransitionshader.cpp; the GL helpers shared with the
// desktop pass in transitionpasshelpers.cpp. Teardown is small enough (no
// fullscreen claim, no endpoint caches — liveness belongs to the view spring)
// that it lives here rather than in a fourth TU.
namespace PlasmaZones {

StripTransitionManager::StripTransitionManager(PlasmaZonesEffect* effect)
    : m_effect(effect)
{
}

StripTransitionManager::~StripTransitionManager()
{
    // GL resources are released by their unique_ptrs. Do NOT touch
    // KWin::effects here — teardown ordering during plugin unload is not
    // guaranteed. reset() is the explicit-cleanup path while the compositor
    // is live.
}

void StripTransitionManager::notifyLeg(KWin::LogicalOutput* output, const QString& effectId, const QVariantMap& params,
                                       int viewDelta, PhosphorProtocol::ScrollAxis axis)
{
    if (!output) {
        return;
    }
    // Empty id — or a pack that is uninstalled / not strip-classed — disarms
    // the output. The erase contract matters as much as the arm: the batch
    // path calls notifyLeg on every seeded leg, so clearing the pack (or
    // disabling animations) mid-flight tears the pass down on the very next
    // wheel tick rather than stranding it until the spring settles.
    bool runnable = !effectId.isEmpty();
    PhosphorAnimationShaders::AnimationShaderEffect eff;
    if (runnable) {
        eff = m_effect->m_shaderManager.shaderRegistry().effect(effectId);
        // Contract-validate against the leaf, exactly as the desktop pass
        // does in prepareTransitionPrototype: a stale override naming a
        // non-strip pack must fall through to the plain translation, not
        // install a pass whose sampler contract the pack never binds.
        if (!eff.isValid()
            || !PhosphorAnimationShaders::shaderEffectAppliesToEventPath(
                eff, PhosphorAnimation::ProfilePaths::ScrollingView)) {
            runnable = false;
        }
    }
    auto it = m_active.find(output);
    if (!runnable) {
        if (it != m_active.end()) {
            // Damage BEFORE the erase when the pass was still presenting:
            // paintOutput replaces the whole output with the decorated
            // capture while a pass holds, and holdsAfterSettle keeps it
            // holding after the spring settles — an erase with no repaint
            // then leaves that capture on the un-damaged regions of the last
            // presented frame until unrelated damage arrives. The immediate
            // (heartbeat) path hits this arm on every tick with no live
            // spring, which is exactly the settled-spring-open-fade shape.
            const bool wasPresenting = m_effect->m_stripViewAnimator->isAnimatingOn(output)
                || it->second.motion.holdsAfterSettle(ShaderInternal::shaderClockNowMs());
            // notifyLeg fires from the D-Bus batch path, off the paint
            // thread; the erase frees the entry's capture texture.
            ensureGlContextCurrent();
            m_active.erase(it);
            if (wasPresenting && KWin::effects) {
                KWin::effects->addRepaint(output->geometry());
            }
        }
        return;
    }
    // This runs BEFORE the batch's applyBatchDelta (the caller's ordering
    // contract, see the header), so "spring not live" here means a FRESH
    // leg is about to start rather than a retarget of a running one — and
    // axisFor() still reports the axis the spring was built on, which is the
    // only moment the two can be compared.
    const bool springLive = m_effect->m_stripViewAnimator->isAnimatingOn(output);
    // An axis FLIP is a fresh leg, not a retarget: applyBatchDelta is about to
    // cancel the spring and zero its accumulator rather than retarget it, so
    // the sampler's baseline (measured along the old axis) has nothing left to
    // be compensated against and the next sample would read the discontinuity
    // as velocity.
    const bool axisFlipped = axis != m_effect->m_stripViewAnimator->axisFor(output);
    if (it == m_active.end()) {
        OutputStripPass pass;
        pass.effectId = effectId;
        TransitionPass::translatePackParams(eff, params, pass.customParams, pass.customColors);
        m_active.emplace(output, std::move(pass));
        return;
    }
    OutputStripPass& pass = it->second;
    // holdsAfterSettle keeps an OPEN settle fade out of the reset arm: a
    // wheel notch landing just after the spring settles reaches here with
    // springLive false (this runs before the batch's applyBatchDelta starts
    // the new spring), and resetting then swallowed the fade with a hard
    // velocity cut — the exact mid-fade restart sampleLive documents as
    // resuming seamlessly. The retarget arm's baseline compensation is right
    // for this case too: the batch steps the committed value with no time
    // passing either way.
    const bool settleFadeOpen = pass.motion.holdsAfterSettle(ShaderInternal::shaderClockNowMs());
    if ((!springLive && !settleFadeOpen) || axisFlipped || pass.effectId != effectId) {
        // Fresh leg on a stale armed entry (spring cleared outside the
        // paint bracket: animations toggled, teardown races), an AXIS FLIP,
        // or a pack SWAP mid-flight — in every case the new pass must not
        // inherit the old one's clock, frame count or velocity. The capture
        // texture stays (size/format revalidation owns its lifetime).
        pass.motion.reset();
        pass.frameCount = 0;
    } else {
        // RETARGET: the batch is about to step the spring's committed value
        // by viewDelta with no time passing. Shift the sampler's offset
        // baseline by the same amount so the finite difference sees only
        // spring motion — without this every wheel notch spiked the
        // velocity by the column width over one frame.
        pass.motion.compensateBatchJump(qreal(viewDelta));
    }
    // Params re-translated either way so a settings change mid-scroll
    // applies on the next frame.
    pass.effectId = effectId;
    TransitionPass::translatePackParams(eff, params, pass.customParams, pass.customColors);
}

bool StripTransitionManager::isRunning() const
{
    const qint64 nowMs = ShaderInternal::shaderClockNowMs();
    for (const auto& entry : m_active) {
        if (m_effect->m_stripViewAnimator->isAnimatingOn(entry.first) || entry.second.motion.holdsAfterSettle(nowMs)) {
            return true;
        }
    }
    return false;
}

bool StripTransitionManager::isRunningForOutput(KWin::LogicalOutput* screen) const
{
    // Liveness is the SPRING's, extended by the settle fade: an armed output
    // whose leg has settled keeps painting only while the fade is open, and
    // reapSettled frees its entry once it closes. The spring is also how
    // every kill path folds in — animations disabled, the animator's clock
    // reap, forgetOutput — with the fade bounding how long a killed leg can
    // linger (kSettleFadeMaxMs after its last painted frame).
    const auto it = m_active.find(screen);
    if (it == m_active.end()) {
        return false;
    }
    return m_effect->m_stripViewAnimator->isAnimatingOn(screen)
        || it->second.motion.holdsAfterSettle(ShaderInternal::shaderClockNowMs());
}

bool StripTransitionManager::paintOutput(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                                         int mask, const KWin::Region& deviceRegion, KWin::LogicalOutput* screen)
{
    // As on the desktop pass: the damage region does not participate —
    // prePaintScreen sets PAINT_SCREEN_TRANSFORMED for a running output and
    // the spring's repaint callback damages the full output every frame.
    Q_UNUSED(deviceRegion)
    if (!screen) {
        return false;
    }
    auto it = m_active.find(screen);
    if (it == m_active.end()) {
        return false;
    }
    // POINTER, not a reference, and re-seated after the capture below: the
    // capture re-enters the scene through KWin::effects->paintScreen, and
    // nothing may hold an m_active reference across that. No mutator of
    // m_active is reachable from inside the nested walk today (the walk only
    // re-enters our paintWindow), so this is structural rather than a live
    // fix — but every other `it`/`pass` lifetime in this function is spelled
    // out for the same reason, and an invalidation here would be a
    // use-after-free in the paint path.
    OutputStripPass* pass = &it->second;
    // Pinned per-pass clock (one timestamp per output pass; re-sampling per
    // call is the multi-pass ghosting trap the pin exists for). -1 means a
    // caller outside any bracket, which cannot happen from paintScreen, but
    // fall back rather than compare time against a sentinel.
    qint64 nowMs = m_effect->m_shaderManager.currentFrameClockMs();
    if (nowMs < 0) {
        nowMs = ShaderInternal::shaderClockNowMs();
    }
    const bool springLive = m_effect->m_stripViewAnimator->isAnimatingOn(screen);
    if (!springLive && !pass->motion.holdsAfterSettle(nowMs)) {
        // Settled with the fade closed (or killed while idle) — fall
        // through to the normal scene THIS frame; reapSettled frees the
        // entry from postPaintScreen.
        return false;
    }

    // Compile BEFORE capturing, for the desktop pass's reason: a capture
    // renders the entire scene, so doing it first would burn a full-screen
    // pass only to throw it away when the shader turns out not to compile —
    // and the failure sentinel stops re-COMPILES, not re-captures, so that
    // waste would repeat every frame for the rest of the leg.
    // Unlike `it`/`pass`, `cs` survives the nested paintScreen below: the
    // shader cache is a std::unordered_map, whose references and pointers stay
    // valid across inserts and rehashes, and nothing erases from it inside the
    // walk.
    CompiledStripShader* cs = compiledShader(pass->effectId);
    if (!cs || !cs->shader) {
        // Compile failed — abandon the pass rather than paint a black
        // output; the normal scene (the plain translation) paints instead,
        // this frame and every later one (the sentinel keeps notifyLeg's
        // re-arms pointless but harmless: we end here again before any
        // capture). `it` and `pass` are DEAD after endOutput — return
        // immediately, do not add reads between these two lines.
        endOutput(screen);
        return false;
    }

    // Keep KWin's own cursor out of the capture below (see the header): the
    // scene walk paints the overlay item last, and a software cursor drawn
    // into uStrip is smeared by the pack and never redrawn sharp, since this
    // pass replaces the output's paint. Hidden here, blitted by drawCursor
    // at the tail.
    hideCursorForPass(screen);

    // Ensure the persistent capture target, revalidated against this frame's
    // device size and on-screen format (output scale/mode change, HDR flip).
    const QSize deviceSize = viewport.deviceSize();
    const GLenum captureFormat = TransitionPass::captureFormatFor(renderTarget);
    if (pass->captureTex
        && (pass->captureTex->size() != deviceSize || pass->captureTex->internalFormat() != captureFormat)) {
        pass->captureFbo.reset();
        pass->captureTex.reset();
    }
    if (!pass->captureTex) {
        pass->captureTex = TransitionPass::allocateOutputTexture(deviceSize, captureFormat);
        if (pass->captureTex) {
            pass->captureFbo = std::make_unique<KWin::GLFramebuffer>(pass->captureTex.get());
            if (!pass->captureFbo->valid()) {
                pass->captureFbo.reset();
                pass->captureTex.reset();
            }
        }
        if (!pass->captureTex) {
            // Allocation failed — abandon this LEG rather than retry per
            // frame. A later wheel batch re-arms via notifyLeg and retries
            // the allocation once per batch, which is the wanted behaviour
            // for a transient failure and bounded for a persistent one.
            // `it`/`pass` are DEAD after endOutput — return immediately.
            endOutput(screen);
            return false;
        }
    }

    // Render the live scene into the capture. This is the downstream chain
    // call (effects below us + the scene), NOT a re-entry into our own
    // paintScreen — but it DOES re-enter our paintWindow, which is the point:
    // the strip translation, the parked-column relocation and the tab-pill
    // blit all apply inside the capture, so the pack decorates exactly the
    // frame the user would otherwise have seen. The
    // pass bracket's m_currentPassOutput is still this output, so the
    // foreign-output cull behaves identically to the on-screen path.
    {
        const ShaderInternal::ScopedGlState glStateGuard;
        KWin::RenderTarget captureTarget(pass->captureFbo.get(), renderTarget.colorDescription());
        KWin::RenderViewport captureViewport(screen->geometryF(), screen->scale(), captureTarget, QPoint());
        KWin::GLFramebuffer::pushFramebuffer(pass->captureFbo.get());
        // Scope-guarded for the same throw path the two latch guards below
        // exist for: a throw inside the scene walk must not leave KWin's
        // framebuffer stack pushed. Declared first, so it pops LAST, after
        // those guards have cleared their state.
        const auto popCaptureFbo = qScopeGuard([] {
            KWin::GLFramebuffer::popFramebuffer();
        });
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        // Exclude everything stacked ABOVE the strip from the capture (OSDs,
        // notifications, floating windows, panels, daemon overlays): the
        // pack must not smear surfaces that are not scrolling. "Above" is a
        // STACKING fact, not a role: the boundary is the topmost strip
        // COLUMN managed by this output in KWin's stacking order, and only
        // windows above that boundary are excluded. The tab pills need no
        // clause of their own: they are a blit at the anchor's slot rather
        // than a window in the stacking order, so they land inside the
        // capture and ride the strip with the columns they label.
        // A floating window stacked BELOW a raised
        // column stays in the capture and is smeared with it, which is what
        // its stacking says should happen; a closing or dragged column that
        // is genuinely below the topmost live column likewise stays inside.
        // Windows whose expanded geometry does not intersect this output are
        // never excluded — they contribute nothing to the capture (the
        // viewport clips them) and excluding them would composite them onto
        // the wrong output at full decoration-fold cost every frame.
        //
        // paintWindow records the excluded set in paint order while the
        // latch is set, and the tail below composites exactly that set
        // sharp on top of the shader output. Both the latch and the two
        // containers are scope-guarded so a throw inside the scene walk can
        // neither leak the latch into live painting nor strand recorded
        // EffectWindow pointers past the frame.
        m_effect->m_stripCaptureAboveStrip.clear();
        {
            const QList<KWin::EffectWindow*> stack = KWin::effects->stackingOrder();
            int topStrip = -1;
            int topStripParked = -1;
            for (int i = 0; i < stack.size(); ++i) {
                KWin::EffectWindow* sw = stack.at(i);
                if (!sw) {
                    continue;
                }
                // The five-state paintability filter the tab-anchor election
                // (paint_pipeline prePaintScreen) applies, for the same
                // reason: KWin plainly will not draw these, so a boundary
                // elected on one never paints in this capture, and everything
                // stacked between it and the topmost PAINTING column would be
                // captured and smeared instead of composited sharp. The
                // reachable case is a desktop switch: off-desktop columns
                // stay tiled-tracked (#808), unparked, and can sit above the
                // current desktop's strip in the stacking order — the next
                // scroll then smeared every above-strip window. isDeleted()
                // first also keeps getWindowId off a corpse.
                // Activity is the other half of "on the current workspace":
                // scrollManagedOutputFor applies neither term, so an
                // off-activity column stays scroll-managed exactly like an
                // off-desktop one. Same boundary as the pill anchor election
                // in paint_pipeline.cpp, which drops only the isDeleted term
                // (its above-anchor set must admit grab-held corpses; the
                // anchor outcome is identical either way because
                // scrollManagedOutputFor rejects deleted windows).
                if (sw->isDeleted() || sw->isMinimized() || sw->isHidden() || sw->isHiddenByShowDesktop()
                    || !sw->isOnCurrentDesktop() || !sw->isOnCurrentActivity()) {
                    continue;
                }
                if (m_effect->scrollManagedOutputFor(sw) != screen) {
                    continue;
                }
                // Parked columns do not win the election, the same way the
                // tab-anchor election in prePaintScreen skips them and for the
                // same reason: paintWindow culls a parked column outright, so
                // one elected here would be a boundary drawn from a window that
                // never paints in this capture. Everything stacked below it but
                // above the topmost PAINTING column would then fall outside
                // m_stripCaptureAboveStrip and be captured into the strip pass,
                // moving with the columns instead of staying composited sharp
                // on top.
                //
                // They are still REMEMBERED, because "no unparked member" is
                // not the same as "no strip". With every column on this output
                // parked, an unconditional skip leaves topStrip at -1 and the
                // whole scene — panels, OSDs, daemon overlays — gets captured
                // and post-processed by the pack, which is strictly worse than
                // the boundary this election used to find. Falling back to the
                // topmost parked member reverts that case to the pre-skip
                // behaviour instead.
                if (m_effect->scrollParkedOffscreen(sw, m_effect->getWindowId(sw))) {
                    topStripParked = i;
                    continue;
                }
                topStrip = i;
            }
            if (topStrip < 0) {
                topStrip = topStripParked;
            }
            if (topStrip >= 0) {
                const QRectF outputGeoF = screen->geometryF();
                for (int i = topStrip + 1; i < stack.size(); ++i) {
                    KWin::EffectWindow* sw = stack.at(i);
                    if (!sw || !QRectF(sw->expandedGeometry()).intersects(outputGeoF)) {
                        continue;
                    }
                    m_effect->m_stripCaptureAboveStrip.insert(sw);
                }
            }
            // topStrip < 0 (no strip member found — a mode teardown race)
            // leaves the set empty: the whole scene is captured and the pack
            // decorates everything, which is the safe degenerate.
        }
        m_effect->m_stripCaptureSkippedWindows.clear();
        m_effect->m_stripCaptureExclusionOutput = screen;
        // The latch and the above-strip set are consumed only INSIDE the
        // capture, so their guard clears unconditionally. The recorded list
        // is consumed by the composite tail AFTER this scope closes, so its
        // guard is dismissed on the normal path and fires only when the
        // scene walk throws — which is what keeps the header's
        // entries-never-outlive-the-frame contract true on the unwind path.
        const auto exclusionGuard = qScopeGuard([this] {
            m_effect->m_stripCaptureExclusionOutput = nullptr;
            m_effect->m_stripCaptureAboveStrip.clear();
        });
        auto skippedUnwindGuard = qScopeGuard([this] {
            m_effect->m_stripCaptureSkippedWindows.clear();
        });
        // Device-space region rooted at (0, 0) — the FBO's own space, not the
        // output-positioned logical geometry; see captureLiveScene's note.
        // The pill blit inside this walk clips to the walk's own (full-FBO)
        // region. The effect's painted latch is deliberately NOT reset here,
        // unlike the desktop captures: this capture IS the presented frame
        // (paintOutput returns true), so its one blit is the pass's one blit.
        {
            const KWin::Region walkRegion(KWin::Rect(QPoint(), captureViewport.deviceSize()));
            const PlasmaZonesEffect::ScrollTabWalkScope walkScope(*m_effect, walkRegion,
                                                                  /*resetPaintedLatch=*/false);
            KWin::effects->paintScreen(captureTarget, captureViewport, mask, walkRegion, screen);
        }
        skippedUnwindGuard.dismiss(); // normal exit: the composite tail consumes the list
    }

    // Re-seat across the nested scene walk. `pass` was obtained before
    // paintScreen re-entered the scene, so re-find it by key rather than
    // trusting the old address; the tail below reads the sampler, the frame
    // counter and the capture texture through it.
    it = m_active.find(screen);
    if (it == m_active.end()) {
        // Unreachable today (nothing inside the walk mutates m_active), but if
        // it ever happens the recorded list must not outlive the frame — the
        // unwind guard was dismissed on the assumption the tail consumes it,
        // and that tail is now skipped. No framebuffer has been pushed yet at
        // this point, so there is nothing else to unwind.
        m_effect->m_stripCaptureSkippedWindows.clear();
        return false;
    }
    pass = &it->second;

    // Motion sample, on the pinned per-pass clock captured at the top of
    // this function. Live legs run the finite-difference estimator; a leg
    // whose spring has settled runs the settle fade instead — the frozen
    // last live velocity decaying to zero so velocity-driven packs land
    // without a pop — and self-pumps its remaining frames (the spring's own
    // repaint pump died with it).
    // The shader pass stays ONE-DIMENSIONAL on purpose, so it takes the signed
    // scalar along the strip's own axis rather than the resolved point. Which
    // way that axis points reaches the shader as a separate uniform.
    const qreal offsetLogical = m_effect->m_stripViewAnimator->offsetAlongAxis(screen);
    const qreal velocityLogical =
        springLive ? pass->motion.sampleLive(offsetLogical, nowMs) : pass->motion.sampleSettleFade(nowMs);
    if (!springLive) {
        KWin::effects->addRepaint(screen->geometry());
    }

    const qreal scale = screen->scale();
    const float offsetDevice = float(offsetLogical * scale);
    const float velocityDevice = float(velocityLogical * scale);
    // Normalized by the output's extent ALONG THE STRIP AXIS, not always its
    // width. iStripMotion .z/.w are documented as "offset/velocity as a
    // fraction of the output", and on a vertical strip that fraction is of the
    // height — dividing by width there would hand a pack a figure scaled by
    // the aspect ratio, so a tuned displacement would be visibly wrong rather
    // than merely rotated.
    // Taken from the animator, which already holds the axis per OUTPUT and is
    // the same source the paint translation uses. Going through a screen id
    // would need a reverse lookup the effect's map does not provide, and would
    // be a second source of one fact.
    const bool vertical = m_effect->m_stripViewAnimator->axisFor(screen) == PhosphorProtocol::ScrollAxis::Vertical;
    const float deviceAlong = vertical ? float(deviceSize.height() > 0 ? deviceSize.height() : 1)
                                       : float(deviceSize.width() > 0 ? deviceSize.width() : 1);

    // The strip's work area (panels/docks excluded), output-local device px.
    // clientArea(MaximizeArea) is the same panel-excluded rect the daemon's
    // available-geometry report uses (screenchangehandler.cpp). A degenerate
    // rect uploads as zeros, which strip_transition.glsl's stripMask treats
    // as "mask nothing". Kept as a FLOAT rect end to end (no toRect round
    // trip) so a fractional-scale strut boundary is not off by a device
    // pixel. Resolved once per painted frame; the rect only changes on a
    // strut/geometry change, but caching it against those signals would buy
    // one virtual call per frame at the cost of an invalidation wire —
    // resolve-per-frame is the documented trade.
    QVector4D stripRect;
    const QRectF workArea = KWin::effects->clientArea(KWin::MaximizeArea, screen); // RectF converts implicitly
    const QRectF outputGeo = screen->geometryF();
    if (!workArea.isEmpty()) {
        const QRectF local = workArea.translated(-outputGeo.topLeft());
        stripRect = QVector4D(float(local.x() * scale), float(local.y() * scale), float(local.width() * scale),
                              float(local.height() * scale));
    }

    const ShaderInternal::ScopedGlState glStateGuard;
    // Draw into the framebuffer KWin handed us, sized to that target — same
    // shape as the desktop blend's tail (see its comments for the
    // rotated-output and HDR-intermediate reasoning).
    KWin::GLFramebuffer* const targetFb = renderTarget.framebuffer();
    if (targetFb) {
        KWin::GLFramebuffer::pushFramebuffer(targetFb);
    }
    const auto popTargetFb = qScopeGuard([targetFb] {
        if (targetFb) {
            KWin::GLFramebuffer::popFramebuffer();
        }
    });
    // pushFramebuffer already set this viewport for the targetFb branch; the
    // explicit call is load-bearing only on the no-framebuffer fallback (no
    // push happened, the capture pass's viewport is still current). Kept
    // unconditional so both branches leave identical state — same shape and
    // reasoning as the desktop blend tail. Full-target at (0,0) matches what
    // pushFramebuffer itself does; backends expressing a render offset do so
    // through viewport.projectionMatrix(), which the quad draw uses.
    const QSize targetSize = targetFb ? targetFb->size() : deviceSize;
    glViewport(0, 0, targetSize.width(), targetSize.height());
    glDisable(GL_BLEND); // the decorated scene is itself opaque — replace the output

    {
        KWin::ShaderBinder binder(cs->shader.get());
        cs->shader->setUniform(KWin::GLShader::Mat4Uniform::ModelViewProjectionMatrix, viewport.projectionMatrix());
        if (cs->iTimeLoc >= 0) {
            // SECONDS of PAINTED time this pass has run — monotonic, never
            // rewinding on a retarget, and a suspension gap (desktop
            // transition, DPMS) adds nothing, so a time-driven pack resumes
            // where it left off instead of leaping by the gap. NOT
            // progress; see strip_transition.glsl.
            cs->shader->setUniform(cs->iTimeLoc, float(qreal(pass->motion.timeAccumMs) / 1000.0));
        }
        if (cs->iResolutionLoc >= 0) {
            // The CAPTURE's size (viewport.deviceSize()), deliberately not
            // targetFb->size(): the pack's uv arithmetic runs in uStrip's
            // texture space, so iResolution must describe that texture. A
            // differently-sized on-screen target (an HDR intermediate, a
            // rotated output) is bridged by the projection matrix on the
            // quad, not by iResolution.
            cs->shader->setUniform(cs->iResolutionLoc,
                                   QVector2D(float(deviceSize.width()), float(deviceSize.height())));
        }
        if (cs->iFrameLoc >= 0) {
            cs->shader->setUniform(cs->iFrameLoc, pass->frameCount);
        }
        ++pass->frameCount;
        if (cs->iStripMotionLoc >= 0) {
            cs->shader->setUniform(
                cs->iStripMotionLoc,
                QVector4D(offsetDevice, velocityDevice, offsetDevice / deviceAlong, velocityDevice / deviceAlong));
        }
        if (cs->iStripAxisLoc >= 0) {
            // Unit vector along this output's travel axis. The motion lanes
            // above are scalars ALONG it, so a pack needs both to displace
            // correctly — and the normalization divisor below follows the
            // same axis for the same reason.
            cs->shader->setUniform(cs->iStripAxisLoc, vertical ? QVector2D(0.0F, 1.0F) : QVector2D(1.0F, 0.0F));
        }
        if (cs->iStripRectLoc >= 0) {
            cs->shader->setUniform(cs->iStripRectLoc, stripRect);
        }
        for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams; ++slot) {
            if (cs->customParamsLoc[slot] >= 0) {
                cs->shader->setUniform(cs->customParamsLoc[slot], pass->customParams[slot]);
            }
        }
        for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors; ++slot) {
            if (cs->customColorsLoc[slot] >= 0) {
                cs->shader->setUniform(cs->customColorsLoc[slot], pass->customColors[slot]);
            }
        }
        if (cs->uStripLoc >= 0) {
            cs->shader->setUniform(cs->uStripLoc, 0);
            glActiveTexture(GL_TEXTURE0);
            pass->captureTex->bind();
        }

        TransitionPass::drawOutputQuad(viewport);

        // Unbind the capture unit: ScopedGlState restores the active-unit
        // ENUM, not the BINDINGS, and a name still bound when a reap later
        // deletes it survives as a dangling reference — the exact hole the
        // desktop pass documents at its own unbind.
        if (cs->uStripLoc >= 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    } // binder out of scope before the composite binds its own shaders

    // Composite the windows the capture excluded — exactly the set recorded
    // during the exclusion latch, in the same bottom-to-top paint order —
    // sharp on top of the shader output. Same direct-drive shape as the
    // desktop pass's compositeWindowsInto: drive each window through OUR OWN
    // paintWindow so decorations, rule opacity and in-flight per-window
    // transitions all apply, with m_directPaintCapture terminating its tail
    // in a raw draw (the chain iterator is at begin() here). These windows
    // DID get prePaintWindow this frame — they were in the scene walk and
    // skipped only at paint — so no state is missing.
    //
    // Swap, not move-assign: a moved-from QList drops its capacity, so the
    // record site would malloc from scratch on every frame of the leg. The
    // swap hands the allocation back and forth between the member and this
    // local instead.
    QList<KWin::EffectWindow*> aboveStrip;
    aboveStrip.swap(m_effect->m_stripCaptureSkippedWindows);
    if (!aboveStrip.isEmpty()) {
        // Re-establish blending BEFORE the composite. The quad tail above
        // ran with GL_BLEND disabled, and nothing in OUR draw paths (the
        // decoration present, the transition draw) enables it — they
        // inherit ambient state, and the desktop pass's composites all run
        // BEFORE its own disable, so this pass is the only one that
        // composites windows downstream of a glDisable(GL_BLEND). Without
        // this, a translucent OSD or notification would render fully
        // opaque for the whole leg. Premultiplied-alpha func, KWin's
        // convention; ScopedGlState restores the previous state at scope
        // end.
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        m_effect->m_directPaintCapture = true;
        const auto directPaintGuard = qScopeGuard([this] {
            m_effect->m_directPaintCapture = false;
        });
        for (KWin::EffectWindow* w : std::as_const(aboveStrip)) {
            // ItemEffect is a QPointer plus an effect-reference count on the
            // item (scene/item.h) — per-frame construction is noise next to
            // the window paint it brackets, so no renderability pre-check is
            // worth its complexity. These windows are normally visible, but
            // the ref is kept for the one that stops being so mid-leg (a
            // notification starting its close) while still in our list.
            KWin::ItemEffect keepRenderable(w->windowItem());
            KWin::WindowPaintData aboveData;
            // The window's OWN opacity, not 1.0: unlike the desktop pass's
            // composite (which reconstructs windows into a static capture),
            // this draws live on screen every frame, so a client with
            // per-window opacity or a notification mid-fade must keep it.
            aboveData.setOpacity(w->opacity());
            const int aboveMask = KWin::Effect::PAINT_WINDOW_TRANSFORMED | KWin::Effect::PAINT_WINDOW_TRANSLUCENT;
            // infinite() rather than the injected-paint deviceRegion clip: the
            // shader quad already repainted the whole output, so this pass
            // damages full-output anyway and every pixel of these windows has
            // to be redrawn over it. A region clip would only risk dropping
            // parts the scene's own damage never listed.
            m_effect->paintWindow(renderTarget, viewport, w, aboveMask, KWin::Region::infinite(), aboveData);
        }
    }
    aboveStrip.clear();
    aboveStrip.swap(m_effect->m_stripCaptureSkippedWindows); // return the allocation for the next frame
    // Last draw of the pass: the cursor, above everything, where KWin's own
    // overlay item would have put it had this pass not replaced the paint.
    if (m_cursorHidden && cursorOnOutput(screen)) {
        drawCursor(renderTarget, viewport);
    }
    return true;
}

bool StripTransitionManager::cursorOnOutput(KWin::LogicalOutput* screen) const
{
    return screen && KWin::effects && screen->geometryF().contains(KWin::effects->cursorPos());
}

void StripTransitionManager::hideCursorForPass(KWin::LogicalOutput* screen)
{
    if (m_cursorHidden || !KWin::effects || !cursorOnOutput(screen)) {
        return;
    }
    // Another effect (zoom, a screen-edge peek) already owns the hidden
    // state; drawing our own copy would resurrect a cursor it wanted gone.
    if (KWin::effects->isCursorHidden()) {
        return;
    }
    KWin::effects->hideCursor();
    m_cursorHidden = true;
}

void StripTransitionManager::updateCursorHiding()
{
    if (!m_cursorHidden) {
        return;
    }
    for (const auto& entry : m_active) {
        if (cursorOnOutput(entry.first)) {
            return; // a live pass still paints the cursor itself
        }
    }
    if (KWin::effects) {
        KWin::effects->showCursor();
    }
    m_cursorHidden = false;
}

void StripTransitionManager::drawCursor(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport)
{
    // The workspace scene is reached through any window item: the effects
    // API exposes no scene accessor, and Item::scene() on a member of the
    // scene IS the workspace scene. An empty stacking order means no strip
    // either, so there is nothing to draw the cursor over.
    const QList<KWin::EffectWindow*> stack = KWin::effects->stackingOrder();
    KWin::WorkspaceScene* scene = nullptr;
    for (KWin::EffectWindow* w : stack) {
        if (w && w->windowItem()) {
            scene = qobject_cast<KWin::WorkspaceScene*>(w->windowItem()->scene());
            break;
        }
    }
    if (!scene || !scene->cursorItem()) {
        return;
    }
    // WorkspaceScene::updateCursor only moves the item while the cursor is
    // shown; hidden, its position is whatever the pointer was at when the
    // hide landed. Track the live pointer the way that slot does (the item's
    // own hotspot offset lives in its child, so the position IS the pointer).
    scene->cursorItem()->setPosition(KWin::effects->cursorPos());
    // Same call paintGenericScreen makes for the overlay item, with the same
    // viewport, so the cursor lands exactly where the un-passed frame would
    // have put it, at the item's own scale, for a theme sprite and a
    // client-provided surface alike. Rendered as the ROOT of the call, which
    // is what makes the hidden item drawable: the renderer honours
    // explicitVisible on children only. No colour-space handling of our own,
    // the renderer's item path carries it. Hand GL state back as found, as
    // every draw in this tail does.
    const ShaderInternal::ScopedGlState glStateGuard;
    scene->renderer()->renderItem(renderTarget, viewport, scene->cursorItem(), KWin::Effect::PAINT_SCREEN_TRANSFORMED,
                                  KWin::Region::infinite(), KWin::WindowPaintData{}, {}, {});
}

void StripTransitionManager::reapSettled()
{
    bool contextEnsured = false;
    // LIVE clock, while paintOutput samples the frame-pinned one — a known,
    // accepted skew (isRunning / isRunningForOutput read live too). The gap
    // is sub-millisecond, and the worst it can do is reap a fade whose final
    // frame the pinned clock would still have painted: one truncated fade
    // frame, cosmetic. Pinning a clock here would need this postPaintScreen
    // hook threaded into the paint bracket for no visible gain.
    const qint64 nowMs = ShaderInternal::shaderClockNowMs();
    for (auto it = m_active.begin(); it != m_active.end();) {
        if (!m_effect->m_stripViewAnimator->isAnimatingOn(it->first) && !it->second.motion.holdsAfterSettle(nowMs)) {
            // The settle frame itself needed no repaint — paintOutput
            // returned false and the normal scene painted in that same frame
            // — so this is pure resource hygiene. The erase frees an
            // output-sized GLTexture; postPaintScreen runs on the paint
            // thread, but ensure anyway for parity with the other mutators.
            if (!contextEnsured) {
                ensureGlContextCurrent();
                contextEnsured = true;
            }
            it = m_active.erase(it);
        } else {
            ++it;
        }
    }
    updateCursorHiding();
}

void StripTransitionManager::endOutput(KWin::LogicalOutput* screen)
{
    m_active.erase(screen);
}

void StripTransitionManager::ensureGlContextCurrent()
{
    if (KWin::effects) {
        KWin::effects->makeOpenGLContextCurrent();
    }
}

void StripTransitionManager::invalidateShaderCache()
{
    // Fires from the AnimationShaderRegistry file watcher between frames,
    // where the compositor GL context is NOT current; the GLShaders' frees
    // want one. Teardown (!effects) reclaims them regardless.
    ensureGlContextCurrent();
    m_shaderCache.clear();
}

void StripTransitionManager::outputRemoved(KWin::LogicalOutput* screen)
{
    // A disconnected output must not linger as a key: paintOutput and
    // reapSettled deref it against the spring map. StripViewAnimator's own
    // forgetOutput runs beside this in the effect's screenRemoved handler.
    const auto it = m_active.find(screen);
    if (it == m_active.end()) {
        return;
    }
    // screenRemoved fires off the paint thread; the erase frees the entry's
    // capture texture.
    ensureGlContextCurrent();
    m_active.erase(it);
    updateCursorHiding();
}

void StripTransitionManager::reset()
{
    // Teardown path (compositor reset / plugin unload). Clearing the shader
    // cache HERE — not leaving it for the destructor, which deliberately
    // can't make a context current — is what makes this the real "release GL
    // resources" path.
    ensureGlContextCurrent();
    m_active.clear();
    m_shaderCache.clear();
    updateCursorHiding();
}

} // namespace PlasmaZones
