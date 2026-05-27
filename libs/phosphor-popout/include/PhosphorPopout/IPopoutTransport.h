// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorPopout/PopoutRequest.h>
#include <PhosphorPopout/phosphorpopout_export.h>

#include <QString>
#include <QtCore/qtclasshelpermacros.h> // Q_DISABLE_COPY_MOVE

#include <functional>

namespace PhosphorPopout {

// Transport seam between the arbitration policy in PopoutController
// and the actual layer-shell surface stack. Production uses a
// transport backed by phosphor-layer's SurfaceFactory. Tests inject
// a fake.
//
// The transport owns the lifetime of every surface it creates. The
// controller hands it a PopoutRequest and gets back an opaque string
// handle. Handles are globally unique within the transport instance.
// closeSurface is idempotent. Calling it twice or on an already-
// dismissed surface is a no-op.
//
// The dismissed callback notifies the controller when a surface goes
// away on its own initiative. Possible triggers include focus loss,
// a click outside the surface, the QML delegate calling close, or
// compositor revocation. The controller uses that signal to update
// its arbitration state without re-issuing closeSurface back to the
// transport. Re-issuing would loop.
class PHOSPHORPOPOUT_EXPORT IPopoutTransport
{
public:
    IPopoutTransport() = default;
    virtual ~IPopoutTransport() = default;
    Q_DISABLE_COPY_MOVE(IPopoutTransport)

    // Create and show a popout surface for the request. Returns an
    // opaque handle the controller uses for later close and lookup.
    // The empty string is a sentinel meaning the transport refused
    // to open. Reasons include an invalid screen, missing content,
    // or layer-shell being unavailable. The controller treats an
    // empty return as "open failed" and does not update its
    // arbitration tables.
    [[nodiscard]] virtual QString openSurface(const PopoutRequest& request) = 0;

    // Tear down the surface for handle. No-op if the handle is
    // unknown or already closed. Must NOT invoke the dismissed
    // callback for this handle. The callback is reserved for
    // surfaces that close themselves. The controller may call
    // closeSurface synchronously from inside the dismissed callback
    // for OTHER handles (e.g., during arbitration-driven teardown);
    // the controller's re-entrancy guard handles this case.
    virtual void closeSurface(const QString& handle) = 0;

    // Install a callback fired when a surface dismisses itself. The
    // controller installs exactly one callback per transport during
    // construction and detaches it via an empty std::function in its
    // destructor. Implementations must replace any prior callback when
    // called, so the controller's detach-on-destruction cannot leave
    // a dangling lambda behind. The callback receives the handle of
    // the dismissed surface. The transport must NOT call this for
    // surfaces closed via closeSurface. Those are caller-initiated
    // and the controller already knows. Transports also must NOT
    // synchronously invoke this from inside openSurface or
    // closeSurface; the controller has a re-entrancy guard for
    // misbehaving transports but well-behaved transports defer
    // dismiss events to the next event-loop tick.
    //
    // Thread affinity: the callback must be invoked on the same
    // thread the PopoutController lives on (typically the GUI
    // thread). The controller mutates internal tables synchronously
    // from the callback and does not lock. Transports that observe
    // surface-dismissed events on a Wayland reader or background
    // thread must marshal via QMetaObject::invokeMethod or a queued
    // signal-slot connection before firing the callback.
    virtual void setSurfaceDismissedCallback(std::function<void(const QString&)> callback) = 0;
};

} // namespace PhosphorPopout
