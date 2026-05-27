// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorPopout/PopoutRequest.h>
#include <PhosphorPopout/phosphorpopout_export.h>

#include <QString>

#include <functional>

namespace PhosphorPopout {

// Transport seam between the arbitration policy (PopoutController) and
// the actual layer-shell surface stack. Production uses a transport
// backed by phosphor-layer's SurfaceFactory; tests inject a fake.
//
// The transport owns the lifetime of every surface it creates. The
// controller hands it a PopoutRequest and gets back an opaque string
// handle. Handles are globally unique within the transport instance.
// closeSurface() is idempotent; calling it twice or on an already-
// dismissed surface is a no-op.
//
// The dismissed callback notifies the controller when a surface goes
// away on its own initiative (focus loss, user clicked the close
// button, the QML delegate called close, the compositor revoked the
// surface). The controller uses that signal to update its arbitration
// state without re-issuing closeSurface back to the transport, which
// would loop.
class PHOSPHORPOPOUT_EXPORT IPopoutTransport
{
public:
    virtual ~IPopoutTransport() = default;

    // Create and show a popout surface for `request`. Returns an opaque
    // handle the controller uses for later close() / lookup. The empty
    // string is a sentinel meaning the transport refused to open
    // (invalid screen, missing content, layer-shell unavailable). The
    // controller treats an empty return as "open failed" and does not
    // update its arbitration tables.
    [[nodiscard]] virtual QString openSurface(const PopoutRequest& request) = 0;

    // Tear down the surface for `handle`. No-op if the handle is unknown
    // or already closed. Must NOT invoke the dismissed callback for this
    // handle; the callback is reserved for surfaces that close themselves.
    virtual void closeSurface(const QString& handle) = 0;

    // True while a surface is still alive (the transport has not yet
    // confirmed its destruction). Used by the controller to validate
    // its handle cache when called from re-entrant contexts.
    [[nodiscard]] virtual bool isSurfaceAlive(const QString& handle) const = 0;

    // Install a callback fired when a surface dismisses itself. The
    // controller installs exactly one callback per transport during
    // construction. The callback receives the handle of the dismissed
    // surface. The transport must NOT call this for surfaces closed
    // via closeSurface() (those are caller-initiated; the controller
    // already knows).
    virtual void setSurfaceDismissedCallback(std::function<void(const QString&)> callback) = 0;
};

} // namespace PhosphorPopout
