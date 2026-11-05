// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWayland/SurfaceIdentity.h>

#include <QWindow>
#include <QtWaylandClient/private/qwaylandwindow_p.h>

#include <wayland-client-core.h>

namespace PhosphorWayland {

quint32 surfaceObjectId(QWindow* window)
{
    if (!window) {
        return 0;
    }
    // dynamic_cast rather than a platform-name test, matching IdleInhibitor:
    // it answers "is there a realised Wayland window here" in one step, and a
    // null handle (window never shown) falls out of the same branch.
    auto* waylandWindow = dynamic_cast<QtWaylandClient::QWaylandWindow*>(window->handle());
    if (!waylandWindow) {
        return 0;
    }
    struct wl_surface* surface = waylandWindow->wlSurface();
    if (!surface) {
        return 0;
    }
    return wl_proxy_get_id(reinterpret_cast<struct wl_proxy*>(surface));
}

} // namespace PhosphorWayland
