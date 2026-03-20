// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Abstract config backend interface.
//
// Provides a grouped key-value store with typed read/write operations.
// Implementations can use QSettings (portable), KConfig (KDE), or any
// other config system (Quickshell, JSON, D-Bus, etc.).
//
// The interface mirrors the subset of KConfig API that PlasmaZones uses,
// making migration straightforward while keeping the door open for
// alternative backends.

#pragma once

#include <QColor>
#include <QString>
#include <QStringList>
#include <QVariant>
#include "plasmazones_export.h"

namespace PlasmaZones {

/// A view into a single config group (e.g., [Activation], [Display]).
/// Lightweight value type — cheap to copy/pass by value.
class PLASMAZONES_EXPORT ConfigGroup
{
public:
    virtual ~ConfigGroup() = default;

    // Typed reads with defaults
    virtual QString readString(const QString& key, const QString& defaultValue = {}) const = 0;
    virtual int readInt(const QString& key, int defaultValue = 0) const = 0;
    virtual bool readBool(const QString& key, bool defaultValue = false) const = 0;
    virtual double readDouble(const QString& key, double defaultValue = 0.0) const = 0;
    virtual QColor readColor(const QString& key, const QColor& defaultValue = {}) const = 0;

    // Typed writes
    virtual void writeString(const QString& key, const QString& value) = 0;
    virtual void writeInt(const QString& key, int value) = 0;
    virtual void writeBool(const QString& key, bool value) = 0;
    virtual void writeDouble(const QString& key, double value) = 0;
    virtual void writeColor(const QString& key, const QColor& value) = 0;

    // Key existence
    virtual bool hasKey(const QString& key) const = 0;
};

/// Top-level config backend.  Owns the connection to the config store
/// and provides group access, sync, and enumeration.
class PLASMAZONES_EXPORT IConfigBackend
{
public:
    virtual ~IConfigBackend() = default;

    /// Get a group view.  Caller owns the returned pointer.
    virtual std::unique_ptr<ConfigGroup> group(const QString& name) = 0;

    /// Re-read config from disk (discard in-memory changes).
    virtual void reparseConfiguration() = 0;

    /// Flush pending writes to disk.
    virtual void sync() = 0;

    /// Delete an entire group and its keys.
    virtual void deleteGroup(const QString& name) = 0;

    /// List all top-level group names.
    virtual QStringList groupList() const = 0;
};

} // namespace PlasmaZones
