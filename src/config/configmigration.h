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
///     animation overrides exclusively from the unified rule store.
///     The legacy `Exclusions` group (`Applications` / `WindowClasses`
///     comma-joined pattern lists) folds into the same windowrules.json: each
///     surviving pattern becomes an Application-subject `AppId AppIdMatches
///     <pattern>` matcher with a terminal `Exclude` action, matching the
///     shape the legacy runtime bridge produced (see
///     `appendExclusionRulesFromStash` in configmigration.cpp for the
///     builder) so an upgrading user's exclusion behaviour is preserved.
///     The standalone
///     "Exclusions" settings page disappears; the three global window-filtering
///     knobs (excludeTransientWindows / minimumWindowWidth /
///     minimumWindowHeight) move to the General page. See
///     docs/window-rule-refactor-design.md §8.
///     Each layout's retired per-layout `appRules` triple
///     (`{pattern, zoneNumber, targetScreen}`) also folds into windowrules.json
///     as an `AppId AppIdMatches <pattern> → SnapToZone [zoneNumber]` rule,
///     deduped by normalized pattern across layouts. A legacy `targetScreen` is
///     carried over as a companion `RouteToScreen` action so the app reopens on
///     its pinned monitor. See appendLayoutAppRulesAsSnapToZone in
///     configmigration.cpp.
///     Additionally renames the drag-time zone-overlay groups
///     Snapping.Appearance.{Colors,Opacity,Border,Labels} → Snapping.Zones.*,
///     freeing the Snapping.Appearance.* namespace for the new per-window
///     snapped-window decoration settings (snapping*). See moveGroupAtPath
///     in configmigration.cpp.
///     v4 also relocates per-layout SETTINGS out of the layout files. The
///     settings that used to be embedded in each layout JSON (per-zone
///     appearance, gap/padding overrides, showZoneNumbers, overlay display mode,
///     auto-assign, shader binding) move into a single layout-settings.json
///     sidecar keyed by layout UUID — the same sibling-store pattern as
///     windowrules.json / quicklayouts.json. Layout files keep only their
///     structural definition (zones, geometry, identity, matching rules). The
///     relocation runs from finalizeV4Conversion (see relocateLayoutSettings),
///     and the runtime LayoutSettingsStore (in phosphor-zones) merges the
///     sidecar back onto each layout on load, so the in-memory model is
///     unchanged.
inline constexpr int ConfigSchemaVersion = 5;

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
    ///   - Removes the `Exclusions.Applications` and `Exclusions.WindowClasses`
    ///     comma-joined pattern lists and stashes them under the temporary
    ///     `_v4ExclusionStash` root key.
    ///   - Removes the `Animations.WindowFiltering.Applications` and
    ///     `Animations.WindowFiltering.WindowClasses` comma-joined pattern
    ///     lists and stashes them under the temporary
    ///     `_v4AnimationExclusionStash` root key.
    /// All four stashes feed @ref finalizeV4Conversion. Empty inputs produce
    /// no stash entries (the finalizer treats an absent key as a no-op for
    /// that input). Stamps `_version = 4`.
    static void migrateV3ToV4(QJsonObject& root);

    /// Post-chain finalizer for the v4 conversion. The cross-file migration
    /// (config.json + assignments.json → windowrules.json) cannot live in a
    /// single `void(QJsonObject&)` migration step, so this runs after the
    /// chain, from @ref ensureJsonConfigImpl.
    ///
    /// It reads assignments.json + the four `_v4*` stashes left in
    /// config.json (`_v4DisableStash`, `_v4AnimationRulesStash`,
    /// `_v4ExclusionStash`, `_v4AnimationExclusionStash`), builds the
    /// WindowRuleSet (assignment rules + disable-list rules + per-window
    /// animation-override rules ported from the legacy AnimationAppRule
    /// JSON + `Exclude`-action rules + `ExcludeAnimations`-action rules),
    /// writes windowrules.json (atomic), relocates the QuickLayouts slots
    /// into the quicklayouts.json sidecar, strips all four stash keys
    /// from config.json, then — as the last, irreversible step — retires
    /// assignments.json (renamed to `.migrated` for forensic recovery; if
    /// the rename fails the file is removed outright).
    ///
    /// Idempotent: the cleanup-only branch runs whenever windowrules.json
    /// already exists as a valid v4 `WindowRuleSet` (probed via
    /// `WindowRuleSet::loadFromFile`, which requires `_version ==
    /// ConfigSchemaVersion` exactly). It is NOT a strict no-op — it
    /// retries the still-pending tail steps (strip surviving `_v4*Stash`
    /// keys, retire a still-present assignments.json) so a partial earlier
    /// run that crashed between windowrules.json commit and the tail
    /// converges to a clean state on the next startup. The rule-rebuild
    /// path itself NEVER runs from the cleanup branch.
    ///
    /// Rebuild trigger: a missing/invalid windowrules.json triggers a
    /// rebuild PROVIDED config.json has reached
    /// `_version == ConfigSchemaVersion`. When assignments.json is absent
    /// the rebuild still runs and writes a rule set carrying only the
    /// provider-default catch-all (plus any disable-list, animation-rule,
    /// exclusion, and animation-exclusion stash entries from
    /// config.json). If the migration chain stalled below v4 (e.g. a
    /// chain step's side-effect write failed), finalizeV4Conversion
    /// refuses to commit a stub windowrules.json so the next run can
    /// retry the chain without masking the stall. The
    /// rebuild-vs-cleanup-only decision keys off the windowrules.json
    /// probe alone; the config-version gate is layered on top to refuse
    /// the stub case.
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

    /// v4 → v5 schema step. The v4 schema shipped per-mode (separate Snapping
    /// vs Tiling) window appearance (borders, title bars, colours) and gap
    /// settings in config.json; this branch deleted those settings and routes
    /// the same concerns through window rules instead. This step reads the
    /// deleted v4 groups (`Snapping.Appearance.*`, `Snapping.Gaps`, the
    /// `Tiling.*` equivalents, and the per-screen `PerScreen.Snapping` /
    /// `PerScreen.Autotile` gap subsets), stashes the values that DIFFER from
    /// the v4 compile defaults under the temporary `_v5AppearanceStash` root
    /// key, REMOVES the consumed keys/groups from config.json (leaving the
    /// surviving non-appearance keys such as `Snapping.Gaps.AdjacentThreshold`
    /// and `Tiling.Gaps.SmartGaps` in place), and stamps `_version = 5`. A
    /// clean v4 config (everything at its default) stashes nothing. The stash
    /// feeds @ref finalizeV5Conversion.
    static void migrateV4ToV5(QJsonObject& root);

    /// Post-chain finalizer for the v5 conversion. Mirrors
    /// @ref finalizeV4Conversion: the override rules cannot be built inside a
    /// `void(QJsonObject&)` migration step (they live in windowrules.json), so
    /// this runs after the chain from @ref ensureJsonConfigImpl, right after
    /// finalizeV4Conversion (which guarantees windowrules.json exists).
    ///
    /// It reads `_v5AppearanceStash` from config.json, converts each differing
    /// value into a non-managed override WindowRule — two IsTiled-matched rules
    /// ("Snapping appearance" matching `IsTiled Equals false`, "Tiling
    /// appearance" matching `IsTiled Equals true`) plus one `ScreenId`-matched
    /// gap rule per per-screen entry — ADDS them to the existing rule set
    /// (never clobbering user rules or the managed baselines), saves
    /// windowrules.json, then strips `_v5AppearanceStash` from config.json.
    ///
    /// Idempotent: runs only while the stash is present. Once stripped it is a
    /// no-op, and the override rule ids are deterministic so a re-run before the
    /// strip succeeds cannot duplicate rules (addRule rejects colliding ids).
    ///
    /// @param jsonPath Path to config.json (windowrules.json is derived as a
    ///                 sibling via ConfigDefaults).
    /// @return true on success or a clean no-op; false on an I/O failure.
    static bool finalizeV5Conversion(const QString& jsonPath);

    /// Part of the v4 conversion: read every `*.json` layout in @p layoutsDir,
    /// split its embedded per-layout settings into the @p sidecarPath store
    /// (keyed by layout UUID, in the LayoutSettingsStore format), and rewrite the
    /// layout file stripped of those settings. Merges into an existing sidecar
    /// rather than clobbering it, and skips already-slimmed files, so it is
    /// idempotent and crash-safe — finalizeV4Conversion calls it on every run.
    /// A missing layouts dir is a no-op success. Returns false only on a write
    /// failure. Public for direct testing.
    static bool relocateLayoutSettings(const QString& layoutsDir, const QString& sidecarPath);

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
