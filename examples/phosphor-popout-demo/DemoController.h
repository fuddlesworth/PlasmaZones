// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

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
    Q_PROPERTY(QStringList openPopoutIds READ openPopoutIds NOTIFY openPopoutsChanged)
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

    [[nodiscard]] QStringList openPopoutIds() const;
    [[nodiscard]] bool modalActive() const;

Q_SIGNALS:
    void openPopoutsChanged();
    void modalActiveChanged();

private:
    QQmlComponent* buildContentComponent(const QString& qmlPath);

    PhosphorPopout::PopoutController* m_controller = nullptr;
    InAppPopoutTransport* m_transport = nullptr;
    QPointer<QQmlEngine> m_engine;

    // Cached content components, one per demo popout. Built lazily on
    // first toggle so the QML engine is wired before we touch it.
    QQmlComponent* m_calendarComponent = nullptr;
    QQmlComponent* m_alertComponent = nullptr;
    QQmlComponent* m_noteComponent = nullptr;

    QStringList m_openIds;
};

} // namespace PhosphorPopoutDemo
