// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configmigration.h"

#include "configbackends.h"
#include "configdefaults.h"
#include "configkeys.h"
#include "perscreenresolver.h"
#include "settings.h"
#include "configmigration_v4detail.h"

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorConfig/MigrationRunner.h>
#include <PhosphorConfig/QSettingsBackend.h>
#include <PhosphorConfig/Schema.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/IdentityKey.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleSet.h>
#include <PhosphorZones/LayoutRegistry.h>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QLockFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

#include <array>
#include <atomic>
#include <optional>
#include <string_view>

namespace PlasmaZones {

namespace {

/// Parse a comma-separated disable list, dropping empties / whitespace.
QStringList parseDisableList(const QString& csv)
{
    QStringList out;
    for (const QString& part : csv.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty()) {
            out.append(trimmed);
        }
    }
    return out;
}

/// Build a context rule from a v3 monitor disable-list entry (`screenId`).
PhosphorRules::Rule disableRuleForMonitor(const QString& screenId, PhosphorZones::AssignmentEntry::Mode mode)
{
    const QString name = disableRulePrefixFor(mode) + screenId;
    return PhosphorRules::ContextRuleBridge::makeDisableRule(name, screenId, 0, QString(),
                                                             PhosphorZones::modeToWireString(mode),
                                                             PhosphorRules::ContextRuleBridge::kContextBandBase);
}

/// Build a context rule from a v3 desktop disable-list entry (`screenId/N`).
/// Returns nullopt on a malformed entry.
///
/// Screen ids MUST NOT contain '/': the desktop number is the last '/'-segment
/// (split on `lastIndexOf('/')`), so a screen id with embedded slashes would be
/// truncated. This matches the `screenId/desktop` composite-key convention used
/// by Settings::writeDisableEntries.
std::optional<PhosphorRules::Rule> disableRuleForDesktop(const QString& entry,
                                                         PhosphorZones::AssignmentEntry::Mode mode)
{
    const int slash = entry.lastIndexOf(QLatin1Char('/'));
    if (slash <= 0 || slash == entry.size() - 1) {
        return std::nullopt;
    }
    const QString screenId = entry.left(slash);
    bool ok = false;
    const int desktop = entry.mid(slash + 1).toInt(&ok);
    if (!ok || desktop <= 0) {
        return std::nullopt;
    }
    const QString name = disableRulePrefixFor(mode) + screenId + disableRuleDesktopSuffix(desktop);
    return PhosphorRules::ContextRuleBridge::makeDisableRule(name, screenId, desktop, QString(),
                                                             PhosphorZones::modeToWireString(mode),
                                                             PhosphorRules::ContextRuleBridge::kContextBandBase);
}

/// Build a context rule from a v3 activity disable-list entry
/// (`screenId/activityUuid`). Returns nullopt on a malformed entry.
///
/// Use `lastIndexOf('/')` so a disambiguated screen ID
/// (`Manuf:Model:Serial/CONNECTOR` per `PhosphorScreens::ScreenIdentity`)
/// splits at the activity boundary, not at the connector boundary inside
/// the screen ID. Activity UUIDs are canonical and never contain `/`, so
/// the trailing segment is unambiguously the activity uuid; everything
/// to the left is the screen ID (which may carry an embedded `/CONNECTOR`
/// suffix). Matches the live `Settings::writeDisableEntries` decoder in
/// src/config/settings.cpp.
std::optional<PhosphorRules::Rule> disableRuleForActivity(const QString& entry,
                                                          PhosphorZones::AssignmentEntry::Mode mode)
{
    const int slash = entry.lastIndexOf(QLatin1Char('/'));
    if (slash <= 0 || slash == entry.size() - 1) {
        return std::nullopt;
    }
    const QString screenId = entry.left(slash);
    const QString activity = entry.mid(slash + 1);
    const QString name = disableRulePrefixFor(mode) + screenId + disableRuleActivitySuffix();
    return PhosphorRules::ContextRuleBridge::makeDisableRule(name, screenId, 0, activity,
                                                             PhosphorZones::modeToWireString(mode),
                                                             PhosphorRules::ContextRuleBridge::kContextBandBase);
}

/// Parse one Assignment:* group name into (screenId, desktop, activity).
/// Returns false on a malformed name.
bool parseAssignmentGroup(const QString& groupName, const QString& prefix, QString& screenId, int& desktop,
                          QString& activity)
{
    if (!groupName.startsWith(prefix)) {
        return false;
    }
    QString remainder = groupName.mid(prefix.size());
    if (remainder.isEmpty()) {
        return false;
    }
    desktop = 0;
    activity.clear();
    // Anchor at the LAST occurrence of each suffix so a disambiguated
    // screen id like `Manuf:Model:Serial/CONNECTOR` that happens to
    // contain `:Activity:` or `:Desktop:` as a literal substring doesn't
    // truncate to a stub. Matches the screen-id parsing convention used by
    // disableRuleForDesktop / disableRuleForActivity above (both use
    // lastIndexOf). Production v3 screen ids don't carry those substrings
    // — this is defence against future screen-id formats.
    static const QLatin1String kActivityTag(":Activity:");
    static const QLatin1String kDesktopTag(":Desktop:");
    const int actIdx = remainder.lastIndexOf(kActivityTag);
    if (actIdx >= 0) {
        const QString a = remainder.mid(actIdx + kActivityTag.size());
        if (!a.isEmpty()) {
            activity = a;
        }
        remainder = remainder.left(actIdx);
    }
    const int deskIdx = remainder.lastIndexOf(kDesktopTag);
    if (deskIdx >= 0) {
        bool ok = false;
        const int d = remainder.mid(deskIdx + kDesktopTag.size()).toInt(&ok);
        if (ok && d > 0) {
            desktop = d;
        }
        remainder = remainder.left(deskIdx);
    }
    screenId = remainder;
    return !screenId.isEmpty();
}

/// Human-readable label for a migrated assignment rule. Reuses the same
/// suffix helpers the disable-rule writer goes through (configkeys.h)
/// so a future tweak to the suffix format (e.g. localising the bullet,
/// switching to ` — Desktop `) flows to both assignment and disable
/// rule names in lockstep — the rule editor stays scannable instead of
/// forking visual styles.
QString assignmentRuleName(const QString& screenId, int desktop, const QString& activity)
{
    QString name = screenId;
    if (desktop > 0) {
        name += disableRuleDesktopSuffix(desktop);
    }
    if (!activity.isEmpty()) {
        name += disableRuleActivitySuffix();
    }
    return name;
}

// Frozen on-disk keys for the v4 per-layout settings relocation. Local literals
// (NOT the live `ZoneJsonKeys::` / LayoutSettingsStore accessors) so a future
// rename of those live keys can never retarget this migration away from what v4
// layout files had on disk. The layout-settings.json format produced here MUST
// match what PhosphorZones::LayoutSettingsStore reads — a round-trip test locks
// the two together.
constexpr QLatin1String kLayoutIdKey{"id"};
constexpr QLatin1String kLayoutZonesKey{"zones"};
constexpr QLatin1String kLayoutAppearanceKey{"appearance"};
constexpr QLatin1String kSettingsVersionKey{"_version"};
constexpr QLatin1String kSettingsZoneAppearanceMapKey{"zoneAppearance"};
constexpr int kLayoutSettingsSchemaVersion = 1; // mirrors LayoutSettingsStore::SchemaVersion

// The per-LAYOUT setting keys that move out of the layout file into the sidecar.
// The per-ZONE appearance block is handled separately. Order is irrelevant.
constexpr std::array<QLatin1String, 14> kLayoutSettingKeys{{
    QLatin1String{"zonePadding"},
    QLatin1String{"outerGap"},
    QLatin1String{"usePerSideOuterGap"},
    QLatin1String{"outerGapTop"},
    QLatin1String{"outerGapBottom"},
    QLatin1String{"outerGapLeft"},
    QLatin1String{"outerGapRight"},
    QLatin1String{"showZoneNumbers"},
    QLatin1String{"overlayDisplayMode"},
    QLatin1String{"autoAssign"},
    // hiddenFromSelector is a user preference (which layouts the curated picker
    // shows), relocated to the sidecar by the runtime store. It MUST be listed
    // here too or a v3 user who hid a layout keeps the key embedded in the
    // (otherwise slimmed) layout file and it never reaches the sidecar.
    QLatin1String{"hiddenFromSelector"},
    QLatin1String{"useFullScreenGeometry"},
    QLatin1String{"shaderId"},
    QLatin1String{"shaderParams"},
}};

/// Extract the settings object (per-layout setting keys + per-zone appearance
/// map) from a full layout JSON, in the LayoutSettingsStore on-disk shape.
/// Returns an empty object when the layout carries no settings.
QJsonObject extractLayoutSettings(const QJsonObject& full)
{
    QJsonObject settings;
    for (const QLatin1String key : kLayoutSettingKeys) {
        if (full.contains(key)) {
            settings.insert(key, full.value(key));
        }
    }
    QJsonObject zoneAppearance;
    const QJsonArray zones = full.value(kLayoutZonesKey).toArray();
    for (const QJsonValue& zoneVal : zones) {
        const QJsonObject zone = zoneVal.toObject();
        const QString zoneId = zone.value(kLayoutIdKey).toString();
        if (!zoneId.isEmpty() && zone.contains(kLayoutAppearanceKey)) {
            zoneAppearance.insert(zoneId, zone.value(kLayoutAppearanceKey));
        }
    }
    if (!zoneAppearance.isEmpty()) {
        settings.insert(QString(kSettingsZoneAppearanceMapKey), zoneAppearance);
    }
    return settings;
}

/// Return the structural-only layout JSON: the full layout minus every settings
/// key and minus each zone's appearance block.
QJsonObject stripLayoutSettings(const QJsonObject& full)
{
    QJsonObject structural = full;
    for (const QLatin1String key : kLayoutSettingKeys) {
        structural.remove(key);
    }
    // Kept in lockstep with PhosphorZones::LayoutSettingsStore::stripSettings:
    // only touch zones when present, and only strip the appearance of an
    // id-bearing zone (its appearance is what extractLayoutSettings moved to the
    // sidecar map, keyed by zone id). An id-less zone keeps its inline appearance.
    if (structural.contains(kLayoutZonesKey)) {
        QJsonArray zones = structural.value(kLayoutZonesKey).toArray();
        for (int i = 0; i < zones.size(); ++i) {
            QJsonObject zone = zones.at(i).toObject();
            if (!zone.value(kLayoutIdKey).toString().isEmpty()) {
                zone.remove(kLayoutAppearanceKey);
                zones.replace(i, zone);
            }
        }
        structural.insert(QString(kLayoutZonesKey), zones);
    }
    return structural;
}

/// Worker for ConfigMigration::relocateLayoutSettings — kept in this anonymous
/// namespace so it can reach the frozen `kLayout*` literals above.
bool relocateLayoutSettingsImpl(const QString& layoutsDir, const QString& sidecarPath)
{
    QDir dir(layoutsDir);
    if (!dir.exists()) {
        return true; // nothing to relocate — not an error
    }

    // Merge into any existing sidecar rather than clobbering it, so a re-run
    // (or a sidecar already partly written by the runtime store) is preserved.
    QJsonObject sidecar;
    {
        QFile sf(sidecarPath);
        if (sf.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(sf.readAll());
            if (doc.isObject()) {
                sidecar = doc.object();
            }
        }
    }

    // Pass 1: scan every layout file, accumulate its settings into the in-memory
    // sidecar, and stage the slimmed structural body — but DON'T touch any layout
    // file on disk yet. The sidecar is the authoritative copy; it must be durably
    // written BEFORE any source file is slimmed, so a crash (or a sidecar write
    // failure) can never leave settings stripped from the layout file but absent
    // from the sidecar. Mirrors finalizeV4Conversion's "write rules.json
    // before retiring assignments.json" ordering.
    struct PendingStrip
    {
        QString path;
        QJsonObject structural;
    };
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    QList<PendingStrip> pending;
    bool sidecarDirty = false;
    for (const QString& fileName : files) {
        const QString path = dir.filePath(fileName);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            qWarning("ConfigMigration: layout-settings relocation could not read %s — skipping", qPrintable(path));
            continue;
        }
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        f.close();
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning("ConfigMigration: layout-settings relocation skipping unparseable %s", qPrintable(path));
            continue;
        }
        const QJsonObject full = doc.object();
        const QString layoutId = full.value(kLayoutIdKey).toString();
        const QJsonObject settings = extractLayoutSettings(full);
        if (layoutId.isEmpty() || settings.isEmpty()) {
            continue; // already slimmed, or no settings / unidentifiable — leave as-is
        }

        sidecar.insert(layoutId, settings);
        sidecarDirty = true;
        pending.append({path, stripLayoutSettings(full)});
    }

    if (!sidecarDirty) {
        return true; // every layout already slim — no writes, fully idempotent
    }

    // Commit the authoritative sidecar FIRST. If it can't be persisted, leave the
    // layout files untouched (their embedded settings are still read by the
    // runtime store) and report failure — the next run retries.
    sidecar.insert(QString(kSettingsVersionKey), kLayoutSettingsSchemaVersion);
    QDir().mkpath(QFileInfo(sidecarPath).absolutePath());
    if (!PhosphorConfig::JsonBackend::writeJsonAtomically(sidecarPath, sidecar)) {
        qWarning("ConfigMigration: layout-settings relocation failed to write %s — leaving layout files intact",
                 qPrintable(sidecarPath));
        return false;
    }

    // Pass 2: slim the source files now that their settings are durably stored.
    // A failure here is recoverable — the still-fat file keeps its embedded
    // settings (harmlessly re-applied by mergeSettings) and is re-slimmed on the
    // next run.
    bool allOk = true;
    for (const PendingStrip& p : pending) {
        if (!PhosphorConfig::JsonBackend::writeJsonAtomically(p.path, p.structural)) {
            qWarning("ConfigMigration: layout-settings relocation failed to rewrite %s", qPrintable(p.path));
            allOk = false;
        }
    }
    return allOk;
}

} // namespace

bool ConfigMigration::relocateLayoutSettings(const QString& layoutsDir, const QString& sidecarPath)
{
    return relocateLayoutSettingsImpl(layoutsDir, sidecarPath);
}

bool ConfigMigration::finalizeV4Conversion(const QString& jsonPath)
{
    const QString rulesPath = ConfigDefaults::rulesFilePath();
    const QString assignmentsPath = legacyAssignmentsFilePath();

    // ── Adopt the pre-v5 rule store filename ───────────────────────────────
    // The rule store moved from windowrules.json to rules.json in v5. A store
    // converted under the old name (or shipped at v4) still has windowrules.json
    // on disk; adopt it under the new name BEFORE the "already converted" gate
    // below probes rulesPath — otherwise a converted user reads as un-converted
    // and gets rebuilt from the retired assignments.json. Same same-directory
    // rename this function uses to retire assignments.json; idempotent (only
    // fires when the new file is absent and the legacy one present).
    {
        const QString legacyRulesPath = QFileInfo(rulesPath).absolutePath() + QStringLiteral("/windowrules.json");
        if (!QFile::exists(rulesPath) && QFile::exists(legacyRulesPath)) {
            if (QFile::rename(legacyRulesPath, rulesPath)) {
                qInfo("ConfigMigration: adopted legacy windowrules.json as rules.json");
            } else {
                qWarning("ConfigMigration: failed to adopt legacy windowrules.json as rules.json");
            }
        }
    }

    // ── Relocate per-layout settings out of the layout files (v4) ──────────
    // Independent of the rules/assignments machinery below: split each
    // layout file's embedded settings into the layout-settings.json sidecar and
    // slim the file. Idempotent (already-slim files are skipped) and best-effort
    // — a relocation failure leaves the settings embedded (still read by the
    // runtime store) and must not abort the v4 conversion, so its result does
    // not gate the return value.
    //
    // Deliberately runs BEFORE — and independent of — the config-version stall
    // gate further down (the `configVersion < ConfigSchemaVersion` guard that
    // refuses to commit rules.json on a stalled chain). The layout file
    // format is NOT tied to config.json's `_version`: layouts live in the data
    // dir, the version stamp lives in config.json. Relocating them is correct
    // and safe regardless of whether the config chain advanced, and the step is
    // crash-safe and idempotent, so running it on a stalled-chain retry is a
    // no-op-or-progress, never a regression.
    {
        const QString layoutsDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QLatin1Char('/') + ConfigDefaults::layoutsSubdir();
        if (!relocateLayoutSettings(layoutsDir, ConfigDefaults::layoutSettingsFilePath())) {
            qWarning(
                "ConfigMigration: per-layout settings relocation reported a write failure — "
                "affected layouts keep their embedded settings until next save");
        }
    }

    // ── Conversion-done vs cleanup-done — two SEPARATE concerns ─────────────
    // The conversion is multi-step: write rules.json (the irreversible
    // commit) → relocate QuickLayouts → strip config.json's `_v4DisableStash`
    // → delete assignments.json. These split into two questions that MUST NOT
    // be conflated:
    //
    //   "Is the v3→v4 conversion done?"  ⇒ does rules.json exist as a
    //       valid v4 RuleSet? If so the conversion IS done — the rule
    //       store is authoritative and may have since been edited by the user
    //       (rule editor) or Settings (disable lists). It must NEVER be
    //       rebuilt-and-overwritten from the dead assignments.json again.
    //
    //   "Is post-conversion cleanup done?" ⇒ assignments.json removed AND
    //       all four `_v4*Stash` scratch keys (`_v4DisableStash`,
    //       `_v4AnimationRulesStash`, `_v4ExclusionStash`,
    //       `_v4AnimationExclusionStash`) stripped from config.json. These
    //       tail steps are safe and idempotent; if they failed (read-only
    //       fs, lock) they are retried on the next run — but the rebuild
    //       is NOT.
    //
    // Probe "conversion done" by actually loading rules.json as a
    // RuleSet (named SchemaVersion check, not a bare `_version >= 4` on
    // an unrelated version namespace) — a file that parses as a v4 rule set is
    // by definition the completed conversion output.
    const bool rulesAlreadyConverted =
        QFile::exists(rulesPath) && PhosphorRules::RuleSet::loadFromFile(rulesPath).has_value();

    if (rulesAlreadyConverted) {
        // The conversion is complete. NEVER rebuild + overwrite rules.json
        // — doing so would silently destroy every user-authored rule and every
        // disable-list edit made since the first conversion. Only retry the
        // still-pending, idempotent cleanup steps.
        bool ok = true;

        // Strip the four v4 scratch keys (`_v4DisableStash`, `_v4AnimationRulesStash`,
        // `_v4ExclusionStash`, `_v4AnimationExclusionStash`) from config.json if any
        // survived a partial earlier run. Log every failure mode (open / parse /
        // not-object / write) so a leftover-stash configuration that can't be
        // cleaned surfaces as an actionable warning rather than silently succeeding.
        if (QFile::exists(jsonPath)) {
            QFile cf(jsonPath);
            if (!cf.open(QIODevice::ReadOnly)) {
                qWarning("ConfigMigration: cleanup retry: failed to open %s for reading: %s", qPrintable(jsonPath),
                         qPrintable(cf.errorString()));
                ok = false;
            } else {
                QJsonParseError err;
                const QJsonDocument doc = QJsonDocument::fromJson(cf.readAll(), &err);
                cf.close();
                if (err.error != QJsonParseError::NoError) {
                    qWarning("ConfigMigration: cleanup retry: %s did not parse as JSON: %s", qPrintable(jsonPath),
                             qPrintable(err.errorString()));
                    ok = false;
                } else if (!doc.isObject()) {
                    qWarning(
                        "ConfigMigration: cleanup retry: %s parsed but root is not an object — cannot strip "
                        "stash keys",
                        qPrintable(jsonPath));
                    ok = false;
                } else if (doc.object().contains(kV4DisableStashKey())
                           || doc.object().contains(kV4AnimationRulesStashKey())
                           || doc.object().contains(kV4ExclusionStashKey())
                           || doc.object().contains(kV4AnimationExclusionStashKey())) {
                    QJsonObject configRoot = doc.object();
                    configRoot.remove(kV4DisableStashKey());
                    configRoot.remove(kV4AnimationRulesStashKey());
                    configRoot.remove(kV4ExclusionStashKey());
                    configRoot.remove(kV4AnimationExclusionStashKey());
                    if (!PhosphorConfig::JsonBackend::writeJsonAtomically(jsonPath, configRoot)) {
                        qWarning("ConfigMigration: failed to strip v4 stash keys from %s during cleanup retry",
                                 qPrintable(jsonPath));
                        ok = false;
                    }
                }
            }
        }

        // Re-attempt the assignments.json cleanup. Quarantine to
        // `assignments.json.migrated` instead of deleting: a rename succeeds
        // on filesystems where remove may not (and the renamed file is inert —
        // nothing reads it), so a delete-failure can never loop forever.
        retireLegacyAssignmentsFile(assignmentsPath);

        // Prune the retired provider-default catch-all rule from rules.json.
        // Idempotent: once gone (or on a config that never had one) it is a
        // no-op. Folded here rather than into a schema bump because the gated
        // default resolver already ignores the rule at runtime.
        ok = pruneRetiredProviderDefaultRule(jsonPath) && ok;

        return ok;
    }

    // From here down: rules.json does NOT yet exist as a valid v4 rule
    // set — a genuine first run, or a crash before rules.json was
    // written. Only this path rebuilds and writes the rule store.

    // Pre-flight rules.json itself: the "already converted" probe
    // above gates on a `RuleSet::loadFromFile(...).has_value()` parse
    // check. If the file EXISTS but the loader returned nullopt (malformed
    // JSON, truncated write, hand-edit error), we'd otherwise fall through
    // to a rebuild that overwrites the corrupt-but-recoverable original
    // with a freshly-seeded rule set — destroying every user-authored
    // rule. Quarantine to `.corrupt.bak` and abort instead, mirroring the
    // assignments-prevalidate contract below.
    if (!prevalidateRulesFile(rulesPath)) {
        return false;
    }

    // Pre-flight the legacy assignments.json: a malformed sidecar must abort
    // BEFORE we write rules.json (otherwise we'd commit a rule set that
    // silently drops every assignment AND the quick-layout slots, and then
    // quarantine the corrupt original to `.migrated` — masking the failure as
    // a successful migration).
    // Defense-in-depth: the ensureJsonConfigImpl pre-chain guard catches the
    // version<schema entry; this catches the fresh-install / post-corruption
    // / already-current re-entry paths. (B5 data-loss fix.)
    if (!prevalidateLegacyAssignmentsFile(assignmentsPath)) {
        return false;
    }

    // ── Read config.json + extract the disable stash ───────────────────────
    QJsonObject configRoot;
    bool haveConfig = false;
    if (QFile::exists(jsonPath)) {
        QFile cf(jsonPath);
        if (cf.open(QIODevice::ReadOnly)) {
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(cf.readAll(), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                configRoot = doc.object();
                haveConfig = true;
            }
        }
    }

    // Refuse to commit the rules.json write below when config.json has
    // not actually reached the current schema version (the gate checks
    // configVersion < ConfigSchemaVersion). If a prior
    // run's migration chain stalled (e.g. migrateV1ToV2's side-effect
    // writes failed, leaving the disk-side _version at 1),
    // `MigrationRunner::runOnFile` returns `true` for a no-op chain —
    // ensureJsonConfigImpl then proceeds here with a still-v1
    // configRoot. Writing an empty/stub rules.json now would set
    // `rulesAlreadyConverted=true` on the next run; the cleanup-
    // only branch above would then strip all four stash keys
    // (populated by a later successful chain run) without porting them
    // into rules — permanently losing the user's disable lists,
    // animation app rules, snapping exclusion lists, AND animation
    // exclusion lists. Bail out and let the next run retry the chain.
    if (haveConfig) {
        const int configVersion = configRoot.value(ConfigKeys::versionKey()).toInt(0);
        if (configVersion < ConfigSchemaVersion) {
            qWarning(
                "ConfigMigration::finalizeV4Conversion: refusing to commit rules.json — "
                "config.json is still at v%d (target v%d). The migration chain did not advance; "
                "a stub rules.json now would mask the stalled chain on the next run and "
                "silently drop the user's disable lists / animation app rules when the chain "
                "eventually succeeds.",
                configVersion, ConfigSchemaVersion);
            return false;
        }
    }

    const QJsonObject stash = configRoot.value(kV4DisableStashKey()).toObject();
    const QJsonArray animationRulesStash = configRoot.value(kV4AnimationRulesStashKey()).toArray();
    const QJsonObject exclusionStash = configRoot.value(kV4ExclusionStashKey()).toObject();
    const QJsonObject animationExclusionStash = configRoot.value(kV4AnimationExclusionStashKey()).toObject();

    // ── Read assignments.json ──────────────────────────────────────────────
    // The prevalidate guard above already aborted on a malformed file, so a
    // parse failure here would only be a TOCTOU race (file replaced between
    // prevalidate and this read). Treat it the same way — abort rather than
    // silently dropping every assignment.
    QJsonObject assignmentsRoot;
    bool haveAssignments = false;
    if (QFile::exists(assignmentsPath)) {
        QFile af(assignmentsPath);
        if (af.open(QIODevice::ReadOnly)) {
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(af.readAll(), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                assignmentsRoot = doc.object();
                haveAssignments = true;
            } else {
                qCritical("ConfigMigration: %s became malformed after prevalidation (%s) — aborting v4 conversion",
                          qPrintable(assignmentsPath), qPrintable(err.errorString()));
                return false;
            }
        } else {
            // Prevalidate just passed for this path (top of finalizeV4Conversion),
            // so an open-failure here is a TOCTOU (permissions / lock flipped
            // between the prevalidate and this read). Treat it as
            // "no assignments to migrate" so the disable-only rule set still
            // gets written — but surface a warning so the user has a paper
            // trail when the migration silently produces an empty assignments
            // section.
            qWarning(
                "ConfigMigration: %s opened during prevalidation but failed to reopen here (%s) — "
                "continuing with an empty assignments set.",
                qPrintable(assignmentsPath), qPrintable(af.errorString()));
        }
    }

    // rules.json is always written below (see "Write rules.json"),
    // regardless of how much v3 data was found. When there is nothing to
    // convert (no stash, no assignments file) the `rules` list stays empty and
    // an empty rule set is written — the daemon's store still needs a stable
    // file to exist on disk.

    QList<PhosphorRules::Rule> rules;

    // ── Assignment rules ───────────────────────────────────────────────────
    QJsonObject quickLayoutsToRelocate;
    if (haveAssignments) {
        const QString prefix = ConfigKeys::Legacy::v3assignmentGroupPrefix();
        for (auto it = assignmentsRoot.constBegin(); it != assignmentsRoot.constEnd(); ++it) {
            const QString& groupName = it.key();
            if (groupName == ConfigKeys::Legacy::v3quickLayoutsGroup()) {
                // QuickLayouts slots are NOT rules — relocate them to the
                // quicklayouts.json sidecar.
                quickLayoutsToRelocate = it.value().toObject();
                continue;
            }
            QString screenId;
            int desktop = 0;
            QString activity;
            if (!parseAssignmentGroup(groupName, prefix, screenId, desktop, activity)) {
                continue;
            }
            const QJsonObject grp = it.value().toObject();
            // "Mode" / "SnappingLayout" / "TilingAlgorithm" are intentionally
            // frozen legacy field names — they belong to the dead v3
            // assignments.json format this finalizer is the last reader of.
            // They are NOT live config keys; the frozen `Legacy::v3Assignment*`
            // accessors keep the literals out of this call site so a future
            // editor of this function can't drift them by accident.
            const int modeInt = grp.value(ConfigKeys::Legacy::v3AssignmentMode()).toInt(0);
            // v3 only knew Snapping (0) and Autotile (1). modeToWireString
            // routes any future Mode through one mapping; the migration here
            // sees only the historical two-valued vocabulary.
            const auto mode =
                (modeInt == 1) ? PhosphorZones::AssignmentEntry::Autotile : PhosphorZones::AssignmentEntry::Snapping;
            const QString snappingLayout = grp.value(ConfigKeys::Legacy::v3AssignmentLayout()).toString();
            const QString tilingAlgorithm = grp.value(ConfigKeys::Legacy::v3AssignmentAlgorithm()).toString();

            // Priority is the only precedence value (highest wins per slot).
            // Seed migrated assignments in the Context band, nudged up by the
            // pinned-dimension count so a more-specific assignment still
            // outranks a broader one for the contexts they both match — this
            // explicit value reproduces the upgrader's prior effective
            // ordering now that the resolver no longer computes specificity.
            const int priority = PhosphorRules::ContextRuleBridge::kContextBandBase + (activity.isEmpty() ? 0 : 3)
                + (desktop > 0 ? 2 : 0) + (screenId.isEmpty() ? 0 : 1);

            rules.append(PhosphorRules::ContextRuleBridge::makeAssignmentRule(
                assignmentRuleName(screenId, desktop, activity), screenId, desktop, activity,
                PhosphorZones::modeToWireString(mode), snappingLayout, tilingAlgorithm, priority));
        }
    }

    // ── Disable-list rules ─────────────────────────────────────────────────
    // Collected into a separate list first so exact-duplicate
    // (mode, screen, desktop, activity) rules can be collapsed before being
    // merged into the final set — migrateV2ToV3 duplicates each v2 value into
    // both the snapping and autotile lists, so a stash carried forward from a
    // hand-edited or doubly-migrated config can hold the same entry twice.
    QList<PhosphorRules::Rule> disableRules;
    auto appendMonitorRules = [&disableRules](const QString& csv, PhosphorZones::AssignmentEntry::Mode mode) {
        for (const QString& entry : parseDisableList(csv)) {
            disableRules.append(disableRuleForMonitor(entry, mode));
        }
    };
    auto appendDesktopRules = [&disableRules](const QString& csv, PhosphorZones::AssignmentEntry::Mode mode) {
        for (const QString& entry : parseDisableList(csv)) {
            if (const auto rule = disableRuleForDesktop(entry, mode)) {
                disableRules.append(*rule);
            }
        }
    };
    auto appendActivityRules = [&disableRules](const QString& csv, PhosphorZones::AssignmentEntry::Mode mode) {
        for (const QString& entry : parseDisableList(csv)) {
            if (const auto rule = disableRuleForActivity(entry, mode)) {
                disableRules.append(*rule);
            }
        }
    };
    using PhosphorZones::AssignmentEntry;
    appendMonitorRules(stash.value(kStashSnappingMonitorsField).toString(), AssignmentEntry::Snapping);
    appendMonitorRules(stash.value(kStashAutotileMonitorsField).toString(), AssignmentEntry::Autotile);
    appendDesktopRules(stash.value(kStashSnappingDesktopsField).toString(), AssignmentEntry::Snapping);
    appendDesktopRules(stash.value(kStashAutotileDesktopsField).toString(), AssignmentEntry::Autotile);
    appendActivityRules(stash.value(kStashSnappingActivitiesField).toString(), AssignmentEntry::Snapping);
    appendActivityRules(stash.value(kStashAutotileActivitiesField).toString(), AssignmentEntry::Autotile);

    // Collapse exact-duplicate disable rules: dedup on the semantic identity
    // (mode-token, screenId, desktop, activity) so the migrated store is no
    // noisier than necessary.
    {
        namespace CRB = PhosphorRules::ContextRuleBridge;
        QSet<QString> seen;
        for (const PhosphorRules::Rule& rule : std::as_const(disableRules)) {
            QString screenId;
            int desktop = 0;
            QString activity;
            // contextDimsOf returns false on a malformed match (no
            // context-equality leaf found). The freshly-built disable
            // rules above are well-formed, so the false branch is
            // unreachable today — but treat a future regression in the
            // producer as "drop the malformed rule" rather than letting
            // every malformed rule collapse to a single (empty, 0, empty)
            // identity bucket and silently dropping all but one.
            if (!CRB::contextDimsOf(rule.match, screenId, desktop, activity)) {
                continue;
            }
            const std::optional<QString> modeToken = CRB::disableRuleMode(rule);
            const QString identity = (modeToken ? *modeToken : QStringLiteral("?")) + QLatin1Char('|') + screenId
                + QLatin1Char('|') + QString::number(desktop) + QLatin1Char('|') + activity;
            if (seen.contains(identity)) {
                continue;
            }
            seen.insert(identity);
            rules.append(rule);
        }
    }

    // ── Animation App Rules → Rules ──────────────────────────────────
    // Port the (now-deleted) AnimationAppRuleBridge logic against the stashed
    // legacy JSON. The animation rules target slot prefixes (`anim-shader:`,
    // `anim-timing:`) that no other rule type fills, so they coexist with the
    // assignment/disable rules above regardless of priority interleaving.
    appendAnimationRulesFromStash(rules, animationRulesStash);

    // ── Exclusions → Rules ───────────────────────────────────────────
    // Promote the legacy `Exclusions.{Applications,WindowClasses}` lists into
    // first-class Rules so the runtime no longer needs the bridge that
    // re-built them on every settings change. Each surviving pattern becomes
    // an Application-subject `AppId AppIdMatches <pattern>` matcher with a
    // terminal `Exclude` action — the same shape the legacy runtime bridge
    // produced for the daemon's navigation gates (see
    // `appendExclusionRulesFromStash` for the builder), so behaviour is
    // preserved for an upgrading user.
    appendExclusionRulesFromStash(rules, exclusionStash);

    // ── Animation exclusions → Rules ────────────────────────────────
    // Same fold for the animation-page exclusion lists: each surviving
    // pattern becomes a `DesktopFile`/`WindowClass Contains <pattern>`
    // matcher with a terminal `ExcludeAnimations` action — the new
    // action type the effect's shouldAnimateWindow gate resolves against.
    // Migration preserves the legacy effect-side Contains-leaf semantics
    // so an upgrading user's "no animations for firefox" rule keeps the
    // same matching behaviour.
    appendAnimationExclusionRulesFromStash(rules, animationExclusionStash);

    // ── Premade Steam rule ─────────────────────────────────────────────────
    // Ship the built-in fix for Steam's tiling misbehaviour — the Friends List
    // and self-drawn notification-toast top-levels that slip the effect's
    // structural popup filter and get auto-tiled. Seeded once here so every
    // fresh install AND every v3→v4 upgrade gets it; the
    // `rulesAlreadyConverted` gate at the top of this function keeps the
    // rebuild path from re-seeding it (or resurrecting it after a user deletes
    // it) on any later run. See `appendSteamDefaultRule` for the match/enforcement
    // rationale.
    appendSteamDefaultRule(rules);

    // ── Per-layout app rules → SnapToZone Rules ──────────────────────
    // v3 stored app→zone assignments on each Layout (`Layout::appRules`); v4
    // unifies them into the window-rule store. Read every layout file's legacy
    // `appRules` array and emit one SnapToZone rule per assignment, so an
    // upgrading user's pinned apps keep snapping to their zone(s) through the
    // new single system. Layouts live in the user-writable data dir (separate
    // from config.json / rules.json), so resolve that path directly. This
    // runs only on the first conversion (the `rulesAlreadyConverted` gate
    // above), which every real v3→v4 upgrade hits exactly once.
    {
        const QString layoutsDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QLatin1Char('/') + ConfigDefaults::layoutsSubdir();
        appendLayoutAppRulesAsSnapToZone(rules, layoutsDir);
    }

    // Stamp a band priority onto the Exclude / animation-exclusion / SnapToZone
    // rules the helpers above left at 0, so they display sensibly on a fresh load instead of all
    // reading "Priority 0".
    assignBandPrioritiesToZeroRules(rules);

    // ── Relocate QuickLayouts to the quicklayouts.json sidecar (FIRST) ─────
    // Quick-layout slots are NOT rules — they belong in the sibling
    // sidecar LayoutRegistry reads (next to rules.json), not in the rule
    // store and not in config.json. The v3 slots are all snapping (zone-layout)
    // bindings, wrapped below under the snapping key of the mode-nested
    // quicklayouts.json shape — the single format LayoutRegistry reads.
    //
    // Write-order rationale (B4 data-loss fix): the sidecar MUST be durably
    // written BEFORE rules.json — rules.json is the irreversible
    // commit marker (its mere existence flips the `rulesAlreadyConverted`
    // probe on the next run, gating the rebuild path off forever). If the
    // sidecar write fails AFTER rules.json was committed, the next run
    // takes the cleanup-only branch and never re-attempts the relocation,
    // while assignments.json gets quarantined to .migrated — the slot data is
    // recoverable only by hand. Writing the sidecar first means a sidecar
    // failure aborts the whole conversion with assignments.json still in
    // place, leaving every datum recoverable on the next run.
    if (!quickLayoutsToRelocate.isEmpty()) {
        const QString quickLayoutsPath = ConfigDefaults::quickLayoutsFilePath();
        // An existing quicklayouts.json is authoritative only if it is a
        // valid, non-empty JSON object — a partial earlier run that wrote an
        // empty/corrupt file then crashed must NOT shadow the real slots,
        // which would otherwise be lost when assignments.json is retired
        // below. In that case re-derive from assignments.json.
        bool existingIsAuthoritative = false;
        if (QFile::exists(quickLayoutsPath)) {
            QFile qf(quickLayoutsPath);
            if (qf.open(QIODevice::ReadOnly)) {
                QJsonParseError err;
                const QJsonDocument doc = QJsonDocument::fromJson(qf.readAll(), &err);
                if (err.error == QJsonParseError::NoError && doc.isObject() && !doc.object().isEmpty()) {
                    existingIsAuthoritative = true;
                }
            }
        }
        if (!existingIsAuthoritative) {
            QDir().mkpath(QFileInfo(quickLayoutsPath).absolutePath());
            // Emit the mode-nested shape the reader expects (snapping slots
            // under the snapping key, autotile empty). There is exactly one
            // on-disk format — the reader does not accept a bare flat map.
            QJsonObject nested;
            nested.insert(PhosphorZones::LayoutRegistry::QuickSlotsSnappingKey, quickLayoutsToRelocate);
            nested.insert(PhosphorZones::LayoutRegistry::QuickSlotsAutotileKey, QJsonObject{});
            if (!PhosphorConfig::JsonBackend::writeJsonAtomically(quickLayoutsPath, nested)) {
                qWarning("ConfigMigration: failed to write %s — aborting v4 conversion before committing rules.json",
                         qPrintable(quickLayoutsPath));
                return false;
            }
            qInfo("ConfigMigration: relocated %d quick-layout slots to %s",
                  static_cast<int>(quickLayoutsToRelocate.size()), qPrintable(quickLayoutsPath));
        }
    }

    // ── Write rules.json (atomic — the irreversible commit) ──────────
    // This is the marker that gates `rulesAlreadyConverted` on the next
    // run. It MUST go after the sidecar relocation (see comment above) — once
    // this file exists as a valid v4 rule set, the cleanup-only branch
    // short-circuits the rebuild forever.
    PhosphorRules::RuleSet ruleSet;
    const int inputRuleCount = rules.size();
    ruleSet.setRules(rules);
    const int storedRuleCount = ruleSet.count();
    QDir().mkpath(QFileInfo(rulesPath).absolutePath());
    if (!ruleSet.saveToFile(rulesPath)) {
        qWarning("ConfigMigration: failed to write %s — aborting v4 conversion", qPrintable(rulesPath));
        return false;
    }
    // Log the input-vs-stored delta whenever setRules drops candidates.
    // Two drop classes feed this delta:
    //   1. UUIDv5 dedup. On the snapping side, identical `(Field::AppId,
    //      Operator::AppIdMatches, "firefox")` tuples in BOTH
    //      Exclusions.Applications AND Exclusions.WindowClasses collapse
    //      to one rule — same 3-segment UUIDv5 key because both lists
    //      encode their patterns under `AppId AppIdMatches`. The
    //      animation side does NOT have this cross-list collapse
    //      property (its encoding includes the field discriminator, so
    //      DesktopFile vs WindowClass entries hash distinctly); dedup
    //      only fires there for literal within-list duplicates
    //      ("firefox,firefox" in one list).
    //   2. Validator rejection. RuleSet::setRules silently drops
    //      rules whose `Rule::isValid()` returns false (null id,
    //      invalid match, zero actions, or action-validator failure).
    //      The migration's builders should never produce such rules
    //      today; a non-zero delta with no UUIDv5 collision in the
    //      preceding setRules warnings points at a builder regression.
    // Surfacing the delta makes either case forensically visible.
    if (storedRuleCount != inputRuleCount) {
        qInfo(
            "ConfigMigration: wrote %d rules to %s (dropped %d of %d candidates — UUIDv5 collision OR "
            "validator rejection; see preceding setRules warnings to discriminate)",
            storedRuleCount, qPrintable(rulesPath), inputRuleCount - storedRuleCount, inputRuleCount);
    } else {
        qInfo("ConfigMigration: wrote %d rules to %s", storedRuleCount, qPrintable(rulesPath));
    }

    // ── Rewrite config.json: strip the temporary stash keys ────────────────
    // The real Display.*Disabled* / Exclusions.{Applications,WindowClasses} /
    // Animations.WindowFiltering.{Applications,WindowClasses} / Animations.
    // AnimationAppRules keys were already removed by migrateV3ToV4; only
    // the four `_v4*Stash` scratch keys (`_v4DisableStash`,
    // `_v4AnimationRulesStash`, `_v4ExclusionStash`,
    // `_v4AnimationExclusionStash`) remain to be cleaned up here.
    // Serialised under the cross-process QLockFile acquired in
    // `ensureJsonConfig` (see the QLockFile setup near the top of that
    // function): on a successful tryLock the rewrite races no peer, so
    // the value we read into configRoot at the top of this function is
    // still authoritative here. The lock is best-effort — a tryLock
    // failure logs a warning at the matching site and falls through, in
    // which case a concurrent peer COULD interleave; the warning is the
    // operator's signal that the serialisation guarantee was downgraded.
    // Predicate gates the rewrite so a clean config (no stash keys) isn't
    // needlessly touched.
    if (haveConfig
        && (configRoot.contains(kV4DisableStashKey()) || configRoot.contains(kV4AnimationRulesStashKey())
            || configRoot.contains(kV4ExclusionStashKey()) || configRoot.contains(kV4AnimationExclusionStashKey()))) {
        configRoot.remove(kV4DisableStashKey());
        configRoot.remove(kV4AnimationRulesStashKey());
        configRoot.remove(kV4ExclusionStashKey());
        configRoot.remove(kV4AnimationExclusionStashKey());
        if (!PhosphorConfig::JsonBackend::writeJsonAtomically(jsonPath, configRoot)) {
            qWarning("ConfigMigration: failed to rewrite %s after v4 conversion", qPrintable(jsonPath));
            return false;
        }
    }

    // ── Retire assignments.json — the post-conversion cleanup tail ─────────
    // rules.json (and quicklayouts.json) now durably hold every datum
    // assignments.json carried; the runtime LayoutRegistry reads the rule
    // store exclusively. Retiring the legacy file LAST keeps the conversion
    // crash-recoverable. This step is non-fatal and idempotent: if it fails,
    // the next run sees rules.json already at the v4 RuleSet
    // schema, takes the cleanup-only branch, and retries the retire — it does
    // NOT rebuild-and-overwrite the (possibly user-edited) rule store.
    retireLegacyAssignmentsFile(assignmentsPath);

    return true;
}

bool ConfigMigration::pruneRetiredProviderDefaultRule(const QString& jsonPath)
{
    // The provider-default catch-all assignment rule is retired: the gated
    // default resolver is the sole global-default source, and it already ignores
    // the priority-0 catch-all at runtime. Delete the stale rule from rules.json
    // by its deterministic id (stable, so this is idempotent — a re-run after
    // the rule is gone finds nothing). No schema bump: this runs from
    // finalizeV4Conversion's cleanup path. config.json is untouched; jsonPath
    // only gates on the conversion having happened.
    if (!QFile::exists(jsonPath)) {
        return true;
    }
    const QString rulesPath = ConfigDefaults::rulesFilePath();
    auto setOpt = PhosphorRules::RuleSet::loadFromFile(rulesPath);
    if (!setOpt.has_value()) {
        // No rules.json yet (conversion not established) — nothing to strip. A
        // fresh install never wrote a provider-default rule in the first place.
        return true;
    }

    // The provider-default's identity was fixed: the UUID derivation
    // ContextRuleBridge keyed off the "provider-default" family with an empty
    // (screen, desktop, activity) tuple. Reconstruct it directly — the rule
    // factory that produced it is gone, but the id scheme is preserved verbatim.
    namespace CRB = PhosphorRules::ContextRuleBridge;
    const QUuid providerDefaultId = QUuid::createUuidV5(
        CRB::detail::namespaceUuid(),
        CRB::detail::contextIdentityKey(QLatin1StringView("provider-default"), QString(), 0, QString()));

    PhosphorRules::RuleSet ruleSet = *setOpt;
    if (!ruleSet.removeRule(providerDefaultId)) {
        // No provider-default rule present — already stripped, or a fresh
        // install that never had one. Clean no-op.
        return true;
    }
    if (!ruleSet.saveToFile(rulesPath)) {
        qWarning(
            "ConfigMigration::pruneRetiredProviderDefaultRule: failed to write %s after removing the "
            "provider-default rule",
            qPrintable(rulesPath));
        return false;
    }
    qInfo("ConfigMigration::pruneRetiredProviderDefaultRule: removed the provider-default rule from %s",
          qPrintable(rulesPath));
    return true;
}

} // namespace PlasmaZones
