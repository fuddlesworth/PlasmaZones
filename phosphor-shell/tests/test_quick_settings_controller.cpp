// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "QuickSettingsController.h"

#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTime>

#include <memory>
#include <utility>

namespace {
const QString compositorService = QStringLiteral("org.kde.KWin");
const QString nightPath = QStringLiteral("/org/kde/KWin/NightLight");
const QString nightInterface = QStringLiteral("org.kde.KWin.NightLight");
const QString scheduleService = QStringLiteral("org.kde.NightTime");
const QString schedulePath = QStringLiteral("/org/kde/NightTime/Manager");
const QString powerService = QStringLiteral("org.freedesktop.UPower.PowerProfiles");
const QString powerPath = QStringLiteral("/org/freedesktop/UPower/PowerProfiles");
const QString propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");

class QuickSettingsFixture : public QDBusVirtualObject
{
public:
    explicit QuickSettingsFixture(QDBusConnection connection, QString configPath)
        : bus(std::move(connection))
        , path(std::move(configPath))
    {
    }

    QDBusConnection bus;
    QString path;
    QVariantMap night{{QStringLiteral("available"), true},
                      {QStringLiteral("enabled"), false},
                      {QStringLiteral("running"), false},
                      {QStringLiteral("inhibited"), false},
                      {QStringLiteral("scheduledTransitionDateTime"), qint64(0)}};
    QString activeProfile = QStringLiteral("balanced");
    QStringList profiles{QStringLiteral("power-saver"), QStringLiteral("balanced"), QStringLiteral("performance")};
    QString degradation;
    uint scheduleVersion = 1;
    int reconfigureCalls = 0;
    int inhibitCalls = 0;
    int uninhibitCalls = 0;
    uint nextCookie = 41;
    QList<uint> releasedCookies;
    QStringList profileWrites;
    bool refusePowerWrite = false;
    bool refuseReconfigure = false;
    bool refuseInhibit = false;
    bool refuseUninhibit = false;
    bool delayPowerWrites = false;
    bool delayPowerReads = false;
    bool delayInhibit = false;
    QList<QDBusMessage> heldWrites;
    QList<QDBusMessage> heldReads;
    QList<QVariantMap> heldReadValues;
    QList<QDBusMessage> heldInhibits;

    QString introspect(const QString&) const override
    {
        return {};
    }

    QVariantMap powerValues() const
    {
        QList<QVariantMap> rows;
        for (const auto& id : profiles)
            rows.append({{QStringLiteral("Profile"), id}, {QStringLiteral("Driver"), QStringLiteral("fixture")}});
        return {{QStringLiteral("ActiveProfile"), activeProfile},
                {QStringLiteral("Profiles"), QVariant::fromValue(rows)},
                {QStringLiteral("PerformanceDegraded"), degradation}};
    }

    void changed(const QString& path, const QString& interface)
    {
        auto message = QDBusMessage::createSignal(path, propertiesInterface, QStringLiteral("PropertiesChanged"));
        message << interface << QVariantMap{} << QStringList{};
        bus.send(message);
    }

    void finishPowerWrite(const QDBusMessage& message, bool refuse)
    {
        if (refuse) {
            bus.send(message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.AccessDenied"),
                                              QStringLiteral("Fixture refused the profile")));
            return;
        }
        activeProfile = qvariant_cast<QDBusVariant>(message.arguments().at(2)).variant().toString();
        bus.send(message.createReply());
        changed(powerPath, powerService);
    }

    void finishInhibit(const QDBusMessage& message)
    {
        if (refuseInhibit) {
            bus.send(message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.Failed"),
                                              QStringLiteral("Fixture refused inhibition")));
            return;
        }
        night.insert(QStringLiteral("inhibited"), true);
        bus.send(message.createReply(QVariantList{nextCookie++}));
        changed(nightPath, nightInterface);
    }

    bool handleMessage(const QDBusMessage& message, const QDBusConnection&) override
    {
        if (message.interface() == propertiesInterface && message.member() == QLatin1String("GetAll")) {
            QVariantMap values;
            if (message.path() == nightPath)
                values = night;
            else if (message.path() == schedulePath)
                values = {{QStringLiteral("Version"), scheduleVersion}};
            else if (message.path() == powerPath) {
                values = powerValues();
                if (delayPowerReads) {
                    message.setDelayedReply(true);
                    heldReads.append(message);
                    heldReadValues.append(values);
                    return true;
                }
            } else {
                return false;
            }
            bus.send(message.createReply(QVariantList{values}));
            return true;
        }
        if (message.path() == powerPath && message.member() == QLatin1String("Set")) {
            if (message.arguments().at(0).toString() != powerService
                || message.arguments().at(1).toString() != QLatin1String("ActiveProfile")) {
                bus.send(message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"),
                                                  QStringLiteral("Unexpected profile property")));
                return true;
            }
            profileWrites.append(qvariant_cast<QDBusVariant>(message.arguments().at(2)).variant().toString());
            if (delayPowerWrites) {
                message.setDelayedReply(true);
                heldWrites.append(message);
            } else {
                finishPowerWrite(message, refusePowerWrite);
            }
            return true;
        }
        if (message.path() == QLatin1String("/KWin") && message.member() == QLatin1String("reconfigure")) {
            ++reconfigureCalls;
            if (refuseReconfigure) {
                bus.send(message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.Failed"),
                                                  QStringLiteral("Fixture failed to reconfigure")));
                return true;
            }
            const auto config = KSharedConfig::openConfig(path, KConfig::SimpleConfig);
            config->reparseConfiguration();
            const KConfigGroup group(config, QStringLiteral("NightColor"));
            const bool enabled = group.readEntry(QStringLiteral("Active"), false);
            night.insert(QStringLiteral("enabled"), enabled);
            night.insert(QStringLiteral("running"), enabled);
            bus.send(message.createReply());
            changed(nightPath, nightInterface);
            return true;
        }
        if (message.path() == nightPath && message.member() == QLatin1String("inhibit")) {
            ++inhibitCalls;
            if (delayInhibit) {
                message.setDelayedReply(true);
                heldInhibits.append(message);
            } else {
                finishInhibit(message);
            }
            return true;
        }
        if (message.path() == nightPath && message.member() == QLatin1String("uninhibit")) {
            ++uninhibitCalls;
            if (refuseUninhibit) {
                bus.send(message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.Failed"),
                                                  QStringLiteral("Fixture refused to resume")));
                return true;
            }
            releasedCookies.append(message.arguments().at(0).toUInt());
            night.insert(QStringLiteral("inhibited"), false);
            bus.send(message.createReply());
            changed(nightPath, nightInterface);
            return true;
        }
        return false;
    }
};

QByteArray contents(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
}

class TestQuickSettingsController : public QObject
{
    Q_OBJECT
    QProcess m_daemon;
    QString m_address;
    std::unique_ptr<QDBusConnection> m_service;
    std::unique_ptr<QDBusConnection> m_client;
    std::unique_ptr<QTemporaryDir> m_directory;
    std::unique_ptr<QuickSettingsFixture> m_fixture;

    QString configPath() const
    {
        return m_directory->filePath(QStringLiteral("kwinrc"));
    }
    QString scheduleConfigPath() const
    {
        return m_directory->filePath(QStringLiteral("knighttimerc"));
    }

private Q_SLOTS:
    void initTestCase()
    {
        qDBusRegisterMetaType<QList<QVariantMap>>();
        m_daemon.start(QStringLiteral("dbus-daemon"),
                       {QStringLiteral("--session"), QStringLiteral("--nofork"), QStringLiteral("--print-address=1")});
        QVERIFY(m_daemon.waitForReadyRead());
        m_address = QString::fromUtf8(m_daemon.readLine()).trimmed();
    }

    void init()
    {
        m_service = std::make_unique<QDBusConnection>(
            QDBusConnection::connectToBus(m_address, QStringLiteral("quick-settings-service")));
        m_client = std::make_unique<QDBusConnection>(
            QDBusConnection::connectToBus(m_address, QStringLiteral("quick-settings-client")));
        QVERIFY(m_service->isConnected());
        QVERIFY(m_client->isConnected());
        m_directory = std::make_unique<QTemporaryDir>();
        QVERIFY(m_directory->isValid());
        m_fixture = std::make_unique<QuickSettingsFixture>(*m_service, configPath());
        QVERIFY(m_service->registerVirtualObject(QStringLiteral("/"), m_fixture.get(), QDBusConnection::SubPath));
        QVERIFY(m_service->registerService(compositorService));
        QVERIFY(m_service->registerService(powerService));
        QVERIFY(m_service->registerService(scheduleService));
    }

    void cleanup()
    {
        m_service->unregisterService(compositorService);
        m_service->unregisterService(powerService);
        m_service->unregisterService(scheduleService);
        m_service->unregisterObject(QStringLiteral("/"), QDBusConnection::UnregisterTree);
        m_fixture.reset();
        m_client.reset();
        m_service.reset();
        QDBusConnection::disconnectFromBus(QStringLiteral("quick-settings-client"));
        QDBusConnection::disconnectFromBus(QStringLiteral("quick-settings-service"));
        m_directory.reset();
    }

    void cleanupTestCase()
    {
        m_daemon.terminate();
        QVERIFY(m_daemon.waitForFinished());
    }

    void usesServiceStateAndOnlyWritesOnRequest()
    {
        PhosphorShellApp::QuickSettingsController settings(*m_client, *m_client, configPath());
        QTRY_VERIFY(settings.nightLightAvailable());
        QTRY_VERIFY(settings.nightLightScheduleAvailable());
        QTRY_VERIFY(settings.powerAvailable());
        QVERIFY(!settings.nightLightEnabled());
        QVERIFY(!QFileInfo::exists(configPath()));
        QVERIFY(!QFileInfo::exists(scheduleConfigPath()));
        QVERIFY(m_fixture->profileWrites.isEmpty());
        QCOMPARE(m_fixture->reconfigureCalls, 0);
        settings.toggleNightLight();
        settings.toggleNightLight();
        QTRY_VERIFY(settings.nightLightEnabled());
        QCOMPARE(m_fixture->reconfigureCalls, 1);
        const auto saved = KSharedConfig::openConfig(configPath(), KConfig::SimpleConfig);
        saved->reparseConfiguration();
        QCOMPARE(KConfigGroup(saved, QStringLiteral("NightColor")).readEntry(QStringLiteral("Active"), false), true);
        settings.toggleNightLight();
        QTRY_VERIFY(!settings.nightLightEnabled());
        QCOMPARE(m_fixture->reconfigureCalls, 2);
        QVERIFY(m_service->unregisterService(compositorService));
        QTRY_VERIFY(!settings.nightLightAvailable());
        settings.toggleNightLight();
        QCOMPARE(m_fixture->reconfigureCalls, 2);
    }

    void absoluteConfigurationTracksExternalEditsAndAtomicReplacement()
    {
        PhosphorShellApp::QuickSettingsController settings(*m_client, *m_client, configPath());
        QTRY_VERIFY(settings.nightLightAvailable());
        QCOMPARE(settings.nightLightTemperature(), 4500);
        KConfig external(configPath(), KConfig::SimpleConfig);
        KConfigGroup night(&external, QStringLiteral("NightColor"));
        night.writeEntry(QStringLiteral("NightTemperature"), 3700);
        night.writeEntry(QStringLiteral("Mode"), QStringLiteral("Constant"));
        QVERIFY(external.sync());
        QTRY_COMPARE(settings.nightLightTemperature(), 3700);
        QCOMPARE(settings.nightLightSchedule(), QStringLiteral("manual"));

        KConfig sharedSchedule(scheduleConfigPath(), KConfig::SimpleConfig);
        KConfigGroup(&sharedSchedule, QStringLiteral("General"))
            .writeEntry(QStringLiteral("Source"), QStringLiteral("Times"));
        KConfigGroup times(&sharedSchedule, QStringLiteral("Times"));
        times.writeEntry(QStringLiteral("SunriseStart"), QTime(8, 15));
        times.writeEntry(QStringLiteral("SunsetStart"), QTime(20, 45));
        QVERIFY(sharedSchedule.sync());
        night.writeEntry(QStringLiteral("Mode"), QStringLiteral("DarkLight"));
        QVERIFY(external.sync());
        QTRY_COMPARE(settings.nightLightSchedule(), QStringLiteral("custom"));
        QTRY_COMPARE(settings.nightLightMorning(), QStringLiteral("08:15"));
        QCOMPARE(settings.nightLightEvening(), QStringLiteral("20:45"));

        // Editors commonly replace the inode rather than write into the file.
        QSaveFile replacement(configPath());
        QVERIFY(replacement.open(QIODevice::WriteOnly));
        const QByteArray replacementContents("[NightColor]\nMode=Constant\nNightTemperature=2900\n");
        QCOMPARE(replacement.write(replacementContents), replacementContents.size());
        QVERIFY(replacement.commit());
        QTRY_COMPARE(settings.nightLightTemperature(), 2900);
        QCOMPARE(settings.nightLightSchedule(), QStringLiteral("manual"));

        external.reparseConfiguration();
        night.writeEntry(QStringLiteral("NightTemperature"), 3100);
        QVERIFY(external.sync());
        QTRY_COMPARE(settings.nightLightTemperature(), 3100);
        QCOMPARE(m_fixture->reconfigureCalls, 0);
        QVERIFY(m_fixture->profileWrites.isEmpty());
    }

    void relativeConfigurationTracksKConfigNotificationsInPrivateConfigHome()
    {
        const QByteArray previousConfigHome = qgetenv("XDG_CONFIG_HOME");
        const bool hadConfigHome = qEnvironmentVariableIsSet("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", m_directory->path().toUtf8());
        const auto restoreConfigHome = qScopeGuard([previousConfigHome, hadConfigHome] {
            if (hadConfigHome)
                qputenv("XDG_CONFIG_HOME", previousConfigHome);
            else
                qunsetenv("XDG_CONFIG_HOME");
        });
        QCOMPARE(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation), m_directory->path());
        QVERIFY(QDBusConnection::sessionBus().isConnected());
        {
            PhosphorShellApp::QuickSettingsController settings(*m_client, *m_client, QStringLiteral("kwinrc"));
            QTRY_VERIFY(settings.nightLightAvailable());
            QVERIFY(!QFileInfo::exists(configPath()));
            KConfig external(QStringLiteral("kwinrc"), KConfig::SimpleConfig);
            KConfigGroup night(&external, QStringLiteral("NightColor"));
            night.writeEntry(QStringLiteral("NightTemperature"), 3300, KConfig::Notify);
            night.writeEntry(QStringLiteral("Mode"), QStringLiteral("Constant"), KConfig::Notify);
            QVERIFY(external.sync());
            QTRY_COMPARE(settings.nightLightTemperature(), 3300);
            QCOMPARE(settings.nightLightSchedule(), QStringLiteral("manual"));
            QVERIFY(QFileInfo::exists(configPath()));

            KConfig sharedSchedule(QStringLiteral("knighttimerc"), KConfig::SimpleConfig);
            KConfigGroup(&sharedSchedule, QStringLiteral("General"))
                .writeEntry(QStringLiteral("Source"), QStringLiteral("Times"), KConfig::Notify);
            KConfigGroup times(&sharedSchedule, QStringLiteral("Times"));
            times.writeEntry(QStringLiteral("SunriseStart"), QTime(7, 30), KConfig::Notify);
            times.writeEntry(QStringLiteral("SunsetStart"), QTime(21, 15), KConfig::Notify);
            QVERIFY(sharedSchedule.sync());
            QTRY_COMPARE(settings.nightLightMorning(), QStringLiteral("07:30"));
            QCOMPARE(settings.nightLightEvening(), QStringLiteral("21:15"));
            night.writeEntry(QStringLiteral("Mode"), QStringLiteral("DarkLight"), KConfig::Notify);
            QVERIFY(external.sync());
            QTRY_COMPARE(settings.nightLightSchedule(), QStringLiteral("custom"));
            QCOMPARE(m_fixture->reconfigureCalls, 0);
        }
        QVERIFY(QFile::remove(configPath()));
        QVERIFY(QFile::remove(scheduleConfigPath()));
    }

    void powerProfilesFollowServiceCapabilities()
    {
        m_fixture->profiles = {QStringLiteral("balanced"), QStringLiteral("future-profile"),
                               QStringLiteral("power-saver"), QStringLiteral("balanced")};
        PhosphorShellApp::QuickSettingsController settings(*m_client, *m_client, configPath());
        QTRY_VERIFY(settings.powerAvailable());
        QCOMPARE(settings.powerProfiles(), QStringList({QStringLiteral("balanced"), QStringLiteral("power-saver")}));
        QCOMPARE(settings.powerProfile(), QStringLiteral("balanced"));
        settings.setPowerProfile(QStringLiteral("performance"));
        QVERIFY(!settings.powerError().isEmpty());
        QVERIFY(!settings.powerPending());
        QVERIFY(m_fixture->profileWrites.isEmpty());
        settings.setPowerProfile(QStringLiteral("power-saver"));
        QTRY_COMPARE(settings.powerProfile(), QStringLiteral("power-saver"));
        QVERIFY(settings.powerError().isEmpty());
        QCOMPARE(m_fixture->profileWrites, QStringList{QStringLiteral("power-saver")});
    }

    void degradedPerformanceRemainsSelectable()
    {
        m_fixture->degradation = QStringLiteral("high-operating-temperature");
        PhosphorShellApp::QuickSettingsController settings(*m_client, *m_client, configPath());
        QTRY_VERIFY(settings.powerAvailable());
        QCOMPARE(settings.performanceDegraded(), m_fixture->degradation);
        settings.setPowerProfile(QStringLiteral("performance"));
        QTRY_COMPARE(settings.powerProfile(), QStringLiteral("performance"));
        QVERIFY(settings.powerError().isEmpty());
        QCOMPARE(m_fixture->profileWrites, QStringList{QStringLiteral("performance")});
    }

    void powerWriteFailureKeepsConfirmedProfileAndAllowsRetry()
    {
        PhosphorShellApp::QuickSettingsController settings(*m_client, *m_client, configPath());
        QTRY_VERIFY(settings.powerAvailable());
        m_fixture->refusePowerWrite = true;
        settings.setPowerProfile(QStringLiteral("power-saver"));
        QTRY_VERIFY(!settings.powerPending());
        QVERIFY(!settings.powerError().isEmpty());
        QCOMPARE(settings.powerProfile(), QStringLiteral("balanced"));
        m_fixture->refusePowerWrite = false;
        settings.setPowerProfile(QStringLiteral("power-saver"));
        QTRY_COMPARE(settings.powerProfile(), QStringLiteral("power-saver"));
        QVERIFY(settings.powerError().isEmpty());
    }

    void duplicatePowerRequestsWaitForConfirmation()
    {
        PhosphorShellApp::QuickSettingsController settings(*m_client, *m_client, configPath());
        QTRY_VERIFY(settings.powerAvailable());
        m_fixture->delayPowerWrites = true;
        settings.setPowerProfile(QStringLiteral("performance"));
        settings.setPowerProfile(QStringLiteral("performance"));
        settings.setPowerProfile(QStringLiteral("power-saver"));
        QTRY_COMPARE(m_fixture->heldWrites.size(), 1);
        QVERIFY(settings.powerPending());
        QCOMPARE(settings.powerProfile(), QStringLiteral("balanced"));
        m_fixture->finishPowerWrite(m_fixture->heldWrites.constFirst(), false);
        QTRY_COMPARE(settings.powerProfile(), QStringLiteral("performance"));
        QVERIFY(!settings.powerPending());
        QCOMPARE(m_fixture->profileWrites.size(), 1);
        settings.setPowerProfile(QStringLiteral("performance"));
        QCOMPARE(m_fixture->profileWrites.size(), 1);
    }

    void stalePowerReadCannotReplaceNewerState()
    {
        PhosphorShellApp::QuickSettingsController settings(*m_client, *m_client, configPath());
        QTRY_VERIFY(settings.powerAvailable());
        m_fixture->delayPowerReads = true;
        settings.refreshPower();
        QTRY_COMPARE(m_fixture->heldReads.size(), 1);
        m_fixture->delayPowerReads = false;
        m_fixture->activeProfile = QStringLiteral("power-saver");
        settings.refreshPower();
        QTRY_COMPARE(settings.powerProfile(), QStringLiteral("power-saver"));
        m_service->send(
            m_fixture->heldReads.constFirst().createReply(QVariantList{m_fixture->heldReadValues.constFirst()}));
        QTest::qWait(30);
        QCOMPARE(settings.powerProfile(), QStringLiteral("power-saver"));
    }

    void powerRestartInvalidatesOldWriteWithoutClearingNewPendingRequest()
    {
        PhosphorShellApp::QuickSettingsController settings(*m_client, *m_client, configPath());
        QTRY_VERIFY(settings.powerAvailable());
        m_fixture->delayPowerWrites = true;
        settings.setPowerProfile(QStringLiteral("power-saver"));
        QTRY_COMPARE(m_fixture->heldWrites.size(), 1);
        QVERIFY(m_service->unregisterService(powerService));
        QTRY_VERIFY(!settings.powerAvailable());
        QVERIFY(!settings.powerPending());
        QVERIFY(m_service->registerService(powerService));
        QTRY_VERIFY(settings.powerAvailable());
        settings.setPowerProfile(QStringLiteral("performance"));
        QTRY_COMPARE(m_fixture->heldWrites.size(), 2);
        m_fixture->finishPowerWrite(m_fixture->heldWrites.at(0), true);
        QTest::qWait(30);
        QVERIFY(settings.powerPending());
        QVERIFY(settings.powerError().isEmpty());
        m_fixture->finishPowerWrite(m_fixture->heldWrites.at(1), false);
        QTRY_COMPARE(settings.powerProfile(), QStringLiteral("performance"));
        QVERIFY(!settings.powerPending());
    }

    void savesNightTemperatureAndSharedScheduleWithNativeKConfigTypes()
    {
        PhosphorShellApp::QuickSettingsController settings(*m_client, *m_client, configPath());
        QTRY_VERIFY(settings.nightLightAvailable());
        QTRY_VERIFY(settings.nightLightScheduleAvailable());
        settings.setNightLightSchedule(QStringLiteral("manual"));
        QTRY_VERIFY(!settings.nightLightPending());
        QCOMPARE(settings.nightLightSchedule(), QStringLiteral("manual"));
        auto saved = KSharedConfig::openConfig(configPath(), KConfig::SimpleConfig);
        saved->reparseConfiguration();
        QCOMPARE(KConfigGroup(saved, QStringLiteral("NightColor")).readEntry(QStringLiteral("Mode"), QString()),
                 QStringLiteral("Constant"));
        QVERIFY(!QFileInfo::exists(scheduleConfigPath()));
        settings.setNightLightTemperature(3200);
        QTRY_VERIFY(!settings.nightLightPending());
        QCOMPARE(settings.nightLightTemperature(), 3200);
        saved->reparseConfiguration();
        QCOMPARE(KConfigGroup(saved, QStringLiteral("NightColor")).readEntry(QStringLiteral("NightTemperature"), 0),
                 3200);
        settings.setNightLightTimes(QStringLiteral("06:15"), QStringLiteral("22:30"));
        QTRY_VERIFY(!settings.nightLightPending());
        QCOMPARE(settings.nightLightSchedule(), QStringLiteral("custom"));
        QCOMPARE(settings.nightLightMorning(), QStringLiteral("06:15"));
        QCOMPARE(settings.nightLightEvening(), QStringLiteral("22:30"));
        const auto schedule = KSharedConfig::openConfig(scheduleConfigPath(), KConfig::SimpleConfig);
        schedule->reparseConfiguration();
        saved->reparseConfiguration();
        QCOMPARE(KConfigGroup(saved, QStringLiteral("NightColor")).readEntry(QStringLiteral("Mode"), QString()),
                 QStringLiteral("DarkLight"));
        QCOMPARE(KConfigGroup(schedule, QStringLiteral("General")).readEntry(QStringLiteral("Source"), QString()),
                 QStringLiteral("Times"));
        const KConfigGroup times(schedule, QStringLiteral("Times"));
        QCOMPARE(times.readEntry(QStringLiteral("SunriseStart"), QTime()), QTime(6, 15));
        QCOMPARE(times.readEntry(QStringLiteral("SunsetStart"), QTime()), QTime(22, 30));
        QCOMPARE(times.readEntry(QStringLiteral("TransitionDuration"), 0), 1800);
        settings.setNightLightTimes(QStringLiteral("06:00"), QStringLiteral("06:10"));
        QTRY_VERIFY(!settings.nightLightPending());
        schedule->reparseConfiguration();
        QCOMPARE(KConfigGroup(schedule, QStringLiteral("Times")).readEntry(QStringLiteral("TransitionDuration"), 0),
                 300);
        settings.setNightLightSchedule(QStringLiteral("automatic"));
        QTRY_VERIFY(!settings.nightLightPending());
        QCOMPARE(settings.nightLightSchedule(), QStringLiteral("automatic"));
        schedule->reparseConfiguration();
        QCOMPARE(KConfigGroup(schedule, QStringLiteral("General")).readEntry(QStringLiteral("Source"), QString()),
                 QStringLiteral("Location"));
    }

    void invalidNightRequestsNeverTouchConfiguration()
    {
        PhosphorShellApp::QuickSettingsController settings(*m_client, *m_client, configPath());
        QTRY_VERIFY(settings.nightLightAvailable());
        settings.setNightLightTemperature(999);
        settings.setNightLightTemperature(6501);
        settings.setNightLightTimes(QStringLiteral("25:00"), QStringLiteral("22:00"));
        settings.setNightLightTimes(QStringLiteral("06:00"), QStringLiteral("06:00"));
        settings.setNightLightTimes(QString(), QStringLiteral("22:00"));
        settings.setNightLightSchedule(QStringLiteral("unsupported"));
        QVERIFY(!settings.nightLightError().isEmpty());
        QVERIFY(!settings.nightLightPending());
        QVERIFY(!QFileInfo::exists(configPath()));
        QVERIFY(!QFileInfo::exists(scheduleConfigPath()));
        QCOMPARE(m_fixture->reconfigureCalls, 0);
        QTRY_VERIFY(settings.nightLightScheduleAvailable());
        settings.setNightLightTimes(QStringLiteral("07:00"), QStringLiteral("21:00"));
        QTRY_VERIFY(!settings.nightLightPending());
        const auto previousConfig = contents(configPath());
        const auto previousSchedule = contents(scheduleConfigPath());
        settings.setNightLightTemperature(-1);
        settings.setNightLightTimes(QStringLiteral("no time"), QStringLiteral("21:00"));
        QCOMPARE(contents(configPath()), previousConfig);
        QCOMPARE(contents(scheduleConfigPath()), previousSchedule);
        QCOMPARE(m_fixture->reconfigureCalls, 1);
    }

    void unavailableSharedScheduleStillAllowsManualNightLight()
    {
        m_fixture->scheduleVersion = 0;
        PhosphorShellApp::QuickSettingsController settings(*m_client, *m_client, configPath());
        QTRY_VERIFY(settings.nightLightAvailable());
        QVERIFY(!settings.nightLightScheduleAvailable());
        settings.setNightLightTimes(QStringLiteral("07:00"), QStringLiteral("21:00"));
        QVERIFY(!settings.nightLightError().isEmpty());
        QVERIFY(!QFileInfo::exists(configPath()));
        QVERIFY(!QFileInfo::exists(scheduleConfigPath()));
        settings.setNightLightSchedule(QStringLiteral("manual"));
        QTRY_VERIFY(!settings.nightLightPending());
        QCOMPARE(settings.nightLightSchedule(), QStringLiteral("manual"));
        QVERIFY(settings.nightLightError().isEmpty());
        m_fixture->scheduleVersion = 1;
        QVERIFY(m_service->unregisterService(scheduleService));
        QVERIFY(m_service->registerService(scheduleService));
        QTRY_VERIFY(settings.nightLightScheduleAvailable());
    }

    void reconfigureFailureReportsSavedButUnappliedSetting()
    {
        m_fixture->refuseReconfigure = true;
        PhosphorShellApp::QuickSettingsController settings(*m_client, *m_client, configPath());
        QTRY_VERIFY(settings.nightLightAvailable());
        settings.setNightLightEnabled(true);
        QTRY_VERIFY(!settings.nightLightPending());
        QVERIFY(!settings.nightLightError().isEmpty());
        QVERIFY(!settings.nightLightEnabled());
        const auto saved = KSharedConfig::openConfig(configPath(), KConfig::SimpleConfig);
        saved->reparseConfiguration();
        QVERIFY(KConfigGroup(saved, QStringLiteral("NightColor")).readEntry(QStringLiteral("Active"), false));
        m_fixture->refuseReconfigure = false;
        settings.setNightLightEnabled(true);
        QTRY_VERIFY(settings.nightLightEnabled());
        QVERIFY(settings.nightLightError().isEmpty());
    }

    void pauseOwnsOneCookieAndRetriesFailedResume()
    {
        m_fixture->night.insert(QStringLiteral("enabled"), true);
        PhosphorShellApp::QuickSettingsController settings(*m_client, *m_client, configPath());
        QTRY_VERIFY(settings.nightLightEnabled());
        settings.toggleNightLightPause();
        settings.toggleNightLightPause();
        QTRY_VERIFY(settings.nightLightPaused());
        QTRY_VERIFY(!settings.nightLightPending());
        QCOMPARE(m_fixture->inhibitCalls, 1);
        QTRY_VERIFY(settings.nightLightInhibited());
        m_fixture->refuseUninhibit = true;
        settings.toggleNightLightPause();
        QTRY_VERIFY(!settings.nightLightPending());
        QVERIFY(settings.nightLightPaused());
        QVERIFY(!settings.nightLightError().isEmpty());
        QVERIFY(m_fixture->releasedCookies.isEmpty());
        m_fixture->refuseUninhibit = false;
        settings.toggleNightLightPause();
        QTRY_VERIFY(!settings.nightLightPaused());
        QCOMPARE(m_fixture->releasedCookies, QList<uint>{41});
        QVERIFY(settings.nightLightError().isEmpty());
    }

    void pauseFailureDoesNotClaimOwnershipOfExternalInhibition()
    {
        m_fixture->night.insert(QStringLiteral("enabled"), true);
        m_fixture->night.insert(QStringLiteral("inhibited"), true);
        m_fixture->refuseInhibit = true;
        PhosphorShellApp::QuickSettingsController settings(*m_client, *m_client, configPath());
        QTRY_VERIFY(settings.nightLightEnabled());
        QVERIFY(settings.nightLightInhibited());
        QVERIFY(!settings.nightLightPaused());
        settings.toggleNightLightPause();
        QTRY_VERIFY(!settings.nightLightPending());
        QVERIFY(!settings.nightLightPaused());
        QVERIFY(!settings.nightLightError().isEmpty());
        QCOMPARE(m_fixture->uninhibitCalls, 0);
    }

    void destructionReleasesOnlyTheOwnedCookie()
    {
        m_fixture->night.insert(QStringLiteral("enabled"), true);
        {
            PhosphorShellApp::QuickSettingsController settings(*m_client, *m_client, configPath());
            QTRY_VERIFY(settings.nightLightEnabled());
            settings.toggleNightLightPause();
            QTRY_VERIFY(settings.nightLightPaused());
        }
        QTRY_COMPARE(m_fixture->releasedCookies, QList<uint>{41});
        QCOMPARE(m_fixture->uninhibitCalls, 1);
    }

    void scheduledPauseResumesAtNextTransition()
    {
        m_fixture->night.insert(QStringLiteral("enabled"), true);
        m_fixture->night.insert(QStringLiteral("scheduledTransitionDateTime"), QDateTime::currentSecsSinceEpoch() + 2);
        PhosphorShellApp::QuickSettingsController settings(*m_client, *m_client, configPath());
        QTRY_VERIFY(settings.nightLightEnabled());
        settings.toggleNightLightPause();
        QTRY_VERIFY(settings.nightLightPaused());
        QTRY_VERIFY_WITH_TIMEOUT(!settings.nightLightPaused(), 4000);
        QCOMPARE(m_fixture->releasedCookies, QList<uint>{41});
    }

    void failedAutomaticResumeStopsRetryingUntilUserRequestsIt()
    {
        m_fixture->night.insert(QStringLiteral("enabled"), true);
        m_fixture->night.insert(QStringLiteral("scheduledTransitionDateTime"), QDateTime::currentSecsSinceEpoch() - 1);
        m_fixture->refuseUninhibit = true;
        PhosphorShellApp::QuickSettingsController settings(*m_client, *m_client, configPath());
        QTRY_VERIFY(settings.nightLightEnabled());
        settings.toggleNightLightPause();
        QTRY_COMPARE(m_fixture->uninhibitCalls, 1);
        QTRY_VERIFY(!settings.nightLightPending());
        QVERIFY(settings.nightLightPaused());
        QVERIFY(!settings.nightLightError().isEmpty());
        QTest::qWait(120);
        QCOMPARE(m_fixture->uninhibitCalls, 1);
        m_fixture->refuseUninhibit = false;
        settings.toggleNightLightPause();
        QTRY_VERIFY(!settings.nightLightPaused());
        QCOMPARE(m_fixture->releasedCookies, QList<uint>{41});
        QCOMPARE(m_fixture->uninhibitCalls, 2);
        QVERIFY(settings.nightLightError().isEmpty());
    }

    void pausedScheduleChangesCancelAndRearmAutomaticResume()
    {
        const qint64 firstTransition = QDateTime::currentSecsSinceEpoch() + 2;
        m_fixture->night.insert(QStringLiteral("scheduledTransitionDateTime"), firstTransition);
        PhosphorShellApp::QuickSettingsController settings(*m_client, *m_client, configPath());
        QTRY_VERIFY(settings.nightLightAvailable());
        QTRY_VERIFY(settings.nightLightScheduleAvailable());
        settings.setNightLightEnabled(true);
        QTRY_VERIFY(settings.nightLightEnabled());
        settings.toggleNightLightPause();
        QTRY_VERIFY(settings.nightLightPaused());
        settings.setNightLightSchedule(QStringLiteral("manual"));
        QTRY_VERIFY(!settings.nightLightPending());
        QCOMPARE(settings.nightLightSchedule(), QStringLiteral("manual"));
        QTRY_VERIFY_WITH_TIMEOUT(QDateTime::currentSecsSinceEpoch() > firstTransition, 4000);
        QVERIFY(settings.nightLightPaused());
        QCOMPARE(m_fixture->uninhibitCalls, 0);
        m_fixture->night.insert(QStringLiteral("scheduledTransitionDateTime"), QDateTime::currentSecsSinceEpoch() + 2);
        settings.setNightLightSchedule(QStringLiteral("automatic"));
        QTRY_VERIFY(!settings.nightLightPending());
        QCOMPARE(settings.nightLightSchedule(), QStringLiteral("automatic"));
        QTRY_VERIFY_WITH_TIMEOUT(!settings.nightLightPaused(), 4000);
        QCOMPARE(m_fixture->releasedCookies, QList<uint>{41});
    }

    void disablingPausedNightLightReleasesItsInhibition()
    {
        m_fixture->night.insert(QStringLiteral("enabled"), true);
        PhosphorShellApp::QuickSettingsController settings(*m_client, *m_client, configPath());
        QTRY_VERIFY(settings.nightLightEnabled());
        settings.toggleNightLightPause();
        QTRY_VERIFY(settings.nightLightPaused());
        settings.setNightLightEnabled(false);
        QTRY_VERIFY(!settings.nightLightEnabled());
        QTRY_VERIFY(!settings.nightLightPaused());
        QTRY_VERIFY(!settings.nightLightPending());
        QCOMPARE(m_fixture->releasedCookies, QList<uint>{41});
        QCOMPARE(m_fixture->uninhibitCalls, 1);
    }

    void destructionDuringPendingPauseReleasesTheReplyCookie()
    {
        m_fixture->night.insert(QStringLiteral("enabled"), true);
        m_fixture->delayInhibit = true;
        auto settings = std::make_unique<PhosphorShellApp::QuickSettingsController>(*m_client, *m_client, configPath());
        QTRY_VERIFY(settings->nightLightEnabled());
        settings->toggleNightLightPause();
        QTRY_COMPARE(m_fixture->heldInhibits.size(), 1);
        QVERIFY(settings->nightLightPending());
        settings.reset();
        m_fixture->finishInhibit(m_fixture->heldInhibits.constFirst());
        QTRY_COMPARE(m_fixture->releasedCookies, QList<uint>{41});
        QCOMPARE(m_fixture->uninhibitCalls, 1);
    }

    void compositorRestartDiscardsThePreviousOwnersCookie()
    {
        m_fixture->night.insert(QStringLiteral("enabled"), true);
        PhosphorShellApp::QuickSettingsController settings(*m_client, *m_client, configPath());
        QTRY_VERIFY(settings.nightLightEnabled());
        settings.toggleNightLightPause();
        QTRY_VERIFY(settings.nightLightPaused());
        QVERIFY(m_service->unregisterService(compositorService));
        QTRY_VERIFY(!settings.nightLightAvailable());
        QVERIFY(!settings.nightLightPaused());
        QVERIFY(!settings.nightLightPending());
        m_fixture->night.insert(QStringLiteral("inhibited"), false);
        QVERIFY(m_service->registerService(compositorService));
        QTRY_VERIFY(settings.nightLightEnabled());
        settings.toggleNightLightPause();
        QTRY_VERIFY(settings.nightLightPaused());
        settings.toggleNightLightPause();
        QTRY_VERIFY(!settings.nightLightPaused());
        QCOMPARE(m_fixture->releasedCookies, QList<uint>{42});
    }
};
QTEST_GUILESS_MAIN(TestQuickSettingsController)
#include "test_quick_settings_controller.moc"
