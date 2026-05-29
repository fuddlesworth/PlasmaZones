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

namespace PlasmaZones {

/// Current config schema version. Written by JsonBackend::sync() (fresh
/// installs, via the version stamp wired up in configbackends.cpp),
/// migrateIniToJson() (INI upgrades), and migrateV1ToV2() (schema upgrades).
/// v1: flat groups (Activation, Display, Appearance, etc.)
/// v2: nested dot-path groups (Snapping.Behavior.ZoneSpan, Tiling.Gaps, etc.)
/// v3: per-mode disable lists — Snapping.Behavior.Display.{Disabled*} relocates
///     to Display.{Snapping,Autotile}Disabled{Monitors,Desktops,Activities}.
/// v4: window-rule consolidation — zone Assignments (assignments.json) and the
///     per-mode disable lists become context-only WindowRules in the new
///     windowrules.json store. config.json loses the Display.*Disabled* keys;
///     assignments.json is superseded. QuickLayouts slots relocate to the
///     quicklayouts.json sidecar. The Animations.AnimationAppRules array also
///     folds into windowrules.json as OverrideAnimation{Shader,Timing} actions
///     on `WindowClass Contains <pattern>` matchers — the legacy
///     AnimationAppRule/Bridge types are removed and the runtime reads
///     animation overrides exclusively from the unified rule store. See
///     docs/window-rule-refactor-design.md §8.
inline constexpr int ConfigSchemaVersion = 4;

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

    // Schema migration functions (one per version bump).
    // Public so the `PhosphorConfig::MigrationStep` registry built in
    // makeMigrationSchema() can take their addresses.
    static void migrateV1ToV2(QJsonObject& root);
    static void migrateV2ToV3(QJsonObject& root);

    /// v3 → v4 schema step. Each migration step has signature
    /// `void(QJsonObject&)` — it can only touch config.json. This step:
    ///   - Removes the Display.*Disabled* keys and stashes their values under
    ///     the temporary `_v4DisableStash` root key.
    ///   - Removes the `Animations.AnimationAppRules` array and stashes it
    ///     under the temporary `_v4AnimationRulesStash` root key.
    /// Both stashes feed @ref finalizeV4Conversion. Empty inputs produce no
    /// stash entries (the finalizer treats an absent key as a no-op for that
    /// input). Stamps `_version = 4`.
    static void migrateV3ToV4(QJsonObject& root);

    /// Post-chain finalizer for the v4 conversion. The cross-file migration
    /// (config.json + assignments.json → windowrules.json) cannot live in a
    /// single `void(QJsonObject&)` migration step, so this runs after the
    /// chain, from @ref ensureJsonConfigImpl.
    ///
    /// It reads assignments.json + the `_v4DisableStash` and
    /// `_v4AnimationRulesStash` left in config.json, builds the
    /// WindowRuleSet (assignment rules + disable-list rules + per-window
    /// animation-override rules ported from the legacy AnimationAppRule
    /// JSON), writes windowrules.json (atomic), relocates the QuickLayouts
    /// slots into the quicklayouts.json sidecar, strips both stash keys
    /// from config.json, then — as the last, irreversible step — deletes
    /// assignments.json.
    ///
    /// Idempotent: a no-op when windowrules.json already exists at
    /// `_version >= 4`. Safe to call on every startup.
    ///
    /// Rebuild trigger: a missing windowrules.json with a surviving
    /// assignments.json ALWAYS triggers a rebuild, independent of any
    /// config.json corruption context. There is no separate "config is
    /// corrupt" guard — the rebuild keys solely off the two sidecar files.
    ///
    /// Degraded path under config corruption: if config.json is corrupt (or
    /// absent) with no INI fallback, the rebuild proceeds with an empty
    /// `configRoot`, so the provider-default rule is derived with empty
    /// `DefaultLayoutId` / tiling-algorithm and degrades to the bare snapping
    /// placeholder. This is accepted degradation — no regression versus the
    /// pre-PR behaviour — and is intentionally not treated as a failure.
    ///
    /// @param jsonPath Path to config.json (assignments.json / windowrules.json
    ///                 are derived as siblings via ConfigDefaults).
    /// @return true on success or a clean no-op; false on an I/O failure.
    static bool finalizeV4Conversion(const QString& jsonPath);

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
