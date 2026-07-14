// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "desktoptransitionmanager.h"

#include "plasmazoneseffect.h"
#include "shadertransitionmanager.h"
#include "plasmazoneseffect/shader_internal.h"

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/ProfilePaths.h>

#include <effect/effecthandler.h>
#include <core/output.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>
#include <opengl/glvertexbuffer.h>

#include <QColor>
#include <QVector2D>

#include <algorithm>
#include <array>
#include <cmath>

namespace PlasmaZones {

namespace {
// A full-screen quad in the RenderViewport's DEVICE coordinate space (logical
// pixels × scale, y-down), projected by viewport.projectionMatrix() in the
// caller. Local copy (unique name) so the Unity build cannot ODR-collide with
// surfacelayers.cpp's drawFullscreenQuad — this TU is also excluded from the
// Unity blob in CMakeLists as belt-and-braces.
//
// Texcoords are pinned to SCREEN corners, so `uv` stays TOP-DOWN (uv.y == 0 at
// the top of the output) whatever the output transform is. That is the space
// desktop_transition.glsl's getFromColor/getToColor undo the capture FBO's Y-up
// origin against (`1.0 - uv.y`), and the space iSwitchDelta's "+y is one row
// down" is stated in — so the fragment stage and the packs need no change.
//
// The pairing matters: emitting clip-space directly happened to give the same
// top-down uv only because the default target transform is FlipY. Re-deriving it
// from screen corners is what keeps that true once the projection is applied.
void drawDesktopBlendQuad(const KWin::RenderViewport& viewport)
{
    const KWin::Rect sr = viewport.scaledRenderRect();
    const float x0 = float(sr.left());
    const float y0 = float(sr.top());
    const float x1 = float(sr.right());
    const float y1 = float(sr.bottom());

    const std::array<KWin::GLVertex2D, 4> verts = {{
        {QVector2D(x0, y1), QVector2D(0.0f, 1.0f)}, // bottom-left
        {QVector2D(x1, y1), QVector2D(1.0f, 1.0f)}, // bottom-right
        {QVector2D(x0, y0), QVector2D(0.0f, 0.0f)}, // top-left
        {QVector2D(x1, y0), QVector2D(1.0f, 0.0f)}, // top-right
    }};
    KWin::GLVertexBuffer* const vbo = KWin::GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setVertices(verts);
    vbo->render(GL_TRIANGLE_STRIP);
}

// Resolve p_<name> parameter values into the customParams[] / customColors[]
// slot pools, shared by begin() and beginPeek(). translateAnimationParams fills
// the metadata defaults when the profile carries no override — WITHOUT this the
// shaders run at customParams == 0 (slide has no direction, dissolve no speckle
// scale, etc.) and appear broken. Color params land as normalised rgba, exactly
// as the per-window transition path uploads them (see shader_transitions.cpp);
// translateAnimationParams coerces every color to a valid QColor (default →
// Qt::transparent), so the isValid guard is defence-in-depth against a caller
// that bypasses the registry encoder.
void translatePackParams(
    const PhosphorAnimationShaders::AnimationShaderEffect& eff, const QVariantMap& params,
    std::array<QVector4D, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams>& customParams,
    std::array<QVector4D, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors>& customColors)
{
    namespace ASC = PhosphorAnimationShaders::AnimationShaderContract;
    const QVariantMap translated =
        PhosphorAnimationShaders::AnimationShaderRegistry::translateAnimationParams(eff, params);
    for (int slot = 0; slot < ASC::kMaxCustomParams; ++slot) {
        auto pull = [&](char comp) -> float {
            const auto it = translated.constFind(ASC::slotKey(slot, comp));
            if (it == translated.constEnd()) {
                return 0.0f;
            }
            bool ok = false;
            const float v = it->toFloat(&ok);
            return ok ? v : 0.0f;
        };
        customParams[slot] = QVector4D(pull('x'), pull('y'), pull('z'), pull('w'));
    }
    for (int slot = 0; slot < ASC::kMaxCustomColors; ++slot) {
        const auto it = translated.constFind(ASC::colorKey(slot));
        if (it == translated.constEnd()) {
            continue;
        }
        const QColor c = it->value<QColor>();
        if (!c.isValid()) {
            continue;
        }
        customColors[slot] = QVector4D(c.redF(), c.greenF(), c.blueF(), c.alphaF());
    }
}

} // namespace

DesktopTransitionManager::DesktopTransitionManager(PlasmaZonesEffect* effect)
    : m_effect(effect)
{
}

DesktopTransitionManager::~DesktopTransitionManager()
{
    // GL resources (textures/shaders) are released by their unique_ptrs. Do NOT
    // touch KWin::effects here — teardown ordering during plugin unload is not
    // guaranteed. reset() is the explicit-cleanup path while the compositor is live.
}

bool DesktopTransitionManager::prepareTransitionPrototype(const QString& effectId, const QVariantMap& params,
                                                          const QString& eventPath, int durationMs,
                                                          std::shared_ptr<const PhosphorAnimation::Curve> progressCurve,
                                                          OutputTransition* proto)
{
    const PhosphorAnimationShaders::AnimationShaderEffect eff =
        m_effect->m_shaderManager.shaderRegistry().effect(effectId);
    if (!eff.isValid()) {
        // The resolved shader id is not installed (e.g. the profile references an
        // uninstalled pack). Do NOT claim the fullscreen effect or populate
        // m_active — bail so KWin's native behaviour plays instead of a blank
        // transition that the first paintOutput would only then abandon.
        return false;
    }
    if (!PhosphorAnimationShaders::shaderEffectAppliesToEventPath(eff, eventPath)) {
        // The resolved shader is not a desktop-contract pack — a window/surface
        // or universal shader inherited from a broader profile scope (the desktop
        // settings page only offers appliesTo:["desktop"] packs, but a `window`
        // or `global` scope override cascades here). It has no getFromColor /
        // getToColor, so it would sample the unbound uFromDesktop / uToDesktop and
        // render garbage. Refuse it and let KWin's native behaviour play. Routes
        // through the same predicate the settings picker filters on, so the
        // runtime and the picker share one opt-in desktop policy.
        return false;
    }
    proto->effectId = effectId;
    translatePackParams(eff, params, proto->customParams, proto->customColors);
    proto->startTimeMs = ShaderInternal::shaderClockNowMs();
    // Spring lifetime + envelope clamp, shared with the per-window shader path.
    // No separate non-positive fallback: resolveTransitionLifetimeMs floors every
    // result at Limits::MinAnimationDurationMs, so even a 0 arrives as a sane
    // 50 ms rather than settling on its first paint. The live callers cannot pass
    // one regardless — resolveEventMotionProfile clamps the cascade's duration
    // into the envelope before lifecycle.cpp rounds it.
    proto->durationMs = ShaderInternal::resolveTransitionLifetimeMs(durationMs, progressCurve.get());
    proto->progressCurve = std::move(progressCurve); // shapes iTime in paintOutput; null → linear
    return true;
}

void DesktopTransitionManager::begin(KWin::VirtualDesktop* from, KWin::VirtualDesktop* to, KWin::LogicalOutput* output,
                                     const QString& effectId, const QVariantMap& params, int durationMs,
                                     std::shared_ptr<const PhosphorAnimation::Curve> progressCurve)
{
    if (!from || !to || from == to || effectId.isEmpty()) {
        return; // nothing to animate (no shader assigned, or a no-op switch)
    }
    // Shared resolve prologue: pack validation + parameter pools + clamped
    // lifetime + curve, identical for every output this begin() touches.
    OutputTransition proto;
    if (!prepareTransitionPrototype(effectId, params, PhosphorAnimation::ProfilePaths::DesktopSwitch, durationMs,
                                    std::move(progressCurve), &proto)) {
        return;
    }

    // Actual switch direction for direction-aware packs (uploaded as
    // iSwitchDelta): the pager-grid delta from -> to, wrap-corrected to the
    // shortest path so a "next desktop" wrap off the last column reads as one
    // step forward, not a full-row jump backwards. Grid coords are (col, row)
    // with row increasing downward, matching the +y-down uv space the packs
    // sample in. Stays zero when either desktop has no grid position.
    QVector4D switchDelta;
    {
        const QPoint fromCoords = KWin::effects->desktopGridCoords(from);
        const QPoint toCoords = KWin::effects->desktopGridCoords(to);
        const QSize grid = KWin::effects->desktopGridSize();
        if (fromCoords.x() >= 0 && fromCoords.y() >= 0 && toCoords.x() >= 0 && toCoords.y() >= 0) {
            float dx = float(toCoords.x() - fromCoords.x());
            float dy = float(toCoords.y() - fromCoords.y());
            // Strict `>`: a delta of EXACTLY half the grid (even grids, e.g.
            // col0 -> col2 on a 4-wide pager) is ambiguous — both directions
            // are equally short — and the tie deliberately keeps the literal
            // (non-wrapped) delta.
            if (grid.width() > 1 && std::abs(dx) > float(grid.width()) / 2.0f) {
                dx -= std::copysign(float(grid.width()), dx);
            }
            if (grid.height() > 1 && std::abs(dy) > float(grid.height()) / 2.0f) {
                dy -= std::copysign(float(grid.height()), dy);
            }
            const float len = std::hypot(dx, dy);
            if (len > 0.0f) {
                switchDelta = QVector4D(dx, dy, dx / len, dy / len);
            }
        }
    }

    // Resolve the affected outputs: a specific output for a per-output switch
    // (Plasma 6.7 per-output desktops, #648), otherwise every output for a
    // global all-output switch.
    QList<KWin::LogicalOutput*> outputs;
    if (output) {
        outputs.append(output);
    } else {
        outputs = KWin::effects->screens();
    }

    // insert_or_assign below destroys any prior OutputTransition still mapped to
    // an output (a re-switch inside the transition window), freeing its captured
    // GLTextures. begin() runs from the desktopChanged signal handler, off the
    // paint thread, so make the compositor context current first — the same
    // discipline the shader-registry reload path uses.
    ensureGlContextCurrent();

    for (KWin::LogicalOutput* screen : outputs) {
        if (!screen) {
            continue;
        }
        OutputTransition tr = proto;
        tr.from = from;
        tr.switchDelta = switchDelta;
        m_active.insert_or_assign(screen, std::move(tr));
    }

    if (m_active.empty()) {
        return;
    }

    // Claim the screen so KWin's built-in Slide (and Overview/Cube) bow out —
    // they check activeFullScreenEffect() in their desktopChanged handlers. This
    // is a race against Slide's own handler (its check is signal-time only, not
    // re-validated at paint), so it wins when our handler runs first; disabling
    // KWin's Desktop-Switch animation guarantees a clean single transition.
    //
    // Only take the screen when nobody else holds it. Overwriting a live claim
    // would evict whoever owns it (Overview open across a desktop switch), and
    // our release would then go on to clear THEIR claim — see
    // releaseFullScreenClaimIfIdle. Losing the claim is the benign outcome: we
    // simply do not suppress Slide this once.
    //
    // Never take it across a live shown-desktop state or a live peek leg
    // either: EffectsHandler::setActiveFullScreenEffect itself cancels show
    // desktop on every null↔effect transition (the reason beginPeek takes no
    // claim at all), and the eventual RELEASE fires it again. KWin core
    // already cancels show desktop before desktopChanged is emitted
    // (Workspace::updateWindowVisibilityOnDesktopChange), so this arm is
    // defence in depth against signal-order changes, at the same benign cost.
    const bool peekLive = std::any_of(m_active.cbegin(), m_active.cend(), [](const auto& entry) {
        return entry.second.kind != Kind::Switch;
    });
    if (!peekLive && !PlasmaZonesEffect::isShowingDesktop() && !m_fullScreenClaimed
        && !KWin::effects->activeFullScreenEffect()) {
        KWin::effects->setActiveFullScreenEffect(m_effect);
        m_fullScreenClaimed = true;
    }
    KWin::effects->addRepaintFull();
}

void DesktopTransitionManager::reapPeekTransitions()
{
    // Any bail-out from beginPeek must not strand live peek legs: a hide leg
    // still blending would keep painting the windows scene draining into the
    // bare desktop while the toggle it animated has already been reversed
    // (under a foreign claim, or with the pack since unassigned). Reap peek
    // entries (kind-guarded — a live switch is not ours to truncate) and
    // repaint so the real scene draws a frame nothing else schedules.
    bool reapedAny = false;
    for (auto it = m_active.begin(); it != m_active.end();) {
        if (it->second.kind != Kind::Switch) {
            if (!reapedAny) {
                ensureGlContextCurrent();
                reapedAny = true;
            }
            KWin::effects->addRepaint(it->first->geometry());
            it = m_active.erase(it);
        } else {
            ++it;
        }
    }
}

void DesktopTransitionManager::beginPeek(bool showing, const QString& effectId, const QVariantMap& params,
                                         int durationMs, std::shared_ptr<const PhosphorAnimation::Curve> progressCurve)
{
    if (effectId.isEmpty()) {
        // No shader assigned — KWin's instant show-desktop proceeds. Reap so a
        // hide leg begun before the pack was unassigned cannot keep blending
        // against the reversed toggle.
        reapPeekTransitions();
        return;
    }
    // Bow out while ANY effect holds the fullscreen claim — Overview, Cube, or
    // our own in-flight desktop switch. The peek never takes the claim itself
    // (see the note above the repaint below), so for foreign holders this is
    // the whole contention policy: their transition wins and KWin's instant
    // hide/show proceeds under them. Our own switch claim is excluded too, not
    // just foreign ones: proceeding would hand the claim's RELEASE (at the
    // peek's settle, once m_active empties) the same setShowingDesktop(false)
    // side effect the peek path exists to avoid, cancelling the shown state
    // from under a completed hide leg.
    if (KWin::effects->activeFullScreenEffect()) {
        reapPeekTransitions();
        return;
    }
    // Shared resolve prologue: pack validation + parameter pools + clamped
    // lifetime + curve (see begin()).
    OutputTransition proto;
    if (!prepareTransitionPrototype(effectId, params, PhosphorAnimation::ProfilePaths::DesktopPeek, durationMs,
                                    std::move(progressCurve), &proto)) {
        return;
    }

    // insert_or_assign below may destroy a prior OutputTransition (a re-toggle
    // inside the transition window), freeing its captured GLTextures. beginPeek
    // runs from the showingDesktopChanged handler, off the paint thread, so make
    // the compositor context current first.
    ensureGlContextCurrent();

    bool insertedAny = false;
    const QList<KWin::LogicalOutput*> screens = KWin::effects->screens();
    for (KWin::LogicalOutput* screen : screens) {
        if (!screen) {
            continue;
        }
        // The show leg blends FROM the bare desktop, which cannot be captured
        // once the windows are visible again — it replays the hide leg's cached
        // capture. No cache for this output (effect loaded mid-show-desktop, or
        // the hide leg was abandoned before its capture) → skip; the restored
        // windows appear instantly there, which is KWin's default behaviour.
        if (!showing && m_peekDesktopCache.find(screen) == m_peekDesktopCache.end()) {
            // Also drop any STALE hide-leg entry still mapped to this output
            // (a re-toggle before that leg's first paint is exactly how the
            // cache ends up unseeded). Left alive it would deferred-capture
            // the RESTORED-windows scene as the "bare desktop" — poisoning the
            // cache for the next show leg — and blend FROM≈TO for its whole
            // duration. Kind-guarded: a live SWITCH transition mapped here
            // (begun unclaimed under another effect that has since released)
            // is not ours to truncate. The context is already current (above);
            // repaint so the settled scene draws a frame nothing else
            // schedules.
            const auto staleIt = m_active.find(screen);
            if (staleIt != m_active.end() && staleIt->second.kind == Kind::PeekHide) {
                m_active.erase(staleIt);
                KWin::effects->addRepaint(screen->geometry());
            }
            continue;
        }
        OutputTransition tr = proto;
        tr.kind = showing ? Kind::PeekHide : Kind::PeekShow;
        // tr.switchDelta stays zero: a peek has no pager direction, so
        // direction-aware packs fall back to their configured params via
        // switchDirection()'s fallback path.
        m_active.insert_or_assign(screen, std::move(tr));
        insertedAny = true;
    }

    if (!insertedAny) {
        return;
    }

    // Deliberately NO setActiveFullScreenEffect claim on the peek path, unlike
    // begin(). EffectsHandler::setActiveFullScreenEffect itself calls
    // Workspace::setShowingDesktop(false) on every null↔non-null transition
    // (kwin effecthandler.cpp, "if (activeChanged) ... setShowingDesktop(false)").
    // Claiming here — inside the showingDesktopChanged(true) handler — would
    // re-entrantly CANCEL the very show-desktop state the peek animates: KWin
    // unhides every window before our first frame and the blend plays over
    // still-visible windows. Releasing at settle would fire it a second time.
    // This is also why windowaperture/eyeonscreen never pass fullScreen: true.
    // Nothing is lost by not claiming: the show-desktop script effects ignore
    // the claim anyway (syncShowDesktopEffectSuppression unloads them), and
    // contention with Overview/Cube is handled by the bow-out check at the top.
    KWin::effects->addRepaintFull();
}

// captureDesktop, captureLiveScene and capturePeekWindowsScene live in
// desktoptransitioncapture.cpp, along with the texture allocation and format
// helpers they share. compiledShader (the pack-source → GLShader assembly)
// lives in desktoptransitionshader.cpp.

bool DesktopTransitionManager::paintOutput(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                                           int mask, const KWin::Region& deviceRegion, KWin::LogicalOutput* screen)
{
    // The damage region genuinely does not participate: prePaintScreen sets
    // PAINT_SCREEN_TRANSFORMED for a transitioning output and scheduleRepaints()
    // repaints its full geometry every frame. Kept in the signature to match
    // KWin's paint-chain shape. (The viewport IS used — it projects the blend
    // quad, see drawDesktopBlendQuad.)
    Q_UNUSED(deviceRegion)
    if (!screen) {
        return false;
    }
    auto it = m_active.find(screen);
    if (it == m_active.end()) {
        return false;
    }
    OutputTransition& tr = it->second;

    // Settle check first: once the switch has run its course, tear this output
    // down and let the normal scene (the now-current desktop) paint.
    const qint64 nowMs = ShaderInternal::shaderClockNowMs();
    const qint64 elapsed = nowMs - tr.startTimeMs;
    if (elapsed >= tr.durationMs) {
        endOutput(screen);
        return false;
    }
    // Ease the linear time progress through the per-event timing curve (resolved
    // global → node → rule at begin time), so a `desktop.switch` node's curve
    // (e.g. "Ease Out") shapes iTime — matching the per-window shader path. A
    // stateless curve evaluates the linear point; a stateful spring integrates
    // its CurveState toward target 1 by the inter-frame dt, mirroring
    // AnimatedValue. Null curve → linear. Clamped to [0, 1] per the iTime
    // contract. lastPaintTimeMs is advanced here, once per output paint tick.
    // durationMs is guaranteed >= Limits::MinAnimationDurationMs by
    // resolveTransitionLifetimeMs in begin(), so the divisor is always positive.
    const qreal linearT = qBound<qreal>(0.0, qreal(elapsed) / qreal(tr.durationMs), 1.0);
    // Shared with the per-window transition paint; see easeProgress for the dt
    // cap and the stateful/stateless split. This is the site that OWNS the
    // stateful curve's single per-frame step for this output.
    const qreal easedT = ShaderInternal::easeProgress(tr.progressCurve.get(), tr.progressCurveState, tr.lastPaintTimeMs,
                                                      nowMs, linearT, /*stepCurve=*/true);
    tr.lastPaintTimeMs = nowMs;
    const float t = float(easedT);

    // Compile BEFORE capturing. Each capture allocates an output-sized texture and
    // renders the entire stacking order, so doing it first would burn two
    // full-screen captures only to throw them away when the shader turns out not
    // to compile. compiledShader()'s failure sentinel stops a broken pack being
    // RE-COMPILED, not re-captured, so that waste would repeat on every desktop
    // switch for the rest of the session.
    CompiledDesktopShader* cs = compiledShader(tr.effectId);
    if (!cs || !cs->shader) {
        // Compile failed — abandon the transition rather than paint a black
        // screen; the normal scene paints the settled desktop.
        endOutput(screen);
        return false;
    }

    // Deferred capture (once), per kind.
    if (!tr.captured) {
        switch (tr.kind) {
        case Kind::Switch:
            // The OUTGOING desktop is no longer current, so its windows are
            // reconstructed via the per-window composite. The INCOMING desktop
            // IS the live scene now, so it is captured via effects->paintScreen
            // — drawWindow on the current desktop's already-visible windows
            // renders black.
            tr.fromTex = captureDesktop(tr.from, screen, renderTarget, viewport);
            tr.toTex = captureLiveScene(mask, screen, renderTarget, viewport);
            break;
        case Kind::PeekHide:
            // TO first: the live scene as-is, which with the windows hidden IS
            // the bare desktop (wallpaper + docks). Shared into the per-output
            // cache so the later show leg has a FROM endpoint it can no longer
            // capture itself. FROM = the windows scene, reconstructed as that
            // same bare desktop (blitted in as the base layer) + the hidden
            // windows composited back on top — see capturePeekWindowsScene for
            // why a live capture cannot include them this frame.
            tr.toTex = captureLiveScene(mask, screen, renderTarget, viewport);
            if (tr.toTex) {
                m_peekDesktopCache[screen] = tr.toTex;
                tr.fromTex = capturePeekWindowsScene(tr.toTex.get(), mask, screen, renderTarget, viewport);
            }
            // A null toTex abandons the transition below — no point rendering
            // the FROM composite (a full scene pass) just to throw it away.
            break;
        case Kind::PeekShow: {
            // FROM = the hide leg's cached bare desktop; TO = the live scene
            // with the windows restored. The cache entry is single-consumption,
            // taken out of the map here whatever happens next: a bare desktop
            // is only valid for the show leg that immediately follows its hide
            // leg (the next hide re-seeds it), and holding an output-sized GPU
            // texture between peeks buys nothing. It must also still match
            // this frame's capture geometry — an output scale/mode change or
            // an HDR format flip between the legs makes it unusable, in which
            // case the null fromTex below abandons the transition and the
            // restored windows appear instantly (KWin's default). Validation
            // is deliberately size + internalFormat only: a colour-space
            // change on the SAME format (an SDR brightness tweak mid-peek)
            // blends a slightly stale-space desktop for one leg, which is not
            // worth carrying a ColorDescription alongside every cache entry.
            std::shared_ptr<KWin::GLTexture> desktopTex;
            const auto cachedIt = m_peekDesktopCache.find(screen);
            if (cachedIt != m_peekDesktopCache.end()) {
                desktopTex = std::move(cachedIt->second);
                m_peekDesktopCache.erase(cachedIt);
            }
            if (desktopTex && desktopTex->size() != viewport.deviceSize()) {
                desktopTex = nullptr;
            }
            if (desktopTex) {
                const KWin::GLFramebuffer* const fb = renderTarget.framebuffer();
                const KWin::GLTexture* const targetTex = fb ? fb->colorAttachment() : nullptr;
                if (targetTex && targetTex->internalFormat() != desktopTex->internalFormat()) {
                    desktopTex = nullptr;
                }
            }
            tr.fromTex = desktopTex;
            tr.toTex = desktopTex
                ? std::shared_ptr<KWin::GLTexture>(captureLiveScene(mask, screen, renderTarget, viewport))
                : nullptr;
            break;
        }
        }
        tr.captured = true;
    }
    if (!tr.fromTex || !tr.toTex) {
        // Capture failed — same abandon path.
        endOutput(screen);
        return false;
    }

    // The output's device size in LOGICAL orientation (scaledRenderRect().size(),
    // offset-independent). Same source the capture textures are allocated from, so
    // iResolution, the packs' aspect/texel maths and the sampled textures cannot
    // disagree by a rounding pixel.
    const QSize deviceSize = viewport.deviceSize();

    const ShaderInternal::ScopedGlState glStateGuard;
    // Draw into the framebuffer KWin handed us, sized to that target, instead of
    // assuming the default backbuffer at the device origin. Under a non-default
    // render target (HDR / colour-management intermediate) the target FB is
    // authoritative; on the common path it is the output's device-sized backbuffer,
    // so this matches the previous behaviour there. iResolution below stays the
    // OUTPUT device resolution (what the packs do aspect/texel maths against),
    // independent of the draw target's pixel size — the quad now rotates the
    // result as a unit, so the packs still reason in logical orientation.
    KWin::GLFramebuffer* const targetFb = renderTarget.framebuffer();
    if (targetFb) {
        KWin::GLFramebuffer::pushFramebuffer(targetFb);
    }
    const QSize targetSize = targetFb ? targetFb->size() : deviceSize;
    glViewport(0, 0, targetSize.width(), targetSize.height());
    glDisable(GL_BLEND); // the blend of two opaque desktops is itself opaque — replace the screen

    KWin::ShaderBinder binder(cs->shader.get());
    // Project the quad through KWin's own matrix. It folds in
    // RenderTarget::transform() — the output rotation/flip combined with the
    // buffer's FlipY — and the render offset. Without it a rotated output blends
    // logical-oriented capture textures into a panel-oriented framebuffer, so the
    // desktops paint unrotated and anisotropically stretched for the whole switch.
    // KWin resolves this uniform by name, so the enum setter works on a
    // generateCustomShader program (same as the surface layers do).
    cs->shader->setUniform(KWin::GLShader::Mat4Uniform::ModelViewProjectionMatrix, viewport.projectionMatrix());
    if (cs->iTimeLoc >= 0) {
        cs->shader->setUniform(cs->iTimeLoc, t);
    }
    if (cs->iResolutionLoc >= 0) {
        cs->shader->setUniform(cs->iResolutionLoc, QVector2D(float(deviceSize.width()), float(deviceSize.height())));
    }
    // Monotonic frame counter for glitch-style packs. Advance after the upload
    // so the first painted frame is iFrame == 0, matching the per-window path's
    // zero-based leg frame.
    if (cs->iFrameLoc >= 0) {
        cs->shader->setUniform(cs->iFrameLoc, tr.frameCount);
    }
    ++tr.frameCount;
    // Actual switch direction (grid delta + unit vector) for packs that follow
    // the navigation instead of a fixed configured direction.
    if (cs->iSwitchDeltaLoc >= 0) {
        cs->shader->setUniform(cs->iSwitchDeltaLoc, tr.switchDelta);
    }
    // The p_<name> pack parameters (slide direction, dissolve scale, etc.).
    for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams; ++slot) {
        if (cs->customParamsLoc[slot] >= 0) {
            cs->shader->setUniform(cs->customParamsLoc[slot], tr.customParams[slot]);
        }
    }
    // Color params (customColors[N]) — the pool a `"type":"color"` pack param's
    // p_<name> define resolves to. Bound here so desktop packs get tunable
    // colors, at parity with the per-window transition and surface contracts.
    for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors; ++slot) {
        if (cs->customColorsLoc[slot] >= 0) {
            cs->shader->setUniform(cs->customColorsLoc[slot], tr.customColors[slot]);
        }
    }
    if (cs->iFromDesktopLoc >= 0) {
        cs->shader->setUniform(cs->iFromDesktopLoc, 0);
        glActiveTexture(GL_TEXTURE0);
        tr.fromTex->bind();
    }
    if (cs->iToDesktopLoc >= 0) {
        cs->shader->setUniform(cs->iToDesktopLoc, 1);
        glActiveTexture(GL_TEXTURE1);
        tr.toTex->bind();
    }
    glActiveTexture(GL_TEXTURE0);

    drawDesktopBlendQuad(viewport);

    // Unbind both capture units. ScopedGlState restores the active-unit ENUM, not
    // the BINDINGS, so without this the two capture textures stay bound for the
    // rest of KWin's frame — and then endOutput() (or the wall-clock reap) deletes
    // them. glDeleteTextures only clears the binding on the CURRENTLY ACTIVE unit,
    // so a name bound to any other unit survives as a dangling reference and
    // sampling it is undefined (black on most drivers). Every other bind site in
    // the effect is meticulous about this; this one was the hole.
    if (cs->iToDesktopLoc >= 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    if (cs->iFromDesktopLoc >= 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);

    if (targetFb) {
        KWin::GLFramebuffer::popFramebuffer();
    }
    return true;
}

void DesktopTransitionManager::endOutput(KWin::LogicalOutput* screen)
{
    m_active.erase(screen);
    releaseFullScreenClaimIfIdle();
}

void DesktopTransitionManager::releaseFullScreenClaimIfIdle()
{
    if (!m_active.empty() || !m_fullScreenClaimed || !KWin::effects) {
        return;
    }
    // Clear the claim only while KWin still reports it as OURS. Overview or Cube
    // can take the screen while our transition is in flight; writing nullptr
    // unconditionally would drop THEIR claim and unsuppress every effect that was
    // bowing out to them. Dropping our own flag either way keeps us from later
    // clearing a claim we no longer hold.
    if (KWin::effects->activeFullScreenEffect() == m_effect) {
        KWin::effects->setActiveFullScreenEffect(nullptr);
    }
    m_fullScreenClaimed = false;
}

void DesktopTransitionManager::ensureGlContextCurrent()
{
    if (KWin::effects) {
        KWin::effects->makeOpenGLContextCurrent();
    }
}

void DesktopTransitionManager::scheduleRepaints()
{
    if (!KWin::effects) {
        return; // only reached from postPaintScreen (effects live), guard for parity
    }
    // Reap expired transitions by WALL CLOCK, not by being painted. paintOutput()
    // is the only other settle path, and it runs only for an output that is
    // actually painting — so an output that stops being painted mid-transition
    // (DPMS-asleep but enabled, or one KWin has stopped rendering) would sit in
    // m_active forever while a sibling kept painting, and the fullscreen claim
    // would never release. postPaintScreen calls us on every frame regardless of
    // which output painted, so expiry is handled here even for an unpainted one.
    const qint64 nowMs = ShaderInternal::shaderClockNowMs();
    bool erasedAny = false;
    for (auto it = m_active.begin(); it != m_active.end();) {
        if (nowMs - it->second.startTimeMs >= it->second.durationMs) {
            if (!erasedAny) {
                // The captured GLTextures free on erase and want a live context.
                ensureGlContextCurrent();
                erasedAny = true;
            }
            // Repaint the output we are about to drop. The blend was the last
            // thing painted on it, and the settled scene still has to be drawn —
            // KWin only composites on damage, so without this the output keeps
            // presenting the frozen final blend frame until something unrelated
            // damages it. paintOutput's settle path does not need this because it
            // returns false and paintScreen falls through to the real scene in the
            // SAME frame; a reap has no such frame to fall through to.
            KWin::effects->addRepaint(it->first->geometry());
            it = m_active.erase(it);
        } else {
            ++it;
        }
    }
    if (erasedAny) {
        releaseFullScreenClaimIfIdle();
    }
    // Keys are always non-null: begin() skips null outputs and outputRemoved()
    // reaps a disconnected one, so a null screen can never be stored.
    for (const auto& entry : m_active) {
        KWin::effects->addRepaint(entry.first->geometry());
    }
}

void DesktopTransitionManager::outputRemoved(KWin::LogicalOutput* screen)
{
    // A disconnected output must not linger as a key in m_active: paintOutput()
    // and scheduleRepaints() deref the key (UAF), and the fullscreen-effect claim
    // would never release once its output vanished mid-transition. endOutput()
    // drops the entry and releases the claim when the last transition goes. The
    // peek-desktop cache entry goes with it — its texture is output-sized and
    // its key would dangle.
    const bool hasTransition = m_active.find(screen) != m_active.end();
    const bool hasPeekCache = m_peekDesktopCache.find(screen) != m_peekDesktopCache.end();
    if (!hasTransition && !hasPeekCache) {
        return; // nothing held for this output
    }
    // screenRemoved fires off the paint thread; both erases free GLTextures,
    // which want a current GL context (endOutput's other caller, paintOutput, is
    // already on the paint thread with one current).
    ensureGlContextCurrent();
    m_peekDesktopCache.erase(screen);
    if (hasTransition) {
        endOutput(screen);
    }
}

void DesktopTransitionManager::desktopRemoved(KWin::VirtualDesktop* desktop)
{
    // A desktop removed mid-switch leaves any OutputTransition still referencing
    // it as `from` holding a dangling VirtualDesktop*: the deferred
    // captureDesktop(tr.from, ...) only ever compares the pointer (isOnDesktop),
    // never derefs it, so this is not a crash — but a freed pointer must not
    // linger and be compared against a live desktop. Drop every transition whose
    // outgoing desktop just vanished.
    bool erasedAny = false;
    for (auto it = m_active.begin(); it != m_active.end();) {
        if (it->second.from == desktop) {
            // Erasing frees the entry's captured GLTextures; desktopRemoved fires
            // off the paint thread, so make the context current before the first
            // free — only when there is actually something to free.
            if (!erasedAny) {
                ensureGlContextCurrent();
                erasedAny = true;
            }
            // Repaint the output for the same reason the wall-clock reap does: the
            // blend was the last thing drawn on it and the settled scene still
            // needs a frame, which nothing else will schedule.
            if (KWin::effects) {
                KWin::effects->addRepaint(it->first->geometry());
            }
            it = m_active.erase(it);
        } else {
            ++it;
        }
    }
    if (erasedAny) {
        releaseFullScreenClaimIfIdle();
    }
}

void DesktopTransitionManager::invalidateShaderCache()
{
    // Fires from the AnimationShaderRegistry file watcher between frames, where the
    // compositor GL context is NOT current. m_shaderCache owns GLShaders whose
    // destruction issues glDelete* calls that want a current context — the same
    // discipline reset() applies. Teardown (!effects) reclaims them regardless.
    ensureGlContextCurrent();
    m_shaderCache.clear();
}

void DesktopTransitionManager::reset()
{
    // Teardown path (compositor reset / plugin unload). Make the compositor
    // context current so the captured GLTextures (m_active) and compiled
    // GLShaders (m_shaderCache) free on a live context; when KWin::effects is
    // already gone the driver is tearing GL down and reclaims them regardless.
    // Clearing m_shaderCache HERE — not leaving it for ~DesktopTransitionManager,
    // which deliberately can't make a context current — is what makes this the
    // real "release GL resources" path the header documents.
    ensureGlContextCurrent();
    m_active.clear();
    m_shaderCache.clear();
    m_peekDesktopCache.clear();
    releaseFullScreenClaimIfIdle();
}

} // namespace PlasmaZones
