// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <scene/borderoutline.h>
#include <scene/outlinedborderitem.h>
#include <scene/windowitem.h>
#include <window.h>

#include "../autotilehandler.h"
#include "../snaphandler.h"
#include "shader_resolve.h"
#include "window_query.h"

#include <PhosphorCompositor/AutotileState.h>
#include <PhosphorProtocol/WindowTypeEnum.h>
#include <PhosphorRules/RuleAction.h>

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

void PlasmaZonesEffect::removeWindowBorder(const QString& windowId)
{
    auto it = m_windowBorders.find(windowId);
    if (it == m_windowBorders.end()) {
        return;
    }
    WindowBorder& wb = it.value();
    if (wb.clippedContainer) {
        wb.clippedContainer->setBorderRadius(wb.savedContainerRadius);
    }
    // QPointer: item may already be null if Qt parent-child ownership destroyed it.
    // Use deleteLater() rather than raw delete — OutlinedBorderItem is a QObject
    // parented into the scene graph and may have queued signals / pending paints
    // mid-cycle. CLAUDE.md: never manual-delete QObjects.
    //
    // Hide-then-deleteLater: updateWindowBorder calls removeWindowBorder and then
    // immediately allocates a new OutlinedBorderItem under the same windowItem
    // parent. Without setVisible(false) here, both the old and the new item live
    // in the scene graph for one event-loop iteration (until deleteLater fires)
    // and the user sees a one-frame flicker / Z-fight on every active-window
    // swap. Hiding first short-circuits the old item's render path while the
    // QObject deletion is still deferred per the CLAUDE.md no-manual-delete rule.
    if (wb.item) {
        wb.item->setVisible(false);
        wb.item->deleteLater();
    }
    QObject::disconnect(wb.geometryConnection);
    m_windowBorders.erase(it);
}

void PlasmaZonesEffect::clearAllBorders()
{
    while (!m_windowBorders.isEmpty()) {
        removeWindowBorder(m_windowBorders.begin().key());
    }
}

bool PlasmaZonesEffect::windowMatchesAppearanceScope(const QString& scope, KWin::EffectWindow* w,
                                                     const QString& windowId) const
{
    if (!w) {
        return false;
    }
    if (scope == QLatin1String("all")) {
        return true;
    }
    if (scope == QLatin1String("tiled")) {
        // A window occupying a snap zone OR managed by the autotile engine.
        return isWindowSnapped(windowId) || m_autotileHandler->isTiledWindow(windowId);
    }
    if (scope == QLatin1String("normal")) {
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

    // Window appearance resolves as the config-backed default (gated by the
    // border scope) with user rules overriding per slot. A window whose resolved
    // appearance does not show a border draws nothing — borders are opt-in, the
    // config default them off unless the user enabled them.
    const ResolvedWindowAppearance ovr = resolveEffectiveWindowAppearance(w, windowId);

    if (!ovr.showBorder || !*ovr.showBorder) {
        return;
    }

    const int bw = ovr.borderWidth.value_or(0);
    if (bw <= 0) {
        return;
    }

    if (!w || w->isMinimized() || w->isFullScreen()) {
        return;
    }

    // Colour comes from the resolved appearance: the active colour when the
    // window is focused, the inactive colour otherwise. The resolver already
    // defaulted inactive to active when that slot was unset and resolved the
    // accent sentinel to the live accent, so this is a straight focus-state pick.
    // A rule can also scope a single colour by matching IsFocused.
    const bool isFocused = (w == KWin::effects->activeWindow());
    const QColor bc = (isFocused ? ovr.activeColor : ovr.inactiveColor).value_or(QColor());
    if (!bc.isValid() || bc.alpha() == 0) {
        return;
    }

    // The OutlinedBorderItem draws the border OUTSIDE the innerRect, but the
    // parent WindowItem clips children to the window frame.  Inset the innerRect
    // by borderWidth so the border draws fully inside the frame (no clipping).
    const QRectF frame = w->frameGeometry();
    const KWin::RectF innerRect(bw, bw, frame.width() - 2.0 * bw, frame.height() - 2.0 * bw);
    const int br = ovr.borderRadius.value_or(0);
    // BorderOutline / OutlinedBorderItem paint the outline colour OPAQUE — the
    // scene ignores the QColor alpha channel (verified: a #80…/#40… border drew
    // fully solid). The scene DOES honour a per-item opacity, so split the colour:
    // hand the outline an opaque RGB and drive the translucency through the item's
    // setOpacity(). This makes a focused #80…/unfocused #40… border render at its
    // intended 50% / 25% strength instead of a solid swatch.
    const qreal borderOpacity = bc.alphaF();
    QColor opaqueBc = bc;
    opaqueBc.setAlpha(255);
    const KWin::BorderOutline outline(bw, opaqueBc, KWin::BorderRadius(br));

    KWin::WindowItem* windowItem = w->windowItem();
    if (!windowItem) {
        return;
    }

    WindowBorder wb;
    wb.item = new KWin::OutlinedBorderItem(innerRect, outline, windowItem);
    wb.item->setOpacity(borderOpacity);

    // Clip the window contents so they don't poke past the rounded outline
    // at the corners (dark pixels leaking past the border).
    //
    // Geometry: KWin's BorderOutline takes `radius` as the INNER curve
    // radius (verified against src/scene/outlinedborderitem.cpp:buildQuads —
    // the corner quad is sized `thickness + radius`, with the arc going
    // from the inner straight-edge meeting points at distance `radius` from
    // the corner-quad center). The outer curve is concentric and has
    // radius `radius + thickness`.
    //
    // We pass `br` as BorderOutline.radius and `bw` as thickness, so:
    //   - Outline's INNER curve: radius `br`, located at innerRect edges
    //     `(bw, bw)–(w-bw, h-bw)`.
    //   - Outline's OUTER curve: radius `br + bw`, at the frame edges
    //     `(0, 0)–(w, h)`.
    //
    // Clip on `windowContainer()`, NOT on the SurfaceItem directly:
    //   - WindowItem::m_windowContainer is the parent Item that holds the
    //     surface + decoration. Its rect is the FULL frame (0, 0, w, h) —
    //     identical to the outline's outer rect.
    //   - SurfaceItem::rect() is the client buffer extent, which can be
    //     SMALLER than the frame for SSD windows (decoration adds margin)
    //     or have a non-zero offset within the windowContainer.
    //   - Item::setBorderRadius rounds the item's OWN rect corners, so a
    //     clip on the surface anchors at surface-local origin — wrong for
    //     SSD windows where surface != frame.
    //   - The borderRadius propagates via cornerStack to descendants, so
    //     clipping the windowContainer applies the same RoundedCorners
    //     shader trait to the SurfaceItem render branch but anchored at
    //     the frame corners (where the outline lives), regardless of
    //     surface buffer size or offset.
    //
    // Don't go through Window::setBorderRadius — that triggers KDecoration3
    // active-state outline machinery on focused windows, drawing an extra
    // inset outline that looks visually different from the inactive border.
    //
    // Apply universally when bw > 0: SSD windows we made borderless (their
    // surface IS the content area), CSD windows we left alone (GTK/Electron
    // — hasDecoration returned false so the borderless path skipped them),
    // and any other tiled window whose squared corners would peek past the
    // rounded outline.
    if (bw > 0) {
        KWin::Item* container = windowItem->windowContainer();
        if (container) {
            const int containerRadius = br + bw;
            wb.savedContainerRadius = container->borderRadius();
            container->setBorderRadius(KWin::BorderRadius(containerRadius));
            wb.clippedContainer = container;
        }
    }

    // Keep the border in sync when the window resizes or moves.
    const QString wid = windowId; // capture by value
    wb.geometryConnection =
        connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, this,
                [this, wid, bw](KWin::EffectWindow* ew, const QRectF& /*oldGeo*/) {
                    auto it = m_windowBorders.find(wid);
                    if (it != m_windowBorders.end() && it->item) {
                        const QRectF f = ew->frameGeometry();
                        it->item->setInnerRect(KWin::RectF(bw, bw, f.width() - 2.0 * bw, f.height() - 2.0 * bw));
                    }
                });

    m_windowBorders.insert(windowId, wb);
}

void PlasmaZonesEffect::updateAllBorders()
{
    clearAllBorders();

    // Iterate all effect windows and reconcile each window's border + title-bar
    // from its effective appearance (config-backed default gated by scope, with
    // user rules overriding per slot). updateWindowBorder / reconcileRuleHiddenTitleBar
    // self-gate on that merged appearance, so this runs UNCONDITIONALLY — a
    // config default border / hidden title bar must apply even with an empty rule
    // set, and a per-window reconcile to the unset state clears a now-stale
    // override left by a removed rule (so no bulk restore pass is needed here).
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
        // current desktop. Title-bar hiding (setNoBorder) is a persistent
        // decoration-state change that survives desktop switches, so reconcile
        // it for ALL windows the appearance may hide — otherwise a hide (rule or
        // config default) applying to a window on another virtual desktop would
        // not take effect until that window is next activated.
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

} // namespace PlasmaZones
