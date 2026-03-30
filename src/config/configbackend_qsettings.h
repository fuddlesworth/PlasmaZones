// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// QSettings-based config backend.  Reads/writes ~/.config/plasmazonesrc
// in INI format, compatible with KConfig's on-disk format.

#pragma once

#include "plasmazones_export.h"
#include <QColor>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <memory>

namespace PlasmaZones {

/// A view into a single config group (e.g., [Activation], [Display]).
///
/// Returned by QSettingsConfigBackend::group() as a unique_ptr.  Only one
/// QSettingsConfigGroup may be active per backend at a time — destroy it
/// (let the unique_ptr go out of scope) before creating another.
/// Not copyable or movable.
class PLASMAZONES_EXPORT QSettingsConfigGroup
{
public:
    QSettingsConfigGroup(QSettings* settings, const QString& groupName);
    ~QSettingsConfigGroup();

    QSettingsConfigGroup(const QSettingsConfigGroup&) = delete;
    QSettingsConfigGroup& operator=(const QSettingsConfigGroup&) = delete;
    QSettingsConfigGroup(QSettingsConfigGroup&&) = delete;
    QSettingsConfigGroup& operator=(QSettingsConfigGroup&&) = delete;

    // Typed reads with defaults
    QString readString(const QString& key, const QString& defaultValue = {}) const;
    int readInt(const QString& key, int defaultValue = 0) const;
    bool readBool(const QString& key, bool defaultValue = false) const;
    double readDouble(const QString& key, double defaultValue = 0.0) const;
    QColor readColor(const QString& key, const QColor& defaultValue = {}) const;

    // Typed writes
    void writeString(const QString& key, const QString& value);
    void writeInt(const QString& key, int value);
    void writeBool(const QString& key, bool value);
    void writeDouble(const QString& key, double value);
    void writeColor(const QString& key, const QColor& value);

    // Key management
    bool hasKey(const QString& key) const;
    void deleteKey(const QString& key);

private:
    QSettings* m_settings; // not owned
    QString m_group;
};

/// Top-level config backend.  Owns the connection to the config store
/// and provides group access, sync, and enumeration.
class PLASMAZONES_EXPORT QSettingsConfigBackend
{
public:
    explicit QSettingsConfigBackend(const QString& filePath);
    ~QSettingsConfigBackend() = default;

    /// Get a group view.  Caller owns the returned pointer.
    std::unique_ptr<QSettingsConfigGroup> group(const QString& name);

    /// Re-read config from disk (discard in-memory changes).
    void reparseConfiguration();

    /// Flush pending writes to disk.
    void sync();

    /// Delete an entire group and its keys.
    void deleteGroup(const QString& name);

    /// List all top-level group names.
    QStringList groupList() const;

    /// Create the default config backend for the standard plasmazonesrc file.
    static std::unique_ptr<QSettingsConfigBackend> createDefault();

private:
    QString m_filePath;
    std::unique_ptr<QSettings> m_settings;
};

} // namespace PlasmaZones
