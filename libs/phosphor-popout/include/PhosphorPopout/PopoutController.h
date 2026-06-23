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
// callers reach the same arbiter as C++ callers. A separate instance
// per engine would defeat the single-arbiter guarantee. Consumers that
// want a shared instance across engines must use
// qmlRegisterSingletonInstance to pin the same C++ pointer in each.
//
// Arbitration rules.
//
//   Cooperative   At most one Cooperative popout per scope. Opening a
//                 new Cooperative request in the same scope closes the
//                 prior one before opening the new one. Different
//                 scopes are independent. A second open with a
//                 popoutId that is already open is rejected with an
//                 empty handle regardless of scope. Callers that want
//                 to reopen by id must close first or use toggle.
//
//   Modal         While ANY Modal is open, every new Cooperative
//                 request is rejected. open returns an empty string.
//                 Existing Cooperative popouts are closed when the
//                 first Modal opens. They are NOT restored when the
//                 modal closes. The user explicitly demanded the
//                 modal. Restoring would clobber whatever they focus
//                 next. Modal-on-Modal stacks, but a second Modal
//                 with the same popoutId is rejected like any other
//                 same-id collision.
//
//   Detached      Ignored by scope arbitration. Detached popouts open,
//                 stay open across cooperative-swap and modal-open
//                 events, and only close when explicitly closed or
//                 when their underlying surface dismisses itself.
class PHOSPHORPOPOUT_EXPORT PopoutController : public QObject, public IPopoutService
{
    Q_OBJECT
    // QML-bindable mirror of isModalActive(). UIs binding a "disable
    // popout-trigger buttons while a modal is up" rule reach this
    // directly via `Popouts.modalActive` instead of wiring an
    // imperative onModalActiveChanged handler.
    Q_PROPERTY(bool modalActive READ isModalActive NOTIFY modalActiveChanged)
    QML_ELEMENT
    // PopoutController requires a transport injected at construction.
    // QML cannot default-construct it. The type is registered for
    // type-annotation visibility only. Production wiring uses
    // qmlRegisterSingletonInstance from C++ to publish a pre-built
    // instance to QML. The demo uses a context property.
    QML_UNCREATABLE(
        "Construct PopoutController in C++ with an IPopoutTransport, then expose via qmlRegisterSingletonInstance")

public:
    // transport must be non-null. The controller does not own the
    // transport. The caller must keep the transport alive at least as
    // long as the controller. Null transport aborts via qFatal in both
    // debug and release builds; a silent crash on the first transport
    // method call would be harder to diagnose than the explicit abort.
    explicit PopoutController(IPopoutTransport* transport, QObject* parent = nullptr);
    ~PopoutController() override;

    // QObject blocks copy via Q_OBJECT but leaves move ill-formed
    // rather than deleted. Make the intent explicit so static
    // analyzers and readers see both copy and move are forbidden,
    // matching the disable-copy-move pattern on IPopoutService /
    // IPopoutTransport.
    Q_DISABLE_COPY_MOVE(PopoutController)

    // IPopoutService. Re-declared as Q_INVOKABLE so QML callers reach
    // them through this concrete type. The base interface has no
    // Q_OBJECT/Q_GADGET so it cannot carry Q_INVOKABLE itself.
    [[nodiscard]] Q_INVOKABLE QString open(const PopoutRequest& request) override;
    Q_INVOKABLE void close(const QString& handle) override;
    [[nodiscard]] Q_INVOKABLE QString toggle(const PopoutRequest& request) override;
    [[nodiscard]] Q_INVOKABLE bool isOpen(const QString& popoutId) const override;
    Q_INVOKABLE void closeAll() override;

    // Diagnostic accessor. Returns the handle for a known popoutId, or
    // empty string if the popoutId isn't currently open. Useful from
    // QML for binding visible-state badges to a specific popout.
    [[nodiscard]] Q_INVOKABLE QString handleFor(const QString& popoutId) const;

    // True if any Modal popout is currently open. Cooperative requests
    // issued while this is true are rejected.
    [[nodiscard]] Q_INVOKABLE bool isModalActive() const;

Q_SIGNALS:
    // Fired after a popout is opened. popoutId is the request's
    // popoutId. handle is the transport-assigned identifier. Use
    // handle for subsequent close calls.
    void popoutOpened(const QString& popoutId, const QString& handle);

    // Fired after a popout closes. Fires regardless of who initiated
    // the close. The initiator may be the caller, arbitration policy,
    // or a transport-driven dismiss.
    void popoutClosed(const QString& popoutId, const QString& handle);

    // Fired when modal-active state changes. UIs that want to disable
    // their popout-trigger buttons while a modal is up can bind here.
    void modalActiveChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace PhosphorPopout
