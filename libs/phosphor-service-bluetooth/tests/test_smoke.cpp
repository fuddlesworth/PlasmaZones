// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceBluetooth/BluetoothAdapter.h>
#include <PhosphorServiceBluetooth/BluetoothDevice.h>
#include <PhosphorServiceBluetooth/BluetoothHost.h>
#include <PhosphorServiceBluetooth/QmlRegistration.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QSignalSpy>
#include <QTest>

using namespace PhosphorServiceBluetooth;

// The `a{sa{sv}}` / `a{oa{sa{sv}}}` payloads, registered on the *server* side
// (the fake) only — BluetoothHost's ObjectManager hand-demarshals.
using InterfaceMap = QMap<QString, QVariantMap>;
using ManagedObjectMap = QMap<QDBusObjectPath, InterfaceMap>;
Q_DECLARE_METATYPE(InterfaceMap)
Q_DECLARE_METATYPE(ManagedObjectMap)

namespace {
constexpr auto kService = "org.bluez";
constexpr auto kAdapterIface = "org.bluez.Adapter1";
constexpr auto kDeviceIface = "org.bluez.Device1";
constexpr auto kPropsIface = "org.freedesktop.DBus.Properties";
constexpr auto kAdapterPath = "/org/bluez/hci0";
constexpr auto kDevicePath = "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF";

QVariantMap adapterProps()
{
    return {{QStringLiteral("Address"), QStringLiteral("00:11:22:33:44:55")},
            {QStringLiteral("Name"), QStringLiteral("hci-test")},
            {QStringLiteral("Alias"), QStringLiteral("Test Adapter")},
            {QStringLiteral("Powered"), true},
            {QStringLiteral("Discovering"), false}};
}

QVariantMap deviceProps()
{
    return {{QStringLiteral("Address"), QStringLiteral("AA:BB:CC:DD:EE:FF")},
            {QStringLiteral("Name"), QStringLiteral("Test Headset")},
            {QStringLiteral("Paired"), true},
            {QStringLiteral("Connected"), false},
            {QStringLiteral("Adapter"), QVariant::fromValue(QDBusObjectPath(QLatin1String(kAdapterPath)))},
            {QStringLiteral("UUIDs"), QStringList{QStringLiteral("0000110b-0000-1000-8000-00805f9b34fb")}}};
}
} // namespace

// In-process fake org.bluez exporting ObjectManager, so BluetoothHost can be
// driven over a session bus without a daemon. PropertiesChanged is emitted
// straight from a path via createSignal (no per-object registration needed).
class FakeBluez : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.DBus.ObjectManager")

public:
    explicit FakeBluez(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ManagedObjectMap objects;

    void emitInterfacesRemoved(const QString& path, const QStringList& interfaces)
    {
        Q_EMIT InterfacesRemoved(QDBusObjectPath(path), interfaces);
    }

    void emitPropertiesChanged(const QString& path, const QString& iface, const QVariantMap& changed)
    {
        QDBusMessage signal =
            QDBusMessage::createSignal(path, QLatin1String(kPropsIface), QStringLiteral("PropertiesChanged"));
        signal << iface << changed << QStringList();
        QDBusConnection::sessionBus().send(signal);
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

class TestPhosphorServiceBluetoothSmoke : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        qDBusRegisterMetaType<InterfaceMap>();
        qDBusRegisterMetaType<ManagedObjectMap>();
    }

    void testRegisterQmlTypesIsIdempotent()
    {
        // call_once guard: registering from multiple engine setups is safe.
        registerQmlTypes();
        registerQmlTypes();
        QVERIFY(true);
    }

    void testAdapterReadSurfaceFromInitialProps()
    {
        // Initial properties come from the ObjectManager map; getters reflect
        // them with no bus round-trip required.
        BluetoothAdapter adapter(QDBusConnection::sessionBus(), QLatin1String(kAdapterPath), adapterProps());
        QCOMPARE(adapter.dbusPath(), QLatin1String(kAdapterPath));
        QCOMPARE(adapter.address(), QStringLiteral("00:11:22:33:44:55"));
        QCOMPARE(adapter.name(), QStringLiteral("hci-test"));
        QCOMPARE(adapter.alias(), QStringLiteral("Test Adapter"));
        QVERIFY(adapter.powered());
        QVERIFY(!adapter.discovering());
    }

    void testDeviceReadSurfaceFromInitialProps()
    {
        BluetoothDevice device(QDBusConnection::sessionBus(), QLatin1String(kDevicePath), deviceProps());
        QCOMPARE(device.dbusPath(), QLatin1String(kDevicePath));
        QCOMPARE(device.address(), QStringLiteral("AA:BB:CC:DD:EE:FF"));
        QCOMPARE(device.name(), QStringLiteral("Test Headset"));
        QVERIFY(device.paired());
        QVERIFY(!device.connected());
        QCOMPARE(device.adapter(), QLatin1String(kAdapterPath));
        QCOMPARE(device.uuids(), QStringList{QStringLiteral("0000110b-0000-1000-8000-00805f9b34fb")});
    }

    void testHostInertWithoutDaemonDoesNotCrash()
    {
        // A host on a connection with no org.bluez peer must construct, expose
        // empty-or-real lists, and answer out-of-range queries with nullptr.
        BluetoothHost host(QDBusConnection::sessionBus(), QStringLiteral("org.phosphor.test.AbsentBluez"));
        QVERIFY(host.adapterAt(-1) == nullptr);
        QVERIFY(host.adapterAt(0) == nullptr);
        QVERIFY(host.deviceAt(0) == nullptr);
        QCOMPARE(host.adapterCount(), 0);
        QCOMPARE(host.deviceCount(), 0);
    }

    void testHostEnumeratesAndTracksLiveChanges()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected())
            QSKIP("no session bus available");
        if (!bus.registerService(QLatin1String(kService)))
            QSKIP("could not own the test service name");

        FakeBluez fake;
        fake.objects.insert(QDBusObjectPath(QLatin1String(kAdapterPath)),
                            InterfaceMap{{QLatin1String(kAdapterIface), adapterProps()}});
        fake.objects.insert(QDBusObjectPath(QLatin1String(kDevicePath)),
                            InterfaceMap{{QLatin1String(kDeviceIface), deviceProps()}});
        QVERIFY(bus.registerObject(QStringLiteral("/"), &fake,
                                   QDBusConnection::ExportAdaptors | QDBusConnection::ExportAllContents));

        BluetoothHost host(bus, QLatin1String(kService));
        QSignalSpy deviceAddedSpy(&host, &BluetoothHost::deviceAdded);
        QVERIFY(deviceAddedSpy.wait(3000));

        // The initial walk surfaced both the adapter and its device.
        QCOMPARE(host.adapterCount(), 1);
        QCOMPARE(host.deviceCount(), 1);
        BluetoothAdapter* adapter = host.adapterAt(0);
        BluetoothDevice* device = host.deviceAt(0);
        QVERIFY(adapter);
        QVERIFY(device);
        QCOMPARE(adapter->name(), QStringLiteral("hci-test"));
        QCOMPARE(device->address(), QStringLiteral("AA:BB:CC:DD:EE:FF"));
        QVERIFY(!device->connected());

        // A live PropertiesChanged updates the device.
        QSignalSpy connectedSpy(device, &BluetoothDevice::connectedChanged);
        fake.emitPropertiesChanged(QLatin1String(kDevicePath), QLatin1String(kDeviceIface),
                                   {{QStringLiteral("Connected"), true}});
        QVERIFY(connectedSpy.wait(3000));
        QVERIFY(device->connected());

        // Removing the device interface drops the row; removing the adapter
        // cascades (its devices go too, though here the device is already gone).
        QSignalSpy deviceRemovedSpy(&host, &BluetoothHost::deviceRemoved);
        fake.emitInterfacesRemoved(QLatin1String(kDevicePath), {QLatin1String(kDeviceIface)});
        QVERIFY(deviceRemovedSpy.wait(3000));
        QCOMPARE(host.deviceCount(), 0);

        QSignalSpy adapterRemovedSpy(&host, &BluetoothHost::adapterRemoved);
        fake.emitInterfacesRemoved(QLatin1String(kAdapterPath), {QLatin1String(kAdapterIface)});
        QVERIFY(adapterRemovedSpy.wait(3000));
        QCOMPARE(host.adapterCount(), 0);

        bus.unregisterObject(QStringLiteral("/"));
        bus.unregisterService(QLatin1String(kService));
    }
};

QTEST_GUILESS_MAIN(TestPhosphorServiceBluetoothSmoke)
#include "test_smoke.moc"
