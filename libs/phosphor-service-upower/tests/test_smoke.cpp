// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

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
        // CI runners have no UPower daemon, so both old and new row
        // sets are empty. countChanged should NOT fire on a 0 -> 0
        // transition per the "only emit on change" contract.
        QCOMPARE(countSpy.count(), 0);

        model.setHost(nullptr);
        QCOMPARE(model.host(), nullptr);
        QCOMPARE(hostSpy.count(), 2);
        QCOMPARE(countSpy.count(), 0);
    }

    void modelSurvivesHostDestruction()
    {
        // Pins the destroyed-lambda installed by setHost. Without it,
        // a host destroyed while still bound to a model would leave
        // m_host dangling and the next data()/rowCount() would crash.
        // The lambda must reset m_host to nullptr and clear m_rows
        // inside a beginResetModel / endResetModel pair so QML views
        // observe a clean detach.
        PhosphorServiceUPower::UPowerDeviceModel model;
        QSignalSpy hostSpy(&model, &PhosphorServiceUPower::UPowerDeviceModel::hostChanged);
        QSignalSpy countSpy(&model, &PhosphorServiceUPower::UPowerDeviceModel::countChanged);
        {
            PhosphorServiceUPower::UPowerHost host;
            model.setHost(&host);
            QCOMPARE(model.host(), &host);
        }
        QCOMPARE(model.host(), nullptr);
        QCOMPARE(model.rowCount(), 0);
        // setHost (attach) emitted hostChanged once; destroyed-lambda
        // emits hostChanged again on auto-detach. Both transitions
        // are 0 -> 0 row count so countChanged stays silent.
        QCOMPARE(hostSpy.count(), 2);
        QCOMPARE(countSpy.count(), 0);
    }

    void uPowerDeviceEnumValuesArePublicContract()
    {
        // The DeviceState / DeviceType enum values are wire constants
        // mirroring UPower's own enum; they must not change without a
        // coordinated upstream bump. Pin every enumerator: the prior
        // partial pin would have missed a renumber that started past
        // the last asserted value.
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::UnknownState), 0);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Charging), 1);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Discharging), 2);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Empty), 3);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::FullyCharged), 4);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::PendingCharge), 5);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::PendingDischarge), 6);

        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::UnknownType), 0);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::LinePower), 1);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Battery), 2);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Ups), 3);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Monitor), 4);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Mouse), 5);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Keyboard), 6);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Pda), 7);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Phone), 8);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::MediaPlayer), 9);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Tablet), 10);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Computer), 11);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::GamingInput), 12);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Pen), 13);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Touchpad), 14);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Modem), 15);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Network), 16);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Headset), 17);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Speakers), 18);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Headphones), 19);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Video), 20);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::OtherAudio), 21);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::RemoteControl), 22);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Printer), 23);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Scanner), 24);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Camera), 25);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Wearable), 26);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::Toy), 27);
        QCOMPARE(static_cast<int>(PhosphorServiceUPower::UPowerDevice::BluetoothGeneric), 28);
    }
};

QTEST_MAIN(TestSmoke)
#include "test_smoke.moc"
