// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorServiceUPower/UPowerHost.h>
#include <PhosphorServiceUPower/UPowerDeviceModel.h>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QTest>

namespace {
const QString Service = QStringLiteral("org.phosphor.test.UPower");
const QString HostPath = QStringLiteral("/org/freedesktop/UPower");
const QString BatteryPath = QStringLiteral("/org/freedesktop/UPower/devices/battery_BAT0");
}
class FakeBattery : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.UPower.Device")
    Q_PROPERTY(double Percentage MEMBER percentage)
    Q_PROPERTY(uint State MEMBER state)
    Q_PROPERTY(uint Type MEMBER type)
    Q_PROPERTY(bool IsPresent MEMBER present)
    Q_PROPERTY(bool PowerSupply MEMBER supply)
    Q_PROPERTY(double Energy MEMBER energy)
    Q_PROPERTY(double EnergyFull MEMBER capacity)
    Q_PROPERTY(double EnergyFullDesign MEMBER design)
    Q_PROPERTY(qlonglong TimeToEmpty MEMBER remaining)
public:
    double percentage = 82;
    uint state = 2;
    uint type = 2;
    bool present = true;
    bool supply = true;
    double energy = 41;
    double capacity = 50;
    double design = 55;
    qlonglong remaining = 21600;
};
class FakeUPower : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.UPower")
    Q_PROPERTY(bool OnBattery MEMBER onBattery)
public:
    bool onBattery = true;
    bool hasBattery = true;
public Q_SLOTS:
    QDBusObjectPath GetDisplayDevice()
    {
        return QDBusObjectPath(hasBattery ? BatteryPath : QStringLiteral("/"));
    }
    QList<QDBusObjectPath> EnumerateDevices()
    {
        return hasBattery ? QList<QDBusObjectPath>{QDBusObjectPath(BatteryPath)} : QList<QDBusObjectPath>{};
    }
};
class TestUPowerHost : public QObject
{
    Q_OBJECT
    QDBusConnection bus = QDBusConnection::sessionBus();
    FakeUPower daemon;
    FakeBattery battery;
private Q_SLOTS:
    void init()
    {
        qDBusRegisterMetaType<QList<QDBusObjectPath>>();
        daemon.hasBattery = true;
        battery.percentage = 82;
        QVERIFY(bus.registerService(Service));
        QVERIFY(bus.registerObject(HostPath, &daemon,
                                   QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties));
        QVERIFY(bus.registerObject(BatteryPath, &battery, QDBusConnection::ExportAllProperties));
    }
    void cleanup()
    {
        bus.unregisterObject(HostPath, QDBusConnection::UnregisterTree);
        bus.unregisterService(Service);
    }
    void realPropertiesAndRestartReplaceStaleReadings()
    {
        PhosphorServiceUPower::UPowerHost host(bus, Service);
        PhosphorServiceUPower::UPowerDeviceModel model;
        model.setHost(&host);
        QTRY_VERIFY(host.available());
        QTRY_VERIFY(host.displayDevice() && host.displayDevice()->isPresent());
        QTRY_COMPARE(host.displayDevice()->percentage(), 82.0);
        QTRY_COMPARE(model.rowCount(), 1);
        QCOMPARE(host.displayDevice()->timeToEmpty(), 21600.0);
        QVERIFY(host.displayDevice()->healthPercentage() > 90);
        QVERIFY(host.onBattery());
        QVERIFY(bus.unregisterService(Service));
        QTRY_VERIFY(!host.available());
        QVERIFY(!host.displayDevice());
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!host.onBattery());
        battery.percentage = 35;
        QVERIFY(bus.registerService(Service));
        QTRY_VERIFY(host.available());
        QTRY_VERIFY(host.displayDevice() && host.displayDevice()->percentage() == 35);
        QTRY_COMPARE(model.rowCount(), 1);
    }
    void noBatteryIsAvailableServiceWithNoDevice()
    {
        daemon.hasBattery = false;
        PhosphorServiceUPower::UPowerHost host(bus, Service);
        QTRY_VERIFY(host.available());
        QVERIFY(!host.pending());
        QVERIFY(host.error().isEmpty());
        QVERIFY(!host.displayDevice());
        QCOMPARE(host.deviceCount(), 0);
    }
    void missingServiceCanBeRetried()
    {
        QVERIFY(bus.unregisterService(Service));
        PhosphorServiceUPower::UPowerHost host(bus, Service);
        QTRY_VERIFY(!host.pending());
        QVERIFY(!host.available());
        QVERIFY(!host.error().isEmpty());
        QVERIFY(bus.registerService(Service));
        host.refresh();
        QTRY_VERIFY(host.available());
        QVERIFY(host.error().isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestUPowerHost)
#include "test_upower_host.moc"
