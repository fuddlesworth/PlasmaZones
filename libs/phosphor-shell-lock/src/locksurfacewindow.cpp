// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShellLock/LockSurfaceWindow.h>

#include <PhosphorWayland/LockSurface.h>
#include <PhosphorWayland/SessionLock.h>

#include <QColor>
#include <QLoggingCategory>
#include <QScreen>

Q_LOGGING_CATEGORY(lcLockSurfaceWindow, "phosphorshelllock.surface")

namespace PhosphorShellLock {

LockSurfaceWindow::LockSurfaceWindow(QWindow* parent)
    : QQuickWindow(parent)
{
    // The void ground (A3 consistency table) as the window's clear colour,
    // so the first frame the compositor presents is never a white flash
    // before QML paints.
    setColor(QColor(0x05, 0x09, 0x16));
    // Marks the window BEFORE any show: the platform window's role is
    // decided at creation.
    m_surface = PhosphorWayland::LockSurface::get(this);
    if (m_surface) {
        connect(m_surface, &PhosphorWayland::LockSurface::configuredChanged, this,
                &LockSurfaceWindow::configuredChanged);
    }
}

LockSurfaceWindow::~LockSurfaceWindow() = default;

QScreen* LockSurfaceWindow::screen() const
{
    return QWindow::screen();
}

void LockSurfaceWindow::setScreen(QScreen* screen)
{
    if (QWindow::screen() == screen && screen)
        return;
    if (m_surface)
        m_surface->setScreen(screen);
    else
        QWindow::setScreen(screen);
}

bool LockSurfaceWindow::isConfigured() const
{
    return m_surface && m_surface->isConfigured();
}

bool LockSurfaceWindow::canExist() const
{
    return PhosphorWayland::SessionLock::canCreateSurfaces();
}

void LockSurfaceWindow::showEvent(QShowEvent* event)
{
    if (!canExist()) {
        // Queued rather than inline: this runs inside QWindow::setVisible,
        // which must finish before the window's visibility flips back.
        qCWarning(lcLockSurfaceWindow) << "LockSurface shown while no session lock is held; refusing";
        Q_EMIT refused();
        QMetaObject::invokeMethod(
            this,
            [this]() {
                hide();
            },
            Qt::QueuedConnection);
    }
    QQuickWindow::showEvent(event);
}

} // namespace PhosphorShellLock
