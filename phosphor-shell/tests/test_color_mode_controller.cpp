// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ColorModeController.h"
#include <PhosphorTheme/AppearanceStore.h>
#include <QDBusContext>
#include <QDBusVariant>
#include <QTemporaryDir>
#include <QTest>

using PhosphorShellApp::ColorModeController;
using PhosphorTheme::AppearanceStore;
namespace {
const QString Service = QStringLiteral("org.phosphor.test.colorMode");
const QString Path = QStringLiteral("/org/freedesktop/portal/desktop");
}
class FakeColorPortal : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Settings")
public:
    uint scheme = 1;
    bool failRead = false;
    void change(uint value)
    {
        scheme = value;
        Q_EMIT SettingChanged(QStringLiteral("org.freedesktop.appearance"), QStringLiteral("color-scheme"),
                              QDBusVariant(value));
    }
public Q_SLOTS:
    QDBusVariant Read(const QString&, const QString&)
    {
        if (failRead) {
            sendErrorReply(QDBusError::Failed, QStringLiteral("Unavailable"));
            return {};
        }
        return QDBusVariant(QVariant::fromValue(QDBusVariant(scheme)));
    }
Q_SIGNALS:
    void SettingChanged(const QString& group, const QString& key, const QDBusVariant& value);
};
class TestColorModeController : public QObject
{
    Q_OBJECT
    FakeColorPortal portal;
    QDBusConnection bus = QDBusConnection::sessionBus();
private Q_SLOTS:
    void init()
    {
        portal.scheme = 1;
        portal.failRead = false;
        QVERIFY(bus.registerService(Service));
        QVERIFY(bus.registerObject(Path, &portal, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals));
    }
    void cleanup()
    {
        bus.unregisterObject(Path);
        bus.unregisterService(Service);
    }
    void manualModePreservesStyleAndDarkMaterial()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        QVERIFY(store.applyPreset(QStringLiteral("ember")));
        ColorModeController controller(&store, bus, Service);
        QTRY_VERIFY(controller.systemAvailable());
        const auto palette = store.values().value(QStringLiteral("palette"));
        QVERIFY(controller.setMode(QStringLiteral("light")));
        QCOMPARE(controller.mode(), QStringLiteral("light"));
        QCOMPARE(store.values().value(QStringLiteral("palette")), palette);
        QVERIFY(controller.setMode(QStringLiteral("dark")));
        QCOMPARE(store.values().value(QStringLiteral("material")).toString(), QStringLiteral("solid"));
        QVERIFY(controller.setMode(QStringLiteral("system")));
        portal.change(2);
        QTRY_VERIFY(!controller.effectiveDark());
        QVERIFY(store.setValue(QStringLiteral("material"), QStringLiteral("glass")));
        QCOMPARE(controller.mode(), QStringLiteral("dark"));
        portal.change(1);
        QTRY_VERIFY(controller.systemDark());
        portal.change(2);
        QTRY_VERIFY(!controller.systemDark());
        QCOMPARE(controller.mode(), QStringLiteral("dark"));
        QVERIFY(controller.effectiveDark());
        QVERIFY(!controller.setMode(QStringLiteral("invalid")));
    }
    void followSystemPersistsAndRecoversAfterPortalRestart()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("appearance.json"));
        AppearanceStore store(path);
        ColorModeController controller(&store, bus, Service);
        QTRY_VERIFY(controller.systemAvailable());
        QVERIFY(controller.setMode(QStringLiteral("system")));
        QVERIFY(AppearanceStore(path).values().value(QStringLiteral("followSystemColorScheme")).toBool());
        QVERIFY(bus.unregisterService(Service));
        QTRY_VERIFY(!controller.systemAvailable());
        QVERIFY(controller.effectiveDark());
        QVERIFY(!controller.error().isEmpty());
        portal.scheme = 2;
        QVERIFY(bus.registerService(Service));
        QTRY_VERIFY(controller.systemAvailable());
        QTRY_VERIFY(!controller.effectiveDark());
        QVERIFY(controller.error().isEmpty());
        portal.change(77); // Portal specifies unknown preference values as no preference.
        QTRY_VERIFY(!controller.systemDark());
    }
    void appearancePreviewDefersSystemChangesAndRestoresMode()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("appearance.json"));
        AppearanceStore store(path);
        ColorModeController controller(&store, bus, Service);
        QTRY_VERIFY(controller.systemAvailable());
        QVERIFY(controller.setMode(QStringLiteral("system")));
        QVERIFY(store.beginPreview());
        portal.change(2);
        QTRY_VERIFY(!controller.systemDark());
        QVERIFY(controller.effectiveDark());
        QVERIFY(!store.dirty());
        QVERIFY(controller.setMode(QStringLiteral("light")));
        QVERIFY(store.dirty());
        QVERIFY(AppearanceStore(path).values().value(QStringLiteral("followSystemColorScheme")).toBool());
        store.endPreview();
        QCOMPARE(controller.mode(), QStringLiteral("system"));
        QVERIFY(!controller.effectiveDark());
        QVERIFY(store.beginPreview());
        QVERIFY(controller.setMode(QStringLiteral("dark")));
        QVERIFY(store.applyPreview());
        store.endPreview();
        QCOMPARE(controller.mode(), QStringLiteral("dark"));
        QVERIFY(controller.effectiveDark());
        QVERIFY(!AppearanceStore(path).values().value(QStringLiteral("followSystemColorScheme")).toBool());
    }
    void unavailablePreferenceAllowsManualChoiceAndRetry()
    {
        portal.failRead = true;
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        ColorModeController controller(&store, bus, Service);
        QTRY_VERIFY(!controller.error().isEmpty());
        QVERIFY(!controller.systemAvailable());
        QVERIFY(!controller.setMode(QStringLiteral("system")));
        QVERIFY(controller.setMode(QStringLiteral("light")));
        portal.failRead = false;
        controller.refresh();
        QTRY_VERIFY(controller.systemAvailable());
        QCOMPARE(controller.mode(), QStringLiteral("light"));
        QVERIFY(controller.setMode(QStringLiteral("system")));
        QVERIFY(controller.effectiveDark());
    }
};
QTEST_GUILESS_MAIN(TestColorModeController)
#include "test_color_mode_controller.moc"
