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
 * attach() creates exactly one LayerSurface per QWindow.
 *
 * Compositor-lost detection: PhosphorShell's QPA plugin owns the
 * wlr-layer-shell global-removal signal but does not expose it through
 * a public API, so this transport currently fires compositor-lost
 * callbacks on `QGuiApplication::aboutToQuit` only (clean exit). Mid-
 * session compositor crashes are not detected until PhosphorShell gains
 * a public global-removal accessor.
 *
 * Thread-safe: addCompositorLostCallback() may be called from any thread.
 * Callbacks fire on the GUI thread (where `aboutToQuit` is emitted) and
 * are invoked outside the internal mutex, so callbacks may freely
 * re-enter the transport without deadlocking.
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
