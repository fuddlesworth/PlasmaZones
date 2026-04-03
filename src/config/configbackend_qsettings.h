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
#include <atomic>

namespace PlasmaZones {

class QSettingsConfigBackend; // forward declare for group back-pointer

/// A view into a single config group (e.g., [Activation], [Display]).
///
/// Returned by QSettingsConfigBackend::group() as a unique_ptr.  Only one
/// QSettingsConfigGroup may be active per backend at a time — destroy it
/// (let the unique_ptr go out of scope) before creating another.
/// Not copyable or movable.
class PLASMAZONES_EXPORT QSettingsConfigGroup
{
public:
    QSettingsConfigGroup(QSettings* settings, const QString& groupName, QSettingsConfigBackend* backend);
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
    QSettingsConfigBackend* m_backend; // not owned, for group-count tracking
};

/// Top-level config backend.  Owns the connection to the config store
/// and provides group access, sync, and enumeration.
class PLASMAZONES_EXPORT QSettingsConfigBackend
{
public:
    explicit QSettingsConfigBackend(const QString& filePath);
    ~QSettingsConfigBackend();

    /// Get a group view.  Caller owns the returned pointer.
    std::unique_ptr<QSettingsConfigGroup> group(const QString& name);

    /// Re-read config from disk (discard in-memory changes).
    void reparseConfiguration();

    /// Flush pending writes to disk.
    void sync();

    /// Delete an entire group and its keys.
    void deleteGroup(const QString& name);

    /// Read/write ungrouped (root-level) keys — outside any [Section] header.
    QString readRootString(const QString& key, const QString& defaultValue = {}) const;
    void writeRootString(const QString& key, const QString& value);
    void removeRootKey(const QString& key);

    /// List all top-level group names.
    QStringList groupList() const;

    /// Create the default config backend for the standard plasmazonesrc file.
    static std::unique_ptr<QSettingsConfigBackend> createDefault();

    /// Read config file directly from disk, bypassing Qt's QConfFile cache.
    /// Returns a QSettings::SettingsMap (QMap<QString, QVariant>) with all keys.
    static QMap<QString, QVariant> readConfigFromDisk();

    /// Resolve a shared or fallback backend. If @p shared is non-null it is
    /// returned directly; otherwise a new default backend is created into
    /// @p fallback and returned.  Eliminates repeated resolve boilerplate.
    static QSettingsConfigBackend* resolveBackend(QSettingsConfigBackend* shared,
                                                  std::unique_ptr<QSettingsConfigBackend>& fallback);

private:
    friend class QSettingsConfigGroup; // for group-count tracking
    QString m_filePath;
    std::unique_ptr<QSettings> m_settings;
    std::atomic<int> m_activeGroupCount{0};
};

} // namespace PlasmaZones
