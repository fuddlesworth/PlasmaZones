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

/// Current config schema version. Written by JsonBackend::sync() (fresh
/// installs, via the version stamp wired up in configbackends.cpp),
/// migrateIniToJson() (INI upgrades), and migrateV1ToV2() (schema upgrades).
/// v1: flat groups (Activation, Display, Appearance, etc.)
/// v2: nested dot-path groups (Snapping.Behavior.ZoneSpan, Tiling.Gaps, etc.)
inline constexpr int ConfigSchemaVersion = 2;

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
    ///
    /// Trusts PlasmaZones' single-writer-per-session model: once the guard
    /// latches, a later out-of-process rewrite of config.json (e.g. a user
    /// editing the file by hand, or a second daemon downgrading the schema
    /// mid-session) will NOT be re-detected by this function. Readers still
    /// re-open the file fresh on every load(), so config values themselves
    /// remain live — only the schema-version check is skipped. If you
    /// introduce a workflow that involves external rewrites during a
    /// session, drop the guard first via resetMigrationGuardForTesting().
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
    /// Convert an INI value to its JSON form. @p keyName is the leaf key
    /// (without group prefix); it's used to decide whether a comma-separated
    /// int list should be read as an r,g,b[,a] color — the content heuristic
    /// alone can't tell a color from e.g. a comma-separated layout order.
    static QJsonValue convertValue(const QString& keyName, const QVariant& value);
};

} // namespace PlasmaZones
