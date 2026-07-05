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

} // namespace

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

void PlasmaZonesEffect::removeWindowBorder(const QString& windowId, KWin::EffectWindow* windowHint)
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
        // findWindowById cannot resolve a window that is already deleted (the
        // close path calls this AFTER KWin marks it deleted), so fall back to
        // the caller-supplied pointer. Skipping the release there left the
        // corpse redirected with the present shader still bound while the
        // multipass erase below destroyed the composite textures its sampler
        // uniforms referenced — GL auto-unbinds a deleted texture, an unbound
        // sampler reads opaque black, and the next paint of the Deleted drew
        // the whole expanded quad black (the close flash). setShader /
        // unredirect on the deleted-but-still-painted window are safe: KWin
        // keys both on the redirected-windows map, and the redirect stays
        // live until the window is discarded.
        KWin::EffectWindow* w = findWindowById(windowId);
        if (!w) {
            w = windowHint;
        }
        if (w) {
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
    m_windowBorders.erase(it);

    // Free the window's composite / buffer FBO targets, reclaiming GPU
    // memory — UNLESS a shader transition is mid-flight on this window. The
    // rotate/snap paths re-resolve the decoration DURING the animation
    // (state flip → rule flush → updateWindowBorder → here), and erasing the
    // entry destroys the very composite the animation is sampling — the
    // at-draw probes caught windows animating with compositeTexId 0. The
    // transition keeps the state; its own teardown (endShaderTransition →
    // removeWindowBorder, by then with no live transition) or the
    // windowDeleted backstop erases it.
    {
        KWin::EffectWindow* aliveW = findWindowById(windowId);
        if (!aliveW) {
            aliveW = windowHint;
        }
        if (!aliveW) {
            // Hint-less callers (bulk clears, D-Bus-driven purges) cannot
            // resolve a deleted window through findWindowById; the frozen
            // reverse mapping still can while a close transition holds it
            // alive, and that is exactly the window whose composite the
            // animation is sampling.
            aliveW = m_windowIdReverse.value(windowId);
        }
        if (aliveW && m_shaderManager.findTransition(aliveW)) {
            return;
        }
    }
    m_surfaceMultipass.erase(windowId);
}

void PlasmaZonesEffect::clearAllBorders()
{
    // Skip entries whose window is riding a live shader transition — the
    // close path DELIBERATELY keeps the closing window's border + multipass
    // entries alive so renderSurfaceChain can composite the decoration under
    // the close animation, and this bulk clear runs within milliseconds of
    // every close (focus shift → updateAllBorders, autotile re-layout).
    // Removing them here nuked the kept state before the first close frame:
    // findWindowById refuses deleted windows and the bulk path has no window
    // hint, so removeWindowBorder's own mid-transition guard could never
    // engage. The frozen reverse mapping (kept alive through the transition
    // by slotWindowClosed) resolves the deleted window here; the skipped
    // entry is erased by endShaderTransition's teardown or the windowDeleted
    // backstop.
    const QStringList ids = m_windowBorders.keys();
    for (const QString& windowId : ids) {
        if (KWin::EffectWindow* w = m_windowIdReverse.value(windowId)) {
            if (m_shaderManager.findTransition(w)) {
                continue;
            }
        }
        removeWindowBorder(windowId);
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
        if (pack != QStringLiteral("border")) {
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
    // that re-runs updateWindowBorder (rule edits, focus, snap flips,
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

    // Resolve THIS window's param values for every pack in the chain. Windows
    // on different surface paths (window.tiled / window.snapped /
    // window.floating) can carry different per-pack overrides for the same
    // pack id, while the shared CompiledSurfacePack bakes only the FIRST
    // resolver's values — the render paths prefer these per-window arrays.
    // They refresh on exactly the chain's triggers (tree change, tile/snap
    // membership change) because they ride the same updateWindowBorder call.
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
    bool handlesOpacity = false;
    for (const QString& packId : std::as_const(chain)) {
        const PhosphorSurfaceShaders::SurfaceShaderEffect eff = m_surfaceShaderRegistry.effect(packId);
        if (!eff.isValid()) {
            continue;
        }
        // Any needsBackdrop pack in the chain switches the window onto the
        // composite path with a per-frame backdrop capture (see paintWindow).
        needsBackdrop = needsBackdrop || eff.needsBackdrop;
        handlesOpacity = handlesOpacity || eff.handlesOpacity;
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
    wb.outerPadding = qBound(0, outerPadding, 128);
    wb.needsBackdrop = needsBackdrop;
    wb.chainHandlesOpacity = handlesOpacity;

    // Rule-resolved opacity, routing + capture-dim input (see the field doc).
    // Resolved here rather than per paint: every trigger that can flip a
    // SetOpacity verdict (focus change, snap/float state, rule edits) funnels
    // through updateWindowBorder already.
    if (m_shaderManager.hasOpacityRules()) {
        if (const auto opacity = resolveWindowOpacity(resolveRuleActions(w, windowId))) {
            wb.ruleOpacity = qBound(0.0, *opacity, 1.0);
        }
    }

    // Padded chains paint a margin band OUTSIDE the window rect; KWin's own
    // move/resize damage covers only the window's old/new rects, so track the
    // padded rect and damage old ∪ new at screen level on geometry changes or
    // the glow trails behind a dragged window. Disconnected by
    // removeWindowBorder (updateWindowBorder always removes first).
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
                                             auto it = m_windowBorders.find(wid);
                                             if (it == m_windowBorders.end() || !ew || !KWin::effects) {
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

void PlasmaZonesEffect::updateAllBorders()
{
    clearAllBorders();

    // Iterate all effect windows and reconcile each window's border + title-bar
    // from its effective appearance (config-backed default gated by scope, with
    // user rules overriding per slot). updateWindowBorder /
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
        // inside updateWindowBorder (app-window filter + resolved chain) decides
        // whether it actually decorates. Title-bar hiding (setNoBorder) is a
        // persistent decoration-state change that survives desktop switches, so
        // reconcile it below for ALL windows the appearance may hide — otherwise
        // a hide (rule or config default) applying to a window on another
        // virtual desktop would not take effect until next activation.
        if (w->isOnCurrentDesktop()) {
            updateWindowBorder(wid, w);
        }
        reconcileRuleHiddenTitleBar(wid, w);
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
    // title-bar appearance resolve from the config-backed defaults + rules
    // (resolveEffectiveWindowAppearance), not from this tree, so the baseline is
    // empty/neutral and nothing is auto-inserted. The daemon's real fetch
    // overwrites this whole tree once it arrives.
    m_decorationTree = PhosphorSurfaceShaders::DecorationProfileTree{};
}

// ─────────────────────────────────────────────────────────────────────────────
// Surface shader (the window border / rounded-corner pack and any future surface
// pack) — per-pack compile + registry search-path setup live in
// surface_compile.cpp (compiledPack() / compiledPackForWindow() /
// ensureSurfaceRegistryPaths()), reusing the shared GLSL include / param-preamble
// / #define-PLASMAZONES_KWIN pipeline. The border is the first surface
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
        // Every decorated window rides the composite path: the redirect always
        // holds the present passthrough, and paintWindow's per-frame fold
        // supplies the composite it blits. (The old cheap path bound the pack
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
    // No teardown branch here: !wantsBorder means there is no WindowBorder entry
    // to act on (wantsBorder IS `it != end()`), and border removal always routes
    // through removeWindowBorder, which clears the shader and unredirects.
}

void PlasmaZonesEffect::pushBorderUniforms(KWin::EffectWindow* w, const WindowBorder& wb, const QString& packId,
                                           const CompiledSurfacePack& pack, qreal scale, qreal texturePaddingLogical)
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
    // Padded composite path: the target texture's canvas is the expanded rect
    // inflated by the chain's outer margin (renderSurfaceChainComposite uses
    // the same construction), so the geometry uniforms must describe that
    // padded space. @p texturePaddingLogical is 0 on the unpadded callers
    // (idle blit, transition capture).
    if (texturePaddingLogical > 0.0) {
        expanded.adjust(-texturePaddingLogical, -texturePaddingLogical, texturePaddingLogical, texturePaddingLogical);
    }
    const QVector2D windowExpandedSize(static_cast<float>(expanded.width() * scale),
                                       static_cast<float>(expanded.height() * scale));
    const QVector2D frameTopLeft(static_cast<float>((frame.left() - expanded.left()) * scale),
                                 static_cast<float>((frame.top() - expanded.top()) * scale));
    const QVector2D frameSize(static_cast<float>(frame.width() * scale), static_cast<float>(frame.height() * scale));

    // The caller has the border shader bound (a KWin::ShaderBinder). What
    // carries the values into the eventual draw is PROGRAM-OBJECT persistence
    // (uniform values survive the binder pop; OffscreenData::paint re-binds the
    // same program) — the idle drawWindow caller's binder actually pops before
    // its draw. Either way, setUniform writes to the currently bound program,
    // so we must NOT bind/unbind here or the uniforms would land on the wrong
    // (or no) program.
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

    // Pack-declared parameters (customParams / customColors). Seed from THIS
    // window's resolved values (updateWindowBorder fills packParamValues from
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
    constexpr int kSurfaceChannelBaseUnit = 3 + PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots;
    if (!m_capturingSnapshot && !m_windowBorders.isEmpty() && !m_shaderManager.findTransition(w)) {
        const QString wid = getWindowId(w);
        const auto bit = m_windowBorders.constFind(wid);
        if (bit != m_windowBorders.constEnd() && bit->shaderApplied) {
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
                constexpr int kSurfaceLayerUnitDraw =
                    2 + PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots;
                glActiveTexture(GL_TEXTURE0 + kSurfaceLayerUnitDraw);
                comp->bind();
                // Old-content snapshot (morph cross-fades): same clobber, same
                // last-moment rebind.
                if (st->oldSnapshot) {
                    constexpr int kOldSnapshotUnitDraw =
                        1 + PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots;
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
    for (int i = 0; i < boundChannels; ++i) {
        glActiveTexture(GL_TEXTURE0 + kSurfaceChannelBaseUnit + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    if (boundChannels > 0) {
        glActiveTexture(GL_TEXTURE0);
    }
}

} // namespace PlasmaZones
