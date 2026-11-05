// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorPopout/PopoutRequest.h>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <memory>

class QQmlComponent;
class QQmlEngine;
class QQuickItem;

namespace PhosphorPopout {
class PopoutController;
}

namespace PhosphorPopoutDemo {

class InAppPopoutTransport;

// QML-exposed glue for the demo. Owns the PopoutController and the
// in-app transport, and exposes Q_INVOKABLE methods the QML buttons
// call. Mirroring the controller's state into Q_PROPERTYs lets the
// status bar reflect what's currently open without QML having to
// register the controller as a singleton.
class DemoController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList openPopoutIds READ openPopoutIds NOTIFY openPopoutIdsChanged)
    Q_PROPERTY(bool modalActive READ modalActive NOTIFY modalActiveChanged)
public:
    explicit DemoController(QObject* parent = nullptr);
    ~DemoController() override;

    // Wire QML side after the engine and root window exist. The demo's
    // Main.qml calls this from Component.onCompleted. The QML engine
    // is resolved from the host item via qmlEngine() so QML doesn't
    // need to hand us a raw engine pointer (which QML can't pass
    // through anyway, QQmlEngine isn't a registered QML type).
    Q_INVOKABLE void wire(QQuickItem* hostItem, QQmlComponent* hostComponent);

    // Demo actions. Each toggles one popout of the named flavour. The
    // QML side maps these to button onClicked handlers.
    Q_INVOKABLE void toggleCooperativeA();
    Q_INVOKABLE void toggleCooperativeB();
    Q_INVOKABLE void toggleModal();
    Q_INVOKABLE void toggleDetached();
    Q_INVOKABLE void closeAll();

    // Drain the controller and disconnect from it so QML engine
    // teardown does not race the controller's signal chain. Wired to
    // QCoreApplication::aboutToQuit in main.cpp. Without this, the
    // engine destroying popout host items while the controller's
    // popoutClosed handler is still connected emits signals into
    // partially-destroyed QML bindings and aborts with "pure virtual
    // method called".
    void shutdown();

    [[nodiscard]] QStringList openPopoutIds() const;
    [[nodiscard]] bool modalActive() const;

Q_SIGNALS:
    void openPopoutIdsChanged();
    void modalActiveChanged();

private:
    QQmlComponent* buildContentComponent(const QString& qmlPath);

    // Shared core of the four toggle slots. Builds a PopoutRequest
    // from the per-toggle inputs and forwards to the controller.
    // Caller-owned QQmlComponent (may be a cached member); the
    // controller does not take ownership.
    void toggleNamed(const QString& popoutId, QQmlComponent* content, PhosphorPopout::ExclusiveMode mode,
                     bool dismissOnFocusLoss, const QVariantMap& props = {});

    // Declaration order is load-bearing for destruction safety.
    // m_controller's destructor calls transport->setSurfaceDismissedCallback({}),
    // so it MUST run before m_transport's destructor. C++ destroys
    // members in reverse-declaration order, so m_transport is declared
    // FIRST and m_controller SECOND — the reverse-order destruction
    // tears the controller down first.
    //
    // QObject-parenting these to `this` would defeat the ordering
    // because QObject::deleteChildren() deletes in INSERT order (which
    // matches construction order, i.e. transport first), producing
    // a "pure virtual method called" abort when the controller's
    // destructor calls into the already-freed transport. unique_ptr
    // owns the lifetime directly so the reverse-member-destruction
    // invariant holds.
    std::unique_ptr<InAppPopoutTransport> m_transport;
    std::unique_ptr<PhosphorPopout::PopoutController> m_controller;
    QPointer<QQmlEngine> m_engine;

    // Cached content components, one per demo popout. Built lazily on
    // first toggle so the QML engine is wired before we touch it.
    QQmlComponent* m_calendarComponent = nullptr;
    QQmlComponent* m_alertComponent = nullptr;
    QQmlComponent* m_noteComponent = nullptr;

    QStringList m_openIds;
};

} // namespace PhosphorPopoutDemo
