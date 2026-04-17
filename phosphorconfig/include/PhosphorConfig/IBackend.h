// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorconfig_export.h>

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <memory>

namespace PhosphorConfig {

/// Scoped view into a single configuration group.
///
/// Obtained from IBackend::group() as a unique_ptr. Implementations may
/// restrict the number of concurrently live groups per backend (the bundled
/// JsonBackend and QSettingsBackend enforce one-at-a-time — destroy the
/// unique_ptr before asking the backend for another group).
///
/// Groups support dot-path nesting: "Snapping.Behavior.ZoneSpan" addresses
/// a nested object tree in JsonBackend, and a plain "/"-joined group in
/// QSettingsBackend. Consumers that need other naming semantics (e.g.
/// per-screen overrides keyed by "Prefix:ScreenId") can plug in an
/// IGroupPathResolver on the backend to intercept and rewrite group names
/// before they reach storage.
class PHOSPHORCONFIG_EXPORT IGroup
{
public:
    virtual ~IGroup() = default;

    // Typed reads — return defaultValue when the key is absent, present-but-wrong-type,
    // or otherwise unparseable. Concrete backends document the exact coercion rules.
    virtual QString readString(const QString& key, const QString& defaultValue = {}) const = 0;
    virtual int readInt(const QString& key, int defaultValue = 0) const = 0;
    virtual bool readBool(const QString& key, bool defaultValue = false) const = 0;
    virtual double readDouble(const QString& key, double defaultValue = 0.0) const = 0;
    virtual QColor readColor(const QString& key, const QColor& defaultValue = {}) const = 0;

    /// Write a string. Stored verbatim — no content-dependent reinterpretation.
    ///
    /// Callers that need to persist structured data (array/object) should go
    /// through @c writeJson, which preserves the native JSON type where the
    /// backend supports it (JsonBackend stores as a native array/object;
    /// QSettingsBackend stringifies).
    virtual void writeString(const QString& key, const QString& value) = 0;
    virtual void writeInt(const QString& key, int value) = 0;
    virtual void writeBool(const QString& key, bool value) = 0;
    virtual void writeDouble(const QString& key, double value) = 0;
    virtual void writeColor(const QString& key, const QColor& value) = 0;

    /// Write a structured JSON value (array/object/scalar) natively where
    /// supported, or as a compact-JSON string for backends without a native
    /// representation.
    ///
    /// The default implementation serializes every value — including strings
    /// — to its compact JSON form before storing, so readJson can round-trip
    /// it by re-parsing. Strings land on disk quoted (@c "hello", not
    /// @c hello); consumers that need the raw string should go through
    /// @c writeString / @c readString directly. JsonBackend overrides this
    /// to keep the value as a native JSON node on disk.
    virtual void writeJson(const QString& key, const QJsonValue& value)
    {
        // QJsonDocument only accepts arrays/objects at the root. For scalar
        // values (including strings, so writeJson/readJson round-trip
        // correctly via a single uniform encoding) wrap in a one-element
        // array and strip the brackets; for arrays/objects emit the compact
        // form directly.
        if (value.isArray()) {
            writeString(key, QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)));
            return;
        }
        if (value.isObject()) {
            writeString(key, QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact)));
            return;
        }
        QJsonArray wrapped;
        wrapped.append(value);
        const QByteArray raw = QJsonDocument(wrapped).toJson(QJsonDocument::Compact);
        writeString(key, QString::fromUtf8(raw.mid(1, raw.size() - 2)));
    }

    /// Read a structured JSON value. Returns @c defaultValue when the key
    /// is absent or unparseable.
    ///
    /// The default implementation calls @c readString and parses the result.
    /// Uses @c hasKey (not string emptiness) to distinguish an absent key
    /// from a present empty-string, so empty-string values written through
    /// @c writeJson round-trip correctly. JsonBackend overrides to return
    /// the native JSON node directly without a string round-trip.
    virtual QJsonValue readJson(const QString& key, const QJsonValue& defaultValue = {}) const
    {
        if (!hasKey(key)) {
            return defaultValue;
        }
        const QString raw = readString(key);
        // Wrap so QJsonDocument::fromJson accepts bare scalars.
        const QByteArray wrapped = QByteArray("[") + raw.toUtf8() + QByteArray("]");
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(wrapped, &err);
        if (err.error != QJsonParseError::NoError) {
            return defaultValue;
        }
        const QJsonArray arr = doc.array();
        if (arr.size() != 1) {
            return defaultValue;
        }
        return arr.at(0);
    }

    // Key management.
    virtual bool hasKey(const QString& key) const = 0;
    virtual void deleteKey(const QString& key) = 0;

    /// Enumerate scalar leaf keys directly under this group. Nested sub-groups
    /// (dot-path children in JsonBackend) are NOT included — consumers that
    /// want to purge stale keys without touching declared descendants can
    /// iterate this list against the schema. Order is implementation-defined.
    virtual QStringList keyList() const = 0;

    IGroup(const IGroup&) = delete;
    IGroup& operator=(const IGroup&) = delete;

protected:
    IGroup() = default;
};

/// Pluggable configuration backend.
///
/// Provides group-based access, persistence, and group enumeration.
/// Concrete implementations: JsonBackend (atomic-write JSON file),
/// QSettingsBackend (INI files, for legacy compatibility and migration).
///
/// Root-level ("ungrouped") access is provided via readRootString /
/// writeRootString. How a backend stores root keys is
/// implementation-defined: QSettings routes them to a [General] section,
/// and JsonBackend mirrors that by keeping them under a configurable
/// root-group name (defaults to "General").
class PHOSPHORCONFIG_EXPORT IBackend
{
public:
    virtual ~IBackend() = default;

    /// Return a scoped view into the named group. Caller owns the pointer;
    /// destroy it before asking for another group on this backend.
    virtual std::unique_ptr<IGroup> group(const QString& name) = 0;

    /// Re-read configuration from disk, discarding any pending in-memory
    /// changes. Must not be called while a group view is live.
    virtual void reparseConfiguration() = 0;

    /// Flush pending writes to disk. No-op when nothing is dirty.
    /// Returns @c true on success (or when there was nothing to flush),
    /// @c false on an I/O error — backends log the reason before returning.
    virtual bool sync() = 0;

    /// Delete an entire group and everything inside it. Intermediate
    /// parents are pruned if they become empty (dot-path groups only).
    virtual void deleteGroup(const QString& name) = 0;

    /// Read/write ungrouped (root-level) keys.
    virtual QString readRootString(const QString& key, const QString& defaultValue = {}) const = 0;
    virtual void writeRootString(const QString& key, const QString& value) = 0;
    virtual void removeRootKey(const QString& key) = 0;

    /// Enumerate every top-level group name. Dot-path groups are returned
    /// with their full path ("Snapping", "Snapping.Behavior", ...).
    /// Groups produced by a plugged-in IGroupPathResolver appear in the
    /// resolver's preferred external form.
    virtual QStringList groupList() const = 0;

    IBackend(const IBackend&) = delete;
    IBackend& operator=(const IBackend&) = delete;

protected:
    IBackend() = default;
};

} // namespace PhosphorConfig
