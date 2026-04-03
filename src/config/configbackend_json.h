// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// JSON-based config backend.  Reads/writes ~/.config/plasmazones/config.json
// as structured JSON, supporting nested groups and native complex types.

#pragma once

#include "iconfigbackend.h"
#include "plasmazones_export.h"
#include <QColor>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <atomic>
#include <memory>

namespace PlasmaZones {

class JsonConfigBackend; // forward declare for group back-pointer

/// A view into a single config group within the JSON document.
///
/// Returned by JsonConfigBackend::group() as a unique_ptr.  Only one
/// JsonConfigGroup may be active per backend at a time (enforced in debug
/// builds) to match the QSettingsConfigBackend contract.
class PLASMAZONES_EXPORT JsonConfigGroup : public IConfigGroup
{
public:
    JsonConfigGroup(QJsonObject& root, const QString& groupName, JsonConfigBackend* backend);
    ~JsonConfigGroup() override;

    JsonConfigGroup(JsonConfigGroup&&) = delete;
    JsonConfigGroup& operator=(JsonConfigGroup&&) = delete;

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
    /// Resolve the group object from the root, creating it if needed for writes.
    QJsonObject groupObject() const;
    void setGroupObject(const QJsonObject& obj);

    /// Per-screen groups use a nested path: PerScreen/<Category>/<ScreenId>
    bool isPerScreenGroup() const;
    struct PerScreenPath
    {
        QString category; // e.g. "ZoneSelector", "Autotile", "Snapping"
        QString screenId; // e.g. "EDID-DELL-1234"
    };
    PerScreenPath parsePerScreenGroup() const;

    QJsonObject& m_root; // not owned
    QString m_groupName;
    JsonConfigBackend* m_backend; // not owned, for group-count tracking and dirty flag
};

/// Top-level config backend using JSON format.
///
/// Implements IConfigBackend.  Stores config as a nested JSON document
/// with atomic writes (temp file + rename) for crash safety.
class PLASMAZONES_EXPORT JsonConfigBackend : public IConfigBackend
{
public:
    explicit JsonConfigBackend(const QString& filePath);
    ~JsonConfigBackend() override;

    // IConfigBackend interface
    std::unique_ptr<IConfigGroup> group(const QString& name) override;
    void reparseConfiguration() override;
    void sync() override;
    void deleteGroup(const QString& name) override;
    QString readRootString(const QString& key, const QString& defaultValue = {}) const override;
    void writeRootString(const QString& key, const QString& value) override;
    void removeRootKey(const QString& key) override;
    QStringList groupList() const override;

    /// Read config file directly from disk as a flat QMap for compatibility
    /// with WindowTrackingAdaptor::loadState(). Flattens nested JSON into
    /// "Group/Key" format matching the QSettings convention.
    /// If @p filePath is empty, reads from the default config path.
    static QMap<QString, QVariant> readConfigFromDisk(const QString& filePath = {});

    /// Atomically write a QJsonObject to disk (temp file + rename).
    /// Shared by sync() and ConfigMigration to avoid duplicated write logic.
    /// Returns true on success.
    static bool writeJsonAtomically(const QString& filePath, const QJsonObject& root);

private:
    friend class JsonConfigGroup; // for group-count tracking and dirty flag

    void loadFromDisk();
    void markDirty();

    QString m_filePath;
    QJsonObject m_root;
    bool m_dirty = false;
    // std::atomic despite single-threaded use: prevents UB if a group outlives
    // a backend move/copy (deleted, but guards against future refactoring).
    std::atomic<int> m_activeGroupCount{0};
};

} // namespace PlasmaZones
