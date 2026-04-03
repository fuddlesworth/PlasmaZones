// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// QSettings-based config backend (legacy).  Used for reading old
// ~/.config/plasmazonesrc INI files during migration to JSON.

#pragma once

#include "iconfigbackend.h"
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
class PLASMAZONES_EXPORT QSettingsConfigGroup : public IConfigGroup
{
public:
    QSettingsConfigGroup(QSettings* settings, const QString& groupName, QSettingsConfigBackend* backend);
    ~QSettingsConfigGroup() override;

    QSettingsConfigGroup(QSettingsConfigGroup&&) = delete;
    QSettingsConfigGroup& operator=(QSettingsConfigGroup&&) = delete;

    // IConfigGroup interface
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
    QSettingsConfigBackend* m_backend; // not owned, for group-count tracking
};

/// Top-level config backend using QSettings (INI format).
///
/// Implements IConfigBackend.  Used for reading legacy plasmazonesrc files
/// and as the migration source for the JSON config backend.
class PLASMAZONES_EXPORT QSettingsConfigBackend : public IConfigBackend
{
public:
    explicit QSettingsConfigBackend(const QString& filePath);
    ~QSettingsConfigBackend() override;

    // IConfigBackend interface
    std::unique_ptr<IConfigGroup> group(const QString& name) override;
    void reparseConfiguration() override;
    void sync() override;
    void deleteGroup(const QString& name) override;
    QString readRootString(const QString& key, const QString& defaultValue = {}) const override;
    void writeRootString(const QString& key, const QString& value) override;
    void removeRootKey(const QString& key) override;
    QStringList groupList() const override;

    /// Create a backend for the legacy plasmazonesrc file.
    /// Used only by migration and tests — runtime code uses JsonConfigBackend.
    static std::unique_ptr<QSettingsConfigBackend> createDefault();

    /// Read an INI config from a specific file path, bypassing Qt's QConfFile cache.
    /// Returns a flat QMap<QString, QVariant> with "Group/Key" keys.
    static QMap<QString, QVariant> readConfigFromDisk(const QString& filePath);

private:
    friend class QSettingsConfigGroup; // for group-count tracking
    QString m_filePath;
    std::unique_ptr<QSettings> m_settings;
    std::atomic<int> m_activeGroupCount{0};
};

} // namespace PlasmaZones
