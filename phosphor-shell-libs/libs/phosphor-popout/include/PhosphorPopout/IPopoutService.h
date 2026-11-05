// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorPopout/PopoutRequest.h>
#include <PhosphorPopout/phosphorpopout_export.h>

#include <QString>
#include <QtCore/qtclasshelpermacros.h> // Q_DISABLE_COPY_MOVE

namespace PhosphorPopout {

// Service contract for centralized popout coordination.
//
// A shell instantiates exactly one IPopoutService and exposes it to
// QML and to any C++ caller that wants to open a transient surface.
// The default concrete implementation is PopoutController. Alternate
// implementations plug in by satisfying this interface.
//
// The service is the single arbiter for popout lifetime, focus, and
// exclusivity policy. The transport seam handles the actual surface
// creation. Arbitration lives here. This split is what avoids the
// "two popups fighting over a Wayland grab" bug class. Every
// transient surface in the shell routes through the same arbiter,
// so conflicting open requests are resolved consistently.
//
// The interface stays pure-virtual to keep tests cheap. No QObject
// base. No signal/slot machinery exposed at the abstract layer.
// Concrete implementations bring their own QObject when notifications
// are needed.
//
// Alternate implementations must re-mark their overrides as Q_INVOKABLE
// (the macro can only attach to a Q_OBJECT or Q_GADGET, neither of
// which this interface uses) if QML callers are to reach them through
// a context-property or singleton handle. The default concrete
// implementation, PopoutController, does this.
class PHOSPHORPOPOUT_EXPORT IPopoutService
{
public:
    IPopoutService() = default;
    virtual ~IPopoutService() = default;
    Q_DISABLE_COPY_MOVE(IPopoutService)

    // Open a popout per the request. Returns an opaque handle on
    // success. Returns an empty string when the request was rejected
    // by arbitration policy or by the transport. An arbitration
    // rejection happens when a Cooperative request is issued while
    // a Modal popout is open. A transport rejection happens on an
    // invalid screen or missing content. Callers should not assume
    // an empty handle means an error. It can also mean policy says
    // no right now.
    [[nodiscard]] virtual QString open(const PopoutRequest& request) = 0;

    // Close the popout with handle. No-op if the handle is unknown,
    // already closed, or never opened in the first place.
    virtual void close(const QString& handle) = 0;

    // Toggle a popout by its stable popoutId. If a popout with the
    // request's popoutId is currently open, close it. Otherwise open
    // it per the request. Returns the handle of the newly opened
    // popout. Returns empty string if the call closed an existing
    // popout, or if the open was rejected by policy.
    [[nodiscard]] virtual QString toggle(const PopoutRequest& request) = 0;

    // True if any popout with this popoutId is currently open. Note
    // that "open" is independent of "visible to the user". A
    // Cooperative popout in scope A is open even if a Modal is
    // currently suppressing its successors elsewhere.
    [[nodiscard]] virtual bool isOpen(const QString& popoutId) const = 0;

    // Close every popout the service is currently tracking. Useful
    // on shell teardown and for plugins that want to enforce a clean
    // slate. One use case is before showing a session-lock surface.
    virtual void closeAll() = 0;
};

} // namespace PhosphorPopout
