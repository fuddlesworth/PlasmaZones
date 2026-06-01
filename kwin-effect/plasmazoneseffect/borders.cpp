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

#include <PhosphorCompositor/AutotileState.h>

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

    // Resolve which mode (autotile / snap) manages this window and draw with
    // that mode's border settings. nullptr → neither mode shows a border for it.
    const PhosphorCompositor::BorderState* state = resolveBorderStateFor(windowId);
    if (!state) {
        return;
    }

    const int bw = state->width;
    if (bw <= 0) {
        return;
    }

    if (!w || w->isMinimized() || w->isFullScreen()) {
        return;
    }

    // Choose color: active for focused window, inactive for others
    KWin::EffectWindow* active = KWin::effects->activeWindow();
    const bool isFocused = (w == active);
    const QColor bc = isFocused ? state->color : state->inactiveColor;
    if (!bc.isValid() || bc.alpha() == 0) {
        return;
    }

    // The OutlinedBorderItem draws the border OUTSIDE the innerRect, but the
    // parent WindowItem clips children to the window frame.  Inset the innerRect
    // by borderWidth so the border draws fully inside the frame (no clipping).
    const QRectF frame = w->frameGeometry();
    const KWin::RectF innerRect(bw, bw, frame.width() - 2.0 * bw, frame.height() - 2.0 * bw);
    const int br = state->radius;
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
    // a mode (autotile or snap) that currently shows borders. Per-window
    // resolution lets the two modes carry independent width/colour/radius.
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        if (!w || w->isDeleted() || !w->isOnCurrentDesktop()) {
            continue;
        }
        const QString wid = getWindowId(w);
        if (resolveBorderStateFor(wid)) {
            updateWindowBorder(wid, w);
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
    // Title-bar hiding mirrors AutotileHandler::setWindowBorderless: skip CSD
    // windows (no server-side decoration) and only call setNoBorder once.
    if (m_snapBorder.hideTitleBars && w && w->hasDecoration()) {
        const bool wasBorderless = AutotileStateHelpers::isBorderlessWindow(m_snapBorder, windowId);
        AutotileStateHelpers::addBorderlessOnScreen(m_snapBorder, screenId, windowId);
        if (!wasBorderless) {
            if (KWin::Window* kw = w->window()) {
                hideTitleBarFillingZone(kw);
            }
        }
    }
    // A null w means the window is gone (closed mid-snap); the bucket entry
    // recorded above is then harmless — if the window later closes, slotWindowClosed
    // clears m_snapBorder for it. No border is drawn (nothing to draw on) and none
    // is needed; updateAllBorders() iterates only live windows so it simply skips it.
    if (w) {
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

} // namespace PlasmaZones
