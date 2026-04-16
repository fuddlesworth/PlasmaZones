// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorconfig_export.h>

#include <PhosphorConfig/IBackend.h>

#include <QMap>
#include <QSettings>
#include <QString>
#include <QVariant>

#include <memory>

namespace PhosphorConfig {

class QSettingsBackend;

/// Scoped group view backed by @c QSettings. See @c IGroup for the base
/// contract.
///
/// The underlying @c QSettings is single-threaded and stateful (it tracks
/// "current group"), so only one group view may exist per backend at a
/// time — debug builds assert, release builds warn.
class PHOSPHORCONFIG_EXPORT QSettingsGroup : public IGroup
{
public:
    QSettingsGroup(QSettings* settings, QString groupName, QSettingsBackend* backend);
    ~QSettingsGroup() override;

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
    QSettings* m_settings;
    QString m_group;
    QSettingsBackend* m_backend;
};

/// @c QSettings-backed configuration backend (INI format).
///
/// Primarily exists to read legacy INI files during a one-time migration
/// to the JSON backend. New consumers should prefer @c JsonBackend.
class PHOSPHORCONFIG_EXPORT QSettingsBackend : public IBackend
{
public:
    explicit QSettingsBackend(const QString& filePath);
    ~QSettingsBackend() override;

    // IBackend interface
    std::unique_ptr<IGroup> group(const QString& name) override;
    void reparseConfiguration() override;
    void sync() override;
    void deleteGroup(const QString& name) override;
    QString readRootString(const QString& key, const QString& defaultValue = {}) const override;
    void writeRootString(const QString& key, const QString& value) override;
    void removeRootKey(const QString& key) override;
    QStringList groupList() const override;

    /// Read an INI config from an arbitrary file path, bypassing Qt's
    /// QConfFile cache. Returns a flat QMap keyed by "Group/Key".
    /// Used by migration chains that want to consume a legacy INI without
    /// installing it as an active QSettings source.
    static QMap<QString, QVariant> readConfigFromDisk(const QString& filePath);

private:
    friend class QSettingsGroup;

    void incActiveGroupCount();
    void decActiveGroupCount();
    int activeGroupCount() const;

    QString m_filePath;
    std::unique_ptr<QSettings> m_settings;
    int m_activeGroupCount = 0;
};

} // namespace PhosphorConfig
