// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// QSettings-based config backend.  Reads/writes ~/.config/plasmazonesrc
// in INI format, compatible with KConfig's on-disk format.

#pragma once

#include "configbackend.h"
#include <QSettings>
#include <memory>

namespace PlasmaZones {

class QSettingsConfigGroup : public ConfigGroup
{
public:
    QSettingsConfigGroup(QSettings* settings, const QString& groupName);
    ~QSettingsConfigGroup() override;

    QString readString(const QString& key, const QString& defaultValue = {}) const override;
    int readInt(const QString& key, int defaultValue = 0) const override;
    bool readBool(const QString& key, bool defaultValue = false) const override;
    double readDouble(const QString& key, double defaultValue = 0.0) const override;
    QColor readColor(const QString& key, const QColor& defaultValue = {}) const override;

    void writeString(const QString& key, const QString& value) override;
    void writeInt(const QString& key, int value) override;
    void writeBool(const QString& key, bool value) override;
    void writeDouble(const QString& key, double value) override;
    void writeColor(const QString& key, const QColor& value) override;

    bool hasKey(const QString& key) const override;
    void deleteKey(const QString& key) override;

private:
    QSettings* m_settings; // not owned
    QString m_group;
};

class QSettingsConfigBackend : public IConfigBackend
{
public:
    explicit QSettingsConfigBackend(const QString& filePath);
    ~QSettingsConfigBackend() override = default;

    std::unique_ptr<ConfigGroup> group(const QString& name) override;
    void reparseConfiguration() override;
    void sync() override;
    void deleteGroup(const QString& name) override;
    QStringList groupList() const override;

private:
    QString m_filePath;
    std::unique_ptr<QSettings> m_settings;
};

/// Create the default config backend for the standard plasmazonesrc file.
PLASMAZONES_EXPORT std::unique_ptr<IConfigBackend> createDefaultConfigBackend();

} // namespace PlasmaZones
