// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/PopupWindow.h>

#include <PhosphorLayer/IScreenProvider.h>
#include <PhosphorLayer/Role.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/SurfaceConfig.h>
#include <PhosphorLayer/SurfaceFactory.h>

#include <PhosphorWayland/LayerSurface.h>

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

    QWindow* win = m_anchor->window();
    const QPointF scenePos = m_anchor->mapToScene(QPointF(0, 0));
    const qreal anchorW = m_anchor->width();
    const qreal anchorH = m_anchor->height();

    // For layer-shell surfaces, Qt's window geometry position is unreliable.
    // Read the actual margins set on the surface — these define where the
    // compositor placed it relative to the screen edge.
    qreal winX = 0;
    qreal winY = 0;

    if (win->property(PhosphorWayland::LayerSurfaceProps::IsLayerShell).toBool()) {
        QScreen* screen = win->screen();
        const QRect screenGeom = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);
        const int mLeft = win->property(PhosphorWayland::LayerSurfaceProps::MarginsLeft).toInt();
        const int mTop = win->property(PhosphorWayland::LayerSurfaceProps::MarginsTop).toInt();
        const int anchors = win->property(PhosphorWayland::LayerSurfaceProps::Anchors).toInt();

        constexpr int AnchorLeft = 1 << 2;
        constexpr int AnchorRight = 1 << 3;
        constexpr int AnchorTop = 1 << 0;
        constexpr int AnchorBottom = 1 << 1;

        if (anchors & AnchorLeft) {
            winX = screenGeom.x() + mLeft;
        } else if (anchors & AnchorRight) {
            const int mRight = win->property(PhosphorWayland::LayerSurfaceProps::MarginsRight).toInt();
            winX = screenGeom.x() + screenGeom.width() - win->width() - mRight;
        } else {
            winX = screenGeom.x() + (screenGeom.width() - win->width()) / 2;
        }

        if (anchors & AnchorTop) {
            winY = screenGeom.y() + mTop;
        } else if (anchors & AnchorBottom) {
            const int mBottom = win->property(PhosphorWayland::LayerSurfaceProps::MarginsBottom).toInt();
            winY = screenGeom.y() + screenGeom.height() - win->height() - mBottom;
        } else {
            winY = screenGeom.y() + (screenGeom.height() - win->height()) / 2;
        }
    } else {
        winX = win->x();
        winY = win->y();
    }

    const qreal anchorX = winX + scenePos.x();
    const qreal anchorY = winY + scenePos.y();

    qCDebug(lcPopup) << "Popup anchor at screen (" << anchorX << "," << anchorY << ")"
                     << "size" << anchorW << "x" << anchorH;

    qreal x = 0;
    qreal y = 0;

    switch (m_popupEdge) {
    case Below:
        x = anchorX + (anchorW - m_popupWidth) / 2.0;
        y = anchorY + anchorH + m_gap;
        break;
    case Above:
        x = anchorX + (anchorW - m_popupWidth) / 2.0;
        y = anchorY - m_popupHeight - m_gap;
        break;
    case LeftOf:
        x = anchorX - m_popupWidth - m_gap;
        y = anchorY + (anchorH - m_popupHeight) / 2.0;
        break;
    case RightOf:
        x = anchorX + anchorW + m_gap;
        y = anchorY + (anchorH - m_popupHeight) / 2.0;
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
    role.defaultMargins = QMargins(marginLeft, marginTop, 0, 0);
    role.scopePrefix = QStringLiteral("phosphor-shell-popup");

    PhosphorLayer::SurfaceConfig cfg;
    cfg.role = role;
    cfg.contentItem = std::unique_ptr<QQuickItem>(container);
    cfg.screen = screen;
    cfg.initialSize = QSize(m_popupWidth, m_popupHeight);
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
