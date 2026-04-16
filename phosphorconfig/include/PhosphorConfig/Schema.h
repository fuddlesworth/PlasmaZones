// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorconfig_export.h>

#include <QHash>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QVariant>
#include <QVector>

#include <functional>
#include <memory>

namespace PhosphorConfig {

class IGroupPathResolver;

/// Declaration for a single configuration key.
///
/// A consumer describes each key it persists once, and PhosphorConfig handles
/// defaults, reset-to-default, export/import, and migration-friendly lookups.
///
/// @c expectedType is an optional hint used by Store::write() to warn when a
/// caller supplies a value of a mismatched type. Leave as @c QMetaType::UnknownType
/// to disable the check.
///
/// @c validator runs on both read and write paths, giving consumers a single
/// place to clamp ranges (@c qBound), normalize enum-style strings, or log
/// and fall back to the default on invalid input. The returned QVariant is
/// the value the caller observes (on read) or persists (on write). When
/// unset, values pass through unchanged.
struct PHOSPHORCONFIG_EXPORT KeyDef
{
    QString key;
    QVariant defaultValue;
    QMetaType::Type expectedType = QMetaType::UnknownType;

    /// Human-readable description. Not used at runtime — consumers may
    /// surface it in settings UIs or generated documentation.
    QString description;

    /// Coercion applied on every read and every write. Typical uses:
    ///   - qBound(min, v.toInt(), max)
    ///   - normalize a string to one of a fixed set of values
    ///   - log a warning and return defaultValue on invalid input
    /// The QVariant signature lets one validator slot cover every type.
    std::function<QVariant(const QVariant& value)> validator;
};

/// One step in a schema version migration chain. Transforms the root JSON
/// document in place from @c fromVersion to @c fromVersion+1. Implementations
/// MUST stamp the new version under @c Schema::versionKey before returning;
/// MigrationRunner verifies the bump happened and aborts the chain otherwise.
struct PHOSPHORCONFIG_EXPORT MigrationStep
{
    int fromVersion = 0;
    std::function<void(QJsonObject&)> migrate;
};

/// Declarative description of a configuration store.
///
/// Aggregates the current schema version, the JSON key that holds it, the set
/// of groups and their keys (with defaults), any registered migrations, and an
/// optional path resolver for custom group-name semantics.
///
/// The struct is plain data; construct it once at startup and hand it to
/// @c Store (and/or @c MigrationRunner).
struct PHOSPHORCONFIG_EXPORT Schema
{
    /// Current schema revision. Fresh stores are stamped with this value on
    /// their first successful sync. Increment in lockstep with a new
    /// MigrationStep whenever you change the on-disk shape.
    int version = 1;

    /// JSON key used to persist the schema version. Defaults to "_version"
    /// so it sorts to the top of most JSON libraries and won't collide with
    /// a user key. Change only if you have a legacy store with a different
    /// convention.
    QString versionKey = QStringLiteral("_version");

    /// Keys grouped by their group name. The group name format depends on
    /// the attached resolver + backend combination — the schema itself is
    /// agnostic to whether it's flat ("General"), dot-path
    /// ("Snapping.Behavior"), or resolver-translated ("Prefix:ScreenId").
    QHash<QString, QVector<KeyDef>> groups;

    /// Ordered migration chain. MigrationRunner applies every step whose
    /// @c fromVersion matches the current persisted version, advancing one
    /// version at a time until it reaches @c Schema::version.
    QVector<MigrationStep> migrations;

    /// Optional per-screen / custom name resolver. Ownership is shared so a
    /// single resolver instance can be attached to multiple backends/stores.
    std::shared_ptr<IGroupPathResolver> pathResolver;

    /// Look up a KeyDef by (group, key). Returns nullptr if the key is not
    /// declared in the schema.
    const KeyDef* findKey(const QString& group, const QString& key) const;

    /// Convenience: default value for a declared key, or @c QVariant() if
    /// the key is undeclared.
    QVariant defaultFor(const QString& group, const QString& key) const;
};

} // namespace PhosphorConfig
