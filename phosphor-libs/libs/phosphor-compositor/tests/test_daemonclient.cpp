// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// DaemonClient's registration gate against a fake daemon on the private test
// bus. The daemon admits compositor reports only from the bus name that
// registered the bridge, and a fire-and-forget report never sees its refusal,
// so the client holds every report back until bridgeRegistered and says so
// once per registration. The fake owns the daemon's well-known name on a
// second connection, answers registerBridge, and records the windowClosed
// reports that reach it.

#include <PhosphorCompositor/DaemonClient.h>
#include <PhosphorProtocol/BridgeMarshalling.h>
#include <PhosphorProtocol/BridgeTypes.h>
#include <PhosphorProtocol/Registration.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>

#include <memory>

using PhosphorCompositor::DaemonClient;

namespace {

const QRegularExpression kRefusedReport(QStringLiteral("windowClosed called before registerBridge succeeded"));

} // namespace

/// The daemon's org.plasmazones.CompositorBridge, answering a session or the
/// REJECTED sentinel.
class FakeBridgeAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.CompositorBridge")

public:
    explicit FakeBridgeAdaptor(QObject* parent)
        : QDBusAbstractAdaptor(parent)
    {
    }

    bool reject = false;

public Q_SLOTS:
    PhosphorProtocol::BridgeRegistrationResult registerBridge(const QString& compositorName, const QString& version,
                                                              const QStringList& capabilities)
    {
        Q_UNUSED(version)
        Q_UNUSED(capabilities)
        PhosphorProtocol::BridgeRegistrationResult result;
        result.apiVersion = QString::number(PhosphorProtocol::Service::ApiVersion);
        result.bridgeName = compositorName;
        result.sessionId = reject ? QStringLiteral("REJECTED") : QStringLiteral("fake-session");
        return result;
    }
};

/// The daemon's org.plasmazones.Tiling, recording the window-closed reports
/// that reach it.
class FakeTilingAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Tiling")

public:
    explicit FakeTilingAdaptor(QObject* parent)
        : QDBusAbstractAdaptor(parent)
    {
    }

    QStringList closed;

public Q_SLOTS:
    void windowClosed(const QString& windowId)
    {
        closed.append(windowId);
    }
};

/// One run of the fake daemon: its own bus connection, so it has its own
/// unique name the way a restarted process would, owning the daemon's name
/// from construction until stop().
class FakeDaemon
{
public:
    explicit FakeDaemon(const QString& connectionName)
        : m_connectionName(connectionName)
        , m_bridge(new FakeBridgeAdaptor(&m_root))
        , m_tiling(new FakeTilingAdaptor(&m_root))
    {
        QDBusConnection bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_connectionName);
        m_registered = bus.isConnected()
            && bus.registerObject(PhosphorProtocol::Service::ObjectPath, &m_root, QDBusConnection::ExportAdaptors)
            && bus.registerService(PhosphorProtocol::Service::Name);
    }

    ~FakeDaemon()
    {
        stop();
    }

    bool isRegistered() const
    {
        return m_registered;
    }

    void setReject(bool reject)
    {
        m_bridge->reject = reject;
    }

    QStringList closed() const
    {
        return m_tiling->closed;
    }

    void stop()
    {
        if (m_connectionName.isEmpty()) {
            return;
        }
        {
            QDBusConnection bus(m_connectionName);
            bus.unregisterService(PhosphorProtocol::Service::Name);
            bus.unregisterObject(PhosphorProtocol::Service::ObjectPath);
        }
        QDBusConnection::disconnectFromBus(m_connectionName);
        m_connectionName.clear();
    }

private:
    QString m_connectionName;
    QObject m_root;
    FakeBridgeAdaptor* m_bridge; // owned by m_root
    FakeTilingAdaptor* m_tiling; // owned by m_root
    bool m_registered = false;
};

class TestDaemonClient : public QObject
{
    Q_OBJECT

private:
    static bool busUsable()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        return bus.isConnected() && bus.interface()
            && !bus.interface()->isServiceRegistered(PhosphorProtocol::Service::Name);
    }

private Q_SLOTS:
    void initTestCase()
    {
        // The fake's registerBridge returns the struct, which the bus can
        // marshal only once the wire types are registered in this process.
        PhosphorProtocol::registerWireTypes();
    }

    // Reports sent before registration never reach the daemon, and only the
    // first of them is logged. Once the bridge is registered they go through.
    void reportsWaitForRegistration()
    {
        if (!busUsable()) {
            QSKIP("no private session bus, or a real daemon owns the name");
        }
        FakeDaemon daemon(QStringLiteral("fake-daemon-wait"));
        QVERIFY(daemon.isRegistered());

        DaemonClient client;
        QTest::ignoreMessage(QtWarningMsg, kRefusedReport);
        QTest::failOnWarning(kRefusedReport);
        client.notifyWindowClosed(QStringLiteral("early-1"));
        client.notifyWindowClosed(QStringLiteral("early-2"));

        QSignalSpy registered(&client, &DaemonClient::bridgeRegistered);
        client.registerBridge(QStringLiteral("test"), PhosphorProtocol::Service::ApiVersion, {});
        QTRY_COMPARE(registered.count(), 1);

        // One connection delivers in order, so an early report that had been
        // sent would arrive ahead of this one.
        client.notifyWindowClosed(QStringLiteral("late"));
        QTRY_COMPARE(daemon.closed(), QStringList{QStringLiteral("late")});
    }

    // A rejected registration establishes no session, so reports stay held
    // back until a later registration succeeds.
    void rejectedRegistrationKeepsReportsBack()
    {
        if (!busUsable()) {
            QSKIP("no private session bus, or a real daemon owns the name");
        }
        FakeDaemon daemon(QStringLiteral("fake-daemon-reject"));
        QVERIFY(daemon.isRegistered());
        daemon.setReject(true);

        DaemonClient client;
        QSignalSpy rejected(&client, &DaemonClient::bridgeRejected);
        client.registerBridge(QStringLiteral("test"), PhosphorProtocol::Service::ApiVersion, {});
        QTRY_COMPARE(rejected.count(), 1);

        QTest::ignoreMessage(QtWarningMsg, kRefusedReport);
        client.notifyWindowClosed(QStringLiteral("after-reject"));

        daemon.setReject(false);
        QSignalSpy registered(&client, &DaemonClient::bridgeRegistered);
        client.registerBridge(QStringLiteral("test"), PhosphorProtocol::Service::ApiVersion, {});
        QTRY_COMPARE(registered.count(), 1);
        client.notifyWindowClosed(QStringLiteral("registered"));
        QTRY_COMPARE(daemon.closed(), QStringList{QStringLiteral("registered")});
    }

    // The daemon going away ends the session. Reports are held back again
    // until the restarted daemon accepts a new registration, and the first of
    // them is logged again, since the warning is once per registration.
    void daemonLossClosesTheGateAgain()
    {
        if (!busUsable()) {
            QSKIP("no private session bus, or a real daemon owns the name");
        }
        auto first = std::make_unique<FakeDaemon>(QStringLiteral("fake-daemon-first"));
        QVERIFY(first->isRegistered());

        DaemonClient client;
        QSignalSpy registered(&client, &DaemonClient::bridgeRegistered);
        QSignalSpy disconnected(&client, &DaemonClient::daemonDisconnected);
        client.registerBridge(QStringLiteral("test"), PhosphorProtocol::Service::ApiVersion, {});
        QTRY_COMPARE(registered.count(), 1);

        first.reset();
        QTRY_COMPARE(disconnected.count(), 1);

        FakeDaemon second(QStringLiteral("fake-daemon-second"));
        QVERIFY(second.isRegistered());
        QTest::ignoreMessage(QtWarningMsg, kRefusedReport);
        QTest::failOnWarning(kRefusedReport);
        client.notifyWindowClosed(QStringLiteral("between-1"));
        client.notifyWindowClosed(QStringLiteral("between-2"));

        client.registerBridge(QStringLiteral("test"), PhosphorProtocol::Service::ApiVersion, {});
        QTRY_COMPARE(registered.count(), 2);
        client.notifyWindowClosed(QStringLiteral("after"));
        QTRY_COMPARE(second.closed(), QStringList{QStringLiteral("after")});
    }
};

QTEST_GUILESS_MAIN(TestDaemonClient)
#include "test_daemonclient.moc"
