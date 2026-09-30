// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <KSharedConfig>
#include <KConfigWatcher>
#include <QDBusConnection>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <optional>

namespace PhosphorShellApp {

// Process-owned live controls. Reads never change compositor or power settings.
class QuickSettingsController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool nightLightAvailable READ nightLightAvailable NOTIFY changed)
    Q_PROPERTY(bool nightLightEnabled READ nightLightEnabled NOTIFY changed)
    Q_PROPERTY(bool nightLightRunning READ nightLightRunning NOTIFY changed)
    Q_PROPERTY(bool nightLightInhibited READ nightLightInhibited NOTIFY changed)
    Q_PROPERTY(bool nightLightPaused READ nightLightPaused NOTIFY changed)
    Q_PROPERTY(bool nightLightScheduleAvailable READ nightLightScheduleAvailable NOTIFY changed)
    Q_PROPERTY(int nightLightTemperature READ nightLightTemperature NOTIFY changed)
    Q_PROPERTY(QString nightLightSchedule READ nightLightSchedule NOTIFY changed)
    Q_PROPERTY(QString nightLightMorning READ nightLightMorning NOTIFY changed)
    Q_PROPERTY(QString nightLightEvening READ nightLightEvening NOTIFY changed)
    Q_PROPERTY(bool nightLightPending READ nightLightPending NOTIFY changed)
    Q_PROPERTY(QString nightLightError READ nightLightError NOTIFY changed)
    Q_PROPERTY(bool powerAvailable READ powerAvailable NOTIFY changed)
    Q_PROPERTY(QString powerProfile READ powerProfile NOTIFY changed)
    Q_PROPERTY(QStringList powerProfiles READ powerProfiles NOTIFY changed)
    Q_PROPERTY(QString performanceDegraded READ performanceDegraded NOTIFY changed)
    Q_PROPERTY(bool powerPending READ powerPending NOTIFY changed)
    Q_PROPERTY(QString powerError READ powerError NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit QuickSettingsController(QObject* parent = nullptr);
    QuickSettingsController(QDBusConnection session, QDBusConnection system, QString configPath,
                            QObject* parent = nullptr);
    ~QuickSettingsController() override;
    bool nightLightAvailable() const
    {
        return m_night.available;
    }
    bool nightLightEnabled() const
    {
        return m_night.enabled;
    }
    bool nightLightRunning() const
    {
        return m_night.running;
    }
    bool nightLightInhibited() const
    {
        return m_night.inhibited;
    }
    bool nightLightPaused() const
    {
        return m_cookie.has_value();
    }
    bool nightLightScheduleAvailable() const
    {
        return m_scheduleAvailable;
    }
    int nightLightTemperature() const
    {
        return m_temperature;
    }
    QString nightLightSchedule() const
    {
        return m_schedule;
    }
    QString nightLightMorning() const
    {
        return m_morning;
    }
    QString nightLightEvening() const
    {
        return m_evening;
    }
    bool nightLightPending() const
    {
        return m_nightPending;
    }
    QString nightLightError() const
    {
        return m_nightError;
    }
    bool powerAvailable() const
    {
        return m_power.available;
    }
    QString powerProfile() const
    {
        return m_power.profile;
    }
    QStringList powerProfiles() const
    {
        return m_power.profiles;
    }
    QString performanceDegraded() const
    {
        return m_power.degraded;
    }
    bool powerPending() const
    {
        return m_powerPending;
    }
    QString powerError() const
    {
        return m_powerError;
    }
    QString error() const
    {
        return m_nightError;
    }
    Q_INVOKABLE void toggleNightLight();
    Q_INVOKABLE void setNightLightEnabled(bool enabled);
    Q_INVOKABLE void setNightLightTemperature(int temperature);
    Q_INVOKABLE void setNightLightSchedule(const QString& schedule);
    Q_INVOKABLE void setNightLightTimes(const QString& morning, const QString& evening);
    Q_INVOKABLE void toggleNightLightPause();
    Q_INVOKABLE void refreshNightLight();
    Q_INVOKABLE void refreshPower();
    Q_INVOKABLE void setPowerProfile(const QString& profile);
Q_SIGNALS:
    void changed();
private Q_SLOTS:
    void propertiesChanged(const QString& interface, const QVariantMap& values, const QStringList& invalidated);

private:
    struct NightState
    {
        bool available = false;
        bool enabled = false;
        bool running = false;
        bool inhibited = false;
        qint64 transition = 0;
        bool operator==(const NightState&) const = default;
    };
    struct PowerState
    {
        bool available = false;
        QString profile;
        QStringList profiles;
        QString degraded;
        bool operator==(const PowerState&) const = default;
    };
    void readNightConfig();
    void refreshSchedule();
    void applyNightConfig(const QVariantMap& night, const QVariantMap& general = {}, const QVariantMap& times = {});
    void finishNightWrite(const QString& error);
    void resumeNightLight();
    void scheduleResume();
    void setNightError(const QString& error);
    QDBusConnection m_session;
    QDBusConnection m_system;
    KSharedConfig::Ptr m_config;
    KSharedConfig::Ptr m_scheduleConfig;
    QList<KConfigWatcher::Ptr> m_configWatchers;
    NightState m_night;
    PowerState m_power;
    int m_temperature = 4500;
    QString m_schedule = QStringLiteral("automatic");
    QString m_morning = QStringLiteral("06:00");
    QString m_evening = QStringLiteral("18:00");
    QString m_nightError;
    QString m_powerError;
    bool m_nightPending = false;
    bool m_powerPending = false;
    bool m_scheduleAvailable = false;
    quint64 m_nightEpoch = 0;
    quint64 m_powerEpoch = 0;
    quint64 m_nightRead = 0;
    quint64 m_powerRead = 0;
    quint64 m_scheduleRead = 0;
    std::optional<uint> m_cookie;
    QString m_cookieOwner;
    QTimer m_resumeTimer;
    qint64 m_resumeAt = 0;
    bool m_autoResume = false;
};
}
