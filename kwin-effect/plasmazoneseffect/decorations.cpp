// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

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
#include "surface_fold.h"
#include "shader_resolve.h"

#include <PhosphorCompositor/DecorationDefaults.h>
#include <PhosphorRules/RuleAction.h>

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>

#include <QColor>
#include <QtMath> // qCeil, resolving the chain's outer padding
#include <QVariantMap>

#include <optional>
#include <utility>

namespace PlasmaZones {

namespace {

/// Everything a WindowDecoration contributes to the cached static-prefix fold, in one
/// comparable value. updateWindowDecoration drops the fold cache only when this actually
/// moves, so an identical re-resolve (the common case) keeps the cache.
///
/// Focus has no field of its own: the fold keys on it itself
/// (SurfaceMultipassState::foldedFocus), so it must not drag the far more expensive
/// whole-chain invalidation along on every focus change. The rule-resolved border
/// appearance has no field either, for the opposite reason: in the layer-backed model it is
/// folded into packParamValues by param id, so a border recolour already moves
/// packParamValues below and needs no separate one.
///
/// Opacity and tint sit with the border, NOT with focus, and the distinction is easy to
/// misread. The fold does key on foldedOpacity separately, but the opacity-tint layer's
/// opacity / tintStrength / tintColor are ALSO pack params routed by id, so they ride
/// packParamValues and a move in them does invalidate the whole chain. That is correct and
/// unavoidable now that opacity-tint is a user-selectable pack whose params must be
/// compared like any other pack's. The cost lands on a focus-scoped SetOpacity / SetTint*
/// rule, which re-folds the static prefix on every focus flip. A plain (unscoped) opacity
/// rule or config value does not move per focus and pays nothing.
struct FoldInputs
{
    QStringList chain;
    QHash<QString, SurfaceParamValues> packParamValues;
    // Derived from the chain but compared anyway: these are what the fold actually reads, and
    // a comparison that depends on a derivation staying true elsewhere is the kind that stops
    // holding without anyone noticing. A few word-compares on a decoration refresh.
    QString basePackId;
    int outerPadding = 0;
    bool needsBackdrop = false;

    bool operator==(const FoldInputs&) const = default;
};

inline FoldInputs foldInputsOf(const WindowDecoration& wb)
{
    return FoldInputs{wb.chain, wb.packParamValues, wb.basePackId, wb.outerPadding, wb.needsBackdrop};
}

} // namespace

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

void PlasmaZonesEffect::updateWindowDecoration(const QString& windowId, KWin::EffectWindow* w)
{
    // Did this window already have a decoration? Read LIVE, and read BEFORE the
    // remove-first below, so the focus cross-fade can tell a genuine undecorated→decorated
    // transition (must SNAP to the current focus) from a plain refresh (focus change, snap
    // flip, rule edit — all of which remove-then-readd and must keep easing).
    //
    // This used to take a snapshot from updateAllDecorations, on the theory that the
    // sweep's nested re-entries (resyncWindow and the rule reconcilers emit
    // windowDecorationRestored, whose handler lands right back here) could make a live
    // lookup see an entry that was not there when the sweep began. They can — and the live
    // answer is the RIGHT one. A nested call that already decorated this window leaves a
    // focus ramp that is genuinely in flight, and TRUE is what keeps it. The snapshot said
    // false, which scrubbed that ramp and, worse, forced a full chain re-fold through
    // foldInputsMoved below for a window whose fold inputs had not moved at all. The hint
    // was strictly more destructive than the lookup it replaced.
    const bool wasDecorated = m_windowDecorations.contains(windowId);

    // What the outgoing decoration held, captured BEFORE the remove-first erases it.
    // The remove keeps the GL working set AND the redirect/shader slot (see below),
    // so the three exits where this refresh discovers the window is no longer
    // decoratable have to release both themselves — and by then the entry is gone.
    const auto priorIt = m_windowDecorations.constFind(windowId);
    const bool priorShaderApplied = priorIt != m_windowDecorations.constEnd() && priorIt->shaderApplied;
    const int priorOuterPadding = priorIt != m_windowDecorations.constEnd() ? priorIt->outerPadding : 0;
    // What the outgoing decoration baked into the cached fold, so the re-resolve
    // below can tell a real change from an identical re-resolve. See the
    // invalidation just before the insert.
    const FoldInputs priorFold = priorIt != m_windowDecorations.constEnd() ? foldInputsOf(*priorIt) : FoldInputs{};

    // Remove the existing decoration first, but KEEP its GL working set and its
    // redirect: this is a REFRESH of the same window, about to re-assert the very
    // redirect and shader a teardown would drop, and the composite targets are keyed
    // on (size, chain, scale) which the fold re-validates itself. updateAllDecorations
    // funnels through here on every focus change, so tearing down would make KWin free
    // and reallocate its OffscreenData, and cold-start our capture cache, on every
    // click. `undecorate` below is the path that genuinely tears down.
    removeWindowDecoration(windowId, w, /*keepSurfaceState=*/true);

    // Release everything the remove-first deliberately kept. For a window that turns
    // out to be undecoratable, the kept state is an orphan and the kept redirect is a
    // window left shaded with nothing to shade it with.
    const auto undecorate = [&] {
        // The SAME resolver removeWindowDecoration uses, three lines above. Not the
        // caller's `w` handed straight through: releaseSurfaceState's live-transition
        // guard is `if (target && findTransition(target))`, so a null target sails past
        // it and erases the composite an animation is still sampling, while
        // releaseDecorationGl no-ops on null and leaves the window redirected with a
        // shader whose samplers point at the textures just freed — the unbound-sampler
        // black flash both files document at length.
        //
        // No live caller can pass a null w today (all eight guard first), so this is
        // defence in depth rather than a live path. It is here because the two sites
        // must not disagree about what "the exact window" means, and they did.
        KWin::EffectWindow* const target = resolveDecorationTarget(windowId, w);
        if (priorShaderApplied) {
            releaseDecorationGl(target, priorOuterPadding);
        }
        releaseSurfaceState(windowId, target);
    };

    if (!w || w->isMinimized() || w->isFullScreen()) {
        undecorate();
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
        // Same orphan release as the minimized/fullscreen gate above: the window is
        // no longer decorated, so nothing will reach its kept GL state again.
        undecorate();
        return;
    }

    // User decoration packs from the tree. NO id is reserved: "border" and
    // "opacity-tint" are selectable packs like any other. They also back the
    // plain layers in easy mode, but that injection happens below ONLY when
    // userPacks is empty, so a user entry for either can never double-apply with
    // its plain layer — it simply takes over, rendering through its own params
    // (the settings side seeds those from the plain setting when the pack is
    // added to a chain).
    // enabledChain(): packs the user toggled off stay in the profile but must
    // not render, exactly like a disabled rule is skipped by the evaluator.
    const QString surfacePath = resolveSurfacePathFor(windowId);
    const PhosphorSurfaceShaders::DecorationProfile resolvedProfile = m_decorationTree.resolve(surfacePath);
    QStringList userPacks = resolvedProfile.enabledChain();

    // Rule-resolved decoration-chain override: a matched
    // OverrideDecorationChain rule REPLACES the tree's user packs wholesale
    // (its empty-chain sentinel blocks decoration outright), and its
    // per-pack params override the tree profile's map below. A rule chain may
    // name any pack, "border" / "opacity-tint" included, and the tree's
    // per-layer disable set deliberately does not apply — a
    // rule chain is explicit. Reads the same cached per-window action walk the opacity /
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
    // "border" and "opacity-tint" surface-shader packs. Those two ids are also
    // user-selectable; picking one is what puts the window in custom mode, and
    // this easy-mode injection then does not run — so a pack and its own plain
    // layer never both render. An empty rule
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
    // built-in "border" base and/or the "opacity-tint" layer, both on →
    // both render, opacity-tint folding OVER the border so the window fades
    // as a whole) or the user packs (custom mode). Neither → nothing to
    // render. ("built-in", not "reserved": the two ids are also selectable
    // packs — see updateWindowDecoration's userPacks — and this easy-mode
    // injection just runs when no user pack occupies the chain.)
    QStringList chain;
    if (showBorder) {
        chain.append(QStringLiteral("border"));
    }
    if (showOpacityTint) {
        chain.append(QStringLiteral("opacity-tint"));
    }
    chain.append(userPacks);
    if (chain.isEmpty()) {
        // The DECORATE GATE: no border and no user packs, so this window ends up
        // undecorated. Release the GL state the remove-first step kept.
        undecorate();
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
    // Shared accent fallback for the plain layers below: the live system
    // accent when the daemon has delivered one, else the Breeze default.
    const QColor accentOr = m_borderAccentColor.isValid() ? m_borderAccentColor : QColor(QStringLiteral("#ff3daee9"));
    if (showBorder) {
        // Easy mode: the resolved border appearance rides the built-in
        // "border" pack's OWN declared parameters (borderWidth / cornerRadius /
        // activeColor / inactiveColor, see data/surface/border/metadata.json),
        // routed by id through the same translateSurfaceParams path every
        // other pack's overrides take — no positional slot layout is assumed
        // anywhere. width / radius fall back to the shared DecorationDefaults
        // when only "show border" is set; an omitted colour falls back to the
        // live system accent (matching the useSystemAccent default), which is
        // forced off because the colours arrive here already accent-resolved.
        // inactiveColor mirrors active when unset. The shader picks active vs
        // inactive by uSurfaceFocused. This branch runs ONLY in easy mode
        // (userPacks empty), where the resolved appearance owns the plain
        // layer's params outright — so the insert deliberately overwrites any
        // stale "border" entry the tree profile still carries from a time the
        // user had the pack picked. Pick it again and we are in custom mode,
        // this branch does not run, and the tree's params win instead.
        QVariantMap borderParams;
        borderParams.insert(QStringLiteral("borderWidth"),
                            appearance->borderWidth.value_or(PhosphorCompositor::DecorationDefaults::BorderWidth));
        borderParams.insert(QStringLiteral("cornerRadius"),
                            appearance->borderRadius.value_or(PhosphorCompositor::DecorationDefaults::BorderRadius));
        borderParams.insert(QStringLiteral("useSystemAccent"), false);
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

    // The chain fold reuses the previous raw capture until the window's own
    // content changes (see SurfaceMultipassState::captureTex). This is what tells
    // it the content changed. Connected for EVERY decorated window, padded or
    // not; disconnected by removeWindowDecoration (updateWindowDecoration always
    // removes first).
    {
        const QString wid = windowId; // capture by value
        wb.damageConnection =
            connect(w, &KWin::EffectWindow::windowDamaged, this, [this, wid](KWin::EffectWindow* /*ew*/) {
                // Ignore the repaint WE just scheduled to keep an animated chain
                // ticking — that says nothing about the window's content. Only
                // damage raised outside our own addRepaintFull means the client
                // actually painted something new.
                if (m_selfRepainting) {
                    return;
                }
                const auto sit = m_surfaceMultipass.find(wid);
                if (sit != m_surfaceMultipass.end()) {
                    sit->second.captureValid = false;
                }
            });
    }

    // Padded chains paint a margin band OUTSIDE the window rect; KWin's own
    // move/resize damage covers only the window's old/new rects, so track the
    // padded rect and damage old ∪ new at screen level on geometry changes or
    // the glow trails behind a dragged window. Disconnected by
    // removeWindowDecoration (updateWindowDecoration always removes first).
    if (wb.outerPadding > 0) {
        wb.lastPaddedGeo = paddedBandRect(w, wb.outerPadding);
        const QString wid = windowId; // capture by value
        wb.paddedGeoConnection = connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, this,
                                         [this, wid](KWin::EffectWindow* ew, const QRectF& /*oldGeo*/) {
                                             auto it = m_windowDecorations.find(wid);
                                             if (it == m_windowDecorations.end() || !ew || !KWin::effects) {
                                                 return;
                                             }
                                             // The SAME helper the recorder above uses. This
                                             // was a sixth hand-rolled copy, and it used an
                                             // int pad where the helper uses qreal — a band
                                             // computed one way and damaged another is
                                             // exactly the stale-glow trail the helper exists
                                             // to prevent.
                                             const QRectF padded = paddedBandRect(ew, it->outerPadding);
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

    // Drop the cached static-prefix fold ONLY when a fold input actually moved. The cached
    // prefix (SurfaceMultipassState::prefixTex) bakes the chain and the resolved pack
    // parameters, so a refresh that changed any of them must re-fold — but updateAllDecorations
    // funnels every focus change, snap flip and rule edit through here, and re-resolving is not
    // the same as changing: the common refresh lands on byte-identical inputs. Invalidating
    // unconditionally would re-fold every decorated window's whole chain on every click, which
    // is the exact work this cache exists to avoid. Focus and opacity are deliberately NOT
    // compared (the fold keys on them itself, foldedFocus / foldedOpacity), so a change there
    // invalidates only the windows it affects.
    if (const auto sit = m_surfaceMultipass.find(windowId); sit != m_surfaceMultipass.end()) {
        if (!wasDecorated || priorFold != foldInputsOf(wb)) {
            sit->second.prefixValid = false;
            sit->second.compositeValid = false;
            sit->second.prefixChainEnd = -1;
        }
    }

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
    //
    // Flagged as OURS. This repaint says "the decoration changed", not "the window's
    // content changed" — and windowDamaged fires on repaint SCHEDULING, so an
    // unflagged one would clear captureValid and force a full effects->drawWindow()
    // re-capture. updateAllDecorations funnels every focus change through here, so
    // that would cold-start the capture cache on every click, which is exactly what
    // keeping the surface state across a refresh exists to prevent. The window's
    // content is unchanged; only the fold's inputs moved, and the fold keys on those
    // (foldedFocus / foldedOpacity) itself.
    const auto selfRepaint = selfRepaintScope();
    w->addRepaintFull();
    // Padded chains paint OUTSIDE the window's expanded rect; addRepaintFull
    // (and addLayerRepaint) clip to the window item, so the margin band needs
    // screen-level damage (the documented surface-extent repaint pitfall).
    //
    // The UNION of the outgoing and incoming bands, not just the incoming one.
    // The refresh above removes with keepSurfaceState, which deliberately skips
    // releaseDecorationGl — and releaseDecorationGl is what used to damage the
    // band the OUTGOING chain painted. So when a refresh SHRINKS the padding (a
    // tree/rule edit dropping the glow pack, a rule chain replacing a padded
    // chain, a smaller glowSize), the annulus between the old and new bands is
    // outside both this damage and addRepaintFull's window-item clip, and the
    // old glow lingers there until something unrelated repaints the screen.
    damagePaddedBand(w, qMax(priorOuterPadding, wb.outerPadding));
}

void PlasmaZonesEffect::updateAllDecorations()
{
    // Snapshot which windows are decorated BEFORE the reconcile. This serves the post-loop
    // sweep, and nothing else: it is how the sweep finds the entries this pass did not
    // revisit. (It used to also feed a "was this decorated?" hint into
    // updateWindowDecoration, which read the answer live instead once it turned out the
    // snapshot was the more destructive of the two.)
    const QSet<QString> previouslyDecorated(m_windowDecorations.keyBegin(), m_windowDecorations.keyEnd());

    // NOT clearAllDecorations(). This runs on every focus change, and a bulk clear
    // tears down each decorated window's whole GL working set: it hands back the
    // offscreen redirect, drops the border shader, and frees the capture, prefix and
    // composite targets — only for the loop below to rebuild all of it a microsecond
    // later, for windows whose decoration almost never actually changed. Every window
    // the loop revisits is refreshed in place instead (updateWindowDecoration removes
    // first with keepSurfaceState, so the redirect and the caches survive), and the
    // sweep after the loop removes exactly the entries nothing revisited.

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
    QSet<QString> revisited;
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
            revisited.insert(wid);
            updateWindowDecoration(wid, w);
        }
        reconcileRuleHiddenTitleBar(wid, w);
        // Stacking layer is persistent window state like the title bar, so it
        // reconciles for ALL windows regardless of desktop too — and the
        // reconcile-to-unset restores a window whose SetWindowLayer rule was
        // just removed, mirroring the title-bar note above.
        reconcileRuleWindowLayer(wid, w);
    }

    // Whatever the loop did not revisit still holds a decoration it should no longer
    // have: the window moved to another desktop, or left the stacking order without a
    // close ever reaching us. These are genuine teardowns, so they release the GL
    // working set and the surface state.
    //
    // Entries riding a live shader transition are skipped, exactly as the old bulk
    // clear skipped them. The close path deliberately keeps a closing window's
    // decoration + multipass state alive so the chain can composite under the close
    // animation, and this reconcile runs within milliseconds of every close (the
    // focus shift alone triggers it). The frozen reverse mapping resolves the deleted
    // window; endShaderTransition's teardown or the windowDeleted backstop erases the
    // entry when the animation is done.
    for (const QString& wid : previouslyDecorated) {
        if (revisited.contains(wid)) {
            continue;
        }
        KWin::EffectWindow* w = m_windowIdReverse.value(wid);
        if (w && m_shaderManager.findTransition(w)) {
            continue;
        }
        removeWindowDecoration(wid, w);
    }
}

bool PlasmaZonesEffect::isWindowMarkedSnapped(const QString& windowId) const
{
    return m_snapHandler->isTiledWindow(windowId);
}

QString PlasmaZonesEffect::resolveSurfacePathFor(const QString& windowId) const
{
    // MEMBERSHIP-only resolution: isTiledWindow tests bucket membership, and the
    // resolved profile's enabledChain() (an empty chain = no decoration) is the
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
