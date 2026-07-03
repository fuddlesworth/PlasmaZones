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
#include "shader_resolve.h"
#include "window_query.h"

#include <PhosphorCompositor/AutotileState.h>
#include <PhosphorCompositor/DecorationDefaults.h>

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>

#include <QByteArray>
#include <QColor>
#include <QVariantMap>
#include <QVector2D>
#include <QVector4D>

#include <optional>

namespace PlasmaZones {

void PlasmaZonesEffect::setupDecorationManager()
{
    connect(m_decorationManager.get(), &DecorationManager::windowDecorationRestored, this,
            [this](const QString& windowId) {
                // A veto-driven restore leaves the window rule-owned and
                // still border-eligible — rebuild its overlay instead of
                // dropping it. updateWindowBorder self-gates on the merged
                // appearance (it removes first and re-creates only when
                // something should show). Border overlays are visual-only,
                // so off-desktop windows just get their stale item dropped —
                // the desktopChanged → updateAllBorders refresh rebuilds
                // theirs when they become visible (same policy as
                // updateAllBorders and markWindowSnapped). Exact-id re-check:
                // findWindowById's fuzzy appId fallback could resolve a
                // same-app sibling for a dead id, and creating a border item
                // keyed under the dead id against the sibling would linger
                // until the next full rebuild.
                KWin::EffectWindow* w = findWindowById(windowId);
                if (w && getWindowId(w) == windowId && w->isOnCurrentDesktop()) {
                    updateWindowBorder(windowId, w);
                } else {
                    removeWindowBorder(windowId);
                }
            });
}

void PlasmaZonesEffect::removeWindowBorder(const QString& windowId)
{
    auto it = m_windowBorders.find(windowId);
    if (it == m_windowBorders.end()) {
        return;
    }
    WindowBorder& wb = it.value();
    // Release the offscreen redirect + border shader slot IF this border owns
    // it. When an animation transition currently owns the slot (shaderApplied
    // is false — the transition's begin took it over), we must NOT touch
    // setShader / unredirect: the transition lifecycle owns the handover and a
    // stray unredirect here would tear down the animation mid-flight. The
    // transition-end path re-checks m_windowBorders and only re-applies the
    // border shader when the border still exists, so dropping the entry here
    // (after this guarded release) is the correct teardown.
    if (wb.shaderApplied) {
        if (KWin::EffectWindow* w = findWindowById(windowId)) {
            // Only clear when no transition raced in to own the slot between
            // this border being applied and now — reconcileBorderShader would
            // have cleared shaderApplied in that case, but guard defensively.
            if (!m_shaderManager.findTransition(w)) {
                setShader(w, nullptr);
                unredirect(w);
                // Dropping the redirect/shader on a STATIC window generates no
                // damage of its own (mirror of the addRepaintFull on the apply
                // path in updateWindowBorder) — without this the stale bordered
                // frame lingers until unrelated damage arrives.
                w->addRepaintFull();
            }
        }
    }
    m_windowBorders.erase(it);

    // Free the window's multipass FBO targets (surfaceTex + buffer chain) — these
    // are only allocated for multipass packs and only while the window has a
    // border, so dropping them on border removal / window close reclaims the GPU
    // memory. No-op for single-pass packs (the map never held an entry).
    m_surfaceMultipass.erase(windowId);
}

void PlasmaZonesEffect::clearAllBorders()
{
    while (!m_windowBorders.isEmpty()) {
        removeWindowBorder(m_windowBorders.begin().key());
    }
}

void PlasmaZonesEffect::updateWindowBorder(const QString& windowId, KWin::EffectWindow* w)
{
    // Remove existing border for this window first
    removeWindowBorder(windowId);

    if (!w || w->isMinimized() || w->isFullScreen()) {
        return;
    }

    // APP-WINDOW GATE: decoration applies to application windows only. Reuse the
    // same structural app-window filter that snapping / zone management already
    // use (shouldHandleWindow) — exactly as the animation path reuses it via
    // shouldAnimateWindow — so no border is ever painted onto a non-window surface
    // (docks, panels, the desktop, popups, dialogs, OSDs, tooltips, notifications,
    // portal / plasma-shell surfaces, our own overlays).
    if (!shouldHandleWindow(w)) {
        return;
    }

    // BORDER appearance is owned by the WINDOW RULES (the rule-backed appearance
    // model). resolveWindowAppearance reads the matched SetBorder* actions; the
    // "border" surface-shader pack RENDERS that resolution. The pack is NOT
    // inserted into the user's decoration tree — the rule's show-border flag is
    // the sole border gate, and the resolved width / radius / colours ride the
    // per-window override fields below (pushed by pushBorderUniforms, which picks
    // active vs inactive colour by focus). The decoration tree contributes only
    // the user's OWN pack chain (e.g. glow) composited on top.
    const std::optional<ResolvedWindowAppearance> appearance =
        resolveWindowAppearance(resolveRuleActions(w, windowId), m_borderAccentColor, m_borderInactiveColor);
    const bool showBorder = appearance && appearance->showBorder.value_or(false);

    // User decoration packs from the tree. The "border" id is rule-owned, so an
    // explicit user "border" entry is dropped here (the rule path owns it).
    QStringList userPacks;
    const QString surfacePath = resolveSurfacePathFor(windowId);
    const QStringList treeChain = m_decorationTree.resolve(surfacePath).effectiveChain();
    for (const QString& pack : treeChain) {
        if (pack != QStringLiteral("border")) {
            userPacks.append(pack);
        }
    }

    // DECORATE GATE: render when the rule shows a border OR the tree declares user
    // packs. Neither → nothing to render. The "border" base (when present) renders
    // first; user packs composite over it (chain[1..]).
    QStringList chain;
    if (showBorder) {
        chain.append(QStringLiteral("border"));
    }
    chain.append(userPacks);
    if (chain.isEmpty()) {
        return;
    }
    const QString basePackId = chain.first();

    // Store the resolved chain. pushBorderUniforms reads live frameGeometry()/
    // expandedGeometry() + viewport.scale() per frame so a resize/move/output-scale
    // change needs no geometry-sync bookkeeping — the OffscreenEffect redirect
    // already drives a fresh paint on every change.
    WindowBorder wb;
    wb.chain = chain;
    wb.basePackId = basePackId;
    if (showBorder) {
        // Per-window border appearance from the rule. width / radius fall back to
        // the shared DecorationDefaults when the rule sets only "show border"; an
        // omitted colour falls back to the live system accent (matching the
        // useSystemAccent default), and an unknown accent to the pack's metadata
        // default colour. inactiveColor mirrors active when the rule omits it.
        wb.ruleBorder = true;
        wb.ruleBorderWidth = appearance->borderWidth.value_or(PhosphorCompositor::DecorationDefaults::BorderWidth);
        wb.ruleBorderRadius = appearance->borderRadius.value_or(PhosphorCompositor::DecorationDefaults::BorderRadius);
        const QColor accentOr =
            m_borderAccentColor.isValid() ? m_borderAccentColor : QColor(QStringLiteral("#ff3daee9"));
        wb.ruleBorderActiveColor = appearance->activeColor.value_or(accentOr);
        wb.ruleBorderInactiveColor = appearance->inactiveColor.value_or(wb.ruleBorderActiveColor);
    }

    // Corner rounding and the outline are entirely the SHADER's job (the
    // rounded-rect SDF in the border fragment shader), identically for decorated
    // and borderless windows. It operates on the COMPOSITED redirected texture, so
    // it never clips individual client subsurfaces — and crucially we do NOT touch
    // the KWin window's own BorderRadius: setting it made KWin clip the client
    // surface independently, which on a server-side-decorated window cut the inner
    // surface and left the shader's corner inset behind KWin's. We draw no drop
    // shadow: KWin does not render the shadow into this redirected texture (its
    // expanded margin arrives transparent), so there is nothing here to reshape.

    m_windowBorders.insert(windowId, wb);

    // Apply the offscreen border shader (redirect + setShader) unless an
    // animation transition currently owns this window's shader slot — in which
    // case reconcileBorderShader leaves the transition alone and the border
    // shader is (re-)applied when the transition ends. A geometry change drives
    // its own repaint, so no manual repaint is needed here for the steady case;
    // request one anyway so a border added to a STATIC window (no pending
    // damage) reaches paintWindow and the outline becomes visible immediately.
    reconcileBorderShader(windowId, w);
    // `w` is non-null here (the early-out above returns on !w), so no
    // KWin::effects guard is needed — addRepaintFull is a window method.
    w->addRepaintFull();
}

void PlasmaZonesEffect::updateAllBorders()
{
    clearAllBorders();

    // Iterate all effect windows and (re-)create borders. updateWindowBorder
    // fully self-gates now — the app-window filter (shouldHandleWindow) plus the
    // resolved decoration chain decide whether a window decorates — so border
    // creation is driven solely by that gate, NOT by tiling/snap membership or
    // by whether any animation rule exists. The rule set is still consulted only
    // for the title-bar override reconcile below.
    const bool haveRules = !m_shaderManager.animationRuleSet().isEmpty();
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        if (!w || w->isDeleted()) {
            continue;
        }
        const QString wid = getWindowId(w);
        // Self-heal compositor-initiated noBorder resets: KWin silently
        // re-decorates off-desktop windows on desktop switches. resyncWindow
        // is a self-guarding no-op unless the manager owns the window,
        // believes it hidden, and the compositor reports the decoration
        // back — so running it for every window here is cheap and covers
        // ALL owner kinds (autotile, snap, rule) on every desktop return,
        // activation, and border refresh.
        m_decorationManager->resyncWindow(wid);
        // Border overlays are visual, so only build them for windows on the
        // current desktop. Every on-desktop window is re-resolved; the gate
        // inside updateWindowBorder (app-window filter + resolved chain) decides
        // whether it actually decorates, so a floating application window picks
        // up the window-node border default regardless of tiling/snap membership
        // or whether any animation rule exists.
        if (w->isOnCurrentDesktop()) {
            updateWindowBorder(wid, w);
        }
        // Title-bar hiding (setNoBorder) is a persistent decoration-state change
        // that survives desktop switches, so reconcile the rule override for ALL
        // windows the rule may match — otherwise a SetHideTitleBar rule added
        // while the matched window sits on another virtual desktop would not take
        // effect until that window is next activated.
        if (haveRules) {
            reconcileRuleHiddenTitleBar(wid, w);
        }
    }
    // When no rules remain, the per-window reconcile above is skipped (haveRules
    // is false), so a Rule owner or force-show veto from a now-removed
    // SetHideTitleBar rule would linger until effect teardown. Clear all rule
    // overrides in that case — a no-op when the manager tracks none (the
    // common no-rules path), so this costs nothing when nothing is overridden.
    if (!haveRules) {
        restoreAllRuleHiddenTitleBars();
    }
}

void PlasmaZonesEffect::reconcileRuleHiddenTitleBar(const QString& windowId, KWin::EffectWindow* w)
{
    if (!w || windowId.isEmpty()) {
        return;
    }
    // Tri-state rule override, forwarded to the DecorationManager (Rule is the
    // only owner kind now — there are no mode owners to defer to):
    //   unset → no owner, the title bar shows
    //   true  → the rule hides the title bar
    //   false → the rule FORCE-SHOWS (a veto pinning the decoration visible)
    // The manager owns the capability gate and the geometry re-assert across
    // veto-driven decoration flips.
    const std::optional<ResolvedWindowAppearance> ovr =
        resolveWindowAppearance(resolveRuleActions(w, windowId), m_borderAccentColor, m_borderInactiveColor);
    m_decorationManager->setRuleOverride(windowId, ovr ? ovr->hideTitleBar : std::nullopt);
}

bool PlasmaZonesEffect::isWindowMarkedSnapped(const QString& windowId) const
{
    return m_snapHandler->isTiledWindow(windowId);
}

QString PlasmaZonesEffect::resolveSurfacePathFor(const QString& windowId) const
{
    // MEMBERSHIP-only resolution: isTiledWindow tests bucket membership, and the
    // resolved profile's effectiveChain() (an empty chain = no decoration) is the
    // sole render gate (see updateWindowBorder) — there is no separate show-border
    // gate. Autotile-first precedence; falls back to window.floating for an
    // unmanaged window.
    if (m_autotileHandler->isTiledWindow(windowId)) {
        return QStringLiteral("window.tiled");
    }
    if (m_snapHandler->isTiledWindow(windowId)) {
        return QStringLiteral("window.snapped");
    }
    return QStringLiteral("window.floating");
}

void PlasmaZonesEffect::seedDecorationTreeBaseline()
{
    // Mirror the daemon's ConfigDefaults::decorationProfileTree(): the decoration
    // tree is the user-applied surface-shader pack stack only. Window border and
    // title-bar appearance are owned by the window rules (resolved daemon-side and
    // applied via the DecorationManager), not by this tree, so the baseline is
    // empty/neutral and nothing is auto-inserted. The daemon's real fetch
    // overwrites this whole tree once it arrives.
    m_decorationTree = PhosphorSurfaceShaders::DecorationProfileTree{};
}

void PlasmaZonesEffect::restoreAllRuleHiddenTitleBars()
{
    // The authoritative window-rule state is gone (rule set emptied, daemon
    // loss, effect teardown): clear every Rule owner and force-show veto so the
    // manager restores each title bar to its native state.
    m_decorationManager->clearAllRuleOverrides();
}

// ─────────────────────────────────────────────────────────────────────────────
// Surface shader (the window border / rounded-corner pack and any future surface
// pack) — per-pack compile + registry search-path setup live in
// shader_transitions.cpp (compiledPack() / compiledPackForWindow() /
// ensureSurfaceRegistryPaths()), reusing the shared GLSL include / param-preamble
// / #define-PLASMAZONES_KWIN pipeline there. The border is the first surface
// shader pack: data/surface/border, loaded via SurfaceShaderRegistry.
// ─────────────────────────────────────────────────────────────────────────────

void PlasmaZonesEffect::reconcileBorderShader(const QString& windowId, KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }
    auto it = m_windowBorders.find(windowId);
    // The presence of a WindowBorder entry IS the "wants border" gate now:
    // updateWindowBorder only inserts one for a member window whose resolved
    // profile declares a non-empty pack chain (border appearance moved into the
    // pack's own params, so there is no host width/colour to test here).
    const bool wantsBorder = (it != m_windowBorders.end());

    // An in-flight animation transition owns the shader slot — the transition's
    // begin already called setShader(animationShader) and redirect(). Leave it
    // alone: the transition-end path re-applies the border shader (and keeps the
    // redirect) when the border still exists. Just clear our ownership flag so
    // the per-frame push and removeWindowBorder defer to the transition.
    if (m_shaderManager.findTransition(w)) {
        if (it != m_windowBorders.end()) {
            it->shaderApplied = false;
        }
        return;
    }

    if (wantsBorder) {
        // The redirect shader OffscreenData::paint runs to present this window:
        //   single pack → the pack's own main shader (it blits the redirected
        //                 surface through itself, sampling its buffer iChannels);
        //   multi pack  → the passthrough present shader, which samples the
        //                 pre-composited final FBO that paintWindow's
        //                 renderSurfaceChainComposite produced (the per-pack mains
        //                 ran as FBO passes there, not via OffscreenData).
        // Both compile-on-first-use; a null result tears the redirect down rather
        // than leave the window blitting a dead/stale shader forever (a just-ended
        // transition hands the slot back here still redirected).
        KWin::GLShader* redirectShader = nullptr;
        if (it->chain.size() > 1) {
            redirectShader = surfacePresentShader();
        } else if (CompiledSurfacePack* const pack = compiledPackForWindow(windowId)) {
            redirectShader = pack->shader.get();
        }
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
    // No teardown branch here: !wantsBorder means there is no WindowBorder entry
    // to act on (wantsBorder IS `it != end()`), and border removal always routes
    // through removeWindowBorder, which clears the shader and unredirects.
}

void PlasmaZonesEffect::pushBorderUniforms(KWin::EffectWindow* w, const WindowBorder& wb,
                                           const CompiledSurfacePack& pack, qreal scale)
{
    // Every caller (drawWindow's idle blit, renderSurfaceChain's transition
    // capture, renderSurfaceChainComposite's per-pack fold) has already resolved
    // @p pack and @p wb, confirmed the border is applied, ruled out a transition
    // owning the slot, and bound pack.shader, so this just computes and writes
    // the uniforms onto the bound program. The border APPEARANCE is not a
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
    const QVector2D windowExpandedSize(static_cast<float>(expanded.width() * scale),
                                       static_cast<float>(expanded.height() * scale));
    const QVector2D frameTopLeft(static_cast<float>((frame.left() - expanded.left()) * scale),
                                 static_cast<float>((frame.top() - expanded.top()) * scale));
    const QVector2D frameSize(static_cast<float>(frame.width() * scale), static_cast<float>(frame.height() * scale));

    // The caller binds the border shader (a KWin::ShaderBinder kept in scope
    // through the subsequent effects->drawWindow) — setUniform writes to the
    // currently bound program, so we must NOT bind/unbind here or the uniforms
    // would be set on the wrong (or no) program.
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
    if (pack.uFocusedLoc >= 0) {
        const float focused = (KWin::effects && w == KWin::effects->activeWindow()) ? 1.0f : 0.0f;
        shader->setUniform(pack.uFocusedLoc, focused);
    }
    // Continuous time for an animated pack. -1 (static pack, e.g. the border)
    // pushes nothing; postPaintScreen only drives the window to repaint when a
    // pack actually references iTime (windowSurfaceAnimates), so a static
    // decoration neither pays this push nor forces per-frame repaints.
    if (pack.uTimeLoc >= 0) {
        shader->setUniform(pack.uTimeLoc, surfaceShaderTimeSeconds());
    }

    // Pack-declared parameters (customParams / customColors). Values are resolved
    // at compile time from the pack's DecorationProfile overrides merged over its
    // declared defaults (compiledPack). Only slots the shader actually references
    // resolve to a valid location.
    //
    // Per-window border override: when the base "border" pack is driven by a
    // window rule, push THIS window's rule-resolved width / radius / colours in
    // place of the pack's shared metadata defaults (the per-pack-id cache bakes a
    // single default set, but the rule values vary per window). The border pack
    // lays its params out as customParams[0] = (borderWidth, cornerRadius,
    // useSystemAccent, _) and colours as customColors[0]=active /
    // customColors[1]=inactive, matching data/surface/border/metadata.json; the
    // shader selects active vs inactive by uSurfaceFocused (pushed above).
    // useSystemAccent is forced false because the rule colour is already
    // accent-resolved (resolveWindowAppearance maps the accent sentinel to the
    // live accent before it lands here).
    auto paramsValues = pack.customParamsValues;
    auto colorsValues = pack.customColorsValues;
    if (wb.ruleBorder) {
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
    // Apply the border shader passively for static bordered windows: bind it +
    // set its uniforms, then let OffscreenEffect::drawWindow re-blit the
    // redirected FBO through it. OffscreenData::paint re-binds the SAME program
    // (the one from setShader in reconcileBorderShader), and uniform values
    // persist in the program object across our ShaderBinder pop — so the values
    // we set here are live for that blit. This is the KDE-Rounded-Corners model:
    // the shader applies on every composite, idle included, with no re-render,
    // and no forced per-frame repaints. Skip during a transition (the animation
    // shader owns the setShader slot and paintWindow's transition branch drives
    // it) and during snapshot capture.
    //
    // MULTIPASS surface packs additionally run their buffer passes
    // (renderSurfaceBufferPasses) into per-window FBOs and bind the outputs as
    // iChannel0..3 so the main pass can sample them. Single-pass packs (the
    // border, pack.bufferPasses empty) skip all of this and take the cheap
    // OffscreenData path unchanged.
    //
    // Texture-unit map for the idle border blit's multipass channels: start a
    // few units PAST the animation path's user-texture / old-snapshot /
    // surface-layer units (which live at 0..2+kMaxUserTextureSlots on the
    // paintWindow transition path) so the two paths never collide even though
    // they don't run for the same window at the same time. Unit 0 is uTexture0
    // (KWin's OffscreenData::paint binds the redirected surface there); the
    // buffer-output iChannelN go to kSurfaceChannelBaseUnit + N.
    int boundChannels = 0; // # of iChannel units we bound (for post-draw cleanup)
    constexpr int kSurfaceChannelBaseUnit = 3 + PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots;
    if (!m_capturingSnapshot && !m_windowBorders.isEmpty() && !m_shaderManager.findTransition(w)) {
        const QString wid = getWindowId(w);
        const auto bit = m_windowBorders.constFind(wid);
        if (bit != m_windowBorders.constEnd() && bit->shaderApplied && bit->chain.size() > 1) {
            // MULTI-PACK present: the whole chain was already composited into a
            // per-window FBO by paintWindow (renderSurfaceChainComposite). Bind the
            // final slot to a high unit and point the present passthrough's uFinal
            // at it. OffscreenData::paint re-binds the present program (the
            // setShader one from reconcileBorderShader) for its blit, so the
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
                glActiveTexture(GL_TEXTURE0);
                boundChannels = 1; // unit kSurfaceChannelBaseUnit+0, freed in the cleanup below
            }
        } else if (bit != m_windowBorders.constEnd() && bit->shaderApplied) {
            // Per-window resolved base pack — replaces the old single global
            // m_borderShader. nullptr → compile failed/latched (render nothing).
            CompiledSurfacePack* const pack = compiledPackForWindow(wid);
            if (pack) {
                // Multipass buffer outputs are rendered in paintWindow
                // (renderSurfaceBufferPasses), NOT here. That render re-enters the
                // draw chain (effects->drawWindow) to capture the raw surface;
                // calling it from inside THIS drawWindow override would re-enter
                // KWin's shared draw-window iterator while it is already mid-walk,
                // corrupting it and crashing the OffscreenEffect::drawWindow below.
                // paintWindow runs the capture on a fresh iterator; here we only bind
                // the ready per-window buffer textures as iChannels.
                const auto stateIt = m_surfaceMultipass.find(wid);
                const bool channelsReady = !pack->bufferPasses.empty() && stateIt != m_surfaceMultipass.end()
                    && !stateIt->second.bufferTex.empty();

                KWin::ShaderBinder binder(pack->shader.get());
                pushBorderUniforms(w, *bit, *pack, viewport.scale());

                if (channelsReady) {
                    const SurfaceMultipassState& state = stateIt->second;
                    const int n = qMin(static_cast<int>(state.bufferTex.size()), 4);
                    for (int i = 0; i < n; ++i) {
                        if (!state.bufferTex[i]) {
                            continue;
                        }
                        const int unit = kSurfaceChannelBaseUnit + i;
                        glActiveTexture(GL_TEXTURE0 + unit);
                        state.bufferTex[i]->bind();
                        if (pack->iChannelLoc[i] >= 0) {
                            pack->shader->setUniform(pack->iChannelLoc[i], unit);
                        }
                        if (pack->iChannelResolutionLoc[i] >= 0) {
                            const QVector4D res(static_cast<float>(state.bufferTex[i]->width()),
                                                static_cast<float>(state.bufferTex[i]->height()), 0.0f, 0.0f);
                            pack->shader->setUniform(pack->iChannelResolutionLoc[i], res);
                        }
                        ++boundChannels;
                    }
                    // Restore GL_TEXTURE0 as the active unit so OffscreenData::paint
                    // (which binds the redirected surface to unit 0 without a
                    // preceding glActiveTexture) targets the right unit.
                    glActiveTexture(GL_TEXTURE0);
                }
            }
        }
    }
    KWin::OffscreenEffect::drawWindow(renderTarget, viewport, w, mask, deviceRegion, data);

    // Unbind the multipass channel units we bound and restore GL_TEXTURE0 —
    // texture hygiene mirroring paint_pipeline.cpp, so a stray bind doesn't leak
    // into the next window's draw. No-op when boundChannels == 0 (single-pass).
    for (int i = 0; i < boundChannels; ++i) {
        glActiveTexture(GL_TEXTURE0 + kSurfaceChannelBaseUnit + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    if (boundChannels > 0) {
        glActiveTexture(GL_TEXTURE0);
    }
}

} // namespace PlasmaZones
