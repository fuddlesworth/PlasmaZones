// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceNetwork/AccessPoint.h>
#include <PhosphorServiceNetwork/AccessPointModel.h>
#include <PhosphorServiceNetwork/NetworkConnection.h>
#include <PhosphorServiceNetwork/NetworkConnectionModel.h>
#include <PhosphorServiceNetwork/NetworkDevice.h>
#include <PhosphorServiceNetwork/NetworkDeviceModel.h>
#include <PhosphorServiceNetwork/NetworkHost.h>

#include <QCoreApplication>
#include <QtTest/QtTest>

using namespace PhosphorServiceNetwork;

class TestSmoke : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    // Host construction binds the system bus and fires async bootstrap
    // queries. We can't assert anything about NetworkManager's response on
    // a CI runner with no NetworkManager, but we CAN assert construction
    // doesn't block, doesn't crash, and leaves a sensible initial state.
    void hostConstructsAndExposesInitialState()
    {
        NetworkHost host;
        QCOMPARE(host.deviceCount(), 0);
        QVERIFY(host.devices().isEmpty());
        QVERIFY(host.deviceAt(0) == nullptr);
        QVERIFY(host.deviceAt(-1) == nullptr);
        QCOMPARE(host.networkingEnabled(), false);
        QCOMPARE(host.wirelessEnabled(), false);
        QCOMPARE(host.connectivity(), NetworkHost::UnknownConnectivity);
        QVERIFY(host.primaryConnectionType().isEmpty());
    }

    // scanWifi / setWirelessEnabled must be safe to call on an idle host
    // (no devices, possibly no bus) without crashing.
    void hostWriteApisAreSafeWhenIdle()
    {
        NetworkHost host;
        host.scanWifi(); // no wifi devices -> no-op
        // setWirelessEnabled is NOT optimistic: the cached flag only flips
        // when NetworkManager echoes WirelessEnabled back via
        // PropertiesChanged, which cannot happen synchronously here (no
        // event-loop spin). Pin that contract instead of just asserting
        // the call did not crash.
        host.setWirelessEnabled(true);
        QCOMPARE(host.wirelessEnabled(), false);
    }

    void modelWithoutHostIsEmpty()
    {
        NetworkDeviceModel model;
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!model.host());
        QVERIFY(!model.data(model.index(0), NetworkDeviceModel::DeviceRole).isValid());
    }

    void modelExposesContractRoles()
    {
        NetworkDeviceModel model;
        const auto roles = model.roleNames();
        QVERIFY(roles.contains(NetworkDeviceModel::DeviceRole));
        QVERIFY(roles.contains(NetworkDeviceModel::InterfaceNameRole));
        QVERIFY(roles.contains(NetworkDeviceModel::DeviceTypeRole));
        QVERIFY(roles.contains(NetworkDeviceModel::StateRole));
        QVERIFY(roles.contains(NetworkDeviceModel::ManagedRole));
        // Role-name strings are public contract for QML delegates; pin
        // them so an internal rename can't silently break consumers.
        QCOMPARE(roles[NetworkDeviceModel::DeviceRole], QByteArrayLiteral("device"));
        QCOMPARE(roles[NetworkDeviceModel::InterfaceNameRole], QByteArrayLiteral("interfaceName"));
        QCOMPARE(roles[NetworkDeviceModel::DeviceTypeRole], QByteArrayLiteral("deviceType"));
        QCOMPARE(roles[NetworkDeviceModel::StateRole], QByteArrayLiteral("deviceState"));
        QCOMPARE(roles[NetworkDeviceModel::ManagedRole], QByteArrayLiteral("managed"));
    }

    void modelAttachesAndDetachesHost()
    {
        NetworkDeviceModel model;
        NetworkHost host;
        QSignalSpy hostSpy(&model, &NetworkDeviceModel::hostChanged);
        QSignalSpy countSpy(&model, &NetworkDeviceModel::countChanged);
        model.setHost(&host);
        QCOMPARE(model.host(), &host);
        QCOMPARE(hostSpy.count(), 1);
        // CI runners have no NetworkManager daemon, so both old and new row
        // sets are empty; countChanged must NOT fire on a 0 -> 0 move.
        QCOMPARE(countSpy.count(), 0);

        model.setHost(nullptr);
        QCOMPARE(model.host(), nullptr);
        QCOMPARE(hostSpy.count(), 2);
        QCOMPARE(countSpy.count(), 0);
    }

    void modelSurvivesHostDestruction()
    {
        // Pins the destroyed-lambda installed by setHost: a host destroyed
        // while still bound must reset m_host to nullptr and clear m_rows
        // inside a beginResetModel/endResetModel pair so QML views observe
        // a clean detach instead of a dangling pointer.
        NetworkDeviceModel model;
        QSignalSpy hostSpy(&model, &NetworkDeviceModel::hostChanged);
        QSignalSpy countSpy(&model, &NetworkDeviceModel::countChanged);
        {
            NetworkHost host;
            model.setHost(&host);
            QCOMPARE(model.host(), &host);
        }
        QCOMPARE(model.host(), nullptr);
        QCOMPARE(model.rowCount(), 0);
        // setHost (attach) emitted hostChanged once; destroyed-lambda emits
        // it again on auto-detach. Both transitions are 0 -> 0 row count so
        // countChanged stays silent.
        QCOMPARE(hostSpy.count(), 2);
        QCOMPARE(countSpy.count(), 0);
    }

    void deviceTypeEnumValuesArePublicContract()
    {
        // Wire constants mirroring NetworkManager's NMDeviceType; they must
        // not change without a coordinated upstream bump.
        QCOMPARE(static_cast<int>(NetworkDevice::Unknown), 0);
        QCOMPARE(static_cast<int>(NetworkDevice::Ethernet), 1);
        QCOMPARE(static_cast<int>(NetworkDevice::Wifi), 2);
        QCOMPARE(static_cast<int>(NetworkDevice::Bluetooth), 5);
        QCOMPARE(static_cast<int>(NetworkDevice::Modem), 8);
        QCOMPARE(static_cast<int>(NetworkDevice::Bridge), 13);
        QCOMPARE(static_cast<int>(NetworkDevice::Ppp), 23);
        QCOMPARE(static_cast<int>(NetworkDevice::Wireguard), 29);
    }

    void deviceStateEnumValuesArePublicContract()
    {
        // Wire constants mirroring NetworkManager's NMDeviceState (×10).
        QCOMPARE(static_cast<int>(NetworkDevice::UnknownState), 0);
        QCOMPARE(static_cast<int>(NetworkDevice::Unmanaged), 10);
        QCOMPARE(static_cast<int>(NetworkDevice::Unavailable), 20);
        QCOMPARE(static_cast<int>(NetworkDevice::Disconnected), 30);
        QCOMPARE(static_cast<int>(NetworkDevice::Activated), 100);
        QCOMPARE(static_cast<int>(NetworkDevice::Deactivating), 110);
        QCOMPARE(static_cast<int>(NetworkDevice::Failed), 120);
    }

    void connectivityEnumValuesArePublicContract()
    {
        // Wire constants mirroring NetworkManager's NMConnectivityState.
        QCOMPARE(static_cast<int>(NetworkHost::UnknownConnectivity), 0);
        QCOMPARE(static_cast<int>(NetworkHost::NoConnectivity), 1);
        QCOMPARE(static_cast<int>(NetworkHost::Portal), 2);
        QCOMPARE(static_cast<int>(NetworkHost::Limited), 3);
        QCOMPARE(static_cast<int>(NetworkHost::Full), 4);
    }

    void apModelWithoutDeviceIsEmpty()
    {
        AccessPointModel model;
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!model.device());
        QVERIFY(!model.data(model.index(0), AccessPointModel::SsidRole).isValid());
    }

    void apModelExposesContractRoles()
    {
        AccessPointModel model;
        const auto roles = model.roleNames();
        QVERIFY(roles.contains(AccessPointModel::AccessPointRole));
        QVERIFY(roles.contains(AccessPointModel::SsidRole));
        QVERIFY(roles.contains(AccessPointModel::StrengthRole));
        QVERIFY(roles.contains(AccessPointModel::FrequencyRole));
        QVERIFY(roles.contains(AccessPointModel::BssidRole));
        QVERIFY(roles.contains(AccessPointModel::SecurityRole));
        QVERIFY(roles.contains(AccessPointModel::SecuredRole));
        QCOMPARE(roles[AccessPointModel::AccessPointRole], QByteArrayLiteral("accessPoint"));
        QCOMPARE(roles[AccessPointModel::SsidRole], QByteArrayLiteral("ssid"));
        QCOMPARE(roles[AccessPointModel::StrengthRole], QByteArrayLiteral("strength"));
        QCOMPARE(roles[AccessPointModel::FrequencyRole], QByteArrayLiteral("frequency"));
        QCOMPARE(roles[AccessPointModel::BssidRole], QByteArrayLiteral("bssid"));
        QCOMPARE(roles[AccessPointModel::SecurityRole], QByteArrayLiteral("security"));
        QCOMPARE(roles[AccessPointModel::SecuredRole], QByteArrayLiteral("secured"));
    }

    void apModelAttachesAndDetachesDevice()
    {
        // On a CI runner with no NetworkManager the device is inert and
        // GetAllAccessPoints errors out (ignored), so the model stays
        // empty; what we pin here is the attach/detach bookkeeping and that
        // binding a device doesn't crash.
        AccessPointModel model;
        NetworkDevice device(QStringLiteral("/org/freedesktop/NetworkManager/Devices/0"));
        QSignalSpy deviceSpy(&model, &AccessPointModel::deviceChanged);
        model.setDevice(&device);
        QCOMPARE(model.device(), &device);
        QCOMPARE(deviceSpy.count(), 1);
        QCOMPARE(model.rowCount(), 0);

        model.setDevice(nullptr);
        QCOMPARE(model.device(), nullptr);
        QCOMPARE(deviceSpy.count(), 2);
    }

    void apModelSurvivesDeviceDestruction()
    {
        AccessPointModel model;
        QSignalSpy deviceSpy(&model, &AccessPointModel::deviceChanged);
        {
            NetworkDevice device(QStringLiteral("/org/freedesktop/NetworkManager/Devices/0"));
            model.setDevice(&device);
            QCOMPARE(model.device(), &device);
        }
        QCOMPARE(model.device(), nullptr);
        QCOMPARE(model.rowCount(), 0);
        // attach emitted deviceChanged once; destroyed-lambda emits it again.
        QCOMPARE(deviceSpy.count(), 2);
    }

    void connectionModelConstructsCleanly()
    {
        // Self-bootstraps against NM Settings; ListConnections is async, so
        // the row set is empty synchronously (and stays empty on a CI
        // runner with no NetworkManager). What we pin: construction binds
        // without blocking or crashing.
        NetworkConnectionModel model;
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!model.data(model.index(0), NetworkConnectionModel::IdRole).isValid());
    }

    void connectionModelExposesContractRoles()
    {
        NetworkConnectionModel model;
        const auto roles = model.roleNames();
        QVERIFY(roles.contains(NetworkConnectionModel::ConnectionRole));
        QVERIFY(roles.contains(NetworkConnectionModel::IdRole));
        QVERIFY(roles.contains(NetworkConnectionModel::UuidRole));
        QVERIFY(roles.contains(NetworkConnectionModel::ConnectionTypeRole));
        QCOMPARE(roles[NetworkConnectionModel::ConnectionRole], QByteArrayLiteral("connection"));
        QCOMPARE(roles[NetworkConnectionModel::IdRole], QByteArrayLiteral("id"));
        QCOMPARE(roles[NetworkConnectionModel::UuidRole], QByteArrayLiteral("uuid"));
        QCOMPARE(roles[NetworkConnectionModel::ConnectionTypeRole], QByteArrayLiteral("connectionType"));
    }

    void hostConnectApisAreSafeWhenIdle()
    {
        // Null arguments and inert (CI) objects must no-op, never crash.
        NetworkHost host;
        host.activateConnection(nullptr, nullptr);
        host.connectToAccessPoint(nullptr, nullptr);

        NetworkDevice device(QStringLiteral("/org/freedesktop/NetworkManager/Devices/0"));
        NetworkConnection connection(QStringLiteral("/org/freedesktop/NetworkManager/Settings/0"));
        AccessPoint ap(QStringLiteral("/org/freedesktop/NetworkManager/AccessPoints/0"));
        host.activateConnection(&connection, &device);
        // Without a daemon the AccessPoint's GetAll never lands, so its SSID
        // stays empty and connectToAccessPoint refuses it at the empty-SSID
        // guard (a hidden-network AP behaves the same way). Both calls are
        // therefore exercised up to that guard; what we pin is that they
        // no-op without crashing and without mutating observable state.
        QVERIFY(ap.ssid().isEmpty());
        host.connectToAccessPoint(&device, &ap, QStringLiteral("hunter2"));
        host.connectToAccessPoint(&device, &ap); // open network (no passphrase)
        QCOMPARE(device.state(), NetworkDevice::UnknownState);
    }
};

QTEST_MAIN(TestSmoke)
#include "test_smoke.moc"
