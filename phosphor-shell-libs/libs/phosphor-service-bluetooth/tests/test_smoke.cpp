// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceBluetooth/BluetoothAdapter.h>
#include <PhosphorServiceBluetooth/BluetoothAdapterModel.h>
#include <PhosphorServiceBluetooth/BluetoothAgent.h>
#include <PhosphorServiceBluetooth/BluetoothDevice.h>
#include <PhosphorServiceBluetooth/BluetoothDeviceModel.h>
#include <PhosphorServiceBluetooth/BluetoothHost.h>
#include <PhosphorServiceBluetooth/QmlRegistration.h>

#include <QAbstractItemModel>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QSignalSpy>
#include <QTest>

using namespace PhosphorServiceBluetooth;

// The `a{sa{sv}}` / `a{oa{sa{sv}}}` payloads, registered on the *server* side
// (the fake) only; BluetoothHost's ObjectManager hand-demarshals.
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
        // UUIDs (`as`) survive the wire round-trip: over D-Bus the value nests
        // as a QDBusArgument inside the a{sv}, so this would be empty if the
        // demarshalling fell back to QVariant::toStringList().
        QCOMPARE(device->uuids(), QStringList{QStringLiteral("0000110b-0000-1000-8000-00805f9b34fb")});

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

    void testAdapterModelTracksHost()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected())
            QSKIP("no session bus available");
        if (!bus.registerService(QLatin1String(kService)))
            QSKIP("could not own the test service name");

        FakeBluez fake;
        fake.objects.insert(QDBusObjectPath(QLatin1String(kAdapterPath)),
                            InterfaceMap{{QLatin1String(kAdapterIface), adapterProps()}});
        QVERIFY(bus.registerObject(QStringLiteral("/"), &fake,
                                   QDBusConnection::ExportAdaptors | QDBusConnection::ExportAllContents));

        BluetoothHost host(bus, QLatin1String(kService));
        BluetoothAdapterModel model;
        model.setHost(&host);
        QCOMPARE(model.host(), &host);

        // The host enumerates asynchronously; the model mirrors the adapter
        // via adapterAdded.
        QSignalSpy insertedSpy(&model, &QAbstractItemModel::rowsInserted);
        QVERIFY(insertedSpy.wait(3000));
        QCOMPARE(model.rowCount(), 1);

        const QModelIndex idx = model.index(0);
        QCOMPARE(model.data(idx, BluetoothAdapterModel::NameRole).toString(), QStringLiteral("hci-test"));
        QVERIFY(model.data(idx, BluetoothAdapterModel::PoweredRole).toBool());
        QVERIFY(model.data(idx, BluetoothAdapterModel::AdapterRole).value<QObject*>() != nullptr);
        QCOMPARE(model.roleNames().value(BluetoothAdapterModel::NameRole), QByteArrayLiteral("name"));

        bus.unregisterObject(QStringLiteral("/"));
        bus.unregisterService(QLatin1String(kService));
    }

    void testAdapterWriteMethodsAreSafe()
    {
        // The write methods are fire-and-forget; against a path with no live
        // peer they must construct + dispatch the call (Properties.Set with a
        // QDBusVariant payload, StartDiscovery/StopDiscovery) without crashing.
        BluetoothAdapter adapter(QDBusConnection::sessionBus(), QLatin1String(kAdapterPath), adapterProps());
        adapter.setPowered(true);
        adapter.setDiscoverable(true);
        adapter.startDiscovery();
        adapter.stopDiscovery();
        QVERIFY(true);
    }

    void testDeviceModelTracksHostAndFilters()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected())
            QSKIP("no session bus available");
        if (!bus.registerService(QLatin1String(kService)))
            QSKIP("could not own the test service name");

        constexpr auto kSecondDevicePath = "/org/bluez/hci1/dev_11_22_33_44_55_66";
        QVariantMap deviceB = deviceProps();
        deviceB[QStringLiteral("Address")] = QStringLiteral("11:22:33:44:55:66");
        deviceB[QStringLiteral("Adapter")] = QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/bluez/hci1")));

        FakeBluez fake;
        fake.objects.insert(QDBusObjectPath(QLatin1String(kAdapterPath)),
                            InterfaceMap{{QLatin1String(kAdapterIface), adapterProps()}});
        fake.objects.insert(QDBusObjectPath(QLatin1String(kDevicePath)),
                            InterfaceMap{{QLatin1String(kDeviceIface), deviceProps()}});
        fake.objects.insert(QDBusObjectPath(QLatin1String(kSecondDevicePath)),
                            InterfaceMap{{QLatin1String(kDeviceIface), deviceB}});
        QVERIFY(bus.registerObject(QStringLiteral("/"), &fake,
                                   QDBusConnection::ExportAdaptors | QDBusConnection::ExportAllContents));

        BluetoothHost host(bus, QLatin1String(kService));
        BluetoothDeviceModel model;
        model.setHost(&host);

        // Unfiltered: both devices (under hci0 and hci1) show up.
        QTRY_COMPARE(model.rowCount(), 2);

        // Filter to the hci0 adapter: only its device (dev_AA) remains.
        QVERIFY(host.adapterAt(0));
        model.setAdapter(host.adapterAt(0));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), BluetoothDeviceModel::AddressRole).toString(),
                 QStringLiteral("AA:BB:CC:DD:EE:FF"));

        // Clearing the filter restores the full list.
        model.setAdapter(nullptr);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.roleNames().value(BluetoothDeviceModel::ConnectedRole), QByteArrayLiteral("connected"));

        bus.unregisterObject(QStringLiteral("/"));
        bus.unregisterService(QLatin1String(kService));
    }

    void testDeviceWriteMethodsAreSafe()
    {
        // Fire-and-forget Device1 calls (Connect/Disconnect methods, Trusted/
        // Blocked Properties.Set, Pair/CancelPairing) must dispatch without
        // crashing.
        BluetoothDevice device(QDBusConnection::sessionBus(), QLatin1String(kDevicePath), deviceProps());
        device.connectDevice();
        device.disconnectDevice();
        device.setTrusted(true);
        device.setBlocked(false);
        device.pair();
        device.cancelPairing();
        QVERIFY(true);
    }

    void testAgentConfirmationLifecycle()
    {
        BluetoothAgent agent;
        QSignalSpy requestSpy(&agent, &BluetoothAgent::confirmationRequested);

        // Invoked directly (not via D-Bus): no delayed reply is held, but the
        // request bookkeeping + signal still fire so the state machine is
        // exercised deterministically without a daemon.
        agent.RequestConfirmation(QDBusObjectPath(QLatin1String(kDevicePath)), 123456U);
        QCOMPARE(requestSpy.count(), 1);
        QCOMPARE(requestSpy.at(0).at(0).toString(), QLatin1String(kDevicePath));
        QCOMPARE(requestSpy.at(0).at(1).toUInt(), 123456U);
        QCOMPARE(agent.pendingRequestCount(), 1);

        const quint64 id = requestSpy.at(0).at(2).toULongLong();
        agent.respondConfirmation(id, true);
        QCOMPARE(agent.pendingRequestCount(), 0);

        // A stale id is ignored.
        agent.respondConfirmation(id, true);
        QCOMPARE(agent.pendingRequestCount(), 0);
    }

    void testAgentPasskeyRejectAndCancel()
    {
        BluetoothAgent agent;
        QSignalSpy passkeySpy(&agent, &BluetoothAgent::passkeyRequested);

        agent.RequestPasskey(QDBusObjectPath(QLatin1String(kDevicePath)));
        QCOMPARE(agent.pendingRequestCount(), 1);
        const quint64 id = passkeySpy.at(0).at(1).toULongLong();
        // Wrong-kind response is ignored; the correct reject clears it.
        agent.respondConfirmation(id, true);
        QCOMPARE(agent.pendingRequestCount(), 1);
        agent.rejectRequest(id);
        QCOMPARE(agent.pendingRequestCount(), 0);

        // Cancel withdraws everything in flight and notifies any open prompt.
        QSignalSpy cancelledSpy(&agent, &BluetoothAgent::requestCancelled);
        agent.RequestConfirmation(QDBusObjectPath(QLatin1String(kDevicePath)), 42U);
        agent.RequestAuthorization(QDBusObjectPath(QLatin1String(kDevicePath)));
        QCOMPARE(agent.pendingRequestCount(), 2);
        agent.Cancel();
        QCOMPARE(agent.pendingRequestCount(), 0);
        QCOMPARE(cancelledSpy.count(), 1);
    }

    void testAgentDisplayCallbacksEmitWithoutPending()
    {
        BluetoothAgent agent;
        QSignalSpy passkeyShown(&agent, &BluetoothAgent::passkeyDisplayed);
        QSignalSpy pinShown(&agent, &BluetoothAgent::pinCodeDisplayed);
        agent.DisplayPasskey(QDBusObjectPath(QLatin1String(kDevicePath)), 654321U, 3);
        agent.DisplayPinCode(QDBusObjectPath(QLatin1String(kDevicePath)), QStringLiteral("0000"));
        QCOMPARE(passkeyShown.count(), 1);
        QCOMPARE(passkeyShown.at(0).at(1).toUInt(), 654321U);
        QCOMPARE(pinShown.count(), 1);
        QCOMPARE(agent.pendingRequestCount(), 0);
    }

    void testHostExposesAgentWhenBusConnected()
    {
        // The session bus is connected even though no org.bluez peer exists,
        // so the host sets up (and exports) its agent.
        BluetoothHost host(QDBusConnection::sessionBus(), QStringLiteral("org.phosphor.test.AbsentBluez2"));
        QVERIFY(host.agent() != nullptr);
        QCOMPARE(host.agent()->pendingRequestCount(), 0);
    }

    void testAgentRemainingCallbacks()
    {
        BluetoothAgent agent;

        // PIN code path.
        QSignalSpy pinSpy(&agent, &BluetoothAgent::pinCodeRequested);
        agent.RequestPinCode(QDBusObjectPath(QLatin1String(kDevicePath)));
        QCOMPARE(pinSpy.count(), 1);
        agent.respondPinCode(pinSpy.at(0).at(1).toULongLong(), QStringLiteral("0000"));
        QCOMPARE(agent.pendingRequestCount(), 0);

        // Service-authorization path (answered via respondConfirmation).
        QSignalSpy svcSpy(&agent, &BluetoothAgent::serviceAuthorizationRequested);
        agent.AuthorizeService(QDBusObjectPath(QLatin1String(kDevicePath)),
                               QStringLiteral("0000110b-0000-1000-8000-00805f9b34fb"));
        QCOMPARE(svcSpy.count(), 1);
        QCOMPARE(svcSpy.at(0).at(1).toString(), QStringLiteral("0000110b-0000-1000-8000-00805f9b34fb"));
        agent.respondConfirmation(svcSpy.at(0).at(2).toULongLong(), true);
        QCOMPARE(agent.pendingRequestCount(), 0);

        // Release drops anything still pending.
        QSignalSpy releasedSpy(&agent, &BluetoothAgent::released);
        agent.RequestAuthorization(QDBusObjectPath(QLatin1String(kDevicePath)));
        QCOMPARE(agent.pendingRequestCount(), 1);
        agent.Release();
        QCOMPARE(agent.pendingRequestCount(), 0);
        QCOMPARE(releasedSpy.count(), 1);
    }

    void testAllModelRoleNames()
    {
        BluetoothAdapterModel adapters;
        const auto a = adapters.roleNames();
        QCOMPARE(a.value(BluetoothAdapterModel::AdapterRole), QByteArrayLiteral("adapter"));
        QCOMPARE(a.value(BluetoothAdapterModel::AddressRole), QByteArrayLiteral("address"));
        QCOMPARE(a.value(BluetoothAdapterModel::NameRole), QByteArrayLiteral("name"));
        QCOMPARE(a.value(BluetoothAdapterModel::AliasRole), QByteArrayLiteral("alias"));
        QCOMPARE(a.value(BluetoothAdapterModel::PoweredRole), QByteArrayLiteral("powered"));
        QCOMPARE(a.value(BluetoothAdapterModel::DiscoverableRole), QByteArrayLiteral("discoverable"));
        QCOMPARE(a.value(BluetoothAdapterModel::PairableRole), QByteArrayLiteral("pairable"));
        QCOMPARE(a.value(BluetoothAdapterModel::DiscoveringRole), QByteArrayLiteral("discovering"));
        QCOMPARE(a.size(), 8);

        BluetoothDeviceModel devices;
        const auto d = devices.roleNames();
        QCOMPARE(d.value(BluetoothDeviceModel::DeviceRole), QByteArrayLiteral("device"));
        QCOMPARE(d.value(BluetoothDeviceModel::AddressRole), QByteArrayLiteral("address"));
        QCOMPARE(d.value(BluetoothDeviceModel::NameRole), QByteArrayLiteral("name"));
        QCOMPARE(d.value(BluetoothDeviceModel::AliasRole), QByteArrayLiteral("alias"));
        QCOMPARE(d.value(BluetoothDeviceModel::IconRole), QByteArrayLiteral("icon"));
        QCOMPARE(d.value(BluetoothDeviceModel::PairedRole), QByteArrayLiteral("paired"));
        QCOMPARE(d.value(BluetoothDeviceModel::TrustedRole), QByteArrayLiteral("trusted"));
        QCOMPARE(d.value(BluetoothDeviceModel::BlockedRole), QByteArrayLiteral("blocked"));
        QCOMPARE(d.value(BluetoothDeviceModel::ConnectedRole), QByteArrayLiteral("connected"));
        QCOMPARE(d.value(BluetoothDeviceModel::RssiRole), QByteArrayLiteral("rssi"));
        QCOMPARE(d.value(BluetoothDeviceModel::AdapterRole), QByteArrayLiteral("adapter"));
        QCOMPARE(d.value(BluetoothDeviceModel::UuidsRole), QByteArrayLiteral("uuids"));
        QCOMPARE(d.size(), 12);
    }

    // Proves the delayed-reply mechanism end-to-end: a live D-Bus caller hits
    // the agent's exported Agent1 methods, and the reply arrives only after the
    // consumer answers (never the suppressed auto-reply). The reject + value
    // cases are discriminating: without setDelayedReply the caller would see an
    // immediate success / 0 instead.
    void testAgentDelayedReplyOverDBus()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected())
            QSKIP("no session bus available");
        const QString service = QStringLiteral("org.phosphor.test.Agent");
        if (!bus.registerService(service))
            QSKIP("could not own the test service name");

        BluetoothAgent agent;
        QVERIFY(bus.registerObject(BluetoothAgent::agentPath(), &agent, QDBusConnection::ExportAllSlots));

        // Passkey: respond with a value; the caller must receive exactly it.
        QObject::connect(&agent, &BluetoothAgent::passkeyRequested, [&agent](const QString&, quint64 id) {
            agent.respondPasskey(id, 424242U);
        });
        QDBusMessage passkeyCall = QDBusMessage::createMethodCall(
            service, BluetoothAgent::agentPath(), QStringLiteral("org.bluez.Agent1"), QStringLiteral("RequestPasskey"));
        passkeyCall << QVariant::fromValue(QDBusObjectPath(QLatin1String(kDevicePath)));
        QDBusPendingReply<uint> passkeyReply = bus.asyncCall(passkeyCall);
        {
            QDBusPendingCallWatcher watcher(passkeyReply);
            QSignalSpy finished(&watcher, &QDBusPendingCallWatcher::finished);
            QVERIFY(finished.wait(3000));
        }
        passkeyReply.waitForFinished();
        QVERIFY(!passkeyReply.isError());
        QCOMPARE(passkeyReply.value(), 424242U);

        // Confirmation rejected: the caller must receive org.bluez.Error.Rejected,
        // not the suppressed empty success reply.
        QObject::connect(&agent, &BluetoothAgent::confirmationRequested, [&agent](const QString&, quint32, quint64 id) {
            agent.rejectRequest(id);
        });
        QDBusMessage confirmCall =
            QDBusMessage::createMethodCall(service, BluetoothAgent::agentPath(), QStringLiteral("org.bluez.Agent1"),
                                           QStringLiteral("RequestConfirmation"));
        confirmCall << QVariant::fromValue(QDBusObjectPath(QLatin1String(kDevicePath)))
                    << QVariant::fromValue<uint>(123456U);
        QDBusPendingReply<> confirmReply = bus.asyncCall(confirmCall);
        {
            QDBusPendingCallWatcher watcher(confirmReply);
            QSignalSpy finished(&watcher, &QDBusPendingCallWatcher::finished);
            QVERIFY(finished.wait(3000));
        }
        confirmReply.waitForFinished();
        QVERIFY(confirmReply.isError());
        QCOMPARE(confirmReply.error().name(), QStringLiteral("org.bluez.Error.Rejected"));

        bus.unregisterObject(BluetoothAgent::agentPath());
        bus.unregisterService(service);
    }
};

QTEST_GUILESS_MAIN(TestPhosphorServiceBluetoothSmoke)
#include "test_smoke.moc"
