// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// ActivityManager against a fake org.kde.ActivityManager on the private test
// bus. The activities library learns of the service from a
// QDBusServiceWatcher on that bus name, so registering the name on a second
// connection looks to it exactly like kactivitymanagerd starting, and
// dropping that connection like the daemon going away. The fake answers the
// calls the library makes when the service appears: serviceVersion on
// /ActivityManager, then CurrentActivity and ListActivitiesWithInformation on
// /ActivityManager/Activities, the last of which is what moves the library's
// status to Running.

#include <PhosphorWorkspaces/ActivityManager.h>

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMetaType>
#include <QList>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <memory>

using PhosphorWorkspaces::ActivityManager;

namespace FakeKamd {

/// One activity as ListActivitiesWithInformation marshals it, (ssssi): the
/// signature the activities library reads and the real daemon sends.
struct ActivityInfo
{
    QString id;
    QString name;
    QString description;
    QString icon;
    int state = 2; // the daemon's Running
};

QDBusArgument& operator<<(QDBusArgument& arg, const ActivityInfo& info)
{
    arg.beginStructure();
    arg << info.id << info.name << info.description << info.icon << info.state;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, ActivityInfo& info)
{
    arg.beginStructure();
    arg >> info.id >> info.name >> info.description >> info.icon >> info.state;
    arg.endStructure();
    return arg;
}

} // namespace FakeKamd

Q_DECLARE_METATYPE(FakeKamd::ActivityInfo)

namespace {

const QString kService = QStringLiteral("org.kde.ActivityManager");
const QString kApplicationPath = QStringLiteral("/ActivityManager");
const QString kActivitiesPath = QStringLiteral("/ActivityManager/Activities");
const QString kFirst = QStringLiteral("11111111-1111-1111-1111-111111111111");
const QString kSecond = QStringLiteral("22222222-2222-2222-2222-222222222222");

} // namespace

/// The daemon's application object. The library refuses (qFatal) a service
/// older than the version it requires, so the fake reports one far above it.
class FakeApplication : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.ActivityManager.Application")

public Q_SLOTS:
    QString serviceVersion()
    {
        return QStringLiteral("99.0.0");
    }
};

class FakeActivities : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.ActivityManager.Activities")

public:
    QString current;

    void switchTo(const QString& activityId)
    {
        current = activityId;
        Q_EMIT CurrentActivityChanged(activityId);
    }

public Q_SLOTS:
    QString CurrentActivity()
    {
        return current;
    }

    QList<FakeKamd::ActivityInfo> ListActivitiesWithInformation()
    {
        return {FakeKamd::ActivityInfo{kFirst, QStringLiteral("First"), QString(), QString()},
                FakeKamd::ActivityInfo{kSecond, QStringLiteral("Second"), QString(), QString()}};
    }

Q_SIGNALS:
    // Named as the daemon's D-Bus signal, which is what the library matches.
    void CurrentActivityChanged(const QString& activityId);
};

/// One run of the fake daemon: its own bus connection, so it has its own
/// unique name the way a restarted process would, owning the service name
/// from construction until stop().
class FakeActivityService
{
public:
    FakeActivityService(const QString& connectionName, const QString& currentActivity)
        : m_connectionName(connectionName)
    {
        m_activities.current = currentActivity;
        QDBusConnection bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_connectionName);
        m_registered = bus.isConnected()
            && bus.registerObject(kApplicationPath, &m_application, QDBusConnection::ExportAllSlots)
            && bus.registerObject(kActivitiesPath, &m_activities,
                                  QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)
            && bus.registerService(kService);
    }

    ~FakeActivityService()
    {
        stop();
    }

    bool isRegistered() const
    {
        return m_registered;
    }

    void switchTo(const QString& activityId)
    {
        m_activities.switchTo(activityId);
    }

    void stop()
    {
        if (m_connectionName.isEmpty()) {
            return;
        }
        {
            QDBusConnection bus(m_connectionName);
            bus.unregisterService(kService);
            bus.unregisterObject(kActivitiesPath);
            bus.unregisterObject(kApplicationPath);
        }
        QDBusConnection::disconnectFromBus(m_connectionName);
        m_connectionName.clear();
    }

private:
    QString m_connectionName;
    FakeApplication m_application;
    FakeActivities m_activities;
    bool m_registered = false;
};

class TestActivityManager : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        qDBusRegisterMetaType<FakeKamd::ActivityInfo>();
        qDBusRegisterMetaType<QList<FakeKamd::ActivityInfo>>();
        // The library otherwise asks the bus to start kactivitymanagerd when
        // the name is free. The private test bus can activate nothing, but
        // an ambient one could launch the real daemon over the fake.
        QCoreApplication::instance()->setProperty("org.kde.KActivities.core.disableAutostart", true);
    }

    // The service starts after the manager has, so start() returned without
    // connecting the activity signals. Then it stops and starts again under a
    // new owner. The manager connects late, keeps the last activity through
    // the outage without announcing a switch, and follows the restarted
    // service.
    void lateServiceIsConnectedAndARestartIsFollowed()
    {
        if (!ActivityManager::isAvailable()) {
            QSKIP("built without the activities library");
        }
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected() || !bus.interface()) {
            QSKIP("no session bus");
        }
        if (bus.interface()->isServiceRegistered(kService)) {
            QSKIP("a real activity manager owns the name; this needs the private test bus");
        }

        ActivityManager manager;
        QVERIFY(manager.init());
        manager.start();
        QVERIFY(manager.currentActivity().isEmpty());
        QVERIFY(manager.activities().isEmpty());
        QSignalSpy changed(&manager, &ActivityManager::currentActivityChanged);

        auto first = std::make_unique<FakeActivityService>(QStringLiteral("fake-kamd-1"), kFirst);
        QVERIFY(first->isRegistered());
        QTRY_COMPARE(manager.currentActivity(), kFirst);
        QCOMPARE(changed.count(), 1);
        QVERIFY(manager.activities().contains(kSecond));

        // A switch after the late start arrives through the connected signal.
        first->switchTo(kSecond);
        QTRY_COMPARE(manager.currentActivity(), kSecond);
        QCOMPARE(changed.count(), 2);

        // The service goes away: unavailable, but the activity is kept.
        first.reset();
        QTRY_VERIFY(manager.activities().isEmpty());
        QCOMPARE(manager.currentActivity(), kSecond);
        QCOMPARE(changed.count(), 2);

        // It comes back on the same activity, which is not a switch.
        auto second = std::make_unique<FakeActivityService>(QStringLiteral("fake-kamd-2"), kSecond);
        QVERIFY(second->isRegistered());
        QTRY_VERIFY(manager.activities().contains(kFirst));
        QCOMPARE(manager.currentActivity(), kSecond);
        QCOMPARE(changed.count(), 2);

        // And the restarted service is still followed.
        second->switchTo(kFirst);
        QTRY_COMPARE(manager.currentActivity(), kFirst);
        QCOMPARE(changed.count(), 3);
        QCOMPARE(changed.last().at(0).toString(), kFirst);
    }

    // A stopped manager tracks the service's availability and reports
    // nothing, not the outage and not the switch the restarted service
    // comes back on. start() then catches up on that switch, once.
    void stoppedManagerIsSilentThroughABounce()
    {
        if (!ActivityManager::isAvailable()) {
            QSKIP("built without the activities library");
        }
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected() || !bus.interface()) {
            QSKIP("no session bus");
        }
        if (bus.interface()->isServiceRegistered(kService)) {
            QSKIP("a real activity manager owns the name; this needs the private test bus");
        }

        ActivityManager manager;
        QVERIFY(manager.init());
        manager.start();
        auto first = std::make_unique<FakeActivityService>(QStringLiteral("fake-kamd-stopped-1"), kFirst);
        QVERIFY(first->isRegistered());
        QTRY_COMPARE(manager.currentActivity(), kFirst);

        manager.stop();
        QSignalSpy changed(&manager, &ActivityManager::currentActivityChanged);
        QSignalSpy listChanged(&manager, &ActivityManager::activitiesChanged);

        first.reset();
        QTRY_VERIFY(manager.activities().isEmpty());
        auto second = std::make_unique<FakeActivityService>(QStringLiteral("fake-kamd-stopped-2"), kSecond);
        QVERIFY(second->isRegistered());
        QTRY_VERIFY(manager.activities().contains(kFirst));
        QCOMPARE(changed.count(), 0);
        QCOMPARE(listChanged.count(), 0);
        QCOMPARE(manager.currentActivity(), kFirst);

        manager.start();
        QCOMPARE(manager.currentActivity(), kSecond);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.last().at(0).toString(), kSecond);
    }

    // A second init() keeps the one controller and its one status handler,
    // so the service coming up announces its activity once, not once per
    // init().
    void secondInitIsANoOp()
    {
        if (!ActivityManager::isAvailable()) {
            QSKIP("built without the activities library");
        }
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected() || !bus.interface()) {
            QSKIP("no session bus");
        }
        if (bus.interface()->isServiceRegistered(kService)) {
            QSKIP("a real activity manager owns the name; this needs the private test bus");
        }

        ActivityManager manager;
        QVERIFY(manager.init());
        QVERIFY(manager.init());
        manager.start();
        QSignalSpy changed(&manager, &ActivityManager::currentActivityChanged);

        auto service = std::make_unique<FakeActivityService>(QStringLiteral("fake-kamd-reinit"), kFirst);
        QVERIFY(service->isRegistered());
        QTRY_COMPARE(manager.currentActivity(), kFirst);
        // Both handlers would run off the same status change, so give a
        // stacked second one the time to announce again before counting.
        QTest::qWait(200);
        QCOMPARE(changed.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TestActivityManager)
#include "test_activitymanager.moc"
