// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceBrightness/BrightnessDevice.h>
#include <PhosphorServiceBrightness/BrightnessDeviceModel.h>
#include <PhosphorServiceBrightness/BrightnessHost.h>
#include <PhosphorServiceBrightness/QmlRegistration.h>

#include <QAbstractItemModel>
#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace PhosphorServiceBrightness;

namespace {
constexpr auto kDummySession = "/fake/session"; // non-empty: skips async resolve

void writeFile(const QString& path, const QString& content)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    file.write(content.toUtf8());
}

void makeBacklight(const QString& root, const QString& name, int brightness, int max)
{
    const QString dir = root + QStringLiteral("/class/backlight/") + name;
    writeFile(dir + QStringLiteral("/brightness"), QString::number(brightness));
    writeFile(dir + QStringLiteral("/max_brightness"), QString::number(max));
    writeFile(dir + QStringLiteral("/actual_brightness"), QString::number(brightness));
}

void makeLed(const QString& root, const QString& name, int brightness, int max)
{
    const QString dir = root + QStringLiteral("/class/leds/") + name;
    writeFile(dir + QStringLiteral("/brightness"), QString::number(brightness));
    writeFile(dir + QStringLiteral("/max_brightness"), QString::number(max));
}

BrightnessDevice* findByName(const BrightnessHost& host, const QString& name)
{
    const auto devices = host.devices();
    for (auto* device : devices) {
        if (device->name() == name)
            return device;
    }
    return nullptr;
}
} // namespace

// In-process fake logind session recording SetBrightness, so the write path can
// be exercised over a session bus without a real org.freedesktop.login1.
class FakeLogindSession : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.login1.Session")

public:
    QString lastSubsystem;
    QString lastName;
    uint lastValue = 0;

public Q_SLOTS:
    void SetBrightness(const QString& subsystem, const QString& name, uint value)
    {
        lastSubsystem = subsystem;
        lastName = name;
        lastValue = value;
    }
};

class TestPhosphorServiceBrightnessSmoke : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testRegisterQmlTypesIsIdempotent()
    {
        registerQmlTypes();
        registerQmlTypes();
        QVERIFY(true);
    }

    void testEnumeratesBacklightAndKbdBacklightOnly()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        makeBacklight(root.path(), QStringLiteral("intel_backlight"), 120, 255);
        makeLed(root.path(), QStringLiteral("tpacpi::kbd_backlight"), 1, 2);
        // Indicator LEDs must be excluded: not a brightness control.
        makeLed(root.path(), QStringLiteral("input3::capslock"), 0, 1);
        makeLed(root.path(), QStringLiteral("platform::mute"), 0, 1);

        BrightnessHost host(root.path(), QDBusConnection::sessionBus(), QStringLiteral("org.example.Logind"),
                            QLatin1String(kDummySession));
        QCOMPARE(host.deviceCount(), 2);

        auto* display = findByName(host, QStringLiteral("intel_backlight"));
        auto* keyboard = findByName(host, QStringLiteral("tpacpi::kbd_backlight"));
        QVERIFY(display);
        QVERIFY(keyboard);
        QCOMPARE(display->kind(), BrightnessDevice::Display);
        QCOMPARE(keyboard->kind(), BrightnessDevice::Keyboard);
        QVERIFY(findByName(host, QStringLiteral("input3::capslock")) == nullptr);
        QVERIFY(findByName(host, QStringLiteral("platform::mute")) == nullptr);
    }

    void testReadSurfaceAndPercentage()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        makeBacklight(root.path(), QStringLiteral("intel_backlight"), 120, 240);

        BrightnessHost host(root.path(), QDBusConnection::sessionBus(), QStringLiteral("org.example.Logind"),
                            QLatin1String(kDummySession));
        auto* display = findByName(host, QStringLiteral("intel_backlight"));
        QVERIFY(display);
        QCOMPARE(display->brightness(), 120);
        QCOMPARE(display->maxBrightness(), 240);
        QCOMPARE(display->percentage(), 0.5);

        // deviceAt bounds
        QVERIFY(host.deviceAt(-1) == nullptr);
        QVERIFY(host.deviceAt(0) != nullptr);
        QVERIFY(host.deviceAt(99) == nullptr);
    }

    void testZeroMaxBrightnessDoesNotDivideByZero()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        makeBacklight(root.path(), QStringLiteral("broken"), 0, 0);
        BrightnessHost host(root.path(), QDBusConnection::sessionBus(), QStringLiteral("org.example.Logind"),
                            QLatin1String(kDummySession));
        auto* device = findByName(host, QStringLiteral("broken"));
        QVERIFY(device);
        QCOMPARE(device->percentage(), 0.0);
        device->setBrightness(100); // must no-op, not crash
        device->setPercentage(0.5);
        QVERIFY(true);
    }

    void testLiveWatcherTracksExternalChange()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        makeBacklight(root.path(), QStringLiteral("intel_backlight"), 100, 255);

        BrightnessHost host(root.path(), QDBusConnection::sessionBus(), QStringLiteral("org.example.Logind"),
                            QLatin1String(kDummySession));
        auto* display = findByName(host, QStringLiteral("intel_backlight"));
        QVERIFY(display);
        QCOMPARE(display->brightness(), 100);

        QSignalSpy spy(display, &BrightnessDevice::brightnessChanged);
        // Simulate a hardware-key / external change to the sysfs attribute.
        writeFile(root.path() + QStringLiteral("/class/backlight/intel_backlight/brightness"), QStringLiteral("200"));
        QVERIFY(spy.wait(3000));
        QCOMPARE(display->brightness(), 200);
    }

    void testSetBrightnessRoutesThroughLogind()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected())
            QSKIP("no session bus available");
        const QString service = QStringLiteral("org.phosphor.test.Logind");
        if (!bus.registerService(service))
            QSKIP("could not own the test service name");
        const QString sessionPath = QStringLiteral("/org/phosphor/test/session");

        FakeLogindSession fake;
        QVERIFY(bus.registerObject(sessionPath, &fake, QDBusConnection::ExportAllSlots));

        QTemporaryDir root;
        QVERIFY(root.isValid());
        makeBacklight(root.path(), QStringLiteral("intel_backlight"), 100, 255);

        BrightnessHost host(root.path(), bus, service, sessionPath);
        auto* display = findByName(host, QStringLiteral("intel_backlight"));
        QVERIFY(display);

        // Fire-and-forget over the bus; QTRY polls the fake's recorded state
        // (the in-process call may dispatch before a QSignalSpy::wait window).
        display->setBrightness(200);
        QTRY_COMPARE(fake.lastValue, 200U);
        QCOMPARE(fake.lastSubsystem, QStringLiteral("backlight"));
        QCOMPARE(fake.lastName, QStringLiteral("intel_backlight"));

        // Percentage write clamps + rounds against maxBrightness (255).
        display->setPercentage(1.0);
        QTRY_COMPARE(fake.lastValue, 255U);

        bus.unregisterObject(sessionPath);
        bus.unregisterService(service);
    }

    void testModelMirrorsHostAndForwardsChanges()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        makeBacklight(root.path(), QStringLiteral("intel_backlight"), 100, 200);
        makeLed(root.path(), QStringLiteral("tpacpi::kbd_backlight"), 1, 2);

        BrightnessHost host(root.path(), QDBusConnection::sessionBus(), QStringLiteral("org.example.Logind"),
                            QLatin1String(kDummySession));
        BrightnessDeviceModel model;
        model.setHost(&host);
        QCOMPARE(model.host(), &host);
        QCOMPARE(model.rowCount(), 2);

        // Role names + data on the display row.
        const auto roles = model.roleNames();
        QCOMPARE(roles.value(BrightnessDeviceModel::NameRole), QByteArrayLiteral("name"));
        QCOMPARE(roles.value(BrightnessDeviceModel::PercentageRole), QByteArrayLiteral("percentage"));
        QCOMPARE(roles.size(), 6);

        int displayRow = -1;
        for (int i = 0; i < model.rowCount(); ++i) {
            if (model.data(model.index(i), BrightnessDeviceModel::NameRole).toString()
                == QStringLiteral("intel_backlight")) {
                displayRow = i;
                break;
            }
        }
        QVERIFY(displayRow >= 0);
        const QModelIndex idx = model.index(displayRow);
        QCOMPARE(model.data(idx, BrightnessDeviceModel::BrightnessRole).toInt(), 100);
        QCOMPARE(model.data(idx, BrightnessDeviceModel::MaxBrightnessRole).toInt(), 200);
        QCOMPARE(model.data(idx, BrightnessDeviceModel::PercentageRole).toDouble(), 0.5);
        QVERIFY(model.data(idx, BrightnessDeviceModel::DeviceRole).value<QObject*>() != nullptr);

        // A live external sysfs change is forwarded as dataChanged on that row.
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        writeFile(root.path() + QStringLiteral("/class/backlight/intel_backlight/brightness"), QStringLiteral("160"));
        QVERIFY(changed.wait(3000));
        QCOMPARE(model.data(idx, BrightnessDeviceModel::BrightnessRole).toInt(), 160);
    }
};

QTEST_GUILESS_MAIN(TestPhosphorServiceBrightnessSmoke)
#include "test_smoke.moc"
