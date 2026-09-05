// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "sessionlockwindow.h"
#include "layershellintegration.h"
#include <PhosphorWayland/LockSurface.h>

#include <QLoggingCategory>
#include <QScreen>
#include <qpa/qwindowsysteminterface.h>
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandscreen_p.h>
#include <QtWaylandClient/private/qwaylandshellintegration_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>

Q_LOGGING_CATEGORY(lcSessionLockWindow, "phosphorwayland.qpa.sessionlockwindow")

namespace PhosphorWayland {

static const struct ext_session_lock_surface_v1_listener s_lockSurfaceListener = {
    .configure = SessionLockWindow::handleConfigure,
};

SessionLockWindow::SessionLockWindow(LayerShellIntegration* integration, QtWaylandClient::QWaylandWindow* waylandWindow)
    : QWaylandShellSurface(waylandWindow)
    , m_integration(integration)
    , m_waylandWindow(waylandWindow)
{
    QWindow* qwindow = waylandWindow->window();

    m_wlSurface = QtWaylandClient::QWaylandShellIntegration::wlSurfaceForWindow(waylandWindow);
    if (!m_wlSurface) {
        qCCritical(lcSessionLockWindow) << "wlSurfaceForWindow returned null; cannot create a lock surface";
        return;
    }
    // The lock may have been released between createShellSurface()'s check
    // and here (an unlock racing a late show()).
    struct ext_session_lock_v1* lock = integration->activeSessionLock();
    if (!lock) {
        qCWarning(lcSessionLockWindow) << "Session lock released before the lock surface could be created";
        return;
    }

    // A lock surface is bound to one output; the protocol has no "let the
    // compositor pick" case like layer-shell's null output, so a window whose
    // screen has already gone (hot-unplug) gets no surface at all.
    struct wl_output* output = nullptr;
    QScreen* targetScreen = qwindow->screen();
    if (targetScreen) {
        if (auto* waylandScreen = dynamic_cast<QtWaylandClient::QWaylandScreen*>(targetScreen->handle()))
            output = waylandScreen->output();
    }
    if (!output) {
        qCWarning(lcSessionLockWindow) << "Lock surface window has no wl_output (screen"
                                       << (targetScreen ? targetScreen->name() : QStringLiteral("null")) << ")";
        return;
    }

    m_lockSurface = ext_session_lock_v1_get_lock_surface(lock, m_wlSurface, output);
    if (!m_lockSurface) {
        qCCritical(lcSessionLockWindow) << "ext_session_lock_v1_get_lock_surface failed";
        return;
    }
    ext_session_lock_surface_v1_add_listener(m_lockSurface, &s_lockSurfaceListener, this);

    // No wl_surface_commit here: the compositor sends the first configure on
    // binding, and committing before acking it is a protocol error. Flush so
    // the request leaves before QML compilation stalls the loop.
    if (integration->display())
        wl_display_flush(integration->display()->wl_display());

    qCDebug(lcSessionLockWindow) << "Created lock surface on" << targetScreen->name();
}

SessionLockWindow::~SessionLockWindow()
{
    if (m_lockSurface)
        ext_session_lock_surface_v1_destroy(m_lockSurface);
}

bool SessionLockWindow::isExposed() const
{
    return m_configured;
}

void SessionLockWindow::applyConfigure()
{
    if (!m_waylandWindow || !m_hasPendingConfigure)
        return;
    // Resize to the compositor's exact dimensions and ack in the same step, so
    // the buffer Qt paints next (the commit that answers this ack) matches the
    // acked size. The configure's width/height are surface-local, the same
    // units as QWindow geometry; a zero on either axis would be a compositor
    // bug and is not something a lock surface may commit, so it is left
    // un-acked.
    if (m_pendingWidth > 0 && m_pendingHeight > 0) {
        if (QWindow* qwindow = m_waylandWindow->window()) {
            const QSize newSize(static_cast<int>(m_pendingWidth), static_cast<int>(m_pendingHeight));
            if (newSize != qwindow->size())
                m_waylandWindow->resizeFromApplyConfigure(newSize);
        }
        if (m_lockSurface)
            ext_session_lock_surface_v1_ack_configure(m_lockSurface, m_pendingSerial);
        m_hasPendingConfigure = false;
    }
}

void SessionLockWindow::updatePosition()
{
    // The surface covers its output, so its screen position is the output's
    // origin; telling Qt keeps mapFromGlobal() honest for pointer hit tests.
    if (!m_waylandWindow)
        return;
    QWindow* qwindow = m_waylandWindow->window();
    if (!qwindow || !qwindow->screen())
        return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    m_waylandWindow->repositionFromApplyConfigure(qwindow->screen()->geometry().topLeft());
#endif
}

void SessionLockWindow::handleConfigure(void* data, struct ext_session_lock_surface_v1* /*surface*/, uint32_t serial,
                                        uint32_t width, uint32_t height)
{
    auto* self = static_cast<SessionLockWindow*>(data);
    if (!self->m_lockSurface || !self->m_waylandWindow)
        return;

    // Stash and let the render path ack + size the window (see
    // LayerShellWindow::handleConfigure for why acking here would attach a
    // stale buffer). The first configure is applied inline because the window
    // is not exposed yet and nothing else would drive it.
    self->m_configured = true;
    self->m_pendingSerial = serial;
    self->m_pendingWidth = width;
    self->m_pendingHeight = height;
    self->m_hasPendingConfigure = true;

    QWindow* qwindow = self->m_waylandWindow->window();
    if (qwindow && qwindow->isExposed())
        self->m_waylandWindow->applyConfigureWhenPossible();
    else
        self->applyConfigure();

    self->updatePosition();

    if (qwindow) {
        if (auto* surface = qwindow->property(LockSurfaceProps::Surface).value<LockSurface*>())
            surface->setConfigured(true);
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    self->m_waylandWindow->updateExposure();
#else
    if (qwindow)
        QWindowSystemInterface::handleExposeEvent(qwindow, QRegion(QRect(QPoint(0, 0), qwindow->size())));
#endif

    qCDebug(lcSessionLockWindow) << "Configured:" << width << "x" << height;
}

} // namespace PhosphorWayland
