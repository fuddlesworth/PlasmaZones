// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/FloatingWindow.h>

#include <QLoggingCategory>
#include <QQuickWindow>

// Per-window app_id goes through Qt's shell-surface abstraction, the same
// private layer popupwindow.cpp walks for xdg_popup.reposition.
#include <private/qwaylandshellsurface_p.h>
#include <private/qwaylandwindow_p.h>

namespace {
Q_LOGGING_CATEGORY(lcFloating, "phosphorshell.floating")
}

namespace PhosphorShell {

FloatingWindow::FloatingWindow(QQuickItem* parent)
    : QQuickItem(parent)
{
}

FloatingWindow::~FloatingWindow()
{
    // Hide the QQuickWindow BEFORE the unique_ptr unwinds. Without
    // this, ~QQuickWindow tears down a still-mapped xdg_toplevel
    // role, which can leave the compositor with a half-destroyed
    // surface and (depending on compositor) crash the rendering
    // thread mid-frame. PopupWindow does the same dance.
    if (m_window) {
        // Silence the visibility relay first: the hide below would
        // otherwise report a compositor close from inside the destructor.
        m_window->disconnect(this);
        m_window->hide();
    }
}

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
        // The shell surface exists only once the window is shown, so the
        // per-window identity is sent here, after show() and before the
        // first buffer (the render thread cannot commit until the
        // compositor's first configure has come back).
        applyAppId();
    } else if (m_window) {
        m_window->hide();
    }

    Q_EMIT windowVisibleChanged();
}

QString FloatingWindow::appId() const
{
    return m_appId;
}

void FloatingWindow::setAppId(const QString& appId)
{
    if (m_appId == appId) {
        return;
    }
    m_appId = appId;
    if (m_window && m_window->isVisible()) {
        applyAppId();
    }
    Q_EMIT appIdChanged();
}

int FloatingWindow::minimumWidth() const
{
    return m_minimumWidth;
}

void FloatingWindow::setMinimumWidth(int width)
{
    const int clamped = qMax(0, width);
    if (m_minimumWidth == clamped) {
        return;
    }
    m_minimumWidth = clamped;
    applyMinimumSize();
    Q_EMIT minimumWidthChanged();
}

int FloatingWindow::minimumHeight() const
{
    return m_minimumHeight;
}

void FloatingWindow::setMinimumHeight(int height)
{
    const int clamped = qMax(0, height);
    if (m_minimumHeight == clamped) {
        return;
    }
    m_minimumHeight = clamped;
    applyMinimumSize();
    Q_EMIT minimumHeightChanged();
}

QQuickWindow* FloatingWindow::quickWindow() const
{
    return m_window.get();
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

void FloatingWindow::applyAppId()
{
    if (!m_window || m_appId.isEmpty()) {
        return;
    }
    auto* waylandWindow = dynamic_cast<QtWaylandClient::QWaylandWindow*>(m_window->handle());
    if (!waylandWindow) {
        // Not a Wayland platform (offscreen tests, X11): the identity has
        // nowhere to go. Debug rather than warning: the offscreen case is
        // routine and the property still reads back for consumers.
        qCDebug(lcFloating) << "no Wayland window behind" << m_title << "— app id" << m_appId << "not sent";
        return;
    }
    QtWaylandClient::QWaylandShellSurface* shell = waylandWindow->shellSurface();
    if (!shell) {
        qCWarning(lcFloating) << "no shell surface behind" << m_title << "— app id" << m_appId << "not sent";
        return;
    }
    shell->setAppId(m_appId);
}

bool FloatingWindow::isFrameless() const
{
    return m_frameless;
}

void FloatingWindow::setFrameless(bool frameless)
{
    if (m_frameless == frameless) {
        return;
    }
    m_frameless = frameless;
    if (m_window) {
        m_window->setFlags(m_frameless ? (Qt::Window | Qt::FramelessWindowHint) : Qt::Window);
    }
    Q_EMIT framelessChanged();
}

void FloatingWindow::applyMinimumSize()
{
    if (m_window) {
        m_window->setMinimumSize(QSize(m_minimumWidth, m_minimumHeight));
    }
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
    if (m_frameless) {
        // Qt's xdg-shell client answers a frameless hint by requesting
        // client-side decoration mode, so the compositor draws no titlebar.
        m_window->setFlags(Qt::Window | Qt::FramelessWindowHint);
    }
    m_window->resize(m_windowWidth, m_windowHeight);
    applyMinimumSize();

    // A hide this object did not ask for is the compositor closing the
    // toplevel (Qt turns xdg_toplevel.close into a close event, and an
    // unhandled close event hides the window). Fold it back into the
    // property so the owner sees one consistent state.
    QObject::connect(m_window.get(), &QWindow::visibleChanged, this, [this](bool visible) {
        if (visible || !m_windowVisible) {
            return;
        }
        m_windowVisible = false;
        Q_EMIT windowVisibleChanged();
        Q_EMIT windowClosedByCompositor();
    });

    const auto children = childItems();
    for (QQuickItem* child : children) {
        reparentChildToWindow(child);
    }
}

} // namespace PhosphorShell
