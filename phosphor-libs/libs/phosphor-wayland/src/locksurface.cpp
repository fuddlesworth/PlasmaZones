// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWayland/LockSurface.h>
#include "qpa/layershellintegration.h"

#include <QGuiApplication>
#include <QHash>
#include <QLoggingCategory>
#include <QThread>

Q_LOGGING_CATEGORY(lcLockSurface, "phosphorwayland.locksurface")

namespace PhosphorWayland {

using LockRegistry = QHash<QWindow*, LockSurface*>;
Q_GLOBAL_STATIC(LockRegistry, s_lockSurfaces)

LockSurface::LockSurface(QWindow* window)
    : QObject(window)
    , m_window(window)
{
    window->setProperty(LockSurfaceProps::IsSessionLock, true);
    window->setProperty(LockSurfaceProps::Surface, QVariant::fromValue(this));

    QWindow* rawWindow = window;
    connect(window, &QObject::destroyed, this, [rawWindow]() {
        if (!s_lockSurfaces.isDestroyed())
            s_lockSurfaces->remove(rawWindow);
    });
}

LockSurface::~LockSurface()
{
    Q_ASSERT_X(!qApp || QThread::currentThread() == qApp->thread(), "LockSurface::~LockSurface",
               "must be destroyed from the GUI thread");
    if (!s_lockSurfaces.isDestroyed() && m_window)
        s_lockSurfaces->remove(m_window.data());
}

LockSurface* LockSurface::get(QWindow* window)
{
    Q_ASSERT_X(!qApp || QThread::currentThread() == qApp->thread(), "LockSurface::get",
               "must be called from the GUI thread");
    if (!window)
        return nullptr;
    auto it = s_lockSurfaces->find(window);
    if (it != s_lockSurfaces->end())
        return *it;
    if (window->isVisible()) {
        qCCritical(lcLockSurface) << "LockSurface::get() called after the window is visible; its platform"
                                  << "window already has another role. Call it before QWindow::show().";
        return nullptr;
    }
    auto* surface = new LockSurface(window);
    s_lockSurfaces->insert(window, surface);
    return surface;
}

LockSurface* LockSurface::find(QWindow* window)
{
    Q_ASSERT_X(!qApp || QThread::currentThread() == qApp->thread(), "LockSurface::find",
               "must be called from the GUI thread");
    if (!window)
        return nullptr;
    auto it = s_lockSurfaces->find(window);
    return (it != s_lockSurfaces->end()) ? *it : nullptr;
}

bool LockSurface::isSupported()
{
    auto* integration = LayerShellIntegration::instance();
    return integration && integration->sessionLockManager();
}

void LockSurface::setScreen(QScreen* screen)
{
    if (m_window && m_window->isVisible()) {
        qCWarning(lcLockSurface) << "setScreen() called after show(); the output binding is immutable";
        return;
    }
    QScreen* resolved = screen ? screen : QGuiApplication::primaryScreen();
    if (m_screen == resolved)
        return;
    m_screen = resolved;
    if (m_window && resolved)
        m_window->setScreen(resolved);
    Q_EMIT screenChanged();
}

QScreen* LockSurface::screen() const
{
    return m_screen;
}

bool LockSurface::isConfigured() const
{
    return m_configured;
}

void LockSurface::setConfigured(bool configured)
{
    if (m_configured == configured)
        return;
    m_configured = configured;
    Q_EMIT configuredChanged();
}

} // namespace PhosphorWayland
