// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/phosphorlayer_export.h>

#include <memory>

namespace PhosphorLayer {

/**
 * @brief Default transport: binds surfaces to wlr-layer-shell via
 * PhosphorWayland's LayerSurface class.
 *
 * Stateless — isSupported() proxies to LayerSurface::isSupported(), and
 * attach() creates exactly one LayerSurface per QWindow.
 *
 * Compositor-lost detection covers two edges:
 *  - Mid-session: PhosphorWayland's QPA plugin observes
 *    `wl_registry::global_remove` for `zwlr_layer_shell_v1` and forwards
 *    the edge through `PhosphorWayland::addCompositorLostCallback`. This
 *    transport subscribes at construction so a compositor crash / restart
 *    fires registered callbacks before Qt tears the connection down.
 *  - Clean exit: `QGuiApplication::aboutToQuit` covers the case where the
 *    application terminates before the compositor sent a removal.
 *
 * Both feed the same one-shot internal broadcaster, so consumer callbacks
 * fire at most once.
 *
 * Thread-safe: addCompositorLostCallback() may be called from any thread.
 * Callbacks fire on the GUI thread (where `aboutToQuit` is emitted and
 * where the QPA plugin dispatches Wayland events under the standard
 * QGuiApplication setup) and are invoked outside the internal mutex, so
 * callbacks may freely re-enter the transport without deadlocking.
 */
class PHOSPHORLAYER_EXPORT PhosphorWaylandTransport : public ILayerShellTransport
{
public:
    PhosphorWaylandTransport();
    ~PhosphorWaylandTransport() override;

    bool isSupported() const override;
    std::unique_ptr<ITransportHandle> attach(QQuickWindow* win, const TransportAttachArgs& args) override;
    [[nodiscard]] CompositorLostCookie addCompositorLostCallback(CompositorLostCallback cb) override;
    void removeCompositorLostCallback(CompositorLostCookie cookie) override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace PhosphorLayer
