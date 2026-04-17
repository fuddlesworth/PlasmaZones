// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/phosphorlayer_export.h>

#include <memory>

namespace PhosphorLayer {

/**
 * @brief Fallback transport for compositors without wlr-layer-shell.
 *
 * `xdg_toplevel` is the standard "application window" role — every
 * Wayland compositor supports it. This transport lets PhosphorLayer run
 * on non-Plasma compositors (GNOME/Mutter without layer-shell, nested
 * Wayland sessions, etc.) with degraded behaviour:
 *
 * - Layer (`Background`/`Bottom`/`Top`/`Overlay`): ignored. The window
 *   stacks like any other.
 * - Anchors: converted to position hints via the compositor's geometry
 *   request; edge anchoring is best-effort and honours no protocol
 *   invariants (a "top-anchored panel" becomes a window the user can
 *   move).
 * - Exclusive zone: ignored. Other surfaces may overlap the window.
 * - Keyboard interactivity: forced to OnDemand. `Exclusive` and `None`
 *   cannot be expressed through xdg_toplevel alone.
 *
 * Use via @ref SurfaceFactory::Deps when `PhosphorShellTransport::
 * isSupported()` returns false. Typically consumers probe at startup:
 *
 * @code
 *     auto plasma = std::make_unique<PhosphorShellTransport>();
 *     std::unique_ptr<ILayerShellTransport> transport = plasma->isSupported()
 *         ? std::move(plasma)
 *         : std::unique_ptr<ILayerShellTransport>(new XdgToplevelTransport);
 * @endcode
 */
class PHOSPHORLAYER_EXPORT XdgToplevelTransport : public ILayerShellTransport
{
public:
    XdgToplevelTransport();
    ~XdgToplevelTransport() override;

    bool isSupported() const override;
    std::unique_ptr<ITransportHandle> attach(QQuickWindow* win, const TransportAttachArgs& args) override;
    [[nodiscard]] CompositorLostCookie addCompositorLostCallback(CompositorLostCallback cb) override;
    void removeCompositorLostCallback(CompositorLostCookie cookie) override;

    /**
     * @brief Fire every registered compositor-lost callback.
     *
     * Exposed primarily for tests that want to exercise consumer teardown
     * paths without tearing down QGuiApplication. Consumers may also use
     * this to inject a synthetic compositor-lost event (e.g. when a
     * higher-level transport wrapper detects a logical disconnect that
     * xdg_toplevel itself does not surface). Idempotent: once fired, the
     * broadcaster stays fired and subsequent calls are no-ops.
     */
    void simulateCompositorLost();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace PhosphorLayer
