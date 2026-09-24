// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "QuickSettingsController.h"

#include <QDBusMessage>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class NightLightFixture : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.NightLight")
    Q_PROPERTY(bool available MEMBER available)
    Q_PROPERTY(bool enabled MEMBER enabled)
public:
    bool available = true;
    bool enabled = false;
};
class CompositorFixture : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin")
public:
    NightLightFixture night;
    QString path;
    int calls = 0;
public Q_SLOTS:
    void reconfigure()
    {
        ++calls;
        QSettings config(path, QSettings::IniFormat);
        night.enabled = config.value(QStringLiteral("NightColor/Active"), false).toBool();
    }
};
class TestQuickSettingsController : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void usesServiceStateAndOnlyWritesOnRequest()
    {
        QProcess daemon;
        daemon.start(QStringLiteral("dbus-daemon"),
                     {QStringLiteral("--session"), QStringLiteral("--nofork"), QStringLiteral("--print-address=1")});
        QVERIFY(daemon.waitForReadyRead());
        const QString address = QString::fromUtf8(daemon.readLine()).trimmed();
        auto service = QDBusConnection::connectToBus(address, QStringLiteral("quick-settings-service"));
        auto client = QDBusConnection::connectToBus(address, QStringLiteral("quick-settings-client"));
        QVERIFY(service.isConnected());
        QTemporaryDir dir;
        CompositorFixture compositor;
        compositor.path = dir.filePath(QStringLiteral("kwinrc"));
        QVERIFY(service.registerObject(QStringLiteral("/org/kde/KWin/NightLight"), &compositor.night,
                                       QDBusConnection::ExportAllProperties));
        QVERIFY(service.registerObject(QStringLiteral("/KWin"), &compositor, QDBusConnection::ExportAllSlots));
        QVERIFY(service.registerService(QStringLiteral("org.kde.KWin")));
        {
            PhosphorShellApp::QuickSettingsController settings(client, client, compositor.path);
            QTRY_VERIFY(settings.nightLightAvailable());
            QVERIFY(!settings.nightLightEnabled());
            QVERIFY(!QFileInfo::exists(compositor.path));
            settings.toggleNightLight();
            settings.toggleNightLight(); // Ignore a second click while reconfiguration is pending.
            QTRY_VERIFY(settings.nightLightEnabled());
            QCOMPARE(compositor.calls, 1);
            QSettings saved(compositor.path, QSettings::IniFormat);
            QCOMPARE(saved.value(QStringLiteral("NightColor/Active")).toBool(), true);
            settings.toggleNightLight();
            QTRY_VERIFY(!settings.nightLightEnabled());
            QCOMPARE(compositor.calls, 2);
            service.unregisterService(QStringLiteral("org.kde.KWin"));
            QTRY_VERIFY(!settings.nightLightAvailable());
            settings.toggleNightLight();
            QCOMPARE(compositor.calls, 2);
        }
        QDBusConnection::disconnectFromBus(QStringLiteral("quick-settings-client"));
        QDBusConnection::disconnectFromBus(QStringLiteral("quick-settings-service"));
        daemon.terminate();
        QVERIFY(daemon.waitForFinished());
    }
};
QTEST_GUILESS_MAIN(TestQuickSettingsController)
#include "test_quick_settings_controller.moc"
