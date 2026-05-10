// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/FloatingWindow.h>

#include <QQuickWindow>

namespace PhosphorShell {

FloatingWindow::FloatingWindow(QQuickItem* parent)
    : QQuickItem(parent)
{
}

FloatingWindow::~FloatingWindow() = default; // unique_ptr cleans up m_window

QString FloatingWindow::title() const
{
    return m_title;
}

void FloatingWindow::setTitle(const QString& title)
{
    if (m_title == title) {
        return;
    }
    m_title = title;
    if (m_window) {
        m_window->setTitle(m_title);
    }
    Q_EMIT titleChanged();
}

int FloatingWindow::windowWidth() const
{
    return m_windowWidth;
}

void FloatingWindow::setWindowWidth(int width)
{
    // Clamp to >= 1 — Wayland surface protocols reject 0-dim surfaces,
    // negative width has no meaning, and QWindow::setWidth would itself
    // assert in debug builds. Symmetric with PopupWindow / PanelWindow.
    const int clamped = qMax(1, width);
    if (m_windowWidth == clamped) {
        return;
    }
    m_windowWidth = clamped;
    if (m_window) {
        m_window->setWidth(m_windowWidth);
    }
    Q_EMIT windowWidthChanged();
}

int FloatingWindow::windowHeight() const
{
    return m_windowHeight;
}

void FloatingWindow::setWindowHeight(int height)
{
    const int clamped = qMax(1, height);
    if (m_windowHeight == clamped) {
        return;
    }
    m_windowHeight = clamped;
    if (m_window) {
        m_window->setHeight(m_windowHeight);
    }
    Q_EMIT windowHeightChanged();
}

bool FloatingWindow::isWindowVisible() const
{
    return m_windowVisible;
}

void FloatingWindow::setWindowVisible(bool visible)
{
    if (m_windowVisible == visible) {
        return;
    }
    m_windowVisible = visible;

    if (visible) {
        ensureWindow();
        m_window->show();
    } else if (m_window) {
        m_window->hide();
    }

    Q_EMIT windowVisibleChanged();
}

void FloatingWindow::itemChange(ItemChange change, const ItemChangeData& value)
{
    // QML children declared on the FloatingWindow before ensureWindow() runs
    // are reparented into m_window's contentItem when the window is created.
    // Children added LATER (e.g. via dynamic creation, Loader, or after
    // first show) need to be migrated as well, otherwise they live on the
    // FloatingWindow QQuickItem and are never visible in the floating
    // surface. itemChange catches both first-add and runtime adds.
    if (change == ItemChildAddedChange && m_window && value.item) {
        reparentChildToWindow(value.item);
    }
    QQuickItem::itemChange(change, value);
}

void FloatingWindow::reparentChildToWindow(QQuickItem* child)
{
    child->setParentItem(m_window->contentItem());
}

void FloatingWindow::ensureWindow()
{
    if (m_window) {
        return;
    }

    m_window = std::make_unique<QQuickWindow>();
    m_window->setTitle(m_title);
    // Match PopupWindow's transparent default — frosted/rounded shell
    // elements expect to draw their own background. A consumer that
    // wants opaque can set a Rectangle with a solid color anyway.
    m_window->setColor(Qt::transparent);
    m_window->resize(m_windowWidth, m_windowHeight);

    const auto children = childItems();
    for (QQuickItem* child : children) {
        reparentChildToWindow(child);
    }
}

} // namespace PhosphorShell
