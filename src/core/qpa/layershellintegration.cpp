// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layershellintegration.h"
#include "layershellwindow.h"
#include "../layersurface.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <QLoggingCategory>
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>
#include <QtWaylandClient/private/qwaylandshellintegrationfactory_p.h>

Q_LOGGING_CATEGORY(lcLayerShellIntegration, "plasmazones.qpa.layershell")

namespace PlasmaZones {

LayerShellIntegration* LayerShellIntegration::s_instance = nullptr;

static const struct wl_registry_listener s_registryListener = {
    .global = LayerShellIntegration::registryHandler,
    .global_remove = LayerShellIntegration::registryRemoveHandler,
};

LayerShellIntegration::LayerShellIntegration() = default;

LayerShellIntegration::~LayerShellIntegration()
{
    if (m_layerShell) {
        if (m_boundVersion >= 3) {
            // Protocol-level destroy request (added in v3)
            zwlr_layer_shell_v1_destroy(m_layerShell);
        } else {
            // v1/v2 don't have a destroy request — clean up the client-side proxy
            // to avoid leaking the wl_proxy allocation.
            wl_proxy_destroy(reinterpret_cast<struct wl_proxy*>(m_layerShell));
        }
    }
    if (m_registry) {
        wl_registry_destroy(m_registry);
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

bool LayerShellIntegration::initialize(QtWaylandClient::QWaylandDisplay* display)
{
    m_display = display;

    // Get wl_display and do a registry roundtrip to bind zwlr_layer_shell_v1
    struct wl_display* wlDisplay = display->wl_display();
    m_registry = wl_display_get_registry(wlDisplay);
    wl_registry_add_listener(m_registry, &s_registryListener, this);
    wl_display_roundtrip(wlDisplay);

    if (!m_layerShell) {
        qCWarning(lcLayerShellIntegration) << "Compositor does not support zwlr_layer_shell_v1 —"
                                           << "overlays will fall back to xdg_toplevel (wrong stacking/anchoring)."
                                           << "GNOME/Mutter does not implement this protocol.";
        return false;
    }

    // Set singleton only after successful initialization — if initialize() returns
    // false, Qt may destroy this object, and we must not leave a dangling s_instance.
    Q_ASSERT_X(!s_instance || s_instance == this, "LayerShellIntegration", "Double initialization detected");
    s_instance = this;

    qCInfo(lcLayerShellIntegration) << "Layer shell integration initialized";
    return true;
}

QtWaylandClient::QWaylandShellSurface*
LayerShellIntegration::createShellSurface(QtWaylandClient::QWaylandWindow* window)
{
    // Only create layer surfaces for windows we've marked
    QWindow* qwindow = window->window();
    if (!qwindow || !qwindow->property(LayerSurfaceProps::IsLayerShell).toBool()) {
        // Not a layer-shell window — delegate to xdg-shell so the window gets
        // a proper xdg_toplevel or xdg_popup role. Without this, the window has
        // no shell role and is invisible (Qt logs "Could not create a shell surface object").
        if (!m_xdgShell && m_display) {
            m_xdgShell.reset(
                QtWaylandClient::QWaylandShellIntegrationFactory::create(QStringLiteral("xdg-shell"), m_display));
            if (m_xdgShell) {
                qCDebug(lcLayerShellIntegration) << "Loaded xdg-shell fallback for regular windows";
            } else {
                qCWarning(lcLayerShellIntegration) << "Failed to load xdg-shell fallback";
            }
        }
        if (m_xdgShell) {
            return m_xdgShell->createShellSurface(window);
        }
        return nullptr;
    }

    if (!m_layerShell) {
        qCWarning(lcLayerShellIntegration) << "Cannot create layer surface: no zwlr_layer_shell_v1";
        return nullptr;
    }

    auto* shellWindow = new LayerShellWindow(this, window);
    if (!shellWindow->isValid()) {
        // Constructor failed to create a layer surface (wl_surface was null or
        // the global was removed between our check and the constructor). Delete
        // the zombie to avoid Qt calling methods on a half-initialized object.
        delete shellWindow;
        return nullptr;
    }
    return shellWindow;
}

void LayerShellIntegration::registryHandler(void* data, struct wl_registry* registry, uint32_t id,
                                            const char* interface, uint32_t version)
{
    auto* self = static_cast<LayerShellIntegration*>(data);
    if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        // Only bind once — if a compositor advertises the global multiple times,
        // ignore subsequent advertisements to avoid leaking the first binding.
        if (self->m_layerShell) {
            qCDebug(lcLayerShellIntegration) << "Ignoring duplicate zwlr_layer_shell_v1 global (id" << id << ")";
            return;
        }
        // Maximum protocol version we bind:
        //   v1: base protocol
        //   v2: set_layer (runtime layer changes)
        //   v3: destroy request
        //   v4: on_demand keyboard interactivity — we use this
        //   v5: set_exclusive_edge — we do NOT use this
        // Capping at v4 avoids negotiation issues with older compositors
        // while supporting all features PlasmaZones needs.
        static constexpr uint32_t kMaxBindVersion = 4;
        uint32_t bindVersion = qMin(version, kMaxBindVersion);
        self->m_layerShell = static_cast<struct zwlr_layer_shell_v1*>(
            wl_registry_bind(registry, id, &zwlr_layer_shell_v1_interface, bindVersion));
        self->m_layerShellId = id;
        self->m_boundVersion = bindVersion;
        self->m_globalAvailable = true;
        qCDebug(lcLayerShellIntegration).nospace() << "Bound zwlr_layer_shell_v1 v" << bindVersion;
    }
}

void LayerShellIntegration::registryRemoveHandler(void* data, struct wl_registry* registry, uint32_t id)
{
    Q_UNUSED(registry)
    auto* self = static_cast<LayerShellIntegration*>(data);
    if (id == self->m_layerShellId) {
        qCWarning(lcLayerShellIntegration) << "zwlr_layer_shell_v1 global removed (id" << id << ") —"
                                           << "new layer surface creation will fail."
                                           << "Existing layer surfaces may become invalid."
                                           << "If the compositor crashed, these surfaces are now stale.";
        // Do NOT destroy the global object here — existing LayerShellWindow
        // instances hold zwlr_layer_surface_v1 objects created from this global.
        // Destroying the global while child surfaces are alive causes UAF.
        // The compositor removing the global means it is shutting down or
        // reconfiguring; the layer surfaces will be invalidated by the compositor
        // and receive closed events. We mark the global as unavailable to prevent
        // new surface creation, but keep m_layerShell and m_boundVersion intact
        // so the destructor can properly clean up the wl_proxy binding.
        self->m_globalAvailable = false;
        self->m_layerShellId = 0;

        // Notify any active LayerShellWindow instances that the global is gone.
        // Their zwlr_layer_surface_v1 objects are now stale — the compositor will
        // send closed events, but nulling m_layerSurface proactively prevents us
        // from issuing protocol requests on dead objects in the interim.
        auto callbacks = std::exchange(self->m_globalRemovedCallbacks, {});
        for (const auto& [cbId, cb] : callbacks) {
            cb();
        }
    }
}

LayerShellIntegration::CallbackId LayerShellIntegration::addGlobalRemovedCallback(GlobalRemovedCallback cb)
{
    CallbackId id = m_nextCallbackId++;
    m_globalRemovedCallbacks.push_back({id, std::move(cb)});
    return id;
}

void LayerShellIntegration::removeGlobalRemovedCallback(CallbackId id)
{
    m_globalRemovedCallbacks.erase(std::remove_if(m_globalRemovedCallbacks.begin(), m_globalRemovedCallbacks.end(),
                                                  [id](const auto& entry) {
                                                      return entry.first == id;
                                                  }),
                                   m_globalRemovedCallbacks.end());
}

} // namespace PlasmaZones
