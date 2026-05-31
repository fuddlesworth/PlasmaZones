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

#include <cmath>
#include <limits>

using namespace PhosphorServiceBrightness;

namespace {
constexpr auto kDummySession = "/fake/session"; // non-empty: skips async resolve

// Sink for capturing Qt warnings during the idempotency test: a second
// registration that bypassed the std::call_once guard would re-run
// qmlRegisterType and make Qt warn. A function-pointer handler cannot capture,
// so route through this file-scope pointer.
QStringList* g_warningSink = nullptr;
void captureWarnings(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    if (g_warningSink && (type == QtWarningMsg || type == QtCriticalMsg))
        g_warningSink->append(msg);
}

void writeFile(const QString& path, const QString& content)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    file.write(content.toUtf8());
}

void makeBacklight(const QString& root, const QString& name, int brightness, int max)
{
    // Only brightness + max_brightness are read by the lib (the live value comes
    // from the writable brightness attribute); no actual_brightness is needed.
    const QString dir = root + QStringLiteral("/class/backlight/") + name;
    writeFile(dir + QStringLiteral("/brightness"), QString::number(brightness));
    writeFile(dir + QStringLiteral("/max_brightness"), QString::number(max));
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
        // This is the first call site in the binary, so the first call runs the
        // real registration and the second must be a std::call_once no-op. Pin
        // that by asserting the second pass emits no Qt registration warning
        // (without the guard, the repeat qmlRegisterType warns).
        QStringList warnings;
        g_warningSink = &warnings;
        QtMessageHandler previous = qInstallMessageHandler(captureWarnings);
        registerQmlTypes();
        registerQmlTypes();
        qInstallMessageHandler(previous);
        g_warningSink = nullptr;
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
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

    void testKeyboardBacklightNameFiltering()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        // Valid: a device-prefixed kbd_backlight (the function segment after the
        // last ':' equals kbd_backlight).
        makeLed(root.path(), QStringLiteral("tpacpi::kbd_backlight"), 1, 2);
        // Rejected: a bare kbd_backlight with no colon-delimited prefix.
        makeLed(root.path(), QStringLiteral("kbd_backlight"), 1, 2);
        // Rejected: the function segment must equal kbd_backlight exactly, not
        // merely start with it (suffix mismatch).
        makeLed(root.path(), QStringLiteral("dell::kbd_backlight_rgb"), 1, 2);

        BrightnessHost host(root.path(), QDBusConnection::sessionBus(), QStringLiteral("org.example.Logind"),
                            QLatin1String(kDummySession));
        QCOMPARE(host.deviceCount(), 1);
        QVERIFY(findByName(host, QStringLiteral("tpacpi::kbd_backlight")) != nullptr);
        QVERIFY(findByName(host, QStringLiteral("kbd_backlight")) == nullptr);
        QVERIFY(findByName(host, QStringLiteral("dell::kbd_backlight_rgb")) == nullptr);
    }

    void testSessionPathActivatesWrites()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected())
            QSKIP("no session bus available");
        const QString service = QStringLiteral("org.phosphor.test.LateLogind");
        if (!bus.registerService(service))
            QSKIP("could not own the test service name");
        const QString sessionPath = QStringLiteral("/org/phosphor/test/late_session");

        FakeLogindSession fake;
        QVERIFY(bus.registerObject(sessionPath, &fake, QDBusConnection::ExportAllSlots));

        QTemporaryDir root;
        QVERIFY(root.isValid());
        makeBacklight(root.path(), QStringLiteral("intel_backlight"), 100, 255);

        // Empty session: the write must be inert until a session is bound. This
        // is the reason BrightnessHost::resolveSession exists; exercise it via
        // the public BrightnessDevice ctor to avoid a real logind lookup.
        BrightnessDevice device(bus, service, QString(), BrightnessDevice::Display, QStringLiteral("intel_backlight"),
                                root.path() + QStringLiteral("/class/backlight/intel_backlight"));
        device.setBrightness(200);
        // Give any (erroneous) dispatch a window to land, then assert it did not.
        QTest::qWait(50);
        QCOMPARE(fake.lastValue, 0U);

        // Bind the session (what resolveSession does on the async reply) and
        // confirm the next write goes live.
        device.setSessionPath(sessionPath);
        device.setBrightness(200);
        QTRY_COMPARE(fake.lastValue, 200U);
        QCOMPARE(fake.lastName, QStringLiteral("intel_backlight"));
        QCOMPARE(fake.lastSubsystem, QStringLiteral("backlight"));

        // Clearing the session path makes writes inert again (the same empty-path
        // guard the host relies on before a session resolves / when none is bound).
        device.setSessionPath(QString());
        fake.lastValue = 0;
        device.setBrightness(150);
        QTest::qWait(50);
        QCOMPARE(fake.lastValue, 0U);

        bus.unregisterObject(sessionPath);
        bus.unregisterService(service);
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

    void testEmptySysfsRootEnumeratesNothing()
    {
        // The host is inert (empty device list, no crash) when its sysfs root has
        // no class/backlight or class/leds subtree at all (a documented
        // BrightnessHost contract); no logind lookup is needed either.
        QTemporaryDir root;
        QVERIFY(root.isValid());
        BrightnessHost host(root.path(), QDBusConnection::sessionBus(), QStringLiteral("org.example.Logind"),
                            QLatin1String(kDummySession));
        QCOMPARE(host.deviceCount(), 0);
        QVERIFY(host.deviceAt(0) == nullptr);
        QVERIFY(host.devices().isEmpty());
    }

    void testMalformedSysfsValueFallsBack()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        // A non-numeric brightness attribute must not crash: readSysfsInt falls
        // back (the ctor's fallback is 0) while a valid max_brightness still reads.
        const QString dir = root.path() + QStringLiteral("/class/backlight/intel_backlight");
        writeFile(dir + QStringLiteral("/brightness"), QStringLiteral("not-a-number"));
        writeFile(dir + QStringLiteral("/max_brightness"), QStringLiteral("255"));

        BrightnessHost host(root.path(), QDBusConnection::sessionBus(), QStringLiteral("org.example.Logind"),
                            QLatin1String(kDummySession));
        auto* display = findByName(host, QStringLiteral("intel_backlight"));
        QVERIFY(display);
        QCOMPARE(display->brightness(), 0);
        QCOMPARE(display->maxBrightness(), 255);
        QCOMPARE(display->percentage(), 0.0);
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
        // Writes against a zero-max device must be inert (the maxBrightness <= 0
        // guard returns early), not crash and not move the cached value.
        device->setBrightness(100);
        device->setPercentage(0.5);
        QCOMPARE(device->brightness(), 0);
        QCOMPARE(device->percentage(), 0.0);

        // The same maxBrightness <= 0 guard protects an external (DDC) device:
        // applyExternalValue against a zero-max device must neither emit nor move
        // the cached value.
        BrightnessDevice external(QStringLiteral("i2c-0"), QStringLiteral("Zero"), 0, 0, [](int) { });
        QSignalSpy spy(&external, &BrightnessDevice::brightnessChanged);
        external.applyExternalValue(50);
        QCOMPARE(spy.count(), 0);
        QCOMPARE(external.brightness(), 0);
        QCOMPARE(external.percentage(), 0.0);
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
        makeLed(root.path(), QStringLiteral("dell::kbd_backlight"), 1, 2);

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

        // A keyboard backlight must write through the "leds" subsystem, not
        // "backlight" (the subsystem() mapping branch).
        auto* keyboard = findByName(host, QStringLiteral("dell::kbd_backlight"));
        QVERIFY(keyboard);
        QCOMPARE(keyboard->kind(), BrightnessDevice::Keyboard);
        keyboard->setBrightness(2);
        QTRY_COMPARE(fake.lastName, QStringLiteral("dell::kbd_backlight"));
        QCOMPARE(fake.lastSubsystem, QStringLiteral("leds"));
        QCOMPARE(fake.lastValue, 2U);

        bus.unregisterObject(sessionPath);
        bus.unregisterService(service);
    }

    void testExternalDisplayDeviceRoutesAndUpdates()
    {
        // External-display (DDC/CI) device path, exercised without libddcutil /
        // hardware: setBrightness routes the clamped value to the host-supplied
        // setter (no optimistic update), and applyExternalValue pushes the
        // worker's read-back, emitting on change.
        int lastSet = -1;
        BrightnessDevice device(QStringLiteral("i2c-7"), QStringLiteral("LG Ultra HD"), 60, 100, [&lastSet](int value) {
            lastSet = value;
        });
        QCOMPARE(device.kind(), BrightnessDevice::ExternalDisplay);
        // The id is the stable unique key; the name is the (non-unique) model.
        QCOMPARE(device.id(), QStringLiteral("i2c-7"));
        QCOMPARE(device.name(), QStringLiteral("LG Ultra HD"));
        QCOMPARE(device.brightness(), 60);
        QCOMPARE(device.maxBrightness(), 100);
        QCOMPARE(device.percentage(), 0.6);

        device.setBrightness(150); // clamps to maxBrightness
        QCOMPARE(lastSet, 100);
        QCOMPARE(device.brightness(), 60); // unchanged until the read-back lands

        QSignalSpy spy(&device, &BrightnessDevice::brightnessChanged);
        device.applyExternalValue(80);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(device.brightness(), 80);
        QCOMPARE(device.percentage(), 0.8);

        // A read-back equal to the cached value must not re-emit.
        device.applyExternalValue(80);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(device.brightness(), 80);

        // A read-back above maxBrightness is clamped to the range.
        device.applyExternalValue(500);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(device.brightness(), 100);

        device.setPercentage(0.5); // 50 of 100, routed through the setter
        QCOMPARE(lastSet, 50);

        // Non-finite percentages are rejected before qRound(NaN) UB; the setter
        // is not invoked, so the last routed value stays 50.
        device.setPercentage(std::nan(""));
        QCOMPARE(lastSet, 50);
        device.setPercentage(std::numeric_limits<double>::infinity());
        QCOMPARE(lastSet, 50);

        // A negative raw value clamps to the floor of the range (std::clamp lower
        // bound); the floor, not the unclamped input, reaches the setter.
        device.setBrightness(-5);
        QCOMPARE(lastSet, 0);

        // refresh() only re-reads sysfs; for an external display it is a no-op
        // (values arrive via applyExternalValue), so it must neither change the
        // cached value nor emit.
        QSignalSpy refreshSpy(&device, &BrightnessDevice::brightnessChanged);
        device.refresh();
        QCOMPARE(refreshSpy.count(), 0);
        QCOMPARE(device.brightness(), 100);
    }

    void testModelDataGuardsReturnInvalid()
    {
        // data() must return an invalid QVariant for an invalid index, an
        // out-of-range row, and an unknown role (the defensive guards in data()).
        QTemporaryDir root;
        QVERIFY(root.isValid());
        makeBacklight(root.path(), QStringLiteral("intel_backlight"), 100, 200);
        BrightnessHost host(root.path(), QDBusConnection::sessionBus(), QStringLiteral("org.example.Logind"),
                            QLatin1String(kDummySession));
        BrightnessDeviceModel model;
        model.setHost(&host);
        QCOMPARE(model.rowCount(), 1);

        // Invalid index, and an out-of-range row (index() itself returns invalid).
        QVERIFY(!model.data(QModelIndex(), BrightnessDeviceModel::NameRole).isValid());
        QVERIFY(!model.data(model.index(99), BrightnessDeviceModel::NameRole).isValid());
        // Unknown role on a valid index hits the switch default branch.
        QVERIFY(!model.data(model.index(0), Qt::UserRole + 9999).isValid());
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
        QCOMPARE(roles.value(BrightnessDeviceModel::IdRole), QByteArrayLiteral("id"));
        QCOMPARE(roles.value(BrightnessDeviceModel::NameRole), QByteArrayLiteral("name"));
        QCOMPARE(roles.value(BrightnessDeviceModel::PercentageRole), QByteArrayLiteral("percentage"));
        QCOMPARE(roles.size(), 7);

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
        // For sysfs devices the id is the sysfs name, so it doubles as the key.
        QCOMPARE(model.data(idx, BrightnessDeviceModel::IdRole).toString(), QStringLiteral("intel_backlight"));
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

    void testModelTracksDeviceAddedAndRemoved()
    {
        // Sysfs devices are present at bind, but external displays arrive
        // asynchronously via deviceAdded / deviceRemoved. Drive those signals
        // (they are public) to pin the model's insert / remove transactions and
        // the countChanged emissions independently of any DDC hardware.
        QTemporaryDir root;
        QVERIFY(root.isValid());
        makeBacklight(root.path(), QStringLiteral("intel_backlight"), 100, 200);
        BrightnessHost host(root.path(), QDBusConnection::sessionBus(), QStringLiteral("org.example.Logind"),
                            QLatin1String(kDummySession));
        BrightnessDeviceModel model;
        model.setHost(&host);
        QCOMPARE(model.rowCount(), 1);

        QSignalSpy countSpy(&model, &BrightnessDeviceModel::countChanged);
        QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removeSpy(&model, &QAbstractItemModel::rowsRemoved);

        BrightnessDevice external(QStringLiteral("i2c-9"), QStringLiteral("External"), 50, 100, [](int) { });
        Q_EMIT host.deviceAdded(&external);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(insertSpy.count(), 1);
        QCOMPARE(countSpy.count(), 1);
        const int addedRow = model.rowCount() - 1;
        QCOMPARE(model.data(model.index(addedRow), BrightnessDeviceModel::IdRole).toString(), QStringLiteral("i2c-9"));

        // A duplicate add is ignored (no second row, no extra signal).
        Q_EMIT host.deviceAdded(&external);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(insertSpy.count(), 1);

        Q_EMIT host.deviceRemoved(&external);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(removeSpy.count(), 1);
        QCOMPARE(countSpy.count(), 2);

        // Removing an unknown device is a no-op.
        Q_EMIT host.deviceRemoved(&external);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(removeSpy.count(), 1);
    }

    void testModelClearsWhenHostDestroyed()
    {
        // The model holds host-owned device pointers; if the host is destroyed
        // while bound, the QObject::destroyed handler must clear the rows so the
        // model never dereferences a dangling device.
        QTemporaryDir root;
        QVERIFY(root.isValid());
        makeBacklight(root.path(), QStringLiteral("intel_backlight"), 100, 200);
        auto* host = new BrightnessHost(root.path(), QDBusConnection::sessionBus(),
                                        QStringLiteral("org.example.Logind"), QLatin1String(kDummySession));
        BrightnessDeviceModel model;
        model.setHost(host);
        QCOMPARE(model.rowCount(), 1);

        QSignalSpy countSpy(&model, &BrightnessDeviceModel::countChanged);
        delete host;
        QCOMPARE(model.host(), nullptr);
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(countSpy.count(), 1);
    }

    void testModelRebindsToNewHost()
    {
        // setHost() while already bound must disconnect the old host and its
        // devices and load the new host's rows, so stale signals from the old
        // host no longer reach the model while the new host's do.
        QTemporaryDir rootA;
        QVERIFY(rootA.isValid());
        makeBacklight(rootA.path(), QStringLiteral("intel_backlight"), 100, 200);
        BrightnessHost hostA(rootA.path(), QDBusConnection::sessionBus(), QStringLiteral("org.example.Logind"),
                             QLatin1String(kDummySession));

        QTemporaryDir rootB;
        QVERIFY(rootB.isValid());
        makeBacklight(rootB.path(), QStringLiteral("edp"), 50, 100);
        makeLed(rootB.path(), QStringLiteral("tpacpi::kbd_backlight"), 1, 2);
        BrightnessHost hostB(rootB.path(), QDBusConnection::sessionBus(), QStringLiteral("org.example.Logind"),
                             QLatin1String(kDummySession));

        BrightnessDeviceModel model;
        model.setHost(&hostA);
        QCOMPARE(model.rowCount(), 1);

        model.setHost(&hostB);
        QCOMPARE(model.host(), &hostB);
        QCOMPARE(model.rowCount(), 2);

        // A deviceAdded from the OLD host must no longer reach the model.
        BrightnessDevice straggler(QStringLiteral("i2c-1"), QStringLiteral("Old"), 10, 100, [](int) { });
        Q_EMIT hostA.deviceAdded(&straggler);
        QCOMPARE(model.rowCount(), 2);

        // A deviceAdded from the NEW host is tracked.
        BrightnessDevice fresh(QStringLiteral("i2c-2"), QStringLiteral("New"), 20, 100, [](int) { });
        Q_EMIT hostB.deviceAdded(&fresh);
        QCOMPARE(model.rowCount(), 3);
    }
};

QTEST_GUILESS_MAIN(TestPhosphorServiceBrightnessSmoke)
#include "test_smoke.moc"
