// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layershellintegration.h"
#include "layershellwindow.h"

#include <QLoggingCategory>
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>

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
        zwlr_layer_shell_v1_destroy(m_layerShell);
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

bool LayerShellIntegration::initialize(QtWaylandClient::QWaylandDisplay* display)
{
    s_instance = this;

    // Get wl_display and do a registry roundtrip to bind zwlr_layer_shell_v1
    struct wl_display* wlDisplay = display->wl_display();
    m_registry = wl_display_get_registry(wlDisplay);
    wl_registry_add_listener(m_registry, &s_registryListener, this);
    wl_display_roundtrip(wlDisplay);

    if (!m_layerShell) {
        qCWarning(lcLayerShellIntegration) << "Compositor does not support zwlr_layer_shell_v1";
        return false;
    }

    qCInfo(lcLayerShellIntegration) << "Layer shell integration initialized";
    return true;
}

QtWaylandClient::QWaylandShellSurface*
LayerShellIntegration::createShellSurface(QtWaylandClient::QWaylandWindow* window)
{
    // Only create layer surfaces for windows we've marked
    QWindow* qwindow = window->window();
    if (!qwindow || !qwindow->property("_pz_layer_shell").toBool()) {
        // Not a layer-shell window — return nullptr so Qt falls back
        // to the default shell integration (xdg_toplevel).
        return nullptr;
    }

    if (!m_layerShell) {
        qCWarning(lcLayerShellIntegration) << "Cannot create layer surface: no zwlr_layer_shell_v1";
        return nullptr;
    }

    return new LayerShellWindow(this, window);
}

void LayerShellIntegration::registryHandler(void* data, struct wl_registry* registry, uint32_t id,
                                            const char* interface, uint32_t version)
{
    auto* self = static_cast<LayerShellIntegration*>(data);
    if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        // Bind version 4 max (supports on_demand keyboard interactivity)
        uint32_t bindVersion = qMin(version, 4u);
        self->m_layerShell = static_cast<struct zwlr_layer_shell_v1*>(
            wl_registry_bind(registry, id, &zwlr_layer_shell_v1_interface, bindVersion));
        self->m_layerShellId = id;
        qCDebug(lcLayerShellIntegration) << "Bound zwlr_layer_shell_v1 v" << bindVersion;
    }
}

void LayerShellIntegration::registryRemoveHandler(void* data, struct wl_registry* registry, uint32_t id)
{
    Q_UNUSED(registry)
    auto* self = static_cast<LayerShellIntegration*>(data);
    if (id == self->m_layerShellId) {
        qCWarning(lcLayerShellIntegration) << "zwlr_layer_shell_v1 global removed";
        if (self->m_layerShell) {
            zwlr_layer_shell_v1_destroy(self->m_layerShell);
            self->m_layerShell = nullptr;
        }
    }
}

void LayerShellIntegration::registerPlugin()
{
    // Set environment so Qt uses our shell integration.
    // This must be called before QGuiApplication construction.
    qputenv("QT_WAYLAND_SHELL_INTEGRATION", "pz-layer-shell");
}

LayerShellIntegration* LayerShellIntegration::instance()
{
    return s_instance;
}

} // namespace PlasmaZones
