// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/PopupWindow.h>

#include <QLoggingCategory>
#include <QQuickWindow>

Q_LOGGING_CATEGORY(lcPopup, "phosphorshell.popup")

namespace PhosphorShell {

PopupWindow::PopupWindow(QQuickItem* parent)
    : QQuickItem(parent)
{
}

PopupWindow::~PopupWindow()
{
    hidePopup();
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
}

int PopupWindow::popupWidth() const
{
    return m_popupWidth;
}

void PopupWindow::setPopupWidth(int width)
{
    if (m_popupWidth == width) {
        return;
    }
    m_popupWidth = width;
    Q_EMIT popupWidthChanged();
}

int PopupWindow::popupHeight() const
{
    return m_popupHeight;
}

void PopupWindow::setPopupHeight(int height)
{
    if (m_popupHeight == height) {
        return;
    }
    m_popupHeight = height;
    Q_EMIT popupHeightChanged();
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

QRect PopupWindow::computeAnchorRect() const
{
    if (!m_anchor) {
        return {};
    }

    const QPointF scenePos = m_anchor->mapToScene(QPointF(0, 0));
    return QRect(scenePos.toPoint(), QSize(static_cast<int>(m_anchor->width()), static_cast<int>(m_anchor->height())));
}

void PopupWindow::showPopup()
{
    if (!m_anchor || !m_anchor->window()) {
        qCWarning(lcPopup) << "Cannot show popup: no anchor or anchor has no window";
        return;
    }

    if (!m_popupWindow) {
        m_popupWindow = new QQuickWindow();
        m_popupWindow->setFlag(Qt::Popup);
        m_popupWindow->setColor(Qt::transparent);

        const auto children = childItems();
        for (QQuickItem* child : children) {
            child->setParentItem(m_popupWindow->contentItem());
        }
    }

    m_popupWindow->setTransientParent(m_anchor->window());
    m_popupWindow->resize(m_popupWidth, m_popupHeight);

    const QRect anchorRect = computeAnchorRect();
    m_popupWindow->setProperty("_q_waylandPopupAnchorRect", anchorRect);

    qCDebug(lcPopup) << "Showing popup: anchorRect=" << anchorRect << "size=" << m_popupWidth << "x" << m_popupHeight;
    m_popupWindow->show();
}

void PopupWindow::hidePopup()
{
    if (m_popupWindow && m_popupWindow->isVisible()) {
        m_popupWindow->hide();
        qCDebug(lcPopup) << "Popup hidden";
    }
}

} // namespace PhosphorShell
