// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QDBusVariant>
#include <QString>
#include <QVariant>
#include <functional>
#include <QHash>

namespace PlasmaZones {

class ISettings;
class Settings; // Forward declaration for concrete type

/**
 * @brief D-Bus adaptor for settings operations
 *
 * Provides D-Bus interface: org.plasmazones.Settings
 * Single responsibility: Settings read/write operations
 *
 * Uses registry pattern to avoid Open/Closed violation in setSetting.
 */
class PLASMAZONES_EXPORT SettingsAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Settings")

public:
    explicit SettingsAdaptor(ISettings* settings, QObject* parent = nullptr);
    ~SettingsAdaptor() override = default;

public Q_SLOTS:
    // Settings operations
    void reloadSettings();
    void saveSettings();
    void resetToDefaults();

    // Generic get/set (registry-based)
    QString getAllSettings();
    QDBusVariant getSetting(const QString& key);
    bool setSetting(const QString& key, const QDBusVariant& value);
    QStringList getSettingKeys();

Q_SIGNALS:
    void settingsChanged();

private:
    void initializeRegistry();

    ISettings* m_settings; // Interface type (DIP)

    // Registry pattern for Open/Closed compliance
    using Getter = std::function<QVariant()>;
    using Setter = std::function<bool(const QVariant&)>;

    QHash<QString, Getter> m_getters;
    QHash<QString, Setter> m_setters;
};

} // namespace PlasmaZones
