// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for the shell's notification centre. The class ships in the
// GPL shell binary (src/shell), so its tests live in the top-level GPL
// tree rather than in the LGPL service library's own suite, which covers
// the server and its LIVE model.
//
// What matters here is the one thing the centre adds and the live model
// deliberately does not: RETENTION. A notification that expires must stay
// in this list, because the whole reason to open a notification centre is
// to read what went away while you were looking elsewhere. Every case
// below is a variation on that, plus the guards around acting on an entry
// the server no longer holds.
//
// The server runs on a private peer-to-peer bus, injected through the
// controller's DI constructor: no session daemon, and no fight over the
// well-known name a real one would own.

#include "shell/NotificationController.h"

#include <PhosphorServiceNotifications/Notification.h>
#include <PhosphorServiceNotifications/NotificationServer.h>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusServer>
#include <QDeadlineTimer>
#include <QSignalSpy>
#include <QTest>
#include <QVariantMap>

#include <memory>

using namespace PhosphorShellApp;
using namespace PhosphorServiceNotifications;

class TestNotificationController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    void anArrivalIsRetainedAndCountsAsUnread();
    void aClosedNotificationStaysInTheListAsNotLive();
    void aReplacementUpdatesInPlaceRatherThanStacking();
    void dismissRemovesTheEntry();
    void clearEmptiesTheListAndTheBadge();
    void markAllReadClearsOnlyTheBadge();
    void actingOnAClosedEntryIsRefused();
    void newestIsFirst();

private:
    /// Send a Notify over the wire and return the id the server assigned.
    uint notify(const QString& summary, const QString& body = {}, uint replacesId = 0, int expireTimeout = -1);

    std::unique_ptr<QDBusServer> m_dbusServer;
    QDBusConnection m_serverSide{QStringLiteral("invalid")};
    QDBusConnection m_clientSide{QStringLiteral("invalid")};
    std::unique_ptr<NotificationServer> m_server;
    std::unique_ptr<NotificationController> m_controller;
};

void TestNotificationController::initTestCase()
{
    m_dbusServer = std::make_unique<QDBusServer>(QStringLiteral("unix:tmpdir=/tmp"));
    m_dbusServer->setAnonymousAuthenticationAllowed(true);
    connect(m_dbusServer.get(), &QDBusServer::newConnection, this, [this](const QDBusConnection& connection) {
        m_serverSide = connection;
    });

    m_clientSide = QDBusConnection::connectToPeer(m_dbusServer->address(), QStringLiteral("pz-notif-centre"));
    QDeadlineTimer deadline(3000);
    while ((!m_clientSide.isConnected() || !m_serverSide.isConnected()) && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    QVERIFY2(m_clientSide.isConnected() && m_serverSide.isConnected(), "peer-to-peer connection did not establish");

    m_server = std::make_unique<NotificationServer>(m_serverSide, NotificationServer::serviceName());
}

void TestNotificationController::init()
{
    // A FRESH controller per case, over the one shared server. The server's
    // ids keep climbing across cases, which is why nothing below asserts on
    // a literal id; the list, however, has to start empty or every case
    // would have to be written count-agnostically for no benefit.
    m_controller = std::make_unique<NotificationController>(m_server.get());
}

uint TestNotificationController::notify(const QString& summary, const QString& body, uint replacesId, int expireTimeout)
{
    // Empty service name: on a peer connection there are no bus names, so
    // the call routes to the peer and dispatches by object path.
    QDBusMessage message = QDBusMessage::createMethodCall(QString(), NotificationServer::objectPath(),
                                                          NotificationServer::serviceName(), QStringLiteral("Notify"));
    message << QStringLiteral("test-app") << replacesId << QStringLiteral("dialog-information") << summary << body
            << QStringList{} << QVariantMap{} << expireTimeout;

    // Async plus a pump: a blocking call would deadlock, since the server
    // dispatches on this same thread.
    QDBusPendingCall pending = m_clientSide.asyncCall(message);
    QDeadlineTimer deadline(5000);
    while (!pending.isFinished() && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    const QDBusMessage reply = pending.reply();
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return 0;
    }
    return reply.arguments().constFirst().toUInt();
}

void TestNotificationController::anArrivalIsRetainedAndCountsAsUnread()
{
    QSignalSpy unreadSpy(m_controller.get(), &NotificationController::unreadCountChanged);

    const uint id = notify(QStringLiteral("Build finished"), QStringLiteral("42 targets"));
    QVERIFY(id != 0);

    QCOMPARE(m_controller->rowCount(), 1);
    QCOMPARE(m_controller->unreadCount(), 1);
    QCOMPARE(unreadSpy.count(), 1);

    const QModelIndex row = m_controller->index(0, 0);
    QCOMPARE(row.data(NotificationController::SummaryRole).toString(), QStringLiteral("Build finished"));
    QCOMPARE(row.data(NotificationController::BodyRole).toString(), QStringLiteral("42 targets"));
    QCOMPARE(row.data(NotificationController::AppNameRole).toString(), QStringLiteral("test-app"));
    QVERIFY(row.data(NotificationController::LiveRole).toBool());
}

void TestNotificationController::aClosedNotificationStaysInTheListAsNotLive()
{
    // THE case this class exists for. The live model drops a closed row;
    // this one keeps it and only marks it no longer live, because a
    // notification that expired is exactly what someone opens the centre
    // to read.
    const uint id = notify(QStringLiteral("Transient"));
    QCOMPARE(m_controller->rowCount(), 1);

    m_server->dismissNotification(id);

    QCOMPARE(m_controller->rowCount(), 1);
    QVERIFY(!m_controller->index(0, 0).data(NotificationController::LiveRole).toBool());
    // The text survives the Notification object's destruction, which is why
    // the entry is a snapshot rather than a pointer.
    QCOMPARE(m_controller->index(0, 0).data(NotificationController::SummaryRole).toString(),
             QStringLiteral("Transient"));
}

void TestNotificationController::aReplacementUpdatesInPlaceRatherThanStacking()
{
    // A progress notification is one id updated many times. Appending each
    // update would bury everything else in the centre under one download.
    const uint id = notify(QStringLiteral("Copying 10%"));
    QCOMPARE(m_controller->rowCount(), 1);

    const uint again = notify(QStringLiteral("Copying 90%"), {}, id);
    QCOMPARE(again, id);

    QCOMPARE(m_controller->rowCount(), 1);
    QCOMPARE(m_controller->index(0, 0).data(NotificationController::SummaryRole).toString(),
             QStringLiteral("Copying 90%"));
}

void TestNotificationController::dismissRemovesTheEntry()
{
    const uint id = notify(QStringLiteral("Gone soon"));
    QCOMPARE(m_controller->rowCount(), 1);

    m_controller->dismiss(id);
    QCOMPARE(m_controller->rowCount(), 0);

    // Dismissing an id the list never held is a no-op, not a crash: the
    // panel can race a clear against a click on a row.
    m_controller->dismiss(id);
    QCOMPARE(m_controller->rowCount(), 0);
}

void TestNotificationController::clearEmptiesTheListAndTheBadge()
{
    notify(QStringLiteral("One"));
    notify(QStringLiteral("Two"));
    QCOMPARE(m_controller->rowCount(), 2);
    QCOMPARE(m_controller->unreadCount(), 2);

    m_controller->clear();

    QCOMPARE(m_controller->rowCount(), 0);
    QCOMPARE(m_controller->unreadCount(), 0);
}

void TestNotificationController::markAllReadClearsOnlyTheBadge()
{
    // The badge answers "is there anything new" and the list answers "what
    // arrived". Opening the panel answers the first question, never the
    // second, so the rows must survive.
    notify(QStringLiteral("Still here"));
    QCOMPARE(m_controller->unreadCount(), 1);

    m_controller->markAllRead();

    QCOMPARE(m_controller->unreadCount(), 0);
    QCOMPARE(m_controller->rowCount(), 1);
}

void TestNotificationController::actingOnAClosedEntryIsRefused()
{
    const uint id = notify(QStringLiteral("Expired"));
    m_server->dismissNotification(id);
    QVERIFY(!m_controller->index(0, 0).data(NotificationController::LiveRole).toBool());

    // The sending application has already been told this closed, so the
    // action goes nowhere. The guard is what keeps that a logged no-op
    // rather than a call into a notification the server has freed.
    m_controller->invokeAction(id, QStringLiteral("default"));
    QCOMPARE(m_controller->rowCount(), 1);
}

void TestNotificationController::newestIsFirst()
{
    // Newest first in the model itself, so the panel reads top-down with no
    // proxy and no sort in QML.
    notify(QStringLiteral("Older"));
    notify(QStringLiteral("Newer"));

    QCOMPARE(m_controller->index(0, 0).data(NotificationController::SummaryRole).toString(), QStringLiteral("Newer"));
    QCOMPARE(m_controller->index(1, 0).data(NotificationController::SummaryRole).toString(), QStringLiteral("Older"));
}

QTEST_MAIN(TestNotificationController)

#include "test_notification_controller.moc"
