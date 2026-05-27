// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorPopout/IPopoutService.h>
#include <PhosphorPopout/PopoutRequest.h>
#include <PhosphorPopout/phosphorpopout_export.h>

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

namespace PhosphorPopout {

class IPopoutTransport;

// Concrete IPopoutService. Owns the arbitration state machine and
// delegates surface creation to an injected IPopoutTransport. One
// instance per shell process. Registered as a QML singleton so QML
// callers reach the same arbiter as C++ callers (a separate instance
// per engine would defeat the "single arbiter" guarantee, so consumers
// that want a shared instance across engines must use
// `qmlRegisterSingletonInstance` to pin the same C++ pointer in each).
//
// Arbitration rules:
//
//   Cooperative   At most one Cooperative popout per scope. Opening a
//                 new Cooperative request in the same scope closes the
//                 prior one (via the transport) before opening the new
//                 one. Different scopes are independent.
//
//   Modal         While ANY Modal is open, every new Cooperative
//                 request is rejected (open() returns ""). Existing
//                 Cooperative popouts are closed when the first Modal
//                 opens. They are NOT restored when the modal closes
//                 (the user explicitly demanded the modal; restoring
//                 would clobber whatever they're focused on next).
//                 Modal-on-Modal is allowed; modals stack.
//
//   Detached      Ignored by scope arbitration. Detached popouts open,
//                 stay open across cooperative-swap and modal-open
//                 events, and only close when explicitly closed or
//                 when their underlying surface dismisses itself.
class PHOSPHORPOPOUT_EXPORT PopoutController : public QObject, public IPopoutService
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit PopoutController(IPopoutTransport* transport, QObject* parent = nullptr);
    ~PopoutController() override;

    // IPopoutService.
    [[nodiscard]] Q_INVOKABLE QString open(const PopoutRequest& request) override;
    Q_INVOKABLE void close(const QString& handle) override;
    Q_INVOKABLE QString toggle(const PopoutRequest& request) override;
    [[nodiscard]] Q_INVOKABLE bool isOpen(const QString& popoutId) const override;
    Q_INVOKABLE void closeAll() override;

    // Diagnostic accessors. The handle for a known popoutId, or empty
    // string if the popoutId isn't currently open. Useful from QML
    // for binding visible-state badges to a specific popout.
    [[nodiscard]] Q_INVOKABLE QString handleFor(const QString& popoutId) const;

    // True if any Modal popout is currently open. Cooperative requests
    // issued while this is true are rejected.
    [[nodiscard]] Q_INVOKABLE bool isModalActive() const;

Q_SIGNALS:
    // Fired after a popout is opened. `popoutId` is the request's
    // popoutId; `handle` is the transport-assigned identifier. Use
    // `handle` for subsequent close() calls.
    void popoutOpened(const QString& popoutId, const QString& handle);

    // Fired after a popout closes, regardless of who initiated the
    // close (caller, arbitration, or transport-driven dismiss).
    void popoutClosed(const QString& popoutId, const QString& handle);

    // Fired when modal-active state changes. UIs that want to disable
    // their popout-trigger buttons while a modal is up can bind here.
    void modalActiveChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace PhosphorPopout
