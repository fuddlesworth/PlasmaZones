// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtWaylandClient/private/qwaylandshellsurface_p.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

namespace PlasmaZones {

class LayerShellIntegration;

/// Wayland shell surface that creates a zwlr_layer_surface_v1 instead of
/// an xdg_toplevel. Reads configuration from QWindow dynamic properties
/// set by LayerSurface.
class LayerShellWindow : public QtWaylandClient::QWaylandShellSurface
{
    Q_OBJECT

public:
    LayerShellWindow(LayerShellIntegration* integration, QtWaylandClient::QWaylandWindow* window);
    ~LayerShellWindow() override;

    // QWaylandShellSurface overrides
    bool isExposed() const override;
    void applyConfigure() override;
    void setWindowGeometry(const QRect& rect) override;

    // Apply current properties from LayerSurface → protocol
    void applyProperties();

private:
    static void handleConfigure(void* data, struct zwlr_layer_surface_v1* surface, uint32_t serial, uint32_t width,
                                uint32_t height);
    static void handleClosed(void* data, struct zwlr_layer_surface_v1* surface);

    LayerShellIntegration* m_integration = nullptr;
    struct zwlr_layer_surface_v1* m_layerSurface = nullptr;
    struct wl_output* m_output = nullptr;
    bool m_configured = false;
    uint32_t m_pendingWidth = 0;
    uint32_t m_pendingHeight = 0;
};

} // namespace PlasmaZones
