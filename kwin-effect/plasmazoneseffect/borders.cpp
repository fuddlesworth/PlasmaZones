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
#include "shader_resolve.h"
#include "window_query.h"

#include <PhosphorCompositor/AutotileState.h>

#include <optional>

namespace PlasmaZones {

namespace {
// Drop the server-side decoration while keeping the window filling its zone.
// KWin holds the CLIENT size constant across a decoration change, so calling
// setNoBorder(true) AFTER the zone geometry was applied shrinks the frame by the
// title-bar height and leaves a gap at the bottom of the zone. Re-assert the
// zone rect after dropping the decoration so the content grows to fill it.
//
// CRITICAL: use moveResizeGeometry(), NOT frameGeometry(). On Wayland the latter
// lags behind moveResize() until the client acks the configure, so right after
// applySnapGeometry's moveResize it still reports the window's PRE-snap frame —
// capturing and re-applying that would clobber the snap (the window reverts to
// its floating size/position; this broke snap restore entirely). moveResizeGeometry()
// is the zone rect KWin is already moving toward, set synchronously by moveResize.
// The setNoBorder→moveResize ordering mirrors autotile hiding the title bar
// BEFORE its moveResize (autotilehandler/state.cpp::setWindowBorderless).
void hideTitleBarFillingZone(KWin::Window* kw)
{
    const KWin::RectF zoneTarget = kw->moveResizeGeometry();
    kw->setNoBorder(true);
    // Only re-assert a real target. A degenerate move-resize geometry would
    // otherwise resize the window to nothing; in that case leave the decoration
    // change to settle on its own.
    if (zoneTarget.isValid() && !zoneTarget.isEmpty()) {
        kw->moveResize(zoneTarget);
    }
}
} // namespace

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

    // Choose color: active for focused window, inactive for others.
    const QColor activeColor = (ovr && ovr->borderColor) ? *ovr->borderColor : (state ? state->color : QColor());
    // Inactive falls back to the active rule colour when a rule supplies only
    // an active colour AND the window has no owning mode (floating): without
    // this a rule-bordered floating window would lose its border entirely when
    // it loses focus (invalid inactiveColor → early-return below). A snapped
    // window still inherits its mode's inactive colour.
    const QColor inactiveColor = (ovr && ovr->inactiveBorderColor) ? *ovr->inactiveBorderColor
        : state                                                    ? state->inactiveColor
        : (ovr && ovr->borderColor)                                ? *ovr->borderColor
                                                                   : QColor();
    KWin::EffectWindow* active = KWin::effects->activeWindow();
    const bool isFocused = (w == active);
    const QColor bc = isFocused ? activeColor : inactiveColor;
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
    const bool modeBorderless = AutotileStateHelpers::isBorderlessWindow(m_snapBorder, windowId)
        || m_autotileHandler->isBorderlessWindow(windowId);

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
    if (AutotileStateHelpers::shouldShowBorderForWindow(m_snapBorder, windowId)) {
        return &m_snapBorder;
    }
    return nullptr;
}

void PlasmaZonesEffect::markWindowSnapped(const QString& windowId, const QString& screenId)
{
    using namespace PhosphorCompositor;
    // An empty screenId is never a valid snap owner: the per-screen buckets are
    // keyed by screenId, so recording under "" would pollute the set with an
    // entry that the per-screen stripOtherScreens cleanup can never reclaim.
    // Callers route unresolved/float windows through clearWindowSnapped instead;
    // this guard is defensive depth for any path that slips an empty screen in.
    if (windowId.isEmpty() || screenId.isEmpty()) {
        return;
    }
    // A window can only be snap-managed by one screen at a time. Strip stale
    // tracking from any OTHER screen — both the tiled and borderless buckets —
    // before recording the new owner (mirrors the autotile cross-screen-transfer
    // cleanup in tiling.cpp).
    const auto stripOtherScreens = [&](QHash<QString, QSet<QString>>& byScreen) {
        for (auto it = byScreen.begin(); it != byScreen.end();) {
            if (it.key() != screenId) {
                it.value().remove(windowId);
            }
            if (it.value().isEmpty() && it.key() != screenId) {
                it = byScreen.erase(it);
            } else {
                ++it;
            }
        }
    };
    stripOtherScreens(m_snapBorder.tiledWindowsByScreen);
    stripOtherScreens(m_snapBorder.borderlessWindowsByScreen);
    AutotileStateHelpers::addTiledOnScreen(m_snapBorder, screenId, windowId);

    KWin::EffectWindow* w = findWindowById(windowId);
    KWin::Window* kw = w ? w->window() : nullptr;
    // userCanSetNoBorder() — not hasDecoration() — is the correct test for "may
    // this window's server-side title bar be toggled off." It reports whether KWin
    // ALLOWS the no-border toggle, so it stays true for a normal SSD window even
    // while that window is CURRENTLY borderless — exactly the autotile→snap handoff
    // case, where the window arrives already borderless (autotile stripped its
    // decoration) and hasDecoration() therefore reads false. It is false for windows
    // that have no server-side title bar to hide in the first place — client-side-
    // decorated apps (GTK/Electron) and other non-toggleable windows (override-
    // redirect, or decoration forced by a window rule). Using hasDecoration() here
    // skipped the handoff, so snap never recorded the borderless ownership and the
    // deferred restoreWindowBorders un-hid the title bar autotile had hidden.
    // (AutotileHandler::setWindowBorderless still gates on hasDecoration(); the snap
    // side intentionally diverges because only it must survive the already-borderless
    // handoff.)
    if (m_snapBorder.hideTitleBars && kw && kw->userCanSetNoBorder()) {
        const bool wasBorderless = AutotileStateHelpers::isBorderlessWindow(m_snapBorder, windowId);
        AutotileStateHelpers::addBorderlessOnScreen(m_snapBorder, screenId, windowId);
        if (!wasBorderless) {
            hideTitleBarFillingZone(kw);
        }
    }
    // A null w means the window is gone (closed mid-snap); the bucket entry
    // recorded above is then harmless — if the window later closes, slotWindowClosed
    // clears m_snapBorder for it. No border is drawn (nothing to draw on) and none
    // is needed; updateAllBorders() iterates only live windows so it simply skips it.
    //
    // Border overlays are visual-only, so skip the off-desktop case (consistent
    // with updateAllBorders): an OutlinedBorderItem for an invisible window is
    // wasted work. When the user switches to that window's desktop, the
    // desktopChanged → updateAllBorders connection rebuilds its border.
    if (w && w->isOnCurrentDesktop()) {
        updateWindowBorder(windowId, w);
    }
}

void PlasmaZonesEffect::clearWindowSnapped(const QString& windowId)
{
    using namespace PhosphorCompositor;
    if (windowId.isEmpty()) {
        return;
    }
    const bool wasBorderless = AutotileStateHelpers::isBorderlessWindow(m_snapBorder, windowId);
    AutotileStateHelpers::removeFromAllScreens(m_snapBorder, windowId);
    // Snap never populates zoneGeometries — snap borders render off the live
    // frame geometry, not stored zone geometry; the remove/clear calls exist
    // only for structural symmetry with the autotile BorderState.
    m_snapBorder.zoneGeometries.remove(windowId);
    // Restore the title bar only if snap had hidden it AND autotile doesn't
    // still want it borderless. A window mid-transition between modes can be in
    // both sets briefly; un-hiding here would fight autotile's authoritative
    // borderless management and flash the title bar.
    if (wasBorderless && !m_autotileHandler->isBorderlessWindow(windowId)) {
        if (KWin::EffectWindow* w = findWindowById(windowId)) {
            if (KWin::Window* kw = w->window()) {
                kw->setNoBorder(false);
            }
        }
    }
    removeWindowBorder(windowId);
}

void PlasmaZonesEffect::updateSnapHideTitleBars(bool hide)
{
    using namespace PhosphorCompositor;
    m_snapBorder.hideTitleBars = hide;
    if (hide) {
        // Hide on every currently snap-committed window.
        const auto pairs = AutotileStateHelpers::allTiledPairs(m_snapBorder);
        for (const auto& p : pairs) {
            KWin::EffectWindow* w = findWindowById(p.first);
            if (!w || !w->hasDecoration()) {
                continue;
            }
            if (!AutotileStateHelpers::isBorderlessWindow(m_snapBorder, p.first)) {
                AutotileStateHelpers::addBorderlessOnScreen(m_snapBorder, p.second, p.first);
                if (KWin::Window* kw = w->window()) {
                    hideTitleBarFillingZone(kw);
                }
            }
        }
    } else {
        // Restore every window snap had made borderless, except one autotile
        // still wants borderless. A window mid-transition between modes can be in
        // both sets briefly; un-hiding here would fight autotile's authoritative
        // borderless management and flash the title bar (mirrors the guard in
        // clearWindowSnapped / restoreAllSnapBorderless).
        const auto pairs = AutotileStateHelpers::allBorderlessPairs(m_snapBorder);
        for (const auto& p : pairs) {
            AutotileStateHelpers::removeBorderlessOnScreen(m_snapBorder, p.second, p.first);
            if (m_autotileHandler->isBorderlessWindow(p.first)) {
                continue;
            }
            if (KWin::EffectWindow* w = findWindowById(p.first)) {
                if (KWin::Window* kw = w->window()) {
                    kw->setNoBorder(false);
                }
            }
        }
    }
    updateAllBorders();
}

void PlasmaZonesEffect::restoreAllSnapBorderless()
{
    using namespace PhosphorCompositor;
    // Symmetric with AutotileHandler::restoreAllBorderless: on daemon loss or
    // effect teardown the authoritative snap state is gone, so restore every
    // title bar snapping hid and drop the whole snap border set. Without this,
    // snap-hidden windows would keep their title bars hidden until a new snap
    // event or app restart. The isBorderlessWindow guard below is the live
    // protection in the per-window path (clearWindowSnapped); at the teardown
    // call sites AutotileHandler::restoreAllBorderless() runs first and clears
    // its set, so the guard is a belt-and-braces no-op here (a window autotile
    // shares is already un-hidden by then, and a second setNoBorder(false) is
    // harmless/idempotent).
    // This drops the snap border STATE only; callers pair it with
    // clearAllBorders() to tear down the OutlinedBorderItem scene items.
    const auto pairs = AutotileStateHelpers::allBorderlessPairs(m_snapBorder);
    for (const auto& p : pairs) {
        if (m_autotileHandler->isBorderlessWindow(p.first)) {
            continue;
        }
        if (KWin::EffectWindow* w = findWindowById(p.first)) {
            if (KWin::Window* kw = w->window()) {
                kw->setNoBorder(false);
            }
        }
    }
    m_snapBorder.borderlessWindowsByScreen.clear();
    m_snapBorder.tiledWindowsByScreen.clear();
    m_snapBorder.zoneGeometries.clear();
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
        if (AutotileStateHelpers::isBorderlessWindow(m_snapBorder, windowId)
            || m_autotileHandler->isBorderlessWindow(windowId)) {
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
