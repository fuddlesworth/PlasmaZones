// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include <effect/effecthandler.h>
#include <scene/borderoutline.h>
#include <scene/outlinedborderitem.h>
#include <scene/windowitem.h>

#include "../autotilehandler.h"
#include "../scrollhandler.h"

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

    // A window is managed by exactly one placement mode — the daemon's scroll
    // and autotile screen sets are disjoint — so the handler that tracks the
    // window also owns its border settings (width, colors, radius, visibility).
    const bool isScroll = m_scrollHandler && m_scrollHandler->isTiledWindow(windowId);

    const int bw = isScroll ? m_scrollHandler->borderWidth() : m_autotileHandler->borderWidth();
    if (bw <= 0) {
        return;
    }

    if (!w || w->isMinimized() || w->isFullScreen()) {
        return;
    }

    const bool showBorder = isScroll ? m_scrollHandler->shouldShowBorderForWindow(windowId)
                                     : m_autotileHandler->shouldShowBorderForWindow(windowId);
    if (!showBorder) {
        return;
    }

    // Choose color: active for focused window, inactive for others
    KWin::EffectWindow* active = KWin::effects->activeWindow();
    const bool isFocused = (w == active);
    const QColor bc = isScroll
        ? (isFocused ? m_scrollHandler->borderColor() : m_scrollHandler->inactiveBorderColor())
        : (isFocused ? m_autotileHandler->borderColor() : m_autotileHandler->inactiveBorderColor());
    if (!bc.isValid() || bc.alpha() == 0) {
        return;
    }

    // The OutlinedBorderItem draws the border OUTSIDE the innerRect, but the
    // parent WindowItem clips children to the window frame.  Inset the innerRect
    // by borderWidth so the border draws fully inside the frame (no clipping).
    const QRectF frame = w->frameGeometry();
    const KWin::RectF innerRect(bw, bw, frame.width() - 2.0 * bw, frame.height() - 2.0 * bw);
    const int br = isScroll ? m_scrollHandler->borderRadius() : m_autotileHandler->borderRadius();
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

    // Iterate all effect windows and create borders for tiled ones. Border
    // width is no longer mode-uniform (scroll and autotile carry independent
    // appearance settings), so the per-window bw<=0 short-circuit in
    // updateWindowBorder() handles the disabled case instead of an early bail.
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        if (!w || w->isDeleted() || !w->isOnCurrentDesktop()) {
            continue;
        }
        const QString wid = getWindowId(w);
        const bool showBorder = m_autotileHandler->shouldShowBorderForWindow(wid)
            || (m_scrollHandler && m_scrollHandler->shouldShowBorderForWindow(wid));
        if (showBorder) {
            updateWindowBorder(wid, w);
        }
    }
}

} // namespace PlasmaZones
