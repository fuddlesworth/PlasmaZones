// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/PopupWindow.h>

#include <QLoggingCategory>
#include <QQuickWindow>
#include <QtMath>

Q_LOGGING_CATEGORY(lcPopup, "phosphorshell.popup")

namespace PhosphorShell {

PopupWindow::PopupWindow(QQuickItem* parent)
    : QQuickItem(parent)
{
}

PopupWindow::~PopupWindow()
{
    // Hide first so the compositor sees a clean unmap before the wl_surface
    // is destroyed by ~QQuickWindow.
    hidePopup();
    // unique_ptr destructor reclaims m_popupWindow.
}

QQuickItem* PopupWindow::anchor() const
{
    return m_anchor;
}

void PopupWindow::setAnchor(QQuickItem* anchor)
{
    if (m_anchor == anchor) {
        return;
    }
    m_anchor = anchor;
    Q_EMIT anchorChanged();
    // Re-issue the xdg-positioner. The positioner can only be set at
    // xdg-popup creation time, so a live anchor change requires hide+show.
    reapplyIfVisible();
}

int PopupWindow::popupWidth() const
{
    return m_popupWidth;
}

void PopupWindow::setPopupWidth(int width)
{
    // Clamp to >= 1 — Wayland surface protocols reject 0-dim surfaces,
    // and a negative width has no meaning.
    const int clamped = qMax(1, width);
    if (m_popupWidth == clamped) {
        return;
    }
    m_popupWidth = clamped;
    Q_EMIT popupWidthChanged();
    if (m_popupWindow) {
        m_popupWindow->setWidth(m_popupWidth);
    }
}

int PopupWindow::popupHeight() const
{
    return m_popupHeight;
}

void PopupWindow::setPopupHeight(int height)
{
    const int clamped = qMax(1, height);
    if (m_popupHeight == clamped) {
        return;
    }
    m_popupHeight = clamped;
    Q_EMIT popupHeightChanged();
    if (m_popupWindow) {
        m_popupWindow->setHeight(m_popupHeight);
    }
}

PopupWindow::PopupEdge PopupWindow::popupEdge() const
{
    return m_popupEdge;
}

void PopupWindow::setPopupEdge(PopupEdge edge)
{
    if (m_popupEdge == edge) {
        return;
    }
    m_popupEdge = edge;
    Q_EMIT popupEdgeChanged();
    reapplyIfVisible();
}

int PopupWindow::gap() const
{
    return m_gap;
}

void PopupWindow::setGap(int gap)
{
    if (m_gap == gap) {
        return;
    }
    m_gap = gap;
    Q_EMIT gapChanged();
    reapplyIfVisible();
}

bool PopupWindow::isPopupVisible() const
{
    return m_popupVisible;
}

void PopupWindow::setPopupVisible(bool visible)
{
    if (m_popupVisible == visible) {
        return;
    }
    m_popupVisible = visible;

    if (visible) {
        showPopup();
    } else {
        hidePopup();
    }

    Q_EMIT popupVisibleChanged();
}

void PopupWindow::close()
{
    setPopupVisible(false);
}

QRect PopupWindow::computeAnchorRect() const
{
    if (!m_anchor) {
        return {};
    }

    const QPointF scenePos = m_anchor->mapToScene(QPointF(0, 0));
    // qCeil instead of static_cast<int> — truncation can produce a sub-
    // pixel anchor rect that misses the actual anchor's right/bottom edge,
    // which makes the xdg-positioner snap the popup against the wrong
    // edge for fractional-DPI items.
    return QRect(scenePos.toPoint(), QSize(qCeil(m_anchor->width()), qCeil(m_anchor->height())));
}

void PopupWindow::showPopup()
{
    if (!m_anchor || !m_anchor->window()) {
        qCWarning(lcPopup) << "Cannot show popup: no anchor or anchor has no window";
        return;
    }

    if (!m_popupWindow) {
        m_popupWindow = std::make_unique<QQuickWindow>();
        m_popupWindow->setFlag(Qt::Popup);
        m_popupWindow->setColor(Qt::transparent);

        const auto children = childItems();
        for (QQuickItem* child : children) {
            reparentChildToWindow(child);
        }
    }

    m_popupWindow->setTransientParent(m_anchor->window());
    m_popupWindow->resize(m_popupWidth, m_popupHeight);

    // Translate popupEdge → Wayland xdg-popup positioner anchor + gravity.
    //
    // Without these, Qt's QtWayland defaults to "no anchor / no gravity"
    // → popup ends up centered ON the anchor rect (overlapping it). The
    // bug was hidden for menus anchored to a small top-left button — the
    // default placement happened to land roughly below+right — but became
    // obvious when the anchor lived at the centre of a wider panel.
    //
    // The set of properties Qt's xdg-shell client plugin actually reads
    // (verified by `strings libxdg-shell.so`):
    //   _q_waylandPopupAnchorRect          (QRect)
    //   _q_waylandPopupAnchor              (Qt::Edges → xdg anchor)
    //   _q_waylandPopupGravity             (Qt::Edges → xdg gravity)
    //   _q_waylandPopupConstraintAdjustment (uint, bitmask)
    // There is NO _q_waylandPopupAnchorOffset — Qt does not honour an
    // offset property. We bake the gap into the anchor rect itself by
    // extending the rect past the anchor item's edge by m_gap pixels in
    // the direction we want the popup to attach to.
    Qt::Edges anchorEdges;
    Qt::Edges gravityEdges;
    QRect anchorRect = computeAnchorRect();
    switch (m_popupEdge) {
    case Above:
        anchorEdges = Qt::TopEdge;
        gravityEdges = Qt::TopEdge;
        anchorRect.adjust(0, -m_gap, 0, 0); // grow upward
        break;
    case Below:
        anchorEdges = Qt::BottomEdge;
        gravityEdges = Qt::BottomEdge;
        anchorRect.adjust(0, 0, 0, m_gap); // grow downward
        break;
    case LeftOf:
        anchorEdges = Qt::LeftEdge;
        gravityEdges = Qt::LeftEdge;
        anchorRect.adjust(-m_gap, 0, 0, 0); // grow leftward
        break;
    case RightOf:
        anchorEdges = Qt::RightEdge;
        gravityEdges = Qt::RightEdge;
        anchorRect.adjust(0, 0, m_gap, 0); // grow rightward
        break;
    }

    // xdg_positioner.set_anchor_rect requires the rect to lie within the
    // parent surface's logical bounds AND be non-empty; an out-of-bounds
    // or zero-area rect is a protocol error that drops the wl_display
    // connection. Clamp our gap-extended rect to the parent QQuickWindow's
    // content bounds (which equals the parent surface for layer-shell
    // parents). If the anchor item is fully off-surface and BOTH the
    // gap-extended and unextended rects collapse to empty, abort the
    // show — sending an empty positioner would kill the connection.
    if (auto* parentWindow = m_anchor->window()) {
        const QRect surfaceBounds(0, 0, parentWindow->width(), parentWindow->height());
        anchorRect = anchorRect.intersected(surfaceBounds);
        if (anchorRect.isEmpty()) {
            // Fall back to the un-extended anchor rect — better to lose
            // the gap than to send an invalid positioner.
            anchorRect = computeAnchorRect().intersected(surfaceBounds);
        }
        if (anchorRect.isEmpty()) {
            qCWarning(lcPopup) << "Cannot show popup: anchor item is off-surface (computed rect empty); "
                                  "skipping show to avoid xdg-positioner protocol error";
            // Roll the visibility flag back so a subsequent
            // setPopupVisible(true) re-attempts cleanly. We don't emit
            // popupVisibleChanged again — caller already saw `true`.
            m_popupVisible = false;
            return;
        }
    }

    m_popupWindow->setProperty("_q_waylandPopupAnchorRect", anchorRect);
    // Pass as uint — Qt's xdg-shell plugin reads these via QVariant::toUInt
    // and uses the value as a Qt::Edges-indexed jump-table key. Passing as
    // QFlags<Qt::Edge> via QVariant::fromValue can fail to convert (no
    // registered Qt::Edges→uint conversion) and the property silently
    // falls back to its default (top-left anchor / bottom-right gravity =
    // popup roughly right-of-and-below the anchor — which matches the
    // observed misplacement).
    m_popupWindow->setProperty("_q_waylandPopupAnchor", static_cast<uint>(anchorEdges));
    m_popupWindow->setProperty("_q_waylandPopupGravity", static_cast<uint>(gravityEdges));
    // 0xF = SlideX|SlideY|FlipX|FlipY — let the compositor reposition the
    // popup if it'd run off-screen.
    m_popupWindow->setProperty("_q_waylandPopupConstraintAdjustment", static_cast<uint>(0xF));

    qCDebug(lcPopup) << "Showing popup: anchorRect=" << anchorRect << "size=" << m_popupWidth << "x" << m_popupHeight
                     << "edge=" << m_popupEdge << "anchor=" << static_cast<uint>(anchorEdges)
                     << "gravity=" << static_cast<uint>(gravityEdges)
                     << "anchorItem.scenePos=" << (m_anchor ? m_anchor->mapToScene(QPointF(0, 0)) : QPointF())
                     << "anchorItem.size=" << (m_anchor ? QSizeF(m_anchor->width(), m_anchor->height()) : QSizeF());
    m_popupWindow->show();
}

void PopupWindow::hidePopup()
{
    if (m_popupWindow && m_popupWindow->isVisible()) {
        m_popupWindow->hide();
        qCDebug(lcPopup) << "Popup hidden";
    }
}

void PopupWindow::itemChange(ItemChange change, const ItemChangeData& value)
{
    // Late-added children (created after the popup window has been
    // materialised) need to be migrated to the popup's contentItem,
    // otherwise they'd live on the PopupWindow QQuickItem and never
    // appear inside the floating popup surface. Same pattern as
    // FloatingWindow::itemChange.
    if (change == ItemChildAddedChange && m_popupWindow && value.item) {
        reparentChildToWindow(value.item);
    }
    QQuickItem::itemChange(change, value);
}

void PopupWindow::reparentChildToWindow(QQuickItem* child)
{
    child->setParentItem(m_popupWindow->contentItem());
}

void PopupWindow::reapplyIfVisible()
{
    if (!m_popupVisible || !m_popupWindow) {
        return;
    }
    // Hide+show cycle re-creates the xdg-popup with a fresh positioner.
    // The QQuickWindow itself is reused (children stay parented to its
    // contentItem); only the wl_surface role/positioner is replayed.
    //
    // Capture the focused item before hiding so we can restore focus
    // after the show — without this a TextField / SpinBox / Button that
    // had keyboard focus would lose it whenever the popup re-applies
    // its positioner (e.g. anchor moves while the popup is open).
    QPointer<QQuickItem> focusedBefore(m_popupWindow->activeFocusItem());
    hidePopup();
    showPopup();
    // Only restore focus if showPopup actually succeeded — the off-
    // surface guard in showPopup rolls m_popupVisible back to false
    // and returns without making the popup visible. forceActiveFocus
    // on an item under a hidden window would just queue focus on a
    // surface the user can't see.
    if (m_popupVisible && focusedBefore) {
        focusedBefore->forceActiveFocus();
    }
}

} // namespace PhosphorShell
