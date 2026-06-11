// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <scene/borderoutline.h>
#include <scene/decorationitem.h>
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
    if (wb.clippedDecoration) {
        wb.clippedDecoration->setBorderRadius(wb.savedDecorationRadius);
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
        ovr = resolveWindowAppearance(m_shaderManager.animationRuleEvaluator(),
                                      windowRuleQueryFor(w, getWindowScreenId(w)), windowId);
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
        const KWin::BorderRadius corner(br + bw);
        if (KWin::Item* container = windowItem->windowContainer()) {
            wb.savedContainerRadius = container->borderRadius();
            container->setBorderRadius(corner);
            wb.clippedContainer = container;
        }
        // The container radius does NOT reach the server-side decoration's render
        // branch, so a SHOWN title bar keeps square corners and pokes past the
        // rounded outline. Round the decoration item directly so its corners
        // follow the outline too. Null (skipped) for borderless / CSD windows.
        if (KWin::DecorationItem* deco = windowItem->decorationItem()) {
            wb.savedDecorationRadius = deco->borderRadius();
            deco->setBorderRadius(corner);
            wb.clippedDecoration = deco;
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
    // is false), so a title bar a now-removed SetHideTitleBar rule hid would stay
    // hidden until effect teardown. Restore every rule-hidden title bar in that
    // case — restoreAllRuleHiddenTitleBars is a no-op when the set is already
    // empty (the common no-rules path), so this costs nothing when nothing is hidden.
    if (!haveRules) {
        restoreAllRuleHiddenTitleBars();
    }
}

void PlasmaZonesEffect::reconcileRuleHiddenTitleBar(const QString& windowId, KWin::EffectWindow* w)
{
    using namespace PhosphorCompositor;
    if (!w || windowId.isEmpty()) {
        return;
    }
    // A rule SetHideTitleBar override only ever ADDS hiding (hide == true).
    // hide == false / unset means "no override" and defers to the snap/autotile
    // borderless management — the rule layer never force-shows a title bar that
    // a mode hid, so it can't fight that mode's authoritative decoration state.
    const std::optional<ResolvedWindowAppearance> ovr = resolveWindowAppearance(
        m_shaderManager.animationRuleEvaluator(), windowRuleQueryFor(w, getWindowScreenId(w)), windowId);
    const bool ruleWantsHide = ovr && ovr->hideTitleBar && *ovr->hideTitleBar;
    KWin::Window* kw = w->window();
    const bool weHidIt = m_ruleHiddenTitleBars.contains(windowId);
    // A window snap/autotile already manages borderless must not be physically
    // toggled by the rule layer (it owns the decoration); we only track intent.
    const bool modeBorderless =
        m_snapHandler->isBorderlessWindow(windowId) || m_autotileHandler->isBorderlessWindow(windowId);

    if (ruleWantsHide) {
        if (!weHidIt && kw && kw->userCanSetNoBorder()) {
            m_ruleHiddenTitleBars.insert(windowId);
            if (!modeBorderless) {
                kw->setNoBorder(true);
            }
        }
    } else if (weHidIt) {
        m_ruleHiddenTitleBars.remove(windowId);
        // Restore the decoration unless a mode still wants it borderless.
        if (kw && !modeBorderless) {
            kw->setNoBorder(false);
        }
    }
}

bool PlasmaZonesEffect::isWindowMarkedSnapped(const QString& windowId) const
{
    return m_snapHandler->isTiledWindow(windowId);
}

bool PlasmaZonesEffect::isWindowSnapBorderless(const QString& windowId) const
{
    return m_snapHandler->isBorderlessWindow(windowId);
}

const PhosphorCompositor::BorderState* PlasmaZonesEffect::resolveBorderStateFor(const QString& windowId) const
{
    using namespace PhosphorCompositor;
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
    using namespace PhosphorCompositor;
    // Symmetric with restoreAllSnapBorderless: on daemon loss / effect teardown
    // the authoritative window-rule state is gone, so restore every title bar a
    // SetHideTitleBar rule hid and drop the set. Skip a window a mode still
    // wants borderless — that mode's own teardown (restoreAllSnapBorderless /
    // AutotileHandler::restoreAllBorderless) owns its restore, and a double
    // setNoBorder(false) would fight it.
    for (const QString& windowId : m_ruleHiddenTitleBars) {
        if (m_snapHandler->isBorderlessWindow(windowId) || m_autotileHandler->isBorderlessWindow(windowId)) {
            continue;
        }
        if (KWin::EffectWindow* w = findWindowById(windowId)) {
            if (KWin::Window* kw = w->window()) {
                kw->setNoBorder(false);
            }
        }
    }
    m_ruleHiddenTitleBars.clear();
}

} // namespace PlasmaZones
