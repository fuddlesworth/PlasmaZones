// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// QML facade test for phosphor-service-notifications (milestone 6). Proves the
// imperative registration actually produces a usable QML module: a real
// QQmlEngine imports Phosphor.Service.Notifications 1.0, instantiates a
// NotificationModel bound to a server, reads the Notification.Urgency enum, and
// observes model.count update reactively as the server ingests a notification.
//
// The NotificationServer is created in C++ on a PRIVATE peer bus and handed to
// QML as a context property, so the test never instantiates a bus-grabbing
// server from QML and never touches the real org.freedesktop.Notifications name.
// (The shell creates `NotificationServer {}` directly in QML; that it is
// qmlRegisterType-instantiable is exercised by NotificationModel, registered the
// same way and instantiated here.)

#include <PhosphorServiceNotifications/NotificationServer.h>
#include <PhosphorServiceNotifications/QmlRegistration.h>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusServer>
#include <QDeadlineTimer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTest>

#include <memory>

using namespace PhosphorServiceNotifications;

namespace {
constexpr auto kQml = R"(
import QtQml
import Phosphor.Service.Notifications 1.0

QtObject {
    id: root
    property NotificationModel model: NotificationModel { server: ctxServer }
    readonly property int criticalUrgency: Notification.Critical
    readonly property int normalUrgency: Notification.Normal
    property int liveCount: root.model ? root.model.count : -1
}
)";
} // namespace

class QmlFacadeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void moduleLoadsAndBindsAndEnumResolves();

private:
    std::unique_ptr<NotificationServer> makePeerServer();

    std::unique_ptr<QDBusServer> m_peer;
};

void QmlFacadeTest::initTestCase()
{
    registerQmlTypes(); // make the module importable in this process
    m_peer = std::make_unique<QDBusServer>(QStringLiteral("unix:tmpdir=/tmp"));
    m_peer->setAnonymousAuthenticationAllowed(true);
}

std::unique_ptr<NotificationServer> QmlFacadeTest::makePeerServer()
{
    QDBusConnection client = QDBusConnection::connectToPeer(m_peer->address(), QStringLiteral("pz-notif-qml"));
    QDeadlineTimer deadline(2000);
    while (!client.isConnected() && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    // Handshake timeout is deliberately NOT fatal (unlike test_wire's
    // QVERIFY2 guard): these tests drive the server's slots directly and
    // never round-trip the transport, so an unconnected peer bus still
    // exercises everything they assert (same rationale as the smoke
    // test's makeServer).
    return std::make_unique<NotificationServer>(client, NotificationServer::serviceName());
}

void QmlFacadeTest::moduleLoadsAndBindsAndEnumResolves()
{
    auto server = makePeerServer();

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("ctxServer"), server.get());

    QQmlComponent component(&engine);
    component.setData(kQml, QUrl(QStringLiteral("qrc:/test_facade.qml")));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root != nullptr, qPrintable(component.errorString())); // module + types resolved

    // The Notification.Urgency enum is reachable through the import.
    QCOMPARE(root->property("criticalUrgency").toInt(), 2);
    QCOMPARE(root->property("normalUrgency").toInt(), 1);

    // The model bound to the server through QML; count starts empty.
    QCOMPARE(root->property("liveCount").toInt(), 0);

    // A Notify drives the C++ server, and the QML `liveCount: model.count`
    // binding re-evaluates off countChanged.
    server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("hi"), QString(), {}, {}, 0);
    QCOMPARE(root->property("liveCount").toInt(), 1);
}

QTEST_GUILESS_MAIN(QmlFacadeTest)
#include "test_qmlfacade.moc"
