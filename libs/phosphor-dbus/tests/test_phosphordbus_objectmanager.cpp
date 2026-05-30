// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorDBus/ObjectManager.h>

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QSignalSpy>
#include <QTest>

using PhosphorDBus::InterfaceMap;
using PhosphorDBus::ObjectManager;

// The `a{oa{sa{sv}}}` GetManagedObjects payload. Registered as a D-Bus metatype
// on the *server* side only — the observer under test hand-demarshals, so it
// needs no registration. QString / QVariantMap / QDBusObjectPath already carry
// QDBusArgument operators, and Qt synthesises them for the nested QMaps.
using ManagedObjectMap = QMap<QDBusObjectPath, InterfaceMap>;
Q_DECLARE_METATYPE(InterfaceMap)
Q_DECLARE_METATYPE(ManagedObjectMap)

namespace {
constexpr auto kService = "org.phosphor.test.ObjectManager";
constexpr auto kObjectManagerIface = "org.freedesktop.DBus.ObjectManager";

InterfaceMap makeInterfaces(const QString& iface, const QString& nameKey, const QString& nameValue)
{
    InterfaceMap interfaces;
    interfaces.insert(iface, QVariantMap{{nameKey, nameValue}});
    return interfaces;
}
} // namespace

// In-process fake that exports org.freedesktop.DBus.ObjectManager so the
// observer can be exercised over a real session bus without any daemon.
class FakeObjectManager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.DBus.ObjectManager")

public:
    explicit FakeObjectManager(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ManagedObjectMap objects;

    void emitInterfacesAdded(const QString& path, const InterfaceMap& interfaces)
    {
        Q_EMIT InterfacesAdded(QDBusObjectPath(path), interfaces);
    }
    void emitInterfacesRemoved(const QString& path, const QStringList& interfaces)
    {
        Q_EMIT InterfacesRemoved(QDBusObjectPath(path), interfaces);
    }

public Q_SLOTS:
    ManagedObjectMap GetManagedObjects() const
    {
        return objects;
    }

Q_SIGNALS:
    void InterfacesAdded(const QDBusObjectPath& path, InterfaceMap interfaces);
    void InterfacesRemoved(const QDBusObjectPath& path, QStringList interfaces);
};

class TestPhosphorDBusObjectManager : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        qDBusRegisterMetaType<InterfaceMap>();
        qDBusRegisterMetaType<ManagedObjectMap>();
    }

    void testAccessorsRetainConstruction()
    {
        // Accessors are pure state and need no live peer.
        ObjectManager om(QDBusConnection::sessionBus(), QStringLiteral("org.example.Svc"), QStringLiteral("/example"));
        QCOMPARE(om.service(), QStringLiteral("org.example.Svc"));
        QCOMPARE(om.rootPath(), QStringLiteral("/example"));
    }

    void testNotReadyBeforeEventLoopSpins()
    {
        // The initial GetManagedObjects is async; until the event loop runs the
        // reply cannot have landed, so the observer is never ready synchronously.
        ObjectManager om(QDBusConnection::sessionBus(), QStringLiteral("org.example.Svc"), QStringLiteral("/example"));
        QVERIFY(!om.isReady());
    }

    void testInitialWalkAndIncrementalSignals()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected())
            QSKIP("no session bus available");
        if (!bus.registerService(QLatin1String(kService)))
            QSKIP("could not own the test service name");

        FakeObjectManager fake;
        fake.objects.insert(
            QDBusObjectPath(QStringLiteral("/org/test/adapter")),
            makeInterfaces(QStringLiteral("org.test.Adapter1"), QStringLiteral("Name"), QStringLiteral("hci-test")));
        QVERIFY(bus.registerObject(QStringLiteral("/"), &fake,
                                   QDBusConnection::ExportAdaptors | QDBusConnection::ExportAllContents));

        ObjectManager om(bus, QLatin1String(kService), QStringLiteral("/"));
        QSignalSpy addedSpy(&om, &ObjectManager::interfacesAdded);
        QSignalSpy removedSpy(&om, &ObjectManager::interfacesRemoved);
        QSignalSpy readySpy(&om, &ObjectManager::ready);

        // The initial GetManagedObjects walk surfaces the seeded object, then
        // fires ready exactly once.
        QVERIFY(readySpy.wait(3000));
        QCOMPARE(readySpy.count(), 1);
        QVERIFY(om.isReady());
        QCOMPARE(addedSpy.count(), 1);
        QCOMPARE(addedSpy.at(0).at(0).toString(), QStringLiteral("/org/test/adapter"));
        const auto initialIfaces = addedSpy.at(0).at(1).value<InterfaceMap>();
        QVERIFY(initialIfaces.contains(QStringLiteral("org.test.Adapter1")));
        QCOMPARE(initialIfaces.value(QStringLiteral("org.test.Adapter1")).value(QStringLiteral("Name")).toString(),
                 QStringLiteral("hci-test"));

        // A device appearing after the walk arrives as an incremental add.
        fake.emitInterfacesAdded(QStringLiteral("/org/test/adapter/dev_AA"),
                                 makeInterfaces(QStringLiteral("org.test.Device1"), QStringLiteral("Address"),
                                                QStringLiteral("AA:BB:CC:DD:EE:FF")));
        QVERIFY(addedSpy.wait(3000));
        QCOMPARE(addedSpy.count(), 2);
        QCOMPARE(addedSpy.at(1).at(0).toString(), QStringLiteral("/org/test/adapter/dev_AA"));
        const auto deviceIfaces = addedSpy.at(1).at(1).value<InterfaceMap>();
        QVERIFY(deviceIfaces.contains(QStringLiteral("org.test.Device1")));

        // Removal surfaces the path + the interface-name list.
        fake.emitInterfacesRemoved(QStringLiteral("/org/test/adapter/dev_AA"), {QStringLiteral("org.test.Device1")});
        QVERIFY(removedSpy.wait(3000));
        QCOMPARE(removedSpy.count(), 1);
        QCOMPARE(removedSpy.at(0).at(0).toString(), QStringLiteral("/org/test/adapter/dev_AA"));
        QCOMPARE(removedSpy.at(0).at(1).toStringList(), QStringList{QStringLiteral("org.test.Device1")});

        bus.unregisterObject(QStringLiteral("/"));
        bus.unregisterService(QLatin1String(kService));
    }

    void testErrorReplyStillFiresReady()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected())
            QSKIP("no session bus available");

        // No peer owns this name, so GetManagedObjects errors out; ready must
        // still fire so a consumer waiting on the initial snapshot is not stuck.
        ObjectManager om(bus, QStringLiteral("org.phosphor.test.AbsentService"), QStringLiteral("/"));
        QSignalSpy addedSpy(&om, &ObjectManager::interfacesAdded);
        QSignalSpy readySpy(&om, &ObjectManager::ready);

        QVERIFY(readySpy.wait(3000));
        QCOMPARE(readySpy.count(), 1);
        QVERIFY(om.isReady());
        QCOMPARE(addedSpy.count(), 0);
    }

    void testInertWhenBusDisconnected()
    {
        // A disconnected bus: the observer issues no call, never becomes ready,
        // and surfaces nothing — it must not crash or hang.
        QDBusConnection bad = QDBusConnection::connectToBus(QStringLiteral("unix:path=/phosphor-nonexistent-bus"),
                                                            QStringLiteral("phosphor-om-inert"));
        QVERIFY(!bad.isConnected());
        ObjectManager om(bad, QStringLiteral("org.example.Svc"), QStringLiteral("/"));
        QSignalSpy readySpy(&om, &ObjectManager::ready);
        QSignalSpy addedSpy(&om, &ObjectManager::interfacesAdded);
        QTest::qWait(200);
        QVERIFY(!om.isReady());
        QCOMPARE(readySpy.count(), 0);
        QCOMPARE(addedSpy.count(), 0);
        QDBusConnection::disconnectFromBus(QStringLiteral("phosphor-om-inert"));
    }
};

QTEST_GUILESS_MAIN(TestPhosphorDBusObjectManager)
#include "test_phosphordbus_objectmanager.moc"
