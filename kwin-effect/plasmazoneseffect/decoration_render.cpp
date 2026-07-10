// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>

#include <epoxy/gl.h>

#include <PhosphorAnimation/AnimationShaderContract.h>

#include "../autotilehandler.h"
#include "../snaphandler.h"
#include "shader_internal.h"
#include "shader_resolve.h"
#include "window_query.h"

#include <PhosphorCompositor/AutotileState.h>
#include <PhosphorCompositor/DecorationDefaults.h>
#include <PhosphorProtocol/WindowTypeEnum.h>
#include <PhosphorRules/RuleAction.h>

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>

#include <QByteArray>
#include <QColor>
#include <QVariantMap>
#include <QVector2D>
#include <QVector4D>

#include <optional>

namespace PlasmaZones {

// ─────────────────────────────────────────────────────────────────────────────
// Surface shader (the window border / rounded-corner pack and any future surface
// pack) — per-pack compile + registry search-path setup live in
// surface_compile.cpp (compiledPack() / ensureSurfaceRegistryPaths()),
// reusing the shared GLSL include / param-preamble
// / #define-PLASMAZONES_KWIN pipeline. The border is the first surface
// shader pack: data/surface/border, loaded via SurfaceShaderRegistry.
// ─────────────────────────────────────────────────────────────────────────────

void PlasmaZonesEffect::reconcileDecorationShader(const QString& windowId, KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }
    auto it = m_windowDecorations.find(windowId);
    // The presence of a WindowDecoration entry IS the "wants border" gate now:
    // updateWindowDecoration only inserts one for a member window whose resolved
    // profile declares a non-empty pack chain (border appearance moved into the
    // pack's own params, so there is no host width/colour to test here).
    const bool wantsBorder = (it != m_windowDecorations.end());

    // An in-flight animation transition owns the shader slot — the transition's
    // begin already called setShader(animationShader) and redirect(). Leave it
    // alone: the transition-end path re-applies the border shader (and keeps the
    // redirect) when the border still exists. Just clear our ownership flag so
    // the per-frame push and removeWindowDecoration defer to the transition.
    if (m_shaderManager.findTransition(w)) {
        if (it != m_windowDecorations.end()) {
            it->shaderApplied = false;
        }
        return;
    }

    if (wantsBorder) {
        // The redirect shader OffscreenData::paint runs to present this window is
        // always the passthrough present shader: it samples the pre-composited
        // final FBO that paintWindow's renderSurfaceChainComposite produced (the
        // per-pack mains run as FBO passes there, not via OffscreenData). Every
        // decorated window rides this composite path. Compiles on first use; a
        // null result tears the redirect down rather than leave the window
        // blitting a dead/stale shader forever (a just-ended transition hands the
        // slot back here still redirected). (The old cheap path bound the pack
        // shader directly for single unpadded packs; retired — see drawWindow.)
        KWin::GLShader* const redirectShader = surfacePresentShader();
        if (!redirectShader) {
            // setShader(nullptr)/unredirect are no-ops when the window was never
            // redirected.
            setShader(w, nullptr);
            unredirect(w);
            it->shaderApplied = false;
            return;
        }
        // redirect() is idempotent for an already-redirected window; setShader()
        // replaces any prior pointer. Re-applying the same shader is a no-op.
        redirect(w);
        setShader(w, redirectShader);
        it->shaderApplied = true;
    }
    // No teardown branch here: !wantsBorder means there is no WindowDecoration entry
    // to act on (wantsBorder IS `it != end()`), and border removal always routes
    // through removeWindowDecoration, which clears the shader and unredirects.
}

float PlasmaZonesEffect::advanceFocusFade(const QString& windowId, bool focused)
{
    // Ramp the smoothed focus value toward the hard 0/1 target over
    // m_focusFadeDurationMs (the standalone focusFadeDuration setting) so a
    // focus change fades rather than snaps. Called from pushBorderUniforms
    // only for a pack that reads focus, with @p windowId threaded from the
    // fold so getWindowId(w) is not recomputed per pack.
    // Uses the PINNED per-frame clock: a second call within the same frame (a
    // chain with several focus-reading packs) reads now == lastMs and is an
    // exact no-op, so the ramp advances at most once per frame and the step
    // can never partially double-advance (the live wall clock could straddle a
    // millisecond boundary mid-frame).
    FocusFadeState& fs = m_focusFade[windowId];
    const float target = focused ? 1.0f : 0.0f;
    qint64 now = m_shaderManager.currentFrameClockMs();
    if (now < 0) {
        now = ShaderInternal::shaderClockNowMs();
    }
    // Instant mode: the user set focusFadeDuration to 0, so snap to the hard
    // target with no ramp. windowSurfaceAnimates then never sees an in-flight
    // value and forces no repaints — the switch is immediate.
    if (m_focusFadeDurationMs <= 0) {
        fs.value = target;
        fs.lastMs = now;
        return fs.value;
    }
    if (fs.value < 0.0f) {
        fs.value = target; // first decorate: snap, no fade on appearance
    } else if (fs.lastMs >= 0 && now > fs.lastMs) {
        // Cap the delta: a window resting at 0/1 stops repainting (see
        // windowSurfaceAnimates) so lastMs goes stale, and an uncapped
        // now - lastMs would jump the whole ramp on the first frame after a
        // focus change — the instant-snap bug. A live window's real frame delta
        // is far below the cap, so normal ramps are unaffected. The cap is
        // additionally bounded to half the configured duration so a short
        // duration (≤ 2 × kFocusFadeMaxStepMs) still spans at least two frames
        // instead of completing inside the single 50 ms resume step; the
        // qBound floor of 1 keeps dt non-zero for a 1 ms duration.
        const qint64 maxStep = qBound(qint64(1), qint64(m_focusFadeDurationMs) / 2, kFocusFadeMaxStepMs);
        const qint64 dt = qMin(now - fs.lastMs, maxStep);
        const float step = static_cast<float>(dt) / static_cast<float>(m_focusFadeDurationMs);
        if (fs.value < target) {
            fs.value = qMin(target, fs.value + step);
        } else if (fs.value > target) {
            fs.value = qMax(target, fs.value - step);
        }
    }
    fs.lastMs = now;
    return fs.value;
}

void PlasmaZonesEffect::pushBorderUniforms(KWin::EffectWindow* w, const WindowDecoration& wb, const QString& packId,
                                           const CompiledSurfacePack& pack, qreal scale, qreal texturePaddingLogical,
                                           const QString& windowId)
{
    // The caller (renderSurfaceChainComposite's per-pack fold) has already
    // resolved @p pack and @p wb, confirmed the border is applied, ruled out a
    // transition owning the slot, and bound pack.shader, so this just computes
    // and writes the uniforms onto the bound program. The border APPEARANCE is
    // not a
    // parameter here — it rides the pack's baked customParams/customColors,
    // pushed below (with @p wb's per-window rule override when set).
    KWin::GLShader* shader = pack.shader.get();

    // The shader evaluates a rounded-rect SDF over the window FRAME to round the
    // corners + draw the outline. It needs the expanded (redirected) texture size
    // for the top-down pixel reconstruction, the frame rect placed within that
    // texture (device px), and the logical-to-device scale so the pack can scale
    // its own logical-px appearance params (border width / corner radius).
    // windowExpandedSize is the redirected FBO extent in device px; expandedGeometry
    // covers frame + drop shadow (falls back to frame when empty, e.g. a shadowless
    // window). frameTopLeft = (frameGeometry.topLeft - expandedGeometry.topLeft) *
    // scale is the frame's offset inside that texture (top-down device px), and
    // frameSize = frameGeometry.size * scale its extent.
    const QRectF frame = w->frameGeometry();
    QRectF expanded = w->expandedGeometry();
    if (expanded.isEmpty()) {
        expanded = frame;
    }
    // Padded composite path: the target texture's canvas is the expanded rect
    // inflated by the chain's outer margin (renderSurfaceChainComposite uses
    // the same construction), so the geometry uniforms must describe that
    // padded space. @p texturePaddingLogical is 0 when the chain declares no
    // padding pack.
    if (texturePaddingLogical > 0.0) {
        expanded.adjust(-texturePaddingLogical, -texturePaddingLogical, texturePaddingLogical, texturePaddingLogical);
    }
    const QVector2D windowExpandedSize(static_cast<float>(expanded.width() * scale),
                                       static_cast<float>(expanded.height() * scale));
    const QVector2D frameTopLeft(static_cast<float>((frame.left() - expanded.left()) * scale),
                                 static_cast<float>((frame.top() - expanded.top()) * scale));
    const QVector2D frameSize(static_cast<float>(frame.width() * scale), static_cast<float>(frame.height() * scale));

    // The caller (the composite fold) has the pack shader bound via a
    // KWin::ShaderBinder and draws inside that same scope. setUniform writes
    // to the CURRENTLY BOUND program, so we must NOT bind/unbind here or the
    // uniforms would land on the wrong (or no) program.
    if (pack.uSurfaceSizeLoc >= 0) {
        shader->setUniform(pack.uSurfaceSizeLoc, windowExpandedSize);
    }
    if (pack.uFrameTopLeftLoc >= 0) {
        shader->setUniform(pack.uFrameTopLeftLoc, frameTopLeft);
    }
    if (pack.uFrameSizeLoc >= 0) {
        shader->setUniform(pack.uFrameSizeLoc, frameSize);
    }
    // Logical-to-device scale: the pack multiplies its logical-px params by this.
    if (pack.uScaleLoc >= 0) {
        shader->setUniform(pack.uScaleLoc, static_cast<float>(scale));
    }
    // Focus flag: the pack mixes its active/inactive appearance params on this.
    // SMOOTHED — advanceFocusFade ramps toward the hard 0/1 target so a focus
    // change fades rather than snaps. Advanced ONLY for a pack that actually
    // reads focus (uFocusedLoc >= 0), so a decorated window with no
    // focus-tracking pack never gets an m_focusFade entry and is never dragged
    // into ramp-driven repaints by windowSurfaceAnimates. The pinned per-frame
    // clock inside advanceFocusFade makes repeated same-frame calls (a chain
    // with several focus-reading packs) exact no-ops, so the ramp advances at
    // most once per frame. @p windowId is threaded from the fold so this hot
    // path does not recompute getWindowId(w) per pack.
    if (pack.uFocusedLoc >= 0) {
        const bool focused = KWin::effects && w == KWin::effects->activeWindow();
        shader->setUniform(pack.uFocusedLoc, advanceFocusFade(windowId, focused));
    }
    // Rule-resolved window opacity, for handlesOpacity packs (frost dims its
    // content sample; the present pass skips its final modulation for such
    // chains). Prefer the per-frame cache prePaintWindow refreshed.
    if (pack.uOpacityLoc >= 0) {
        qreal resolved = wb.ruleOpacity;
        if (m_shaderManager.frameOpacityCached(w)) {
            const auto frameOpacity = m_shaderManager.cachedFrameOpacity(w);
            resolved = frameOpacity ? qBound(0.0, *frameOpacity, 1.0) : 1.0;
        }
        shader->setUniform(pack.uOpacityLoc, static_cast<float>(resolved));
    }
    // Continuous time for an animated pack. -1 (static pack, e.g. the border)
    // pushes nothing; postPaintScreen only drives the window to repaint when a
    // pack actually references iTime (windowSurfaceAnimates), so a static
    // decoration neither pays this push nor forces per-frame repaints.
    if (pack.uTimeLoc >= 0) {
        shader->setUniform(pack.uTimeLoc, surfaceShaderTimeSeconds());
    }
    // Cursor position for hover-reactive packs, in the same top-down device-px
    // space as the geometry uniforms above (origin at the padded canvas's
    // top-left); (-1, -1) when the cursor is outside the canvas. .zw is .xy
    // normalized by the canvas size (negative alongside the sentinel),
    // matching the daemon branch's convention. Uses the per-frame cached
    // cursor from prePaintScreen — same rationale as the animation path.
    // Hover packs declare `animated: true` so the vsync repaint loop keeps
    // this fresh; there is no per-cursor-move damage path.
    if (pack.iMouseLoc >= 0) {
        const QPointF cursorGlobal = m_shaderManager.m_cachedCursorGlobal;
        float localX = -1.0f;
        float localY = -1.0f;
        const bool inside = cursorGlobal.x() >= expanded.left() && cursorGlobal.x() < expanded.right()
            && cursorGlobal.y() >= expanded.top() && cursorGlobal.y() < expanded.bottom();
        if (inside) {
            localX = static_cast<float>((cursorGlobal.x() - expanded.left()) * scale);
            localY = static_cast<float>((cursorGlobal.y() - expanded.top()) * scale);
        }
        QVector4D iMouseValue(localX, localY, 0.0f, 0.0f);
        if (windowExpandedSize.x() > 0.0f) {
            iMouseValue.setZ(localX / windowExpandedSize.x());
        }
        if (windowExpandedSize.y() > 0.0f) {
            iMouseValue.setW(localY / windowExpandedSize.y());
        }
        shader->setUniform(pack.iMouseLoc, iMouseValue);
    }
    // Pack-declared parameters (customParams / customColors). Seed from THIS
    // window's resolved values (updateWindowDecoration fills packParamValues from
    // the window's own DecorationProfile), falling back to the compiled pack's
    // baked baseline when the registry couldn't resolve the pack at update
    // time. Only slots the shader actually references resolve to a valid
    // location.
    //
    // Per-window border override: when the base "border" pack is driven by a
    // window rule, push THIS window's rule-resolved width / radius / colours
    // over even the per-window seed. The border pack lays its params out as
    // customParams[0] = (borderWidth, cornerRadius, useSystemAccent, _) and
    // colours as customColors[0]=active / customColors[1]=inactive, matching
    // data/surface/border/metadata.json; the shader selects active vs inactive
    // by uSurfaceFocused (pushed above). useSystemAccent is forced false
    // because the rule colour is already accent-resolved
    // (resolveWindowAppearance maps the accent sentinel to the live accent
    // before it lands here).
    const auto windowVals = wb.packParamValues.constFind(packId);
    auto paramsValues = (windowVals != wb.packParamValues.constEnd()) ? windowVals->params : pack.customParamsValues;
    auto colorsValues = (windowVals != wb.packParamValues.constEnd()) ? windowVals->colors : pack.customColorsValues;
    // Rule override applies ONLY to the rule-owned "border" base pack — the
    // composite fold routes every chain pack through here, and a user pack
    // (e.g. glow) must keep its own slot-0 params.
    if (wb.ruleBorder && packId == wb.basePackId) {
        paramsValues[0] =
            QVector4D(static_cast<float>(wb.ruleBorderWidth), static_cast<float>(wb.ruleBorderRadius), 0.0f, 0.0f);
        const QColor& a = wb.ruleBorderActiveColor;
        const QColor& i = wb.ruleBorderInactiveColor;
        colorsValues[0] = QVector4D(static_cast<float>(a.redF()), static_cast<float>(a.greenF()),
                                    static_cast<float>(a.blueF()), static_cast<float>(a.alphaF()));
        colorsValues[1] = QVector4D(static_cast<float>(i.redF()), static_cast<float>(i.greenF()),
                                    static_cast<float>(i.blueF()), static_cast<float>(i.alphaF()));
    }
    for (int slot = 0; slot < PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomParams; ++slot) {
        if (pack.customParamsLoc[slot] >= 0) {
            shader->setUniform(pack.customParamsLoc[slot], paramsValues[slot]);
        }
    }
    for (int slot = 0; slot < PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomColors; ++slot) {
        if (pack.customColorsLoc[slot] >= 0) {
            shader->setUniform(pack.customColorsLoc[slot], colorsValues[slot]);
        }
    }
}

void PlasmaZonesEffect::drawWindow(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                                   KWin::EffectWindow* w, int mask, const KWin::Region& deviceRegion,
                                   KWin::WindowPaintData& data)
{
    // EVERY decorated window presents through the composite: paintWindow ran
    // the full chain fold (renderSurfaceChainComposite) for it this frame, and
    // this override binds the final composite slot for the present
    // passthrough's blit. One regime for one-pack and many-pack chains alike —
    // the old cheap single-pack OffscreenData path was retired because a
    // window without a rest composite could not carry its decoration through
    // close animations (and every regime split is a class of "decoration
    // missing in instance X" bugs). Skip during a transition (the animation
    // shader owns the setShader slot and paintWindow's transition branch
    // drives it) and during snapshot capture.
    //
    // Texture-unit map: unit 0 is uTexture0 (KWin's OffscreenData::paint binds
    // the redirected surface there); the composite binds at
    // kSurfaceChannelBaseUnit, PAST the animation path's units so the two
    // paths never collide.
    int boundChannels = 0; // # of units we bound (for post-draw cleanup)
    constexpr int kSurfaceChannelBaseUnit = ShaderInternal::kSurfaceChannelBaseUnit;
    if (!m_capturingSnapshot && !m_windowDecorations.isEmpty() && !m_shaderManager.findTransition(w)) {
        const QString wid = getWindowId(w);
        const auto bit = m_windowDecorations.constFind(wid);
        if (bit != m_windowDecorations.constEnd() && bit->shaderApplied) {
            // MULTI-PACK present: the whole chain was already composited into a
            // per-window FBO by paintWindow (renderSurfaceChainComposite). Bind the
            // final slot to a high unit and point the present passthrough's uFinal
            // at it. OffscreenData::paint re-binds the present program (the
            // setShader one from reconcileDecorationShader) for its blit, so the
            // uniform persists; the texture stays bound until the post-draw cleanup.
            KWin::GLShader* const present = surfacePresentShader();
            const auto stateIt = m_surfaceMultipass.find(wid);
            if (present && stateIt != m_surfaceMultipass.end()
                && stateIt->second.compositeTex[stateIt->second.finalSlot]) {
                const int unit = kSurfaceChannelBaseUnit;
                KWin::ShaderBinder binder(present);
                glActiveTexture(GL_TEXTURE0 + unit);
                stateIt->second.compositeTex[stateIt->second.finalSlot]->bind();
                if (m_surfacePresentFinalLoc >= 0) {
                    present->setUniform(m_surfacePresentFinalLoc, unit);
                }
                // Final KWin-style opacity modulation. Pushed EVERY frame —
                // the program is shared across windows, so a stale value from
                // another window would leak. 1.0 when a chain pack handles
                // opacity itself (frost), else the freshest resolved value.
                if (m_surfacePresentOpacityLoc >= 0) {
                    float presentOpacity = 1.0f;
                    if (!bit->chainHandlesOpacity) {
                        qreal resolved = bit->ruleOpacity;
                        if (m_shaderManager.frameOpacityCached(w)) {
                            const auto frameOpacity = m_shaderManager.cachedFrameOpacity(w);
                            resolved = frameOpacity ? qBound(0.0, *frameOpacity, 1.0) : 1.0;
                        }
                        presentOpacity = static_cast<float>(resolved);
                    }
                    present->setUniform(m_surfacePresentOpacityLoc, presentOpacity);
                }
                glActiveTexture(GL_TEXTURE0);
                boundChannels = 1; // unit kSurfaceChannelBaseUnit+0, freed in the cleanup below
            }
        }
    }
    // TRANSITION LAYER REBIND: paintWindow pushed the layer uniforms and
    // bound the composite, but the draw chain between paintWindow and this
    // point (other effects' drawWindow hooks, item re-renders) can and DOES
    // unbind the texture unit — the at-draw probes read an EMPTY unit on
    // every animated frame, which is exactly the "decoration vanishes during
    // animation shaders" symptom (the shader samples an unbound unit and
    // degrades to the raw fallback). Re-bind at the LAST moment before
    // KWin's draw, the same place the rest path's present bind lives — that
    // path never exhibited the loss. Uniform values persist on the program;
    // only the raw unit binding needs re-asserting.
    if (const ShaderTransition* const st = m_shaderManager.findTransition(w); st && st->cached) {
        const auto reIt = m_surfaceMultipass.find(getWindowId(w));
        if (reIt != m_surfaceMultipass.end()) {
            if (KWin::GLTexture* const comp = reIt->second.compositeTex[reIt->second.finalSlot].get()) {
                constexpr int kSurfaceLayerUnitDraw = ShaderInternal::kSurfaceLayerUnit;
                glActiveTexture(GL_TEXTURE0 + kSurfaceLayerUnitDraw);
                comp->bind();
                // Old-content snapshot (morph cross-fades): same clobber, same
                // last-moment rebind.
                if (st->oldSnapshot) {
                    constexpr int kOldSnapshotUnitDraw = ShaderInternal::kOldSnapshotUnit;
                    glActiveTexture(GL_TEXTURE0 + kOldSnapshotUnitDraw);
                    st->oldSnapshot->bind();
                }
                glActiveTexture(GL_TEXTURE0);
            }
        }
    }
    KWin::OffscreenEffect::drawWindow(renderTarget, viewport, w, mask, deviceRegion, data);

    // Unbind the multipass channel units we bound and restore GL_TEXTURE0 —
    // texture hygiene mirroring paint_pipeline.cpp, so a stray bind doesn't leak
    // into the next window's draw. No-op when boundChannels == 0 (single-pass).
    // The transition-rebind branch above binds the layer / old-snapshot units
    // (4/5) but intentionally does NOT unbind them here: paint_pipeline.cpp owns
    // their teardown on its own transition path.
    for (int i = 0; i < boundChannels; ++i) {
        glActiveTexture(GL_TEXTURE0 + kSurfaceChannelBaseUnit + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    if (boundChannels > 0) {
        glActiveTexture(GL_TEXTURE0);
    }
}

} // namespace PlasmaZones
