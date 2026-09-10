// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstdint>
#include <QtWaylandClient/private/qwaylandshellsurface_p.h>
#include "session_lock_protocol.h"

namespace PhosphorWayland {

class LayerShellIntegration;

/// Wayland shell surface that gives a QWindow the `ext_session_lock_surface_v1`
/// role on one output, created against the `ext_session_lock_v1` the
/// integration currently publishes. Protocol contract (ext-session-lock-v1):
///   - the compositor sends the first configure immediately on binding;
///   - the client must ack it before its first commit, and every commit
///     after an ack must carry a buffer of exactly the configured size;
///   - committing a null buffer, or before the first ack, is a fatal error.
/// So unlike LayerShellWindow this class never commits the wl_surface
/// itself: it only resizes the QWindow and acks, and Qt's next frame is the
/// commit that carries a correctly sized buffer.
class SessionLockWindow : public QtWaylandClient::QWaylandShellSurface
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(SessionLockWindow)

public:
    SessionLockWindow(LayerShellIntegration* integration, QtWaylandClient::QWaylandWindow* window);
    ~SessionLockWindow() override;

    bool isExposed() const override;
    void applyConfigure() override;

    /// False when the constructor could not create the lock surface (no
    /// wl_surface, no active lock, or the window's screen has no wl_output).
    [[nodiscard]] bool isValid() const
    {
        return m_lockSurface != nullptr;
    }

    // Public for C callback struct initialization
    static void handleConfigure(void* data, struct ext_session_lock_surface_v1* surface, uint32_t serial,
                                uint32_t width, uint32_t height);

private:
    void updatePosition();

    LayerShellIntegration* m_integration = nullptr;
    QtWaylandClient::QWaylandWindow* m_waylandWindow = nullptr;
    struct wl_surface* m_wlSurface = nullptr;
    struct ext_session_lock_surface_v1* m_lockSurface = nullptr;
    bool m_configured = false;
    bool m_hasPendingConfigure = false;
    uint32_t m_pendingSerial = 0;
    uint32_t m_pendingWidth = 0;
    uint32_t m_pendingHeight = 0;
};

} // namespace PhosphorWayland
