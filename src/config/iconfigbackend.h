// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QColor>
#include <QString>
#include <QStringList>
#include <memory>

namespace PlasmaZones {

/// Abstract interface for a scoped view into a single config group.
///
/// Returned by IConfigBackend::group() as a unique_ptr.  Implementations
/// may restrict one active group per backend at a time.
class PLASMAZONES_EXPORT IConfigGroup
{
public:
    virtual ~IConfigGroup() = default;

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

    // Key management
    virtual bool hasKey(const QString& key) const = 0;
    virtual void deleteKey(const QString& key) = 0;

    IConfigGroup(const IConfigGroup&) = delete;
    IConfigGroup& operator=(const IConfigGroup&) = delete;

protected:
    IConfigGroup() = default;
};

/// Abstract interface for a pluggable config backend.
///
/// Provides group-based access, persistence, and enumeration.
/// Concrete implementations: QSettingsConfigBackend (INI), JsonConfigBackend (JSON).
class PLASMAZONES_EXPORT IConfigBackend
{
public:
    virtual ~IConfigBackend() = default;

    /// Get a group view.  Caller owns the returned pointer.
    virtual std::unique_ptr<IConfigGroup> group(const QString& name) = 0;

    /// Re-read config from disk (discard in-memory changes).
    virtual void reparseConfiguration() = 0;

    /// Flush pending writes to disk.
    virtual void sync() = 0;

    /// Delete an entire group and its keys.
    virtual void deleteGroup(const QString& name) = 0;

    /// Read/write ungrouped (root-level) keys.
    virtual QString readRootString(const QString& key, const QString& defaultValue = {}) const = 0;
    virtual void writeRootString(const QString& key, const QString& value) = 0;
    virtual void removeRootKey(const QString& key) = 0;

    /// List all top-level group names.
    virtual QStringList groupList() const = 0;

    IConfigBackend(const IConfigBackend&) = delete;
    IConfigBackend& operator=(const IConfigBackend&) = delete;

protected:
    IConfigBackend() = default;
};

/// Create the default config backend (currently JsonConfigBackend).
/// Decouples callers from the concrete backend type — only the factory
/// implementation (in configbackend_json.cpp) knows which class to instantiate.
PLASMAZONES_EXPORT std::unique_ptr<IConfigBackend> createDefaultConfigBackend();

/// Create a backend for session.json (ephemeral window tracking state).
/// Separate file from config.json to avoid write contention.
PLASMAZONES_EXPORT std::unique_ptr<IConfigBackend> createSessionBackend();

/// Resolve a shared or fallback backend.  If @p shared is non-null it is
/// returned directly; otherwise a new default backend is created into
/// @p fallback and returned.  Eliminates repeated resolve boilerplate.
inline IConfigBackend* resolveBackend(IConfigBackend* shared, std::unique_ptr<IConfigBackend>& fallback)
{
    if (shared) {
        return shared;
    }
    fallback = createDefaultConfigBackend();
    return fallback.get();
}

// ── Per-screen group helpers ─────────────────────────────────────────────
// Backend-agnostic utilities for per-screen group name resolution.
// Used by Settings, ConfigMigration, and config backend implementations.

/// JSON key for the per-screen container object.
inline constexpr char PerScreenKey[] = "PerScreen";

/// Current config schema version.  Written by sync() (fresh installs),
/// migrateIniToJson() (INI upgrades), and migrateV1ToV2() (schema upgrades).
/// v1: flat groups (Activation, Display, Appearance, etc.)
/// v2: nested dot-path groups (Snapping.Behavior.ZoneSpan, Tiling.Gaps, etc.)
inline constexpr int ConfigSchemaVersion = 2;

namespace detail {
struct PerScreenMapping
{
    const char* prefix; // e.g. "AutotileScreen"
    const char* category; // e.g. "Autotile"
};
inline constexpr PerScreenMapping kPerScreenMappings[] = {
    {"ZoneSelector", "ZoneSelector"},
    {"AutotileScreen", "Autotile"},
    {"SnappingScreen", "Snapping"},
};
} // namespace detail

/// Returns true if @p groupName uses a known per-screen prefix
/// (ZoneSelector:, AutotileScreen:, SnappingScreen:).
/// Assignment groups and other colon-containing names return false.
inline bool isPerScreenPrefix(const QString& groupName)
{
    if (groupName.isEmpty()) {
        return false;
    }
    for (const auto& m : detail::kPerScreenMappings) {
        const auto prefixLen = static_cast<int>(qstrlen(m.prefix));
        if (groupName.size() > prefixLen && groupName.startsWith(QLatin1String(m.prefix))
            && groupName.at(prefixLen) == QLatin1Char(':')) {
            return true;
        }
    }
    return false;
}

/// Map a per-screen group prefix (e.g. "AutotileScreen") to its JSON
/// category key (e.g. "Autotile").  ZoneSelector maps to itself.
inline QString prefixToCategory(const QString& prefix)
{
    for (const auto& m : detail::kPerScreenMappings) {
        if (prefix == QLatin1String(m.prefix)) {
            return QString::fromLatin1(m.category);
        }
    }
    return prefix;
}

/// Reverse of prefixToCategory: "Autotile" → "AutotileScreen", etc.
inline QString categoryToPrefix(const QString& category)
{
    for (const auto& m : detail::kPerScreenMappings) {
        if (category == QLatin1String(m.category)) {
            return QString::fromLatin1(m.prefix);
        }
    }
    return category;
}

} // namespace PlasmaZones
