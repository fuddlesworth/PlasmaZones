// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>

#include "../autotilehandler.h"
#include "../snaphandler.h"
#include "shader_internal.h"
#include "surface_fold.h"
#include "shader_resolve.h"
#include "window_query.h"

#include <PhosphorCompositor/DecorationDefaults.h>
#include <PhosphorProtocol/WindowTypeEnum.h>
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

// Resolve a config-default border-colour string against the live system colour
// its `accent` sentinel maps to. Mirrors resolveWindowAppearance's rule colour
// path: the sentinel yields @p systemColor (nullopt when that colour is not yet
// known), an empty string contributes nothing, anything else parses as hex.
std::optional<QColor> resolveDefaultBorderColor(const QString& value, const QColor& systemColor)
{
    if (value.isEmpty()) {
        return std::nullopt;
    }
    if (value == PhosphorRules::BorderColorToken::Accent) {
        return systemColor.isValid() ? std::optional<QColor>(systemColor) : std::nullopt;
    }
    const QColor color(value);
    return color.isValid() ? std::optional<QColor>(color) : std::nullopt;
}

/// Everything a WindowDecoration contributes to the cached static-prefix fold, in
/// one comparable value. Focus and rule opacity are deliberately absent: the fold
/// keys on those itself (SurfaceMultipassState::foldedFocus / foldedOpacity), so
/// they must not drag the far more expensive whole-chain invalidation along with
/// them on every focus change.
struct FoldInputs
{
    // The two that can genuinely move on their own.
    QStringList chain;
    QHash<QString, SurfaceParamValues> packParamValues;
    // Derived from the two above (basePackId is chain.first(); the other two are
    // resolved from the chain and its params), and compared anyway. Two of them have a
    // second keying inside the fold — outerPadding rides compositeSize /
    // captureScaleKey, and a backdrop-reading PACK is per-frame so it is never cached —
    // but a chain is not a pack: ["border", "frost"] still caches the border in its
    // static prefix, and nothing else keys on needsBackdrop within a fixed chain. These
    // are what the fold actually reads, and a comparison that quietly depends on a
    // derivation staying true elsewhere is the kind that stops holding without anyone
    // noticing. The cost is a few word compares on a decoration refresh.
    QString basePackId;
    int outerPadding = 0;
    bool needsBackdrop = false;
    // Opacity authority is deliberately absent, mirroring WindowDecoration: what the
    // fold actually did with the rule alpha is reported by the fold
    // (SurfaceMultipassState::handledOpacity), never carried as chain metadata here.
    // The rule-backed border appearance, which pushBorderUniforms substitutes for
    // the "border" pack's own baked params. A rule edit that recolours the border
    // moves the fold even though the chain and the pack params are untouched.
    bool ruleBorder = false;
    int ruleBorderWidth = 0;
    int ruleBorderRadius = 0;
    QColor ruleBorderActiveColor;
    QColor ruleBorderInactiveColor;

    bool operator==(const FoldInputs&) const = default;
};

FoldInputs foldInputsOf(const WindowDecoration& wb)
{
    return FoldInputs{
        wb.chain,      wb.packParamValues, wb.basePackId,       wb.outerPadding,          wb.needsBackdrop,
        wb.ruleBorder, wb.ruleBorderWidth, wb.ruleBorderRadius, wb.ruleBorderActiveColor, wb.ruleBorderInactiveColor,
    };
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

bool PlasmaZonesEffect::windowMatchesAppearanceScope(const QString& scope, KWin::EffectWindow* w,
                                                     const QString& windowId) const
{
    if (!w) {
        return false;
    }
    namespace WAS = PhosphorCompositor::WindowAppearanceScope;
    if (scope == WAS::All) {
        return true;
    }
    if (scope == WAS::Tiled) {
        // A window occupying a snap zone OR managed by the autotile engine. Use
        // each engine's render-marked set (populated synchronously on commit),
        // NOT the NavigationHandler zone cache: markWindowSnapped builds the border
        // synchronously before the async windowStateChanged that fills that cache
        // lands, so reading it here would miss a just-snapped window's default
        // border until the next full sweep. The autotile half is already symmetric.
        return isWindowMarkedSnapped(windowId) || m_autotileHandler->isTiledWindow(windowId);
    }
    if (scope == WAS::Normal) {
        return windowTypeFor(w) == PhosphorProtocol::WindowType::Normal && !windowIsTransient(w);
    }
    // Unknown / empty token: contribute no default (the settings side validates
    // the token to one of the three above).
    return false;
}

ResolvedWindowAppearance PlasmaZonesEffect::resolveEffectiveWindowAppearance(KWin::EffectWindow* w,
                                                                             const QString& windowId) const
{
    // Start from the user rule appearance (per-slot optionals). resolveWindowAppearance
    // returns nullopt when no rule fills any slot, including the empty-rule-set case;
    // an empty struct then carries only the config-default fills below.
    std::optional<ResolvedWindowAppearance> ruleOvr;
    if (w && !m_shaderManager.animationRuleSet().isEmpty()) {
        ruleOvr = resolveWindowAppearance(resolveRuleActions(w, windowId), m_borderAccentColor, m_borderInactiveColor);
    }
    ResolvedWindowAppearance out = ruleOvr.value_or(ResolvedWindowAppearance{});

    const WindowAppearanceDefault& def = m_windowAppearanceDefault;

    // Border slots: fill each slot the rules left unset from the config default,
    // but only when the window matches the border scope. An engaged rule slot
    // (even a rule value of false / 0) is left untouched — rules win per slot.
    if (windowMatchesAppearanceScope(def.borderScope, w, windowId)) {
        if (!out.showBorder) {
            out.showBorder = def.showBorder;
        }
        if (!out.borderWidth) {
            out.borderWidth = def.borderWidth;
        }
        if (!out.borderRadius) {
            out.borderRadius = def.borderRadius;
        }
        if (!out.activeColor) {
            out.activeColor = resolveDefaultBorderColor(def.activeColor, m_borderAccentColor);
        }
        if (!out.inactiveColor) {
            out.inactiveColor = resolveDefaultBorderColor(def.inactiveColor, m_borderInactiveColor);
        }
    }
    // Mirror the rule path: an unset inactive colour falls back to the active one
    // so an active-only default keeps its border colour when the window unfocuses.
    if (!out.inactiveColor) {
        out.inactiveColor = out.activeColor;
    }

    // Title-bar slot: the config default only ever contributes a HIDE (true).
    // A config value of false means "no opinion" (show the native title bar),
    // NOT a force-show veto — that veto semantic (engaged false) is reserved for
    // an explicit SetHideTitleBar=false rule, so leave the slot unset when the
    // default is off. Fill only when the default hides AND the window is in scope.
    if (!out.hideTitleBar && def.hideTitleBar && windowMatchesAppearanceScope(def.titleBarScope, w, windowId)) {
        out.hideTitleBar = true;
    }
    return out;
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
        // No live caller can pass a null w today (all seven guard first), so this is
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

    // BORDER appearance resolves as the config-backed default (Windows.* keys,
    // gated by the border scope) with per-window rule overrides layered on top
    // (resolveEffectiveWindowAppearance); the "border" surface-shader pack
    // RENDERS that resolution. The pack is NOT inserted into the user's
    // decoration tree — the resolved show-border flag is the sole border gate,
    // and the resolved width / radius / colours ride the per-window override
    // fields below (pushed by pushBorderUniforms, which picks active vs
    // inactive colour by focus). The decoration tree contributes only the
    // user's OWN pack chain (e.g. glow) composited on top.
    const ResolvedWindowAppearance appearance = resolveEffectiveWindowAppearance(w, windowId);
    const bool showBorder = appearance.showBorder.value_or(false);

    // User decoration packs from the tree. The "border" id is rule-owned, so an
    // explicit user "border" entry is dropped here (the rule path owns it).
    // enabledChain(): packs the user toggled off stay in the profile but must
    // not render, exactly like a disabled rule is skipped by the evaluator.
    QStringList userPacks;
    const QString surfacePath = resolveSurfacePathFor(windowId);
    const PhosphorSurfaceShaders::DecorationProfile resolvedProfile = m_decorationTree.resolve(surfacePath);
    const QStringList treeChain = resolvedProfile.enabledChain();
    for (const QString& pack : treeChain) {
        if (pack != QLatin1String("border")) {
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

    // DECORATE GATE: render when the rule shows a border OR the tree declares user
    // packs. Neither → nothing to render. The "border" base (when present) renders
    // first; user packs composite over it (chain[1..]).
    QStringList chain;
    if (showBorder) {
        chain.append(QStringLiteral("border"));
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
    int outerPadding = 0;
    bool needsBackdrop = false;
    for (const QString& packId : std::as_const(chain)) {
        const PhosphorSurfaceShaders::SurfaceShaderEffect eff = m_surfaceShaderRegistry.effect(packId);
        if (!eff.isValid()) {
            continue;
        }
        // Any needsBackdrop pack in the chain switches the window onto the
        // composite path with a per-frame backdrop capture (see paintWindow).
        // handlesOpacity is deliberately NOT accumulated here: opacity authority
        // is decided per-frame by what the fold actually applied
        // (SurfaceMultipassState::handledOpacity), never by chain metadata.
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

    // Rule-resolved opacity, routing + capture-dim input (see the field doc).
    // Resolved here rather than per paint: every trigger that can flip a
    // SetOpacity verdict (focus change, snap/float state, rule edits) funnels
    // through updateWindowDecoration already.
    if (m_shaderManager.hasOpacityRules()) {
        if (const auto opacity = resolveWindowOpacity(resolveRuleActions(w, windowId))) {
            wb.ruleOpacity = qBound(0.0, *opacity, 1.0);
        }
    }

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

    if (showBorder) {
        // Per-window border appearance from the merged config-default + rule
        // resolution. width / radius fall back to the shared DecorationDefaults
        // when only "show border" is set; an omitted colour falls back to the
        // live system accent (matching the useSystemAccent default), and an
        // unknown accent to the pack's metadata default colour. inactiveColor
        // mirrors active when unset.
        wb.ruleBorder = true;
        wb.ruleBorderWidth = appearance.borderWidth.value_or(PhosphorCompositor::DecorationDefaults::BorderWidth);
        wb.ruleBorderRadius = appearance.borderRadius.value_or(PhosphorCompositor::DecorationDefaults::BorderRadius);
        const QColor accentOr =
            m_borderAccentColor.isValid() ? m_borderAccentColor : QColor(QStringLiteral("#ff3daee9"));
        wb.ruleBorderActiveColor = appearance.activeColor.value_or(accentOr);
        wb.ruleBorderInactiveColor = appearance.inactiveColor.value_or(wb.ruleBorderActiveColor);
    }

    // The cached static-prefix fold (SurfaceMultipassState::prefixTex) bakes the
    // chain and the pack parameters resolved above, so a refresh that MOVED any of
    // them has to drop it. But only then. updateAllDecorations funnels every focus
    // change, snap flip and rule edit through here, and re-resolving is not the same
    // as changing: the overwhelmingly common refresh lands on byte-identical fold
    // inputs. Invalidating unconditionally would re-fold every decorated window's
    // entire chain on every click, which is precisely the work the cache exists to
    // avoid. Focus and rule opacity are NOT compared here: they move constantly and
    // the fold keys on them itself (SurfaceMultipassState::foldedFocus /
    // foldedOpacity), so a change there invalidates exactly the windows it affects.
    if (const auto sit = m_surfaceMultipass.find(windowId); sit != m_surfaceMultipass.end()) {
        const bool foldInputsMoved = !wasDecorated || priorFold != foldInputsOf(wb);
        if (foldInputsMoved) {
            sit->second.prefixValid = false;
            sit->second.compositeValid = false;
            sit->second.prefixPackCount = -1;
        }
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
    damagePaddedBand(w, wb.outerPadding);
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
