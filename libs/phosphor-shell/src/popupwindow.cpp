// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/PopupWindow.h>

#include <PhosphorLayer/IScreenProvider.h>
#include <PhosphorLayer/Role.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/SurfaceConfig.h>
#include <PhosphorLayer/SurfaceFactory.h>

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QMargins>
#include <QQuickWindow>
#include <QScreen>

Q_LOGGING_CATEGORY(lcPopup, "phosphorshell.popup")

namespace PhosphorShell {

PopupWindow::PopupWindow(QQuickItem* parent)
    : QQuickItem(parent)
{
}

PopupWindow::~PopupWindow()
{
    destroySurface();
}

void PopupWindow::initialize(PhosphorLayer::SurfaceFactory* factory, PhosphorLayer::IScreenProvider* screens)
{
    m_factory = factory;
    m_screenProvider = screens;
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
        if (!m_surface) {
            createSurface();
        } else {
            m_surface->show();
        }
    } else {
        if (m_surface) {
            m_surface->hide();
        }
    }

    Q_EMIT popupVisibleChanged();
}

QPointF PopupWindow::computePosition() const
{
    if (!m_anchor || !m_anchor->window()) {
        return {0, 0};
    }

    const QPointF anchorScenePos = m_anchor->mapToScene(QPointF(0, 0));
    const QPointF anchorScreenPos = m_anchor->window()->position() + anchorScenePos;
    const qreal anchorW = m_anchor->width();
    const qreal anchorH = m_anchor->height();

    qreal x = 0;
    qreal y = 0;

    switch (m_popupEdge) {
    case Below:
        x = anchorScreenPos.x() + (anchorW - m_popupWidth) / 2.0;
        y = anchorScreenPos.y() + anchorH + m_gap;
        break;
    case Above:
        x = anchorScreenPos.x() + (anchorW - m_popupWidth) / 2.0;
        y = anchorScreenPos.y() - m_popupHeight - m_gap;
        break;
    case LeftOf:
        x = anchorScreenPos.x() - m_popupWidth - m_gap;
        y = anchorScreenPos.y() + (anchorH - m_popupHeight) / 2.0;
        break;
    case RightOf:
        x = anchorScreenPos.x() + anchorW + m_gap;
        y = anchorScreenPos.y() + (anchorH - m_popupHeight) / 2.0;
        break;
    }

    QScreen* screen = m_screenProvider ? m_screenProvider->primary() : nullptr;
    if (screen) {
        const QRect screenGeom = screen->geometry();
        x = qBound(static_cast<qreal>(screenGeom.x()), x,
                   static_cast<qreal>(screenGeom.x() + screenGeom.width() - m_popupWidth));
        y = qBound(static_cast<qreal>(screenGeom.y()), y,
                   static_cast<qreal>(screenGeom.y() + screenGeom.height() - m_popupHeight));
    }

    return {x, y};
}

void PopupWindow::createSurface()
{
    if (m_surface || !m_factory || !m_screenProvider) {
        if (!m_factory) {
            qCWarning(lcPopup) << "PopupWindow not initialized — call initialize() first";
        }
        return;
    }

    const QPointF pos = computePosition();
    QScreen* screen = m_screenProvider->primary();
    const QRect screenGeom = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);

    const int marginLeft = qMax(0, static_cast<int>(pos.x()) - screenGeom.x());
    const int marginTop = qMax(0, static_cast<int>(pos.y()) - screenGeom.y());

    auto* container = new QQuickItem;
    container->setWidth(m_popupWidth);
    container->setHeight(m_popupHeight);

    const auto children = childItems();
    for (QQuickItem* child : children) {
        child->setParentItem(container);
    }

    PhosphorLayer::Role role;
    role.layer = PhosphorLayer::Layer::Overlay;
    role.anchors = PhosphorLayer::Anchor::Top | PhosphorLayer::Anchor::Left;
    role.exclusiveZone = -1;
    role.keyboard = PhosphorLayer::KeyboardInteractivity::OnDemand;
    role.scopePrefix = QStringLiteral("phosphor-shell-popup");

    PhosphorLayer::SurfaceConfig cfg;
    cfg.role = role;
    cfg.contentItem = std::unique_ptr<QQuickItem>(container);
    cfg.screen = screen;
    cfg.initialSize = QSize(m_popupWidth, m_popupHeight);
    cfg.marginsOverride = QMargins(marginLeft, marginTop, 0, 0);
    cfg.debugName = QStringLiteral("phosphor-shell-popup");

    m_surface = m_factory->create(std::move(cfg), this);
    if (m_surface) {
        m_surface->show();
        qCDebug(lcPopup) << "Popup shown at" << pos << "size" << m_popupWidth << "x" << m_popupHeight;
    } else {
        qCWarning(lcPopup) << "Failed to create popup surface";
    }
}

void PopupWindow::destroySurface()
{
    if (!m_surface) {
        return;
    }

    m_surface->hide();
    delete m_surface;
    m_surface = nullptr;
    qCDebug(lcPopup) << "Popup destroyed";
}

} // namespace PhosphorShell
