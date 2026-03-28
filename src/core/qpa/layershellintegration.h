// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtWaylandClient/private/qwaylandshellintegration_p.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

namespace PlasmaZones {

/// QPA shell integration plugin that binds zwlr_layer_shell_v1 and creates
/// layer surfaces for windows marked with the _pz_layer_shell property.
class LayerShellIntegration : public QtWaylandClient::QWaylandShellIntegration
{
public:
    LayerShellIntegration();
    ~LayerShellIntegration() override;

    bool initialize(QtWaylandClient::QWaylandDisplay* display) override;
    QtWaylandClient::QWaylandShellSurface* createShellSurface(QtWaylandClient::QWaylandWindow* window) override;

    struct zwlr_layer_shell_v1* layerShell() const
    {
        return m_layerShell;
    }

    /// Register this integration as the active shell integration.
    /// Must be called before QGuiApplication is created, or before any
    /// layer-shell windows are shown.
    static void registerPlugin();

    /// Access the singleton instance (available after initialize()).
    static LayerShellIntegration* instance();

private:
    static void registryHandler(void* data, struct wl_registry* registry, uint32_t id, const char* interface,
                                uint32_t version);
    static void registryRemoveHandler(void* data, struct wl_registry* registry, uint32_t id);

    struct zwlr_layer_shell_v1* m_layerShell = nullptr;
    struct wl_registry* m_registry = nullptr;
    uint32_t m_layerShellId = 0;

    static LayerShellIntegration* s_instance;
};

} // namespace PlasmaZones
