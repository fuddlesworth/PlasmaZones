// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWayland/IdleInhibitor.h>
#include "qpa/layershellintegration.h"
#include "qpa/idle_inhibit_protocol.h"

#include <QLoggingCategory>
#include <QPointer>
#include <QWindow>
#include <QtWaylandClient/private/qwaylandwindow_p.h>

Q_LOGGING_CATEGORY(lcIdleInhibitor, "phosphorwayland.idleinhibitor")

namespace PhosphorWayland {

class IdleInhibitor::Private
{
public:
    // QPointer so a window destroyed out from under us auto-nulls rather
    // than dangling — the QML author owns the QWindow, not us.
    QPointer<QWindow> window;
    struct zwp_idle_inhibitor_v1* inhibitor = nullptr;
    // Live handle to the deferred-creation hook on `window`. Stored (not
    // a bool flag) so it can be torn down when the surface changes —
    // otherwise switching surfaces leaks the old connection and the flag
    // suppresses wiring the new one.
    QMetaObject::Connection visibleConn;
};

IdleInhibitor::IdleInhibitor(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
}

IdleInhibitor::~IdleInhibitor()
{
    destroyInhibitor();
}

QWindow* IdleInhibitor::surface() const
{
    return d->window;
}

void IdleInhibitor::setSurface(QWindow* window)
{
    if (d->window == window)
        return;
    destroyInhibitor();
    // Drop any deferred-creation hook bound to the previous window.
    QObject::disconnect(d->visibleConn);
    d->visibleConn = {};
    d->window = window;
    Q_EMIT surfaceChanged();
    createInhibitor();
}

bool IdleInhibitor::isActive() const
{
    return d->inhibitor != nullptr;
}

bool IdleInhibitor::isSupported()
{
    auto* integration = LayerShellIntegration::instance();
    return integration && integration->idleInhibitManager();
}

void IdleInhibitor::createInhibitor()
{
    destroyInhibitor();
    if (!d->window)
        return;
    auto* integration = LayerShellIntegration::instance();
    if (!integration)
        return;
    auto* manager = integration->idleInhibitManager();
    if (!manager) {
        qCDebug(lcIdleInhibitor) << "zwp_idle_inhibit_manager_v1 not available";
        return;
    }
    auto* waylandWindow = dynamic_cast<QtWaylandClient::QWaylandWindow*>(d->window->handle());
    if (!waylandWindow) {
        qCDebug(lcIdleInhibitor) << "Window has no Wayland handle yet — inhibitor deferred";
        // Re-arm the deferred hook for THIS window, replacing any stale
        // connection (createInhibitor may run repeatedly before the
        // surface is realised).
        QObject::disconnect(d->visibleConn);
        d->visibleConn = connect(d->window, &QWindow::visibleChanged, this, [this](bool visible) {
            if (visible && !d->inhibitor)
                createInhibitor();
        });
        return;
    }
    struct wl_surface* wlSurface = waylandWindow->wlSurface();
    if (!wlSurface) {
        qCWarning(lcIdleInhibitor) << "wl_surface is null — cannot create inhibitor";
        return;
    }
    d->inhibitor = zwp_idle_inhibit_manager_v1_create_inhibitor(manager, wlSurface);
    if (d->inhibitor) {
        qCDebug(lcIdleInhibitor) << "Idle inhibitor created for surface" << d->window;
        Q_EMIT activeChanged();
    }
}

void IdleInhibitor::destroyInhibitor()
{
    if (!d->inhibitor)
        return;
    zwp_idle_inhibitor_v1_destroy(d->inhibitor);
    d->inhibitor = nullptr;
    Q_EMIT activeChanged();
}

} // namespace PhosphorWayland
