// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace PhosphorShellApp {

// Live session controls. Only an explicit toggle writes the compositor config;
// construction, reconnection and property updates are read-only.
class QuickSettingsController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool nightLightAvailable READ nightLightAvailable NOTIFY changed)
    Q_PROPERTY(bool nightLightEnabled READ nightLightEnabled NOTIFY changed)
    Q_PROPERTY(QString powerProfile READ powerProfile NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit QuickSettingsController(QObject* parent = nullptr);
    QuickSettingsController(QDBusConnection session, QDBusConnection system, QString configPath,
                            QObject* parent = nullptr);
    bool nightLightAvailable() const
    {
        return m_available;
    }
    bool nightLightEnabled() const
    {
        return m_enabled;
    }
    QString powerProfile() const
    {
        return m_profile;
    }
    QString error() const
    {
        return m_error;
    }
    Q_INVOKABLE void toggleNightLight();
Q_SIGNALS:
    void changed();
private Q_SLOTS:
    void refreshNightLight();
    void refreshPower();
    void propertiesChanged(const QString& interface, const QVariantMap& values, const QStringList& invalidated);

private:
    QDBusConnection m_session;
    QDBusConnection m_system;
    QString m_configPath;
    QString m_profile;
    QString m_error;
    bool m_available = false;
    bool m_enabled = false;
    bool m_pending = false;
};
}
