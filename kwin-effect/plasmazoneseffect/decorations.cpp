// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>
#include <window.h>

#include <epoxy/gl.h>

#include "../autotilehandler.h"
#include "../snaphandler.h"
#include "shader_internal.h"
#include "shader_resolve.h"

#include <PhosphorCompositor/DecorationDefaults.h>
#include <PhosphorRules/RuleAction.h>

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>

#include <QByteArray>
#include <QColor>
#include <QVariantMap>
#include <QVector2D>
#include <QVector4D>

#include <optional>
#include <utility>

namespace PlasmaZones {

void PlasmaZonesEffect::setupDecorationManager()
{
    connect(m_decorationManager.get(), &DecorationManager::windowDecorationRestored, this,
            [this](const QString& windowId) {
                // A veto-driven restore leaves the window rule-owned and
                // still border-eligible — rebuild its overlay instead of
                // dropping it. updateWindowDecoration self-gates on the merged
                // appearance (it removes first and re-creates only when
                // something should show). Border overlays are visual-only,
                // so off-desktop windows just get their stale item dropped —
                // the desktopChanged → updateAllDecorations refresh rebuilds
                // theirs when they become visible (same policy as
                // updateAllDecorations and markWindowSnapped). Exact-id re-check:
                // findWindowById's fuzzy appId fallback could resolve a
                // same-app sibling for a dead id, and creating a border item
                // keyed under the dead id against the sibling would linger
                // until the next full rebuild.
                KWin::EffectWindow* w = findWindowById(windowId);
                if (w && getWindowId(w) == windowId && w->isOnCurrentDesktop()) {
                    updateWindowDecoration(windowId, w);
                } else {
                    removeWindowDecoration(windowId);
                }
            });
}

void PlasmaZonesEffect::removeWindowDecoration(const QString& windowId, KWin::EffectWindow* windowHint)
{
    auto it = m_windowDecorations.find(windowId);
    if (it == m_windowDecorations.end()) {
        return;
    }
    WindowDecoration& wb = it.value();
    // Resolve the EXACT window this decoration belongs to, once, for both the
    // GL teardown and the multipass-guard below. findWindowById's fuzzy appId
    // fallback can return a same-app SIBLING for a dead windowId, and tearing
    // down the sibling's redirect / erasing its FBO would be wrong — the
    // callers (reconcileDecorationOnPlacementFlip, the windowDecorationRestored
    // handler) re-check getWindowId(w) == windowId precisely to avoid this, but
    // this internal re-resolution must too. Preference order: an exact-id live
    // match; else the frozen reverse mapping (resolves a deleted window under
    // its exact id, and survives a close transition); else the caller's hint
    // (the close path passes the corpse, whose getWindowId no longer
    // re-derives to windowId, so it can't be exact-checked here).
    KWin::EffectWindow* target = findWindowById(windowId);
    if (target && getWindowId(target) != windowId) {
        target = nullptr;
    }
    if (!target) {
        target = m_windowIdReverse.value(windowId);
    }
    if (!target) {
        target = windowHint;
    }
    // Release the offscreen redirect + border shader slot IF this border owns
    // it. When an animation transition currently owns the slot (shaderApplied
    // is false — the transition's begin took it over), we must NOT touch
    // setShader / unredirect: the transition lifecycle owns the handover and a
    // stray unredirect here would tear down the animation mid-flight. The
    // transition-end path re-checks m_windowDecorations and only re-applies the
    // border shader when the border still exists, so dropping the entry here
    // (after this guarded release) is the correct teardown.
    if (wb.shaderApplied) {
        // setShader / unredirect on the deleted-but-still-painted window are
        // safe: KWin keys both on the redirected-windows map, and the redirect
        // stays live until the window is discarded. Skipping the release left
        // the corpse redirected with the present shader still bound while the
        // multipass erase below destroyed the composite textures its sampler
        // uniforms referenced — GL auto-unbinds a deleted texture, an unbound
        // sampler reads opaque black, and the next paint of the Deleted drew
        // the whole expanded quad black (the close flash).
        KWin::EffectWindow* w = target;
        if (w) {
            // Only clear when no transition raced in to own the slot between
            // this border being applied and now — reconcileDecorationShader would
            // have cleared shaderApplied in that case, but guard defensively.
            if (!m_shaderManager.findTransition(w)) {
                setShader(w, nullptr);
                unredirect(w);
                // Dropping the redirect/shader on a STATIC window generates no
                // damage of its own (mirror of the addRepaintFull on the apply
                // path in updateWindowDecoration) — without this the stale bordered
                // frame lingers until unrelated damage arrives.
                w->addRepaintFull();
                // A padded chain painted a margin band OUTSIDE the window rect;
                // per-window repaints clip to the window item, so damage it at
                // screen level or the stale glow lingers after removal.
                if (wb.outerPadding > 0 && KWin::effects) {
                    QRectF padded = w->expandedGeometry();
                    if (padded.isEmpty()) {
                        padded = w->frameGeometry();
                    }
                    const int pad = wb.outerPadding;
                    KWin::effects->addRepaint(KWin::RectF(padded.adjusted(-pad, -pad, pad, pad)));
                }
            }
        }
    }
    if (wb.paddedGeoConnection) {
        disconnect(wb.paddedGeoConnection);
    }
    m_windowDecorations.erase(it);
    // The audio-reactive decoration set may have shrunk — re-evaluate whether the
    // effect's cava instance is still needed. DEFERRED + coalesced: this runs as
    // the remove-first step of updateWindowDecoration on every focus/snap/rule
    // refresh, and a synchronous stop() here would block the compositor thread
    // (cava terminate + waitForFinished) and then respawn when the decoration is
    // re-added a step later. scheduleEffectAudioSync collapses the remove+readd
    // to one net decision at event-loop return. Runs before the transition
    // early-return below so both exits keep the run gate in sync.
    scheduleEffectAudioSync();
    // NB: m_focusFade is deliberately NOT scrubbed here. updateWindowDecoration
    // calls removeWindowDecoration as its remove-first step on every refresh
    // (focus change / snap flip / rule edit), so scrubbing here would reset the
    // focus cross-fade on each one. The undecorated→decorated snap is handled in
    // updateWindowDecoration (wasDecorated); close/delete scrub it explicitly.

    // Free the window's composite / buffer FBO targets, reclaiming GPU
    // memory — UNLESS a shader transition is mid-flight on this window. The
    // rotate/snap paths re-resolve the decoration DURING the animation
    // (state flip → rule flush → updateWindowDecoration → here), and erasing the
    // entry destroys the very composite the animation is sampling — the
    // at-draw probes caught windows animating with compositeTexId 0. The
    // transition keeps the state; its own teardown (endShaderTransition →
    // removeWindowDecoration, by then with no live transition) or the
    // windowDeleted backstop erases it.
    // Reuses the exact `target` resolved at the top (exact-id live match, else
    // the frozen reverse mapping, else the hint) — a fuzzy sibling here would
    // early-return on the SIBLING's transition and leak this window's FBO.
    if (target && m_shaderManager.findTransition(target)) {
        return;
    }
    m_surfaceMultipass.erase(windowId);
}

void PlasmaZonesEffect::reconcileDecorationOnPlacementFlip(const QString& windowId)
{
    // SHARED placement-flip funnel — the one way BOTH engines re-decorate a
    // window whose snapped / tiled / floating state just changed. History:
    // snapping tore the decoration down here and rebuilt it a deferred turn
    // later (the drag-start blackout), while autotile removed it and relied
    // on its caller's bulk updateAllDecorations, which only one call path
    // performed. Re-resolving update-or-remove in the SAME turn under the
    // window's new placement state is the only correct shape: the swap is
    // invisible for a chain that resolves identically across states (the
    // root-authored case), and a state-scoped chain correctly appears or
    // drops. Callers flip their engine facts (zone cache, tiled set,
    // floating flag) BEFORE calling, so the resolve sees the new state.
    // Exact-id re-check like the windowDecorationRestored path: the fuzzy
    // appId fallback must not decorate a same-app sibling under a dead id.
    KWin::EffectWindow* w = findWindowById(windowId);
    if (w && getWindowId(w) == windowId && w->isOnCurrentDesktop()) {
        updateWindowDecoration(windowId, w);
    } else {
        // Same exact-id discipline for the remove hint: a fuzzy same-app
        // sibling must not stand in for the dead window's GL release, so only
        // pass w through when it really is windowId (e.g. merely off-desktop).
        removeWindowDecoration(windowId, (w && getWindowId(w) == windowId) ? w : nullptr);
    }
}

void PlasmaZonesEffect::clearAllDecorations()
{
    // Skip entries whose window is riding a live shader transition — the
    // close path DELIBERATELY keeps the closing window's border + multipass
    // entries alive so renderSurfaceChain can composite the decoration under
    // the close animation, and this bulk clear runs within milliseconds of
    // every close (focus shift → updateAllDecorations, autotile re-layout).
    // Removing them here nuked the kept state before the first close frame:
    // findWindowById refuses deleted windows and the bulk path has no window
    // hint, so removeWindowDecoration's own mid-transition guard could never
    // engage. The frozen reverse mapping (kept alive through the transition
    // by slotWindowClosed) resolves the deleted window here; the skipped
    // entry is erased by endShaderTransition's teardown or the windowDeleted
    // backstop.
    const QStringList ids = m_windowDecorations.keys();
    for (const QString& windowId : ids) {
        if (KWin::EffectWindow* w = m_windowIdReverse.value(windowId)) {
            if (m_shaderManager.findTransition(w)) {
                continue;
            }
        }
        removeWindowDecoration(windowId);
    }
}

// windowMatchesAppearanceScope and resolveEffectiveWindowAppearance live in
// decoration_appearance.cpp (split out to keep this TU under the 800-line
// limit, mirroring the surface_backdrop.cpp split).

void PlasmaZonesEffect::updateWindowDecoration(const QString& windowId, KWin::EffectWindow* w,
                                               std::optional<bool> wasDecoratedHint)
{
    // Did this window already have a decoration? Captured BEFORE the remove-first
    // below so the focus cross-fade can tell a genuine undecorated→decorated
    // transition (must SNAP to the current focus) from a plain refresh (focus
    // change / snap flip / rule edit — all of which remove-then-readd and must
    // keep easing). See the m_focusFade reconcile after the insert.
    // updateAllDecorations supplies the hint: it clears the whole decoration map
    // before re-adding, so the local lookup would report false for EVERY window
    // and scrub every in-flight ramp — the fade would snap on each focus change.
    const bool wasDecorated = wasDecoratedHint.value_or(m_windowDecorations.contains(windowId));
    // Remove existing border for this window first
    removeWindowDecoration(windowId);

    if (!w || w->isMinimized() || w->isFullScreen()) {
        return;
    }

    // APP-WINDOW GATE: decoration-specific filter (shouldDecorateWindow), NOT
    // the snapping shouldHandleWindow. It rejects the always-wrong surfaces
    // (docks, panels, desktop, OSDs, notifications, portal / plasma-shell,
    // our own overlays) and honours the user Exclude rules, but the
    // transient family and a minimum-size threshold are user-tunable via the
    // Decorations.WindowFiltering settings (m_decorationExcludeTransientWindows /
    // m_decorationMinWindow{Width,Height}). Defaults preserve the prior
    // shouldHandleWindow behavior (transients skipped, no size threshold).
    if (!shouldDecorateWindow(w)) {
        return;
    }

    // User decoration packs from the tree. The reserved "border" and
    // "opacity-tint" ids are config/rule-owned (the plain layers), so an
    // explicit user entry for either is dropped here — otherwise a stray
    // tree entry would flip the window into CUSTOM mode, silently disabling
    // every plain layer, and render the reserved pack with its baked
    // defaults instead of the resolved appearance.
    // enabledChain(): packs the user toggled off stay in the profile but must
    // not render, exactly like a disabled rule is skipped by the evaluator.
    QStringList userPacks;
    const QString surfacePath = resolveSurfacePathFor(windowId);
    const PhosphorSurfaceShaders::DecorationProfile resolvedProfile = m_decorationTree.resolve(surfacePath);
    const QStringList treeChain = resolvedProfile.enabledChain();
    for (const QString& pack : treeChain) {
        if (pack != QLatin1String("border") && pack != QLatin1String("opacity-tint")) {
            userPacks.append(pack);
        }
    }

    // Rule-resolved decoration-chain override: a matched
    // OverrideDecorationChain rule REPLACES the tree's user packs wholesale
    // (its empty-chain sentinel blocks decoration outright), and its
    // per-pack params override the tree profile's map below. The reserved
    // "border" id was already filtered by the resolver, and the tree's
    // per-layer disable set deliberately does not apply — a rule chain is
    // explicit. Reads the same cached per-window action walk the opacity /
    // border-appearance resolvers use, so it refreshes on every trigger
    // that re-runs updateWindowDecoration (rule edits, focus, snap flips,
    // desktop changes).
    const std::optional<ResolvedDecorationChain> ruleChain = resolveDecorationChain(resolveRuleActions(w, windowId));
    if (ruleChain) {
        userPacks = ruleChain->chain;
    }

    // EASY vs CUSTOM MODE: any user pack on the window (tree chain or rule
    // chain override) puts the window in custom mode — the packs and their own
    // parameters are the whole decoration, and the plain layers (the Windows.*
    // config defaults plus the SetBorder* / SetOpacityTintVisible / SetTint*
    // rule slots) neither render nor apply. Only a window with NO user packs
    // resolves the appearance: the config-backed defaults (each gated by its
    // scope) with per-window rule overrides layered on top, rendered by the
    // reserved "border" and "opacity-tint" surface-shader packs. An empty rule
    // chain (the "no decoration" sentinel) strips the user packs and thus
    // lands back in easy mode — SetBorderVisible / SetOpacityTintVisible stay
    // the layer-off switches. The title-bar slot is NOT part of this split
    // (reconcileRuleHiddenTitleBar resolves it separately). The SetOpacity
    // rule is layer-backed, full stop: it renders through the opacity-tint
    // layer when the layer is on (folded into the pack's opacity param,
    // replacing the config value) and nowhere else. Custom chains dim
    // through their own pack params (frost/glass contentOpacity); a chain
    // without one, a vetoed/off layer, or an undecorated window does not
    // honour the rule — the user's packs (or their transparent theme) own
    // the window's alpha.
    std::optional<ResolvedWindowAppearance> appearance;
    bool showBorder = false;
    bool showOpacityTint = false;
    if (userPacks.isEmpty()) {
        appearance = resolveEffectiveWindowAppearance(w, windowId);
        showBorder = appearance->showBorder.value_or(false);
        showOpacityTint = appearance->showOpacityTint.value_or(false);
        // NO-OP LAYER GATE: an opacity-tint layer at its resting values
        // (opacity 1.0, tint strength 0.0, and no SetOpacity rules loaded)
        // composites to exactly the raw window, but still costs the redirect
        // + per-frame composite fold. Drop it — the output is identical.
        // Gated on rule PRESENCE (hasOpacityRules), not on a per-window
        // match, so a focus-scoped SetOpacity rule doesn't churn the chain
        // (and its composite allocations) on every focus flip; tintStrength
        // needs no such coarsening because the appearance already folds the
        // per-window rule slot.
        if (showOpacityTint && appearance->tintStrength.value_or(0.0) <= 0.0 && appearance->opacity.value_or(1.0) >= 1.0
            && !m_shaderManager.hasOpacityRules()) {
            showOpacityTint = false;
        }
    }

    // DECORATE GATE: the chain is either the plain layers (easy mode — the
    // reserved "border" base and/or the "opacity-tint" layer, both on →
    // both render, opacity-tint folding OVER the border so the window fades
    // as a whole) or the user packs (custom mode). Neither → nothing to
    // render.
    QStringList chain;
    if (showBorder) {
        chain.append(QStringLiteral("border"));
    }
    if (showOpacityTint) {
        chain.append(QStringLiteral("opacity-tint"));
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
    WindowDecoration wb;
    wb.chain = chain;
    wb.basePackId = basePackId;

    // Resolve THIS window's param values for every pack in the chain. Windows
    // on different surface paths (window.tiled / window.snapped /
    // window.floating) can carry different per-pack overrides for the same
    // pack id, while the shared CompiledSurfacePack bakes only the FIRST
    // resolver's values — the render paths prefer these per-window arrays.
    // They refresh on exactly the chain's triggers (tree change, tile/snap
    // membership change) because they ride the same updateWindowDecoration call.
    // A pack the registry cannot resolve gets no entry; consumers fall back
    // to the compiled pack's baked baseline.
    ensureSurfaceRegistryPaths();
    QVariantMap allPackParams = resolvedProfile.effectiveParameters();
    if (ruleChain) {
        // Per-pack REPLACE, not deep-merge: a rule that carries params for a
        // pack owns that pack's values outright (mirroring the animation
        // override's params semantics); packs the rule says nothing about
        // keep the tree/default values.
        for (auto it = ruleChain->params.constBegin(); it != ruleChain->params.constEnd(); ++it) {
            allPackParams.insert(it.key(), it.value());
        }
    }
    if (showBorder) {
        // Easy mode: the resolved border appearance rides the reserved
        // "border" pack's OWN declared parameters (borderWidth / cornerRadius /
        // activeColor / inactiveColor, see data/surface/border/metadata.json),
        // routed by id through the same translateSurfaceParams path every
        // other pack's overrides take — no positional slot layout is assumed
        // anywhere. width / radius fall back to the shared DecorationDefaults
        // when only "show border" is set; an omitted colour falls back to the
        // live system accent (matching the useSystemAccent default), which is
        // forced off because the colours arrive here already accent-resolved.
        // inactiveColor mirrors active when unset. The shader picks active vs
        // inactive by uSurfaceFocused. Neither the tree nor a rule chain can
        // carry "border" params (both strip the reserved id), so this insert
        // overwrites nothing.
        QVariantMap borderParams;
        borderParams.insert(QStringLiteral("borderWidth"),
                            appearance->borderWidth.value_or(PhosphorCompositor::DecorationDefaults::BorderWidth));
        borderParams.insert(QStringLiteral("cornerRadius"),
                            appearance->borderRadius.value_or(PhosphorCompositor::DecorationDefaults::BorderRadius));
        borderParams.insert(QStringLiteral("useSystemAccent"), false);
        const QColor accentOr =
            m_borderAccentColor.isValid() ? m_borderAccentColor : QColor(QStringLiteral("#ff3daee9"));
        const QColor active = appearance->activeColor.value_or(accentOr);
        borderParams.insert(QStringLiteral("activeColor"), active);
        borderParams.insert(QStringLiteral("inactiveColor"), appearance->inactiveColor.value_or(active));
        allPackParams.insert(QStringLiteral("border"), borderParams);
    }
    if (showOpacityTint) {
        // Easy mode: the plain opacity+tint layer rides the "opacity-tint"
        // pack's own declared parameters, routed by id like the border above.
        // Every slot is config default with the per-window rule winning:
        // opacity = the SetOpacity rule when one matches, else the config
        // value; tint strength/colour are the rule-resolved slots with the
        // config default filled in (the fallbacks here are the pack's own
        // defaults, hit only when the layer was rule-forced on for an
        // out-of-scope window). Folding SetOpacity here makes the layer the
        // window's SOLE opacity applier: the rule has no other application
        // path (no KWin paint-data opacity, no present modulation), and
        // chainBakesOpacity below keeps the transition fallback from
        // re-applying it over the folded composite. userPacks is empty in
        // easy mode, so this insert can't clobber a user's own opacity-tint
        // params.
        std::optional<qreal> ruleOpacity;
        if (m_shaderManager.hasOpacityRules()) {
            ruleOpacity = resolveWindowOpacity(resolveRuleActions(w, windowId));
        }
        const double effectiveOpacity =
            ruleOpacity ? qBound(0.0, *ruleOpacity, 1.0) : qBound(0.0, appearance->opacity.value_or(1.0), 1.0);
        wb.foldedOpacity = effectiveOpacity;
        QVariantMap otParams;
        otParams.insert(QStringLiteral("opacity"), effectiveOpacity);
        otParams.insert(QStringLiteral("tintStrength"), appearance->tintStrength.value_or(0.0));
        const QColor accentOr =
            m_borderAccentColor.isValid() ? m_borderAccentColor : QColor(QStringLiteral("#ff3daee9"));
        otParams.insert(QStringLiteral("tintColor"), appearance->tintColor.value_or(accentOr));
        allPackParams.insert(QStringLiteral("opacity-tint"), otParams);
    }
    int outerPadding = 0;
    bool needsBackdrop = false;
    for (const QString& packId : std::as_const(chain)) {
        const PhosphorSurfaceShaders::SurfaceShaderEffect eff = m_surfaceShaderRegistry.effect(packId);
        if (!eff.isValid()) {
            continue;
        }
        // Any needsBackdrop pack in the chain switches the window onto the
        // composite path with a per-frame backdrop capture (see paintWindow).
        needsBackdrop = needsBackdrop || eff.needsBackdrop;
        const QVariantMap packOverrides = allPackParams.value(packId).toMap();
        wb.packParamValues.insert(packId, ShaderInternal::resolveSurfaceParamValues(eff, packOverrides));

        // Outer-margin request (e.g. the glow pack's glowSize): the resolved
        // per-surface override wins, else the param's declared default. The
        // chain's largest request pads the capture canvas (composite path).
        if (!eff.paddingParam.isEmpty()) {
            double request = 0.0;
            if (packOverrides.contains(eff.paddingParam)) {
                request = packOverrides.value(eff.paddingParam).toDouble();
            } else {
                for (const auto& param : eff.parameters) {
                    if (param.id == eff.paddingParam) {
                        request = param.defaultValue.toDouble();
                        break;
                    }
                }
            }
            outerPadding = qMax(outerPadding, qCeil(request));
        }
    }
    // Defensive cap: a hostile/typo'd pack can't request an absurd canvas.
    wb.outerPadding = qBound(0, outerPadding, PhosphorSurfaceShaders::kMaxDecorationOuterPaddingPx);
    wb.needsBackdrop = needsBackdrop;
    // The plain opacity-tint layer folds the window's resolved opacity
    // (config default, SetOpacity rule winning) into its pack param — the
    // chain BAKES the window's opacity. The flag's one runtime job is the
    // transition iWindowOpacity push: 1.0 when the fold produced the
    // composite the transition samples, the rule-resolved fallback otherwise
    // (see paintWindow).
    wb.chainBakesOpacity = showOpacityTint;

    // Padded chains paint a margin band OUTSIDE the window rect; KWin's own
    // move/resize damage covers only the window's old/new rects, so track the
    // padded rect and damage old ∪ new at screen level on geometry changes or
    // the glow trails behind a dragged window. Disconnected by
    // removeWindowDecoration (updateWindowDecoration always removes first).
    if (wb.outerPadding > 0) {
        QRectF paddedNow = w->expandedGeometry();
        if (paddedNow.isEmpty()) {
            paddedNow = w->frameGeometry();
        }
        const int pad0 = wb.outerPadding;
        wb.lastPaddedGeo = paddedNow.adjusted(-pad0, -pad0, pad0, pad0);
        const QString wid = windowId; // capture by value
        wb.paddedGeoConnection = connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, this,
                                         [this, wid](KWin::EffectWindow* ew, const QRectF& /*oldGeo*/) {
                                             auto it = m_windowDecorations.find(wid);
                                             if (it == m_windowDecorations.end() || !ew || !KWin::effects) {
                                                 return;
                                             }
                                             QRectF padded = ew->expandedGeometry();
                                             if (padded.isEmpty()) {
                                                 padded = ew->frameGeometry();
                                             }
                                             const int pad = it->outerPadding;
                                             padded.adjust(-pad, -pad, pad, pad);
                                             if (it->lastPaddedGeo.isValid()) {
                                                 KWin::effects->addRepaint(KWin::RectF(it->lastPaddedGeo));
                                             }
                                             KWin::effects->addRepaint(KWin::RectF(padded));
                                             it->lastPaddedGeo = padded;
                                         });
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

    m_windowDecorations.insert(windowId, wb);

    // A chain carrying an audio-reactive pack (SurfaceShaderEffect::audio) may
    // have just appeared — start the effect's cava instance if the audio-viz
    // toggle is on. DEFERRED + coalesced so this collapses with the remove-first
    // step's schedule above into one net evaluation (no stop-then-start churn).
    scheduleEffectAudioSync();

    // Undecorated→decorated transition: drop any stale focus ramp so
    // advanceFocusFade re-seeds its -1 sentinel and snaps to the current focus
    // instead of resuming from a value left by an earlier decorate cycle (a
    // window decorated, undecorated while open, then re-decorated keeps its
    // m_focusFade entry because removeWindowDecoration deliberately does not
    // scrub it). A refresh (wasDecorated) keeps the ramp so the cross-fade stays
    // continuous across the remove-then-readd.
    if (!wasDecorated) {
        m_focusFade.remove(windowId);
    }

    // Apply the offscreen border shader (redirect + setShader) unless an
    // animation transition currently owns this window's shader slot — in which
    // case reconcileDecorationShader leaves the transition alone and the border
    // shader is (re-)applied when the transition ends. A geometry change drives
    // its own repaint, so no manual repaint is needed here for the steady case;
    // request one anyway so a border added to a STATIC window (no pending
    // damage) reaches paintWindow and the outline becomes visible immediately.
    reconcileDecorationShader(windowId, w);
    // `w` is non-null here (the early-out above returns on !w), so no
    // KWin::effects guard is needed — addRepaintFull is a window method.
    w->addRepaintFull();
    // Padded chains paint OUTSIDE the window's expanded rect; addRepaintFull
    // (and addLayerRepaint) clip to the window item, so the margin band needs
    // screen-level damage (the documented surface-extent repaint pitfall).
    if (wb.outerPadding > 0 && KWin::effects) {
        QRectF padded = w->expandedGeometry();
        if (padded.isEmpty()) {
            padded = w->frameGeometry();
        }
        const int pad = wb.outerPadding;
        KWin::effects->addRepaint(KWin::RectF(padded.adjusted(-pad, -pad, pad, pad)));
    }
}

void PlasmaZonesEffect::updateAllDecorations()
{
    // Snapshot which windows were decorated BEFORE the bulk clear: the hint
    // passed to updateWindowDecoration below is what lets the focus cross-fade
    // survive this reconcile (a post-clear contains() lookup would report every
    // window as freshly decorated and snap its ramp).
    const QSet<QString> previouslyDecorated(m_windowDecorations.keyBegin(), m_windowDecorations.keyEnd());

    clearAllDecorations();

    // Iterate all effect windows and reconcile each window's border + title-bar
    // from its effective appearance (config-backed default gated by scope, with
    // user rules overriding per slot). updateWindowDecoration /
    // reconcileRuleHiddenTitleBar self-gate on that merged appearance (plus the
    // app-window filter and the resolved decoration chain), so this runs
    // UNCONDITIONALLY — a config default border / hidden title bar must apply
    // even with an empty rule set, and a per-window reconcile to the unset state
    // clears a now-stale override left by a removed rule (so no bulk restore
    // pass is needed here).
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
        // inside updateWindowDecoration (app-window filter + resolved chain) decides
        // whether it actually decorates. Title-bar hiding (setNoBorder) is a
        // persistent decoration-state change that survives desktop switches, so
        // reconcile it below for ALL windows the appearance may hide — otherwise
        // a hide (rule or config default) applying to a window on another
        // virtual desktop would not take effect until next activation.
        if (w->isOnCurrentDesktop()) {
            updateWindowDecoration(wid, w, previouslyDecorated.contains(wid));
        }
        reconcileRuleHiddenTitleBar(wid, w);
        // Stacking layer is persistent window state like the title bar, so it
        // reconciles for ALL windows regardless of desktop too — and the
        // reconcile-to-unset restores a window whose SetWindowLayer rule was
        // just removed, mirroring the title-bar note above.
        reconcileRuleWindowLayer(wid, w);
    }
}

void PlasmaZonesEffect::reconcileRuleHiddenTitleBar(const QString& windowId, KWin::EffectWindow* w)
{
    if (!w || windowId.isEmpty()) {
        return;
    }
    // Tri-state override, forwarded to the DecorationManager (Rule is the only
    // owner kind now — there are no mode owners to defer to):
    //   unset → no owner, the title bar shows
    //   true  → hide the title bar (a rule, or the config default in scope)
    //   false → FORCE-SHOW (a veto pinning the decoration visible — only an
    //           explicit SetHideTitleBar=false rule; the config default never
    //           contributes a force-show, see resolveEffectiveWindowAppearance)
    // The manager owns the capability gate and the geometry re-assert across
    // veto-driven decoration flips.
    const ResolvedWindowAppearance ovr = resolveEffectiveWindowAppearance(w, windowId);
    m_decorationManager->setRuleOverride(windowId, ovr.hideTitleBar);
}

void PlasmaZonesEffect::reconcileRuleWindowLayer(const QString& windowId, KWin::EffectWindow* w)
{
    if (!w || w->isDeleted() || windowId.isEmpty()) {
        return;
    }
    // No-layer-rules fast path: with no enabled SetWindowLayer rule and no
    // snapshot to drain, the resolve below can neither apply nor restore
    // anything. Keeps the "no-rules case pays nothing" invariant (see
    // shouldAnimateWindow) on the focus-driven updateAllDecorations path —
    // without this, the layer reconcile would be the one consumer building
    // the ~30-accessor rule query for sessions whose rules never touch the
    // layer (opacity/border-only rule sets included). A lingering snapshot
    // still falls through so the restore below drains it.
    if (!m_shaderManager.hasWindowLayerRules() && !m_ruleWindowLayerSnapshots.contains(windowId)) {
        return;
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        return;
    }
    // Structural / own-surface shield, extending the SetOpacity paint gate
    // (paint_pipeline.cpp shields only the overlay / plasmashell classes;
    // the portal and structural entries here are deliberate hardening for a
    // raw window-state write): a broad match expression must never demote a
    // dock, pin a notification, or strip the daemon overlay's own
    // keep-above. Transients / popups are deliberately NOT shielded:
    // transient utility surfaces are legitimate layer-rule targets, and
    // transient exclusion is per-feature user opt-in in this project (the
    // IsTransient match field), never hardcoded policy. A shielded window
    // resolves as rule-free rather than early-returning — window
    // classification can mutate mid-session (the Electron/CEF class swap,
    // an X11 type change), and a window that was rule-held BEFORE mutating
    // into a shielded class must drain its snapshot through the restore
    // branch below instead of stranding the rule's flags. Fresh shielded
    // windows skip the resolve entirely and never enter the map.
    const QString winClass = w->windowClass();
    const bool shielded = isOwnOverlayClass(winClass) || isPlasmaShellSurface(winClass)
        || isXdgDesktopPortalSurface(winClass) || w->isDesktop() || w->isDock() || w->isNotification()
        || w->isCriticalNotification() || w->isOnScreenDisplay();
    std::optional<QString> layer;
    if (!shielded) {
        layer = resolveWindowLayer(resolveRuleActions(w, windowId));
    }
    const auto it = m_ruleWindowLayerSnapshots.find(windowId);
    if (!layer) {
        // No rule owns the layer. If one did before, put the user's own flags
        // back exactly once and forget the window. Erase BEFORE the setters:
        // they emit KWin signals, and holding a QHash iterator across code
        // that could re-enter this map would be undefined if a future
        // connection routes a keep-above change back into a reconcile.
        if (it != m_ruleWindowLayerSnapshots.end()) {
            const WindowLayerSnapshot snapshot = *it;
            m_ruleWindowLayerSnapshots.erase(it);
            kw->setKeepAbove(snapshot.keepAbove);
            kw->setKeepBelow(snapshot.keepBelow);
        }
        return;
    }
    // First application snapshots the pre-rule flags so the restore above
    // returns the window to the USER's state. Deliberately not re-captured
    // while a rule owns the layer: a manual keepAbove toggle under an active
    // rule is re-asserted away on the next reconcile (the rule owns the
    // property while it matches, same as the DecorationManager rule override),
    // and capturing it would corrupt the restore target.
    if (it == m_ruleWindowLayerSnapshots.end()) {
        m_ruleWindowLayerSnapshots.insert(windowId, {kw->keepAbove(), kw->keepBelow()});
    }
    // Always write the fully-specified pair. Writing only the token's "own"
    // flag leaves the opposite one stale on an above→below rule flip (the
    // Krohnkite asymmetry bug); KWin's setters are change-gated, so the
    // re-asserts are free in the steady state.
    const bool above = (*layer == PhosphorRules::WindowLayerToken::Above);
    const bool below = (*layer == PhosphorRules::WindowLayerToken::Below);
    kw->setKeepAbove(above);
    kw->setKeepBelow(below);
}

void PlasmaZonesEffect::restoreAllRuleWindowLayers()
{
    const QHash<QString, WindowLayerSnapshot> snapshots = std::exchange(m_ruleWindowLayerSnapshots, {});
    for (auto it = snapshots.cbegin(); it != snapshots.cend(); ++it) {
        KWin::EffectWindow* w = findWindowById(it.key());
        // Exact-id re-check, matching every other findWindowById consumer in
        // this file: the fuzzy appId fallback can resolve a same-app SIBLING
        // for a gone windowId, and restoring one window's snapshot onto
        // another would clobber the sibling's own flags.
        if (!w || w->isDeleted() || getWindowId(w) != it.key()) {
            continue;
        }
        if (KWin::Window* kw = w->window()) {
            kw->setKeepAbove(it->keepAbove);
            kw->setKeepBelow(it->keepBelow);
        }
    }
}

bool PlasmaZonesEffect::isWindowMarkedSnapped(const QString& windowId) const
{
    return m_snapHandler->isTiledWindow(windowId);
}

QString PlasmaZonesEffect::resolveSurfacePathFor(const QString& windowId) const
{
    // MEMBERSHIP-only resolution: isTiledWindow tests bucket membership, and the
    // resolved profile's effectiveChain() (an empty chain = no decoration) is the
    // sole render gate (see updateWindowDecoration) — there is no separate show-border
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
    // title-bar appearance resolve from the config-backed defaults + rules
    // (resolveEffectiveWindowAppearance), not from this tree, so the baseline is
    // empty/neutral and nothing is auto-inserted. The daemon's real fetch
    // overwrites this whole tree once it arrives.
    m_decorationTree = PhosphorSurfaceShaders::DecorationProfileTree{};
}

} // namespace PlasmaZones
