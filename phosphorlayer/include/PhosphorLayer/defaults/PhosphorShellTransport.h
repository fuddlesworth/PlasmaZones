// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/phosphorlayer_export.h>

#include <memory>

namespace PhosphorLayer {

/**
 * @brief Default transport: binds surfaces to wlr-layer-shell via
 * PhosphorShell's LayerSurface class.
 *
 * Stateless — isSupported() proxies to LayerSurface::isSupported(), and
 * attach() creates exactly one LayerSurface per QWindow. Compositor-lost
 * detection hooks into PhosphorShell's global-removal registry.
 *
 * Thread-safe: addCompositorLostCallback() may be called from any thread;
 * the stored callbacks run on whatever thread PhosphorShell fires them
 * on (typically the Wayland dispatch context).
 */
class PHOSPHORLAYER_EXPORT PhosphorShellTransport : public ILayerShellTransport
{
public:
    PhosphorShellTransport();
    ~PhosphorShellTransport() override;

    bool isSupported() const override;
    std::unique_ptr<ITransportHandle> attach(QQuickWindow* win, const TransportAttachArgs& args) override;
    void addCompositorLostCallback(CompositorLostCallback cb) override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace PhosphorLayer
