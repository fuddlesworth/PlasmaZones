// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceSni/StatusNotifierHost.h>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QProcess>
#include <QTextStream>
#include <QTimer>
#include <QtTest/QtTest>

#include <memory>

using namespace PhosphorServiceSni;

namespace {

const QString watcherService = QStringLiteral("org.kde.StatusNotifierWatcher");
const QString watcherPath = QStringLiteral("/StatusNotifierWatcher");
const QString propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");

class PeerObject : public QDBusVirtualObject
{
public:
    QString mode;
    int count = 1;
    bool seedRequested = false;

    QString introspect(const QString&) const override
    {
        return {};
    }

    QStringList items(const QDBusConnection& bus) const
    {
        QStringList result;
        for (int i = 0; i < count; ++i)
            result.append(bus.baseService() + QStringLiteral("/Item%1").arg(i));
        return result;
    }

    bool handleMessage(const QDBusMessage& message, const QDBusConnection& bus) override
    {
        if (message.interface() != propertiesInterface || message.member() != QLatin1String("Get")) {
            bus.send(message.createReply());
            return true;
        }
        const QString property = message.arguments().value(1).toString();
        if (message.path() == watcherPath && property == QLatin1String("RegisteredStatusNotifierItems")) {
            if (mode == QLatin1String("stale-seed") && !seedRequested) {
                seedRequested = true;
                message.setDelayedReply(true);
                for (const auto& item : items(bus)) {
                    auto signal = QDBusMessage::createSignal(watcherPath, watcherService,
                                                             QStringLiteral("StatusNotifierItemRegistered"));
                    signal << item;
                    bus.send(signal);
                }
                QTimer::singleShot(100, this, [message, bus] {
                    bus.send(message.createReply({QVariant::fromValue(QDBusVariant(QStringList()))}));
                    bus.send(QDBusMessage::createSignal(watcherPath, watcherService, QStringLiteral("SeedReplied")));
                });
            } else {
                bus.send(message.createReply({QVariant::fromValue(QDBusVariant(items(bus)))}));
            }
            return true;
        }
        if (mode == QLatin1String("vanish")) {
            message.setDelayedReply(true);
            QCoreApplication::quit();
            return true;
        }
        QVariant value;
        if (property == QLatin1String("Id") || property == QLatin1String("Title"))
            value = message.path();
        else if (property == QLatin1String("Status"))
            value = QStringLiteral("Active");
        else if (property == QLatin1String("Category"))
            value = QStringLiteral("ApplicationStatus");
        else if (property == QLatin1String("ItemIsMenu"))
            value = false;
        else if (property == QLatin1String("Menu"))
            value = QVariant::fromValue(QDBusObjectPath(QStringLiteral("/Menu")));
        else if (property.endsWith(QLatin1String("Name")) || property == QLatin1String("IconThemePath"))
            value = QString();
        else {
            bus.send(message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.UnknownProperty"),
                                              QStringLiteral("No image payload")));
            return true;
        }
        bus.send(message.createReply({QVariant::fromValue(QDBusVariant(value))}));
        return true;
    }
};

int runPeer(const QString& mode, int count)
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return 2;
    PeerObject object;
    object.mode = mode;
    object.count = count;
    if (!bus.registerVirtualObject(QStringLiteral("/"), &object, QDBusConnection::SubPath))
        return 3;
    const auto registerItems = [bus, count] {
        for (int i = 0; i < count; ++i) {
            auto call = QDBusMessage::createMethodCall(watcherService, watcherPath, watcherService,
                                                       QStringLiteral("RegisterStatusNotifierItem"));
            call << QStringLiteral("/Item%1").arg(i);
            bus.send(call);
        }
    };
    QDBusServiceWatcher watcher(watcherService, bus, QDBusServiceWatcher::WatchForRegistration);
    QObject::connect(&watcher, &QDBusServiceWatcher::serviceRegistered, &object, registerItems);
    if (mode == QLatin1String("stale-seed")) {
        if (!bus.registerService(watcherService))
            return 4;
    } else {
        registerItems();
    }
    // A bus-daemon round trip flushes all registrations before burst mode
    // exits. The parent deliberately does not dispatch them until afterward.
    auto barrier =
        QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                       QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetId"));
    bus.call(barrier);
    QTextStream(stdout) << bus.baseService() << Qt::endl;
    if (mode == QLatin1String("burst"))
        return 0;
    QTimer::singleShot(20000, QCoreApplication::instance(), &QCoreApplication::quit);
    return QCoreApplication::exec();
}

class PeerProcess : public QProcess
{
public:
    ~PeerProcess() override
    {
        if (state() != QProcess::NotRunning) {
            kill();
            waitForFinished(1000);
        }
    }

    bool startPeer(const QString& mode, int count)
    {
        start(QCoreApplication::applicationFilePath(), {QStringLiteral("--sni-peer"), mode, QString::number(count)});
        if (!waitForStarted(2000))
            return false;
        if (!canReadLine() && !waitForReadyRead(2000))
            return false;
        uniqueName = QString::fromUtf8(readLine()).trimmed();
        return uniqueName.startsWith(QLatin1Char(':'));
    }

    QString uniqueName;
};

} // namespace

class TestWatcherLifecycle : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    void seeded()
    {
        seedReplySeen = true;
    }

private Q_SLOTS:
    void queuedRegistrationsFromExitedPeerDoNotBecomeGhosts()
    {
        StatusNotifierHost host;
        PeerProcess dead;
        QVERIFY(dead.startPeer(QStringLiteral("burst"), 8));
        QVERIFY(dead.state() == QProcess::NotRunning || dead.waitForFinished(2000));
        QCOMPARE(dead.exitCode(), 0);
        PeerProcess live;
        QVERIFY(live.startPeer(QStringLiteral("items"), 1));
        QTRY_COMPARE_WITH_TIMEOUT(host.itemCount(), 1, 5000);
        QCOMPARE(host.itemAt(0)->dbusService(), live.uniqueName);
        verifyWatcherItems(1, dead.uniqueName);
    }

    void exitingOwnerRemovesEveryItem()
    {
        StatusNotifierHost host;
        PeerProcess peer;
        QVERIFY(peer.startPeer(QStringLiteral("items"), 3));
        QTRY_COMPARE_WITH_TIMEOUT(host.itemCount(), 3, 5000);
        peer.kill();
        QVERIFY(peer.waitForFinished(2000));
        QTRY_COMPARE_WITH_TIMEOUT(host.itemCount(), 0, 5000);
        verifyWatcherItems(0);
    }

    void ownerExitingDuringInitialPropertiesCannotLeaveAnItem()
    {
        StatusNotifierHost host;
        PeerProcess vanished;
        QVERIFY(vanished.startPeer(QStringLiteral("vanish"), 1));
        QTRY_COMPARE_WITH_TIMEOUT(vanished.state(), QProcess::NotRunning, 5000);
        PeerProcess live;
        QVERIFY(live.startPeer(QStringLiteral("items"), 1));
        QTRY_COMPARE_WITH_TIMEOUT(host.itemCount(), 1, 5000);
        QCOMPARE(host.itemAt(0)->dbusService(), live.uniqueName);
        verifyWatcherItems(1, vanished.uniqueName);
    }

    void oldSeedReplyKeepsRegistrationsReceivedAfterRequest()
    {
        seedReplySeen = false;
        PeerProcess watcher;
        QVERIFY(watcher.startPeer(QStringLiteral("stale-seed"), 8));
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.connect(watcherService, watcherPath, watcherService, QStringLiteral("SeedReplied"), this,
                            SLOT(seeded())));
        StatusNotifierHost host;
        QTRY_VERIFY_WITH_TIMEOUT(seedReplySeen, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(host.itemCount(), 8, 5000);
        bus.disconnect(watcherService, watcherPath, watcherService, QStringLiteral("SeedReplied"), this,
                       SLOT(seeded()));
    }

    void restartingHostRediscoversAllRunningItems()
    {
        auto host = std::make_unique<StatusNotifierHost>();
        PeerProcess peer;
        QVERIFY(peer.startPeer(QStringLiteral("items"), 8));
        QTRY_COMPARE_WITH_TIMEOUT(host->itemCount(), 8, 5000);
        host.reset();
        host = std::make_unique<StatusNotifierHost>();
        QTRY_COMPARE_WITH_TIMEOUT(host->itemCount(), 8, 5000);
        verifyWatcherItems(8);
    }

private:
    void verifyWatcherItems(int count, const QString& absentOwner = {})
    {
        auto call =
            QDBusMessage::createMethodCall(watcherService, watcherPath, propertiesInterface, QStringLiteral("Get"));
        call << watcherService << QStringLiteral("RegisteredStatusNotifierItems");
        QDBusPendingCallWatcher reply(QDBusConnection::sessionBus().asyncCall(call));
        QTRY_VERIFY_WITH_TIMEOUT(reply.isFinished(), 3000);
        const QDBusPendingReply<QVariant> result = reply;
        QVERIFY2(!result.isError(), qPrintable(result.error().message()));
        const auto items = result.value().toStringList();
        QCOMPARE(items.size(), count);
        if (!absentOwner.isEmpty()) {
            for (const auto& item : items)
                QVERIFY(!item.startsWith(absentOwner + QLatin1Char('/')));
        }
    }

    bool seedReplySeen = false;
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 4 && args.at(1) == QLatin1String("--sni-peer"))
        return runPeer(args.at(2), args.at(3).toInt());
    TestWatcherLifecycle test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_watcherlifecycle.moc"
