// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Milestone-1 smoke test for phosphor-service-notifications. Pins the plumbing
// contract: QML-registration idempotency, the static spec identifiers,
// GetServerInformation / GetCapabilities, monotonic non-zero id allocation with
// replaces_id reuse, and the CloseNotification close-reason signal.
//
// Every server instance is built on a PRIVATE peer D-Bus connection rather than
// the session bus, so the test never registers (or hijacks) the real
// org.freedesktop.Notifications name. The peer transport gives registerObject a
// real connection; well-known-name acquisition is a bus-daemon concept that
// does not apply peer-to-peer, so nameAcquired() is not asserted here (it is
// exercised manually via the CLI demo in milestone 7).

#include <PhosphorServiceNotifications/NotificationServer.h>
#include <PhosphorServiceNotifications/QmlRegistration.h>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusServer>
#include <QDeadlineTimer>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>

#include <memory>

using namespace PhosphorServiceNotifications;

class NotificationsSmokeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void registerQmlTypesIsIdempotent();
    void staticIdentifiers();
    void serverInformation();
    void capabilitiesAdvertiseBody();
    void notifyAllocatesMonotonicNonZeroIds();
    void notifyHonoursReplacesId();
    void closeNotificationEmitsForLiveIdOnly();

private:
    std::unique_ptr<NotificationServer> makeServer();

    std::unique_ptr<QDBusServer> m_peer;
    int m_connCounter = 0;
};

void NotificationsSmokeTest::initTestCase()
{
    // A private peer bus: server instances bind their object to a connection
    // that has nothing to do with the session bus, so no test run can grab the
    // real notifications name. Anonymous auth keeps the handshake dependency
    // free.
    m_peer = std::make_unique<QDBusServer>(QStringLiteral("unix:tmpdir=/tmp"));
    m_peer->setAnonymousAuthenticationAllowed(true);
}

std::unique_ptr<NotificationServer> NotificationsSmokeTest::makeServer()
{
    // A fresh, uniquely-named peer connection per server so two servers never
    // collide on the spec object path within one connection.
    const QString name = QStringLiteral("pz-notif-test-%1").arg(m_connCounter++);
    QDBusConnection client = QDBusConnection::connectToPeer(m_peer->address(), name);

    // Pump the handshake. The logic under test does not actually depend on the
    // transport completing (the slots are called directly), so a timeout is not
    // fatal; it just gives registerObject a connected transport when available.
    QDeadlineTimer deadline(2000);
    while (!client.isConnected() && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);

    return std::make_unique<NotificationServer>(client, NotificationServer::serviceName());
}

void NotificationsSmokeTest::registerQmlTypesIsIdempotent()
{
    // The std::call_once guard must make a second call a no-op (no crash, no
    // duplicate-registration fault). A hot-reloading shell relies on this.
    registerQmlTypes();
    registerQmlTypes();
}

void NotificationsSmokeTest::staticIdentifiers()
{
    QCOMPARE(NotificationServer::serviceName(), QStringLiteral("org.freedesktop.Notifications"));
    QCOMPARE(NotificationServer::objectPath(), QStringLiteral("/org/freedesktop/Notifications"));
}

void NotificationsSmokeTest::serverInformation()
{
    auto server = makeServer();
    QString vendor;
    QString version;
    QString specVersion;
    const QString name = server->GetServerInformation(vendor, version, specVersion);
    QCOMPARE(name, QStringLiteral("Phosphor"));
    QCOMPARE(vendor, QStringLiteral("phosphor-works"));
    QCOMPARE(specVersion, QStringLiteral("1.2"));
    QVERIFY(!version.isEmpty());
}

void NotificationsSmokeTest::capabilitiesAdvertiseBody()
{
    auto server = makeServer();
    const QStringList caps = server->GetCapabilities();
    QVERIFY(!caps.isEmpty());
    // "body" is the only capability milestone 1 can honestly back; the set grows
    // as actions / markup / persistence land in later milestones.
    QVERIFY(caps.contains(QStringLiteral("body")));
}

void NotificationsSmokeTest::notifyAllocatesMonotonicNonZeroIds()
{
    auto server = makeServer();
    const uint first = server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("hi"), QString(), {}, {}, -1);
    const uint second =
        server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("again"), QString(), {}, {}, -1);
    QVERIFY(first != 0);
    QVERIFY(second != 0);
    QVERIFY(second > first);
}

void NotificationsSmokeTest::notifyHonoursReplacesId()
{
    auto server = makeServer();
    const uint id = server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("first"), QString(), {}, {}, -1);

    // replaces_id pointing at a live notification reuses that id in place.
    const uint replaced =
        server->Notify(QStringLiteral("app"), id, QString(), QStringLiteral("updated"), QString(), {}, {}, -1);
    QCOMPARE(replaced, id);

    // replaces_id pointing at an unknown id allocates a fresh one rather than
    // honouring the stale value.
    const uint stale = 999999;
    const uint fresh =
        server->Notify(QStringLiteral("app"), stale, QString(), QStringLiteral("new"), QString(), {}, {}, -1);
    QVERIFY(fresh != stale);
    QVERIFY(fresh != 0);
}

void NotificationsSmokeTest::closeNotificationEmitsForLiveIdOnly()
{
    auto server = makeServer();
    QSignalSpy spy(server.get(), &NotificationServer::NotificationClosed);
    QVERIFY(spy.isValid());

    const uint id =
        server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("closeme"), QString(), {}, {}, -1);

    // Closing a live id emits exactly once with reason 3 (closed by call).
    server->CloseNotification(id);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toUInt(), id);
    QCOMPARE(spy.at(0).at(1).toUInt(), 3u);

    // Closing the same (now dead) id again is a no-op: no further signal.
    server->CloseNotification(id);
    QCOMPARE(spy.count(), 1);

    // Closing an id that was never issued is a no-op.
    server->CloseNotification(424242);
    QCOMPARE(spy.count(), 1);
}

QTEST_GUILESS_MAIN(NotificationsSmokeTest)
#include "test_smoke.moc"
