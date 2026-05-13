// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWayland/IdleInhibitor.h>
#include "qpa/layershellintegration.h"
#include "qpa/idle_inhibit_protocol.h"

#include <QLoggingCategory>
#include <QWindow>
#include <QtWaylandClient/private/qwaylandwindow_p.h>

Q_LOGGING_CATEGORY(lcIdleInhibitor, "phosphorwayland.idleinhibitor")

namespace PhosphorWayland {

class IdleInhibitor::Private
{
public:
    QWindow* window = nullptr;
    struct zwp_idle_inhibitor_v1* inhibitor = nullptr;
    bool deferredConnected = false;
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
        if (!d->deferredConnected) {
            connect(d->window, &QWindow::visibleChanged, this, [this](bool visible) {
                if (visible && !d->inhibitor)
                    createInhibitor();
            });
            d->deferredConnected = true;
        }
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
    }
    Q_EMIT activeChanged();
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
