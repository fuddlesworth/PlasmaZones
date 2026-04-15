// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Config migration system with versioned migration chain.
// Handles INI→JSON conversion and sequential schema upgrades (v1→v2→v3→...).
// Each version bump adds one migration function; the chain runs automatically.

#pragma once

#include "plasmazones_export.h"
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVariant>
#include <span>

namespace PlasmaZones {

/// A single schema migration step: transforms root JSON in-place from
/// fromVersion to fromVersion+1, then stamps the new _version.
struct MigrationStep
{
    int fromVersion;
    void (*migrate)(QJsonObject& root);
};

class PLASMAZONES_EXPORT ConfigMigration
{
public:
    /// Run all needed migrations (INI→JSON and/or schema upgrades).
    ///
    /// Returns true if:
    /// - config.json already exists at current version, or
    /// - No old config exists (fresh install), or
    /// - All migrations succeeded
    ///
    /// Returns false if any migration failed (old file preserved, error logged).
    ///
    /// Internally short-circuits after the first successful call in a given
    /// process, so repeated invocations on the editor startup hot path don't
    /// re-read and re-parse the config file. Tests that swap the backing
    /// config file underneath the process must call
    /// resetMigrationGuardForTesting() between cases — see that method's
    /// docs for the rationale.
    static bool ensureJsonConfig();

    /// Reset the process-level "already migrated" flag set by
    /// ensureJsonConfig(). Exists so test harnesses can reuse a single
    /// process to exercise multiple migration scenarios against different
    /// isolated config directories — in production code the guard is
    /// strictly one-way and this should never be called.
    static void resetMigrationGuardForTesting();

    /// Convert an INI config file to JSON format. Produces v1 JSON.
    /// Used by ensureJsonConfig() for one-time INI migration,
    /// and by settings import for legacy INI files.
    static bool migrateIniToJson(const QString& iniPath, const QString& jsonPath);

    /// Run the schema migration chain on a JSON config file.
    /// Reads the file, applies all steps from current _version to
    /// ConfigSchemaVersion, writes atomically.
    static bool runMigrationChain(const QString& jsonPath);

    /// Run the migration chain in-memory (for INI→JSON + upgrade in one pass).
    static void runMigrationChainInMemory(QJsonObject& root);

    /// The ordered list of all migration steps.
    static std::span<const MigrationStep> migrationSteps();

    // Schema migration functions (one per version bump).
    // Public so the MigrationStep function pointers can reference them.
    static void migrateV1ToV2(QJsonObject& root);
    // static void migrateV2ToV3(QJsonObject& root);  // future

private:
    ConfigMigration() = default;

    /// Actual implementation of ensureJsonConfig() — runs the file
    /// check / INI→JSON / version-upgrade logic unconditionally. The
    /// public ensureJsonConfig() wraps this in the process-level
    /// short-circuit guard so repeat calls on the startup hot path are
    /// free.
    static bool ensureJsonConfigImpl();

    // INI→JSON helpers
    static QJsonObject iniMapToJson(const QMap<QString, QVariant>& flatMap);
    static QJsonValue convertValue(const QVariant& value);
};

} // namespace PlasmaZones
