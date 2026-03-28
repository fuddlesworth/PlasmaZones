// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtWaylandClient/private/qwaylandshellintegration_p.h>
#include "../plasmazones_export.h"
// The generated protocol header uses "namespace" as a parameter name in
// zwlr_layer_shell_v1_get_layer_surface(). This is a C identifier that
// collides with the C++ reserved word. The #define renames it to
// "namespace_" so the header compiles in C++ mode. This is the same
// workaround used by LayerShellQt and other C++ Wayland clients.
extern "C" {
#define namespace namespace_
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace
}

namespace PlasmaZones {

/// QPA shell integration plugin that binds zwlr_layer_shell_v1 and creates
/// layer surfaces for windows marked with the _pz_layer_shell property.
class PLASMAZONES_EXPORT LayerShellIntegration : public QtWaylandClient::QWaylandShellIntegration
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

    /// Access the singleton instance (available after Qt loads the plugin).
    static LayerShellIntegration* instance();

    // Public for C callback struct initialization
    static void registryHandler(void* data, struct wl_registry* registry, uint32_t id, const char* interface,
                                uint32_t version);
    static void registryRemoveHandler(void* data, struct wl_registry* registry, uint32_t id);

private:
    struct zwlr_layer_shell_v1* m_layerShell = nullptr;
    struct wl_registry* m_registry = nullptr;
    uint32_t m_layerShellId = 0;
    uint32_t m_boundVersion = 0;

    static LayerShellIntegration* s_instance;
};

} // namespace PlasmaZones
