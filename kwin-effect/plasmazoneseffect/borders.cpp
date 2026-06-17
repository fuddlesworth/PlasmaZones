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

#include <optional>

namespace PlasmaZones {

void PlasmaZonesEffect::setupDecorationManager()
{
    // The drain-time veto is the authoritative re-check for deferred
    // title-bar restores: a vetoed restore stays QUEUED (the manager
    // re-arms its fallback timer and bounds the retries), so the veto must
    // hold ONLY while a re-acquire is genuinely expected — the window's
    // screen re-entered autotile mid-drain, the mode's hide-title-bars is
    // still on, and the window is not floating (a retile never re-acquires
    // floated windows). Without the latter two conditions, a hide-toggle-off
    // drain or a floated window's restore would be vetoed until the
    // manager's retry bound overrides it.
    m_decorationManager->setRestoreVeto([this](const QString& windowId) {
        KWin::EffectWindow* w = findWindowById(windowId);
        // Exact-id re-check: findWindowById's appId fuzzy fallback can
        // resolve a same-app SIBLING when the exact id misses, and a
        // sibling's screen/floating state must never decide the veto for
        // the window the queue entry tracks (same hazard guard as
        // SnapHandler::markWindowSnapped and the manager's own
        // resolveExact for physical toggles).
        if (!w || getWindowId(w) != windowId || !m_autotileHandler->isAutotileScreen(getWindowScreenId(w))) {
            return false;
        }
        return m_autotileHandler->borderState().hideTitleBars && !isWindowFloating(windowId);
    });
    connect(m_decorationManager.get(), &DecorationManager::windowDecorationRestored, this,
            [this](const QString& windowId) {
                // A veto-driven restore leaves the window mode-owned and
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
    connect(m_decorationManager.get(), &DecorationManager::drainFinished, this, [this]() {
        updateAllBorders();
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

void PlasmaZonesEffect::updateWindowBorder(const QString& windowId, KWin::EffectWindow* w)
{
    // Remove existing border for this window first
    removeWindowBorder(windowId);

    // Base appearance from the owning mode (autotile / snap). nullptr → no mode
    // currently draws a border for this window (floating, or its mode's border
    // is off) — a per-window rule may still force one on below.
    const PhosphorCompositor::BorderState* state = resolveBorderStateFor(windowId);

    // Per-window rule override — applies to ANY matched window, snapped or
    // floating (mirrors SetOpacity). Resolved against the same evaluator the
    // opacity / animation rules use; gated on a non-empty rule set so windows
    // with no rules pay nothing.
    std::optional<ResolvedWindowAppearance> ovr;
    if (w && !m_shaderManager.animationRuleSet().isEmpty()) {
        ovr = resolveWindowAppearance(m_shaderManager.animationRuleEvaluator(), windowRuleQuery(w), windowId);
    }

    // Merge: a rule field wins; otherwise fall back to the owning mode's value
    // (or "no border" when the window has no owning border state).
    // resolveBorderStateFor only returns non-null when that mode SHOWS a
    // border, so a non-null `state` means baseShows == true.
    const bool show = (ovr && ovr->showBorder) ? *ovr->showBorder : (state != nullptr);
    if (!show) {
        return;
    }

    const int bw = (ovr && ovr->borderWidth) ? *ovr->borderWidth : (state ? state->width : 0);
    if (bw <= 0) {
        return;
    }

    if (!w || w->isMinimized() || w->isFullScreen()) {
        return;
    }

    // Choose color. The owning mode (autotile / snap) carries separate active
    // and inactive border colours — global appearance settings, not rules — so
    // pick the one matching the window's current focus state. A per-window
    // SetBorderColor rule, when matched, overrides it. Focus-dependence of the
    // RULE colour is expressed in the rule itself via the IsFocused match
    // condition: `windowRuleQueryFor` set the query's isFocused flag, so a
    // focus-scoped rule (`WHEN focused`/`WHEN NOT focused`) only fills the
    // border-colour slot in its matching state, while a focus-agnostic rule
    // applies in both. Either way `ovr->borderColor` already holds the colour
    // appropriate to this window's current focus — no post-resolution switch.
    // A floating window (no owning mode) whose only rule is focus-scoped thus
    // correctly shows no border in the unmatched state; author a focus-agnostic
    // rule to keep a border in both states.
    const bool isFocused = (w == KWin::effects->activeWindow());
    const QColor modeColor = state ? (isFocused ? state->color : state->inactiveColor) : QColor();
    const QColor bc = (ovr && ovr->borderColor) ? *ovr->borderColor : modeColor;
    if (!bc.isValid() || bc.alpha() == 0) {
        return;
    }

    // The OutlinedBorderItem draws the border OUTSIDE the innerRect, but the
    // parent WindowItem clips children to the window frame.  Inset the innerRect
    // by borderWidth so the border draws fully inside the frame (no clipping).
    const QRectF frame = w->frameGeometry();
    const KWin::RectF innerRect(bw, bw, frame.width() - 2.0 * bw, frame.height() - 2.0 * bw);
    const int br = (ovr && ovr->borderRadius) ? *ovr->borderRadius : (state ? state->radius : 0);
    const KWin::BorderOutline outline(bw, bc, KWin::BorderRadius(br));

    KWin::WindowItem* windowItem = w->windowItem();
    if (!windowItem) {
        return;
    }

    WindowBorder wb;
    wb.item = new KWin::OutlinedBorderItem(innerRect, outline, windowItem);

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

    // Iterate all effect windows and create borders for any window managed by
    // a mode (autotile or snap) that currently shows borders, OR matched by a
    // per-window border rule (which can draw on an otherwise-borderless /
    // floating window). updateWindowBorder self-gates on the merged effective
    // appearance, so calling it when rules exist is safe; reconcile the rule
    // title-bar override in the same pass so it tracks context changes.
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
        // current desktop. Title-bar hiding (setNoBorder) is a persistent
        // decoration-state change that survives desktop switches, so reconcile
        // it for ALL windows the rule may match — otherwise a SetHideTitleBar
        // rule added while the matched window sits on another virtual desktop
        // would not take effect until that window is next activated.
        if (w->isOnCurrentDesktop() && (haveRules || resolveBorderStateFor(wid))) {
            updateWindowBorder(wid, w);
        }
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
    // Tri-state rule override, forwarded to the DecorationManager:
    //   unset → no opinion (mode owners decide)
    //   true  → rule hides (a Rule owner joins the mode owners)
    //   false → rule FORCE-SHOWS (a veto that pins the decoration visible
    //           over any mode owner; owners re-assert when the rule changes)
    // The manager owns the capability gate, the mode-ownership coordination
    // the old m_ruleHiddenTitleBars/modeBorderless dance approximated, and
    // the geometry re-assert across veto-driven decoration flips.
    const std::optional<ResolvedWindowAppearance> ovr =
        resolveWindowAppearance(m_shaderManager.animationRuleEvaluator(), windowRuleQuery(w), windowId);
    m_decorationManager->setRuleOverride(windowId, ovr ? ovr->hideTitleBar : std::nullopt);
}

bool PlasmaZonesEffect::isWindowMarkedSnapped(const QString& windowId) const
{
    return m_snapHandler->isTiledWindow(windowId);
}

const PhosphorCompositor::BorderState* PlasmaZonesEffect::resolveBorderStateFor(const QString& windowId) const
{
    // Autotile takes precedence; a window can transiently appear in both the
    // autotile and snap border sets during a mode switch (the call sites guard
    // against steady-state double-tracking via isAutotileScreen, but the
    // transition window is real), and resolving autotile-first is the
    // authoritative tie-break — this ordering is load-bearing, not cosmetic.
    const BorderState& autotile = m_autotileHandler->borderState();
    if (AutotileStateHelpers::shouldShowBorderForWindow(autotile, windowId)) {
        return &autotile;
    }
    if (m_snapHandler->shouldShowBorderForWindow(windowId)) {
        return &m_snapHandler->borderState();
    }
    return nullptr;
}

void PlasmaZonesEffect::restoreAllRuleHiddenTitleBars()
{
    // The authoritative window-rule state is gone (rule set emptied, daemon
    // loss, effect teardown): clear every Rule owner and force-show veto. The
    // manager restores a title bar only where no mode owner remains, so the
    // modes' decoration management is never fought.
    m_decorationManager->clearAllRuleOverrides();
}

} // namespace PlasmaZones
