// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorServiceUPower/UPowerDeviceModel.h>
#include <PhosphorServiceUPower/UPowerHost.h>

#include <QCoreApplication>
#include <QtTest/QtTest>

class TestSmoke : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    // The host construction binds to the system bus and fires three
    // async queries. We can't assert anything about UPower's response
    // on a CI runner with no UPower, but we CAN assert that the
    // construction doesn't block, doesn't crash, and leaves the host
    // in a sensible initial state.
    void hostConstructsAndExposesInitialState()
    {
        PhosphorServiceUPower::UPowerHost host;
        QCOMPARE(host.deviceCount(), 0);
        QVERIFY(host.devices().isEmpty());
        QVERIFY(host.deviceAt(0) == nullptr);
        QVERIFY(host.deviceAt(-1) == nullptr);
        QVERIFY(host.displayDevice() == nullptr);
    }

    void modelWithoutHostIsEmpty()
    {
        PhosphorServiceUPower::UPowerDeviceModel model;
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!model.host());
    }

    void modelExposesContractRoles()
    {
        PhosphorServiceUPower::UPowerDeviceModel model;
        const auto roles = model.roleNames();
        QVERIFY(roles.contains(PhosphorServiceUPower::UPowerDeviceModel::DeviceRole));
        QVERIFY(roles.contains(PhosphorServiceUPower::UPowerDeviceModel::PercentageRole));
        QVERIFY(roles.contains(PhosphorServiceUPower::UPowerDeviceModel::StateRole));
        QVERIFY(roles.contains(PhosphorServiceUPower::UPowerDeviceModel::TypeRole));
        QVERIFY(roles.contains(PhosphorServiceUPower::UPowerDeviceModel::IconNameRole));
        QVERIFY(roles.contains(PhosphorServiceUPower::UPowerDeviceModel::IsLaptopBatteryRole));
        // Role-name strings are public contract for QML delegates; pin
        // them so an internal rename doesn't silently break consumers.
        QCOMPARE(roles[PhosphorServiceUPower::UPowerDeviceModel::DeviceRole], QByteArrayLiteral("device"));
        QCOMPARE(roles[PhosphorServiceUPower::UPowerDeviceModel::PercentageRole], QByteArrayLiteral("percentage"));
        QCOMPARE(roles[PhosphorServiceUPower::UPowerDeviceModel::StateRole], QByteArrayLiteral("deviceState"));
        QCOMPARE(roles[PhosphorServiceUPower::UPowerDeviceModel::TypeRole], QByteArrayLiteral("deviceType"));
        QCOMPARE(roles[PhosphorServiceUPower::UPowerDeviceModel::IconNameRole], QByteArrayLiteral("iconName"));
        QCOMPARE(roles[PhosphorServiceUPower::UPowerDeviceModel::IsLaptopBatteryRole],
                 QByteArrayLiteral("isLaptopBattery"));
    }

    void modelAttachesAndDetachesHost()
    {
        PhosphorServiceUPower::UPowerDeviceModel model;
        PhosphorServiceUPower::UPowerHost host;
        QSignalSpy hostSpy(&model, &PhosphorServiceUPower::UPowerDeviceModel::hostChanged);
        QSignalSpy countSpy(&model, &PhosphorServiceUPower::UPowerDeviceModel::countChanged);
        model.setHost(&host);
        QCOMPARE(model.host(), &host);
        QCOMPARE(hostSpy.count(), 1);
        // countChanged fires unconditionally on host change because the
        // row set is rebuilt from the host's snapshot — the test
        // doesn't depend on UPower being present, just that the model
        // emits the contract.
        QCOMPARE(countSpy.count(), 1);

        model.setHost(nullptr);
        QCOMPARE(model.host(), nullptr);
        QCOMPARE(hostSpy.count(), 2);
    }

    void uPowerDeviceEnumValuesArePublicContract()
    {
        // The DeviceState / DeviceType enum values are wire constants
        // mirroring UPower's own enum — they must not change without
        // a coordinated upstream bump.
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::UnknownState), 0);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Charging), 1);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Discharging), 2);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Empty), 3);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::FullyCharged), 4);

        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::UnknownType), 0);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::LinePower), 1);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Battery), 2);
    }
};

QTEST_MAIN(TestSmoke)
#include "test_smoke.moc"
