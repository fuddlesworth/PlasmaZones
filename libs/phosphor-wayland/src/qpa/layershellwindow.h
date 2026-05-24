// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstdint>
#include <QtWaylandClient/private/qwaylandshellsurface_p.h>
#include "wlr_layer_shell_protocol.h"

namespace PhosphorWayland {

class LayerShellIntegration;

/// Wayland shell surface that creates a zwlr_layer_surface_v1 instead of
/// an xdg_toplevel. Reads configuration from QWindow dynamic properties
/// set by LayerSurface.
class LayerShellWindow : public QtWaylandClient::QWaylandShellSurface
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(LayerShellWindow)

public:
    LayerShellWindow(LayerShellIntegration* integration, QtWaylandClient::QWaylandWindow* window);
    ~LayerShellWindow() override;

    // QWaylandShellSurface overrides
    bool isExposed() const override;
    void applyConfigure() override;
    void setWindowGeometry(const QRect& rect) override;
    void attachPopup(QtWaylandClient::QWaylandShellSurface* popup) override;

    /// Returns false if the constructor failed to create a layer surface (zombie object).
    [[nodiscard]] bool isValid() const
    {
        return m_layerSurface != nullptr;
    }

    // Apply current properties from LayerSurface -> protocol
    void applyProperties();

    // Calculate and set the window's screen position from anchors/margins/output
    // so QWindow::mapFromGlobal() returns correct local coordinates.
    void updatePosition();

    // Public for C callback struct initialization
    static void handleConfigure(void* data, struct zwlr_layer_surface_v1* surface, uint32_t serial, uint32_t width,
                                uint32_t height);
    static void handleClosed(void* data, struct zwlr_layer_surface_v1* surface);

private:
    /// Compute the window size from a configure event, respecting compositor-controlled axes.
    [[nodiscard]] QSize computeConfigureSize(uint32_t width, uint32_t height) const;

    /// Snapshot of the last layer-shell state we pushed to the compositor.
    /// Each applyProperties() call diffs the QWindow's current properties against
    /// this snapshot and skips the corresponding zwlr_layer_surface_v1_* setter
    /// whenever a field hasn't changed since the last apply. The compositor
    /// re-sends configure events on every virtual-desktop change (KWin protocol
    /// behavior) and the prior code re-sent the entire layer-shell state on each
    /// one, generating ~6 redundant protocol messages per surface per configure.
    /// The first apply (before m_hasAppliedOnce is set) writes every field
    /// unconditionally so the compositor always sees a complete initial state.
    struct AppliedLayerShellState
    {
        int anchors = 0;
        int layer = 0;
        int exclusiveZone = 0;
        int keyboard = 0;
        int marginLeft = 0;
        int marginTop = 0;
        int marginRight = 0;
        int marginBottom = 0;
        uint32_t sizeW = 0;
        uint32_t sizeH = 0;
        int exclusiveEdge = 0;
        bool exclusiveEdgeSent = false; ///< false until we've sent a valid edge at least once
    };

    LayerShellIntegration* m_integration = nullptr;
    QtWaylandClient::QWaylandWindow* m_waylandWindow = nullptr;
    struct zwlr_layer_surface_v1* m_layerSurface = nullptr;
    struct wl_surface* m_wlSurface = nullptr;
    bool m_configured = false;
    uint32_t m_pendingWidth = 0;
    uint32_t m_pendingHeight = 0;
    uint32_t m_pendingSerial = 0; ///< Serial of the most recent unacked configure
    bool m_hasPendingConfigure = false;
    uint64_t m_globalRemovedCallbackId = 0;
    AppliedLayerShellState m_lastApplied{};
    bool m_hasAppliedOnce = false;
};

} // namespace PhosphorWayland
