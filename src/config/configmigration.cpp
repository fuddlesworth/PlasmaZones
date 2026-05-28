// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configmigration.h"

#include "configbackends.h"
#include "configdefaults.h"
#include "perscreenresolver.h"

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorConfig/MigrationRunner.h>
#include <PhosphorConfig/Schema.h>
#include <PhosphorWindowRule/ContextRuleBridge.h>
#include <PhosphorWindowRule/IdentityKey.h>
#include <PhosphorWindowRule/MatchExpression.h>
#include <PhosphorWindowRule/MatchTypes.h>
#include <PhosphorWindowRule/RuleAction.h>
#include <PhosphorWindowRule/WindowRule.h>
#include <PhosphorWindowRule/WindowRuleSet.h>

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
#include <QUuid>
#include <array>
#include <atomic>
#include <optional>

namespace PlasmaZones {

// ─── PhosphorConfig schema synthesis ────────────────────────────────────────
// A minimal schema with only the migration chain populated. PZ's migration
// runner only needs the version, the version key, and the migration steps —
// group/key declarations belong to the Settings layer (future work).

namespace {
PhosphorConfig::Schema makeMigrationSchema()
{
    PhosphorConfig::Schema s;
    s.version = ConfigSchemaVersion;
    s.versionKey = ConfigKeys::versionKey();
    s.migrations = {
        {1, &ConfigMigration::migrateV1ToV2},
        {2, &ConfigMigration::migrateV2ToV3},
        {3, &ConfigMigration::migrateV3ToV4},
    };
    return s;
}
} // namespace

std::span<const MigrationStep> ConfigMigration::migrationSteps()
{
    // Kept for callers/tests that want a flat list of PZ-native steps. Built
    // once lazily; the underlying function pointers never change at runtime.
    static const std::array<MigrationStep, 3> s_steps{{
        {1, &ConfigMigration::migrateV1ToV2},
        {2, &ConfigMigration::migrateV2ToV3},
        {3, &ConfigMigration::migrateV3ToV4},
    }};
    return {s_steps.data(), s_steps.size()};
}

// ── Migration chain runner (delegates to PhosphorConfig::MigrationRunner) ──

void ConfigMigration::runMigrationChainInMemory(QJsonObject& root)
{
    const PhosphorConfig::Schema schema = makeMigrationSchema();
    PhosphorConfig::MigrationRunner(schema).runInMemory(root);
}

bool ConfigMigration::runMigrationChain(const QString& jsonPath)
{
    const PhosphorConfig::Schema schema = makeMigrationSchema();
    return PhosphorConfig::MigrationRunner(schema).runOnFile(jsonPath);
}

// ── ensureJsonConfig ────────────────────────────────────────────────────────

// Process-level "already migrated" flag. Set by ensureJsonConfig() on its
// first successful return, cleared by resetMigrationGuardForTesting(). See
// the docstring on ConfigMigration::ensureJsonConfig() for why this exists.
// File-scope so both ensureJsonConfig() and resetMigrationGuardForTesting()
// see the same atomic.
namespace {
std::atomic<bool> s_migrated{false};

/// The legacy assignments.json path. windowrules.json supersedes it in v4 —
/// migrateV1ToV2 still writes it (a v2 artifact) and finalizeV4Conversion
/// reads then deletes it; no live runtime code touches it, so the path lives
/// here rather than on the public ConfigDefaults surface. It sits beside
/// windowrules.json (the same plasmazones config directory).
QString legacyAssignmentsFilePath()
{
    return QFileInfo(ConfigDefaults::windowRulesFilePath()).absolutePath() + QStringLiteral("/assignments.json");
}

/// Pre-flight check for the legacy assignments.json sidecar: if the file
/// exists but fails to parse (truncation, power-loss, hand-edit error), abort
/// the v3→v4 conversion BEFORE anything irreversible runs.
///
/// Rationale (B5 data-loss fix): silently treating a corrupt assignments.json
/// as "no assignments" would let the conversion write a windowrules.json
/// holding only the provider-default + disable rules, then quarantine the
/// corrupt original to `.migrated` — the user's pinned assignments AND quick-
/// layout slots would be lost without warning. The `.migrated` suffix also
/// falsely implies the file was successfully migrated, masking the failure.
///
/// On failure: rename the malformed file to `assignments.json.corrupt.bak`
/// (NOT `.migrated`), log the parse error at critical severity, and return
/// false so the caller aborts. The user can inspect/repair the `.corrupt.bak`
/// file, rename it back to `assignments.json`, and re-run.
///
/// Returns true if the file is absent, empty (treated as a fresh install with
/// no assignments — not a corruption case), or parses as a JSON object.
bool prevalidateLegacyAssignmentsFile(const QString& assignmentsPath)
{
    if (!QFile::exists(assignmentsPath)) {
        return true;
    }
    QFile af(assignmentsPath);
    if (!af.open(QIODevice::ReadOnly)) {
        // We can't read it to decide either way — log and let the downstream
        // finalize-step's open-failure handling take its course (it will see
        // !haveAssignments and continue with the disable-only rule set). This
        // is the same behaviour as before the prevalidation existed; the
        // critical case (parse error) is the one this guard exists for.
        qWarning("ConfigMigration: could not open %s for prevalidation: %s", qPrintable(assignmentsPath),
                 qPrintable(af.errorString()));
        return true;
    }
    const QByteArray bytes = af.readAll();
    af.close();
    if (bytes.trimmed().isEmpty()) {
        // Empty file is NOT corruption: it carries no data to lose, and the
        // downstream code already handles `!haveAssignments` as "nothing to
        // migrate". Treat it like an absent file.
        return true;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error == QJsonParseError::NoError && doc.isObject()) {
        return true;
    }

    // Corrupt: quarantine to .corrupt.bak (NOT .migrated — that name implies
    // a successful migration). Preserve the original bytes so the user can
    // hand-repair and re-run.
    const QString corruptBak = assignmentsPath + QStringLiteral(".corrupt.bak");
    QFile::remove(corruptBak); // clear any stale backup from a prior failed run
    if (QFile::rename(assignmentsPath, corruptBak)) {
        qCritical(
            "ConfigMigration: %s is malformed (%s) — quarantined to %s. "
            "Aborting v4 conversion to prevent data loss. Inspect/repair the "
            "file and rename it back to assignments.json, then re-run.",
            qPrintable(assignmentsPath), qPrintable(err.errorString()), qPrintable(corruptBak));
    } else {
        qCritical(
            "ConfigMigration: %s is malformed (%s) — also failed to quarantine to %s. "
            "Aborting v4 conversion. Move or repair the file by hand.",
            qPrintable(assignmentsPath), qPrintable(err.errorString()), qPrintable(corruptBak));
    }
    return false;
}

/// Retire the superseded assignments.json once windowrules.json holds every
/// datum it carried. Prefer a rename to `assignments.json.migrated` over an
/// outright delete: a rename is the same directory-entry operation as a remove
/// but leaves an inert quarantined copy, and — critically — if it ever fails it
/// fails the SAME way every run, so a delete-failure cannot drive an
/// overwrite loop. A plain delete is the fallback when the quarantine slot is
/// itself unavailable. Either way the result is non-fatal: a stray
/// assignments.json is inert (nothing reads it) and the conversion's
/// idempotency no longer depends on its absence.
void retireLegacyAssignmentsFile(const QString& assignmentsPath)
{
    if (!QFile::exists(assignmentsPath)) {
        return;
    }
    const QString quarantinePath = assignmentsPath + QStringLiteral(".migrated");
    QFile::remove(quarantinePath); // clear any stale quarantine from a prior run
    if (QFile::rename(assignmentsPath, quarantinePath)) {
        qInfo("ConfigMigration: quarantined superseded %s to %s", qPrintable(assignmentsPath),
              qPrintable(quarantinePath));
        return;
    }
    if (QFile::remove(assignmentsPath)) {
        qInfo("ConfigMigration: deleted superseded %s", qPrintable(assignmentsPath));
        return;
    }
    qWarning("ConfigMigration: failed to retire superseded %s — left in place (inert; ignored next run)",
             qPrintable(assignmentsPath));
}
} // namespace

bool ConfigMigration::ensureJsonConfig()
{
    // Process-level guard: migration is a one-shot operation. Once we've
    // confirmed the config is at the current schema version (or successfully
    // migrated it), skip the full file read + JSON parse on subsequent calls.
    // The editor reaches this function in its ctor path before the QML engine
    // starts, so shaving the disk read here keeps the startup hot path tight.
    // Uses acquire/release so a second caller observing `true` also observes
    // every write the first caller made (the atomic rename in
    // runMigrationChain / migrateIniToJson).
    if (s_migrated.load(std::memory_order_acquire)) {
        return true;
    }

    // Cross-process migration lock: PZ starts multiple processes (daemon,
    // settings, editor) in parallel at session start, each of which calls
    // ensureJsonConfig in its own ctor path. Without this lock, two
    // concurrent starts can race the read-migrate-write sequence of
    // migrateIniToJson / runMigrationChain — the later writer silently
    // overwrites the earlier migration's output.
    //
    // QLockFile is an advisory flock-style lock that cleans up automatically
    // when the owning process exits (including crashes, via stale-lock
    // detection). 5-second stale window covers the real-world migration
    // cost; a genuinely hung peer won't starve us forever.
    //
    // QLockFile requires the lock file's parent directory to exist —
    // ensureJsonConfigImpl below also needs the directory for any write it
    // performs, so create it up front. Silently fail the mkpath and let
    // tryLock report the lock-creation failure as the primary error.
    const QString jsonPath = ConfigDefaults::configFilePath();
    QDir().mkpath(QFileInfo(jsonPath).absolutePath());
    QLockFile lock(jsonPath + QStringLiteral(".migrate.lock"));
    lock.setStaleLockTime(5000);
    if (!lock.tryLock(5000)) {
        // Couldn't acquire the lock within the timeout. The peer may have
        // completed successfully (in which case our next call will see
        // the current schema version and return early) — fall through and
        // let ensureJsonConfigImpl re-check the on-disk state.
        qWarning("ConfigMigration: could not acquire migration lock within 5s — proceeding without lock");
    }

    // Re-check the migrated flag after acquiring the lock. Another process
    // may have just released it after stamping the current schema version;
    // re-reading the file lets us short-circuit without paying the parse
    // cost again.
    const bool ok = ensureJsonConfigImpl();
    if (ok) {
        s_migrated.store(true, std::memory_order_release);
    }
    return ok;
}

bool ConfigMigration::ensureJsonConfigImpl()
{
    const QString jsonPath = ConfigDefaults::configFilePath();
    if (QFile::exists(jsonPath)) {
        QFile f(jsonPath);
        bool whitespaceOnly = false;
        bool corrupt = false;
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QByteArray data = f.readAll();
            if (data.trimmed().isEmpty()) {
                // Truncated / zero-byte files aren't corruption — treat them
                // like a fresh install (proceed to the INI / fresh-install
                // logic below) instead of archiving the empty file.
                whitespaceOnly = true;
            } else {
                QJsonParseError err;
                QJsonDocument doc = QJsonDocument::fromJson(data, &err);
                if (err.error == QJsonParseError::NoError && doc.isObject()) {
                    const int version = doc.object().value(ConfigKeys::versionKey()).toInt(0);
                    if (version < ConfigSchemaVersion) {
                        // Pre-flight the legacy sidecar BEFORE the chain runs
                        // — a malformed assignments.json must abort the
                        // migration without bumping the on-disk _version
                        // stamp, so the user's next run can re-attempt the
                        // conversion against the (now-quarantined) corrupt
                        // input after they repair it. (B5 data-loss fix.)
                        if (!prevalidateLegacyAssignmentsFile(legacyAssignmentsFilePath())) {
                            return false;
                        }
                        if (!runMigrationChain(jsonPath)) {
                            return false;
                        }
                        // The v3→v4 chain step stamps _version and stashes
                        // disable-list data; the two-file conversion (reading
                        // assignments.json, writing windowrules.json) happens
                        // here, after the chain. Idempotent — safe to always run.
                        return finalizeV4Conversion(jsonPath);
                    }
                    // Already at current version — finalizeV4Conversion is a
                    // no-op once windowrules.json exists, but run it so a
                    // crash that left assignments.json behind still completes.
                    return finalizeV4Conversion(jsonPath);
                }
                corrupt = true;
            }
        } else {
            // Open failed — log and treat as fresh-install fall-through.
            qWarning("ConfigMigration: could not open %s for reading: %s", qPrintable(jsonPath),
                     qPrintable(f.errorString()));
            return false;
        }

        // Real parse-error corruption — always back up to .corrupt.bak before
        // removing. Whether or not an INI exists to re-migrate from, the user
        // may want to recover hand-made edits from the corrupt JSON (a parse
        // error can be one stray character in an otherwise valid file).
        // Asymmetric "rename if no INI, silent rm if INI exists" was a trap.
        if (corrupt) {
            const QString corruptBak = jsonPath + QStringLiteral(".corrupt.bak");
            if (QFile::exists(corruptBak) && !QFile::remove(corruptBak)) {
                qWarning("ConfigMigration: failed to remove old %s — leaving corrupt JSON in place",
                         qPrintable(corruptBak));
                return false;
            }
            if (!QFile::rename(jsonPath, corruptBak)) {
                qWarning("ConfigMigration: failed to rename corrupt JSON to %s — leaving it in place",
                         qPrintable(corruptBak));
                return false;
            }
            const QString iniPath = ConfigDefaults::legacyConfigFilePath();
            if (!QFile::exists(iniPath)) {
                qWarning(
                    "ConfigMigration: corrupt JSON config moved to %s — no INI to re-migrate from, "
                    "using defaults",
                    qPrintable(corruptBak));
                return finalizeV4Conversion(jsonPath);
            }
            qWarning("ConfigMigration: corrupt JSON config moved to %s — re-migrating from INI",
                     qPrintable(corruptBak));
            // Fall through to the INI migration path below.
        } else if (whitespaceOnly) {
            // Drop the empty file so the INI-or-fresh-install path below is clean.
            if (!QFile::remove(jsonPath)) {
                qWarning("ConfigMigration: failed to remove empty JSON %s", qPrintable(jsonPath));
                return false;
            }
        }
    }

    const QString iniPath = ConfigDefaults::legacyConfigFilePath();
    if (!QFile::exists(iniPath)) {
        // Fresh install — no old config. Still run the v4 finalizer so a
        // stray assignments.json from a partial earlier conversion is folded
        // into windowrules.json rather than left orphaned.
        return finalizeV4Conversion(jsonPath);
    }

    qInfo("ConfigMigration: migrating %s → %s", qPrintable(iniPath), qPrintable(jsonPath));

    if (!migrateIniToJson(iniPath, jsonPath)) {
        qWarning("ConfigMigration: migration failed — old config preserved at %s", qPrintable(iniPath));
        return false;
    }

    const QString bakPath = iniPath + QStringLiteral(".bak");
    if (QFile::exists(bakPath)) {
        QFile::remove(bakPath);
    }
    if (!QFile::rename(iniPath, bakPath)) {
        qWarning("ConfigMigration: could not rename %s to %s", qPrintable(iniPath), qPrintable(bakPath));
    }

    qInfo("ConfigMigration: migration complete");
    return finalizeV4Conversion(jsonPath);
}

void ConfigMigration::resetMigrationGuardForTesting()
{
    s_migrated.store(false, std::memory_order_release);
}

// ── INI → JSON ──────────────────────────────────────────────────────────────

bool ConfigMigration::migrateIniToJson(const QString& iniPath, const QString& jsonPath)
{
    const QMap<QString, QVariant> flatMap = PhosphorConfig::QSettingsBackend::readConfigFromDisk(iniPath);
    if (flatMap.isEmpty()) {
        qInfo("ConfigMigration: old config is empty — writing minimal JSON to complete migration");
    }

    QJsonObject root = iniMapToJson(flatMap);
    // INI migration produces v1 format; the chain upgrades to current version.
    root[ConfigKeys::versionKey()] = 1;
    runMigrationChainInMemory(root);

    return PhosphorConfig::JsonBackend::writeJsonAtomically(jsonPath, root);
}

QJsonObject ConfigMigration::iniMapToJson(const QMap<QString, QVariant>& flatMap)
{
    QJsonObject root;

    const QString renderingGroup = ConfigDefaults::renderingGroup();
    const QString generalGroup = ConfigDefaults::generalGroup();
    // Hardcoded v1 key name — the INI file uses "RenderingBackend"
    const QString renderingKey = QStringLiteral("RenderingBackend");
    const QString PerScreenKeyStr = PerScreenPathResolver::perScreenKey();

    for (auto it = flatMap.constBegin(); it != flatMap.constEnd(); ++it) {
        const QString& flatKey = it.key();
        const QVariant& value = it.value();

        const int slashIdx = flatKey.indexOf(QLatin1Char('/'));
        if (slashIdx < 0) {
            // Root-level INI key (ungrouped). Route RenderingBackend to its own group.
            if (flatKey == renderingKey) {
                QJsonObject rendering = root.value(renderingGroup).toObject();
                rendering[flatKey] = convertValue(flatKey, value);
                root[renderingGroup] = rendering;
            } else {
                QJsonObject general = root.value(generalGroup).toObject();
                general[flatKey] = convertValue(flatKey, value);
                root[generalGroup] = general;
            }
            continue;
        }

        const QString groupPart = flatKey.left(slashIdx);
        const QString keyPart = flatKey.mid(slashIdx + 1);

        // QSettings maps ungrouped keys AND [General]-grouped keys identically,
        // so RenderingBackend may appear as either a root key (handled above) or
        // as "General/RenderingBackend". Route it to the Rendering group either way.
        if (groupPart == generalGroup && keyPart == renderingKey) {
            QJsonObject rendering = root.value(renderingGroup).toObject();
            rendering[keyPart] = convertValue(keyPart, value);
            root[renderingGroup] = rendering;
            continue;
        }

        // Check for known per-screen group patterns: ZoneSelector:*, AutotileScreen:*, SnappingScreen:*
        // Other colon-containing groups (e.g., Assignment:ScreenId:Desktop:1) are regular groups.
        if (PerScreenPathResolver::isPerScreenPrefix(groupPart)) {
            const int colonIdx = groupPart.indexOf(QLatin1Char(':'));
            const QString prefix = groupPart.left(colonIdx);
            const QString screenId = groupPart.mid(colonIdx + 1);
            const QString category = PerScreenPathResolver::prefixToCategory(prefix);

            QJsonObject perScreen = root.value(PerScreenKeyStr).toObject();
            QJsonObject cat = perScreen.value(category).toObject();
            QJsonObject screen = cat.value(screenId).toObject();
            screen[keyPart] = convertValue(keyPart, value);
            cat[screenId] = screen;
            perScreen[category] = cat;
            root[PerScreenKeyStr] = perScreen;
        } else {
            // Regular group: Group/Key
            QJsonObject groupObj = root.value(groupPart).toObject();
            groupObj[keyPart] = convertValue(keyPart, value);
            root[groupPart] = groupObj;
        }
    }

    return root;
}

QJsonValue ConfigMigration::convertValue(const QString& keyName, const QVariant& value)
{
    const QString s = value.toString();

    // Already a typed bool from INI reader
    if (value.typeId() == QMetaType::Bool) {
        return QJsonValue(value.toBool());
    }

    // Type detection priority (order matters):
    //   1. Boolean strings ("true"/"false")
    //   2. JSON arrays/objects (trigger lists, per-algorithm settings)
    //   3. Comma-separated integers 0-255 on a color-shaped key → hex
    //      (content heuristic alone is ambiguous — a layout-order list like
    //      "1,2,3" also matches, so gate on the key name to avoid false
    //      positives)
    //   4. Plain integers
    //   5. Doubles (only if contains '.' to avoid "0" → 0.0)
    //   6. Fallback: keep as string

    // Boolean strings
    if (s.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0) {
        return QJsonValue(true);
    }
    if (s.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0) {
        return QJsonValue(false);
    }

    // Try JSON (triggers, per-algorithm settings)
    if (!s.isEmpty() && (s.front() == QLatin1Char('[') || s.front() == QLatin1Char('{'))) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError) {
            if (doc.isArray()) {
                return QJsonValue(doc.array());
            }
            if (doc.isObject()) {
                return QJsonValue(doc.object());
            }
        }
    }

    // Color in r,g,b or r,g,b,a format → convert to hex.
    //
    // The old code applied this conversion to ANY comma-separated list of
    // 3–4 ints in 0..255, which corrupts settings whose wire format happens
    // to match — e.g. a layout-order like "1,2,3". Require the key to look
    // like a color key (ends with "Color") before converting.
    //
    // Every v1 color key in the PZ config follows this convention:
    //   HighlightColor / InactiveColor / BorderColor / LabelFontColor /
    //   AutotileBorderColor / AutotileInactiveBorderColor.
    if (keyName.endsWith(QLatin1String("Color")) && s.contains(QLatin1Char(','))) {
        const QStringList parts = s.split(QLatin1Char(','));
        if (parts.size() >= 3 && parts.size() <= 4) {
            bool allNumeric = true;
            QList<int> components;
            for (const QString& part : parts) {
                bool ok = false;
                int v = part.trimmed().toInt(&ok);
                if (!ok || v < 0 || v > 255) {
                    allNumeric = false;
                    break;
                }
                components.append(v);
            }
            if (allNumeric && components.size() >= 3) {
                int a = (components.size() >= 4) ? components[3] : 255;
                QColor c(components[0], components[1], components[2], a);
                return QJsonValue(c.name(QColor::HexArgb));
            }
        }
    }

    // Any other comma-containing value is kept verbatim — comma-separated
    // lists (layout order, exclusion apps, locked screens) round-trip as
    // strings and get parsed by their respective setters.
    if (s.contains(QLatin1Char(','))) {
        return QJsonValue(s);
    }

    // Try integer
    {
        bool ok = false;
        int intVal = s.toInt(&ok);
        if (ok) {
            return QJsonValue(intVal);
        }
    }

    // Try double (but only if it has a decimal point to avoid converting "0" → 0.0)
    if (s.contains(QLatin1Char('.'))) {
        bool ok = false;
        double dblVal = s.toDouble(&ok);
        if (ok) {
            return QJsonValue(dblVal);
        }
    }

    // Default: keep as string
    return QJsonValue(s);
}

// ── Schema migration: v1 → v2 ───────────────────────────────────────────────
// Restructures flat groups (Activation, Display, etc.) into nested dot-path
// hierarchy (Snapping.Behavior.ZoneSpan, Tiling.Gaps, etc.).

namespace {
// Helper: move a key from one JSON object to another, renaming it.
void moveKey(const QJsonObject& src, const QString& srcKey, QJsonObject& dst, const QString& dstKey)
{
    if (src.contains(srcKey)) {
        dst[dstKey] = src.value(srcKey);
    }
}
} // anonymous namespace

void ConfigMigration::migrateV1ToV2(QJsonObject& root)
{
    // Defense-in-depth idempotency guard. The PhosphorConfig::MigrationRunner
    // already gates this function on `version == 1`, and `ensureJsonConfig`
    // bails early when the on-disk version is >= ConfigSchemaVersion. But
    // a direct caller (test harness, future tooling) that hands us a v2 doc
    // would otherwise read the v2 `Animations` group as if it were v1,
    // remove it, fail to find any v1 keys inside, and end up writing back
    // an empty animations group — silently nuking the user's Profile blob.
    // Bail out before touching the document.
    if (root.value(ConfigKeys::versionKey()).toInt(0) >= 2) {
        return;
    }

    // ── Read all v1 groups (using ConfigKeys v1 accessors) ────────────────
    const QJsonObject v1Activation = root.value(ConfigKeys::Legacy::v1ActivationGroup()).toObject();
    const QJsonObject v1Display = root.value(ConfigKeys::Legacy::v1DisplayGroup()).toObject();
    const QJsonObject v1Appearance = root.value(ConfigKeys::Legacy::v1AppearanceGroup()).toObject();
    const QJsonObject v1Zones = root.value(ConfigKeys::Legacy::v1ZonesGroup()).toObject();
    const QJsonObject v1Behavior = root.value(ConfigKeys::Legacy::v1BehaviorGroup()).toObject();
    const QJsonObject v1Exclusions = root.value(ConfigKeys::Legacy::v1ExclusionsGroup()).toObject();
    const QJsonObject v1ZoneSelector = root.value(ConfigKeys::Legacy::v1ZoneSelectorGroup()).toObject();
    const QJsonObject v1Autotiling = root.value(ConfigKeys::Legacy::v1AutotilingGroup()).toObject();
    const QJsonObject v1AutotileShortcuts = root.value(ConfigKeys::Legacy::v1AutotileShortcutsGroup()).toObject();
    const QJsonObject v1Animations = root.value(ConfigKeys::Legacy::v1AnimationsGroup()).toObject();
    const QJsonObject v1GlobalShortcuts = root.value(ConfigKeys::Legacy::v1GlobalShortcutsGroup()).toObject();
    const QJsonObject v1Editor = root.value(ConfigKeys::Legacy::v1EditorGroup()).toObject();
    const QJsonObject v1Ordering = root.value(ConfigKeys::Legacy::v1OrderingGroup()).toObject();
    const QJsonObject v1Rendering = root.value(ConfigKeys::Legacy::v1RenderingGroup()).toObject();
    const QJsonObject v1Shaders = root.value(ConfigKeys::Legacy::v1ShadersGroup()).toObject();

    // ── Remove all v1 groups ────────────────────────────────────────────────
    const QString v1Groups[] = {
        ConfigKeys::Legacy::v1ActivationGroup(),        ConfigKeys::Legacy::v1DisplayGroup(),
        ConfigKeys::Legacy::v1AppearanceGroup(),        ConfigKeys::Legacy::v1ZonesGroup(),
        ConfigKeys::Legacy::v1BehaviorGroup(),          ConfigKeys::Legacy::v1ExclusionsGroup(),
        ConfigKeys::Legacy::v1ZoneSelectorGroup(),      ConfigKeys::Legacy::v1AutotilingGroup(),
        ConfigKeys::Legacy::v1AutotileShortcutsGroup(), ConfigKeys::Legacy::v1AnimationsGroup(),
        ConfigKeys::Legacy::v1GlobalShortcutsGroup(),   ConfigKeys::Legacy::v1EditorGroup(),
        ConfigKeys::Legacy::v1OrderingGroup(),          ConfigKeys::Legacy::v1RenderingGroup(),
        ConfigKeys::Legacy::v1ShadersGroup(),
    };
    for (const auto& key : v1Groups) {
        root.remove(key);
    }

    // ── Snapping (top-level) ────────────────────────────────────────────────
    QJsonObject snappingTop;
    moveKey(v1Activation, QLatin1String("SnappingEnabled"), snappingTop, QLatin1String("Enabled"));

    // ── Snapping.Behavior ───────────────────────────────────────────────────
    QJsonObject snappingBehavior;
    moveKey(v1Activation, QLatin1String("DragActivationTriggers"), snappingBehavior, QLatin1String("Triggers"));
    moveKey(v1Activation, QLatin1String("ToggleActivation"), snappingBehavior, QLatin1String("ToggleActivation"));

    // Snapping.Behavior.ZoneSpan
    QJsonObject zoneSpan;
    moveKey(v1Activation, QLatin1String("ZoneSpanEnabled"), zoneSpan, QLatin1String("Enabled"));
    moveKey(v1Activation, QLatin1String("ZoneSpanModifier"), zoneSpan, QLatin1String("Modifier"));
    moveKey(v1Activation, QLatin1String("ZoneSpanTriggers"), zoneSpan, QLatin1String("Triggers"));

    // Snapping.Behavior.SnapAssist
    QJsonObject snapAssist;
    moveKey(v1Activation, QLatin1String("SnapAssistFeatureEnabled"), snapAssist, QLatin1String("FeatureEnabled"));
    moveKey(v1Activation, QLatin1String("SnapAssistEnabled"), snapAssist, QLatin1String("Enabled"));
    moveKey(v1Activation, QLatin1String("SnapAssistTriggers"), snapAssist, QLatin1String("Triggers"));

    // Snapping.Behavior.Display
    QJsonObject snappingDisplay;
    moveKey(v1Display, QLatin1String("ShowOnAllMonitors"), snappingDisplay, QLatin1String("ShowOnAllMonitors"));
    moveKey(v1Display, QLatin1String("DisabledMonitors"), snappingDisplay, QLatin1String("DisabledMonitors"));
    moveKey(v1Display, QLatin1String("DisabledDesktops"), snappingDisplay, QLatin1String("DisabledDesktops"));
    moveKey(v1Display, QLatin1String("DisabledActivities"), snappingDisplay, QLatin1String("DisabledActivities"));
    moveKey(v1Behavior, QLatin1String("FilterLayoutsByAspectRatio"), snappingDisplay,
            QLatin1String("FilterByAspectRatio"));

    // Snapping.Behavior.WindowHandling
    QJsonObject windowHandling;
    moveKey(v1Behavior, QLatin1String("KeepOnResolutionChange"), windowHandling,
            QLatin1String("KeepOnResolutionChange"));
    moveKey(v1Behavior, QLatin1String("MoveNewToLastZone"), windowHandling, QLatin1String("MoveNewToLastZone"));
    moveKey(v1Behavior, QLatin1String("RestoreSizeOnUnsnap"), windowHandling, QLatin1String("RestoreOnUnsnap"));
    moveKey(v1Behavior, QLatin1String("StickyWindowHandling"), windowHandling, QLatin1String("StickyWindowHandling"));
    moveKey(v1Behavior, QLatin1String("RestoreWindowsToZonesOnLogin"), windowHandling, QLatin1String("RestoreOnLogin"));
    moveKey(v1Behavior, QLatin1String("DefaultLayoutId"), windowHandling, QLatin1String("DefaultLayoutId"));

    // Assemble Snapping.Behavior
    if (!zoneSpan.isEmpty())
        snappingBehavior[QLatin1String("ZoneSpan")] = zoneSpan;
    if (!snapAssist.isEmpty())
        snappingBehavior[QLatin1String("SnapAssist")] = snapAssist;
    if (!snappingDisplay.isEmpty())
        snappingBehavior[QLatin1String("Display")] = snappingDisplay;
    if (!windowHandling.isEmpty())
        snappingBehavior[QLatin1String("WindowHandling")] = windowHandling;

    // ── Snapping.Appearance ─────────────────────────────────────────────────
    QJsonObject sColors;
    moveKey(v1Appearance, QLatin1String("UseSystemColors"), sColors, QLatin1String("UseSystem"));
    moveKey(v1Appearance, QLatin1String("HighlightColor"), sColors, QLatin1String("Highlight"));
    moveKey(v1Appearance, QLatin1String("InactiveColor"), sColors, QLatin1String("Inactive"));
    moveKey(v1Appearance, QLatin1String("BorderColor"), sColors, QLatin1String("Border"));

    QJsonObject sOpacity;
    moveKey(v1Appearance, QLatin1String("ActiveOpacity"), sOpacity, QLatin1String("Active"));
    moveKey(v1Appearance, QLatin1String("InactiveOpacity"), sOpacity, QLatin1String("Inactive"));

    QJsonObject sBorder;
    moveKey(v1Appearance, QLatin1String("BorderWidth"), sBorder, QLatin1String("Width"));
    moveKey(v1Appearance, QLatin1String("BorderRadius"), sBorder, QLatin1String("Radius"));

    QJsonObject sLabels;
    moveKey(v1Appearance, QLatin1String("LabelFontColor"), sLabels, QLatin1String("FontColor"));
    moveKey(v1Appearance, QLatin1String("LabelFontFamily"), sLabels, QLatin1String("FontFamily"));
    moveKey(v1Appearance, QLatin1String("LabelFontSizeScale"), sLabels, QLatin1String("FontSizeScale"));
    moveKey(v1Appearance, QLatin1String("LabelFontWeight"), sLabels, QLatin1String("FontWeight"));
    moveKey(v1Appearance, QLatin1String("LabelFontItalic"), sLabels, QLatin1String("FontItalic"));
    moveKey(v1Appearance, QLatin1String("LabelFontUnderline"), sLabels, QLatin1String("FontUnderline"));
    moveKey(v1Appearance, QLatin1String("LabelFontStrikeout"), sLabels, QLatin1String("FontStrikeout"));

    QJsonObject snappingAppearance;
    if (!sColors.isEmpty())
        snappingAppearance[QLatin1String("Colors")] = sColors;
    if (!sOpacity.isEmpty())
        snappingAppearance[QLatin1String("Opacity")] = sOpacity;
    if (!sBorder.isEmpty())
        snappingAppearance[QLatin1String("Border")] = sBorder;
    if (!sLabels.isEmpty())
        snappingAppearance[QLatin1String("Labels")] = sLabels;

    // ── Snapping.Effects ────────────────────────────────────────────────────
    QJsonObject effects;
    moveKey(v1Appearance, QLatin1String("EnableBlur"), effects, QLatin1String("Blur"));
    moveKey(v1Display, QLatin1String("ShowNumbers"), effects, QLatin1String("ShowNumbers"));
    moveKey(v1Display, QLatin1String("FlashOnSwitch"), effects, QLatin1String("FlashOnSwitch"));
    moveKey(v1Display, QLatin1String("ShowOsdOnLayoutSwitch"), effects, QLatin1String("OsdOnLayoutSwitch"));
    moveKey(v1Display, QLatin1String("ShowNavigationOsd"), effects, QLatin1String("NavigationOsd"));
    moveKey(v1Display, QLatin1String("OsdStyle"), effects, QLatin1String("OsdStyle"));
    moveKey(v1Display, QLatin1String("OverlayDisplayMode"), effects, QLatin1String("OverlayDisplayMode"));

    // ── Snapping.ZoneSelector (keys mostly unchanged) ───────────────────────
    // v1 ZoneSelector keys don't have prefixes, so they stay the same
    QJsonObject zoneSelector = v1ZoneSelector;

    // ── Snapping.Gaps ───────────────────────────────────────────────────────
    QJsonObject snappingGaps;
    moveKey(v1Zones, QLatin1String("Padding"), snappingGaps, QLatin1String("Inner"));
    moveKey(v1Zones, QLatin1String("OuterGap"), snappingGaps, QLatin1String("Outer"));
    moveKey(v1Zones, QLatin1String("UsePerSideOuterGap"), snappingGaps, QLatin1String("UsePerSide"));
    moveKey(v1Zones, QLatin1String("OuterGapTop"), snappingGaps, QLatin1String("Top"));
    moveKey(v1Zones, QLatin1String("OuterGapBottom"), snappingGaps, QLatin1String("Bottom"));
    moveKey(v1Zones, QLatin1String("OuterGapLeft"), snappingGaps, QLatin1String("Left"));
    moveKey(v1Zones, QLatin1String("OuterGapRight"), snappingGaps, QLatin1String("Right"));
    moveKey(v1Zones, QLatin1String("AdjacentThreshold"), snappingGaps, QLatin1String("AdjacentThreshold"));

    // ── Assemble Snapping ───────────────────────────────────────────────────
    QJsonObject snapping = snappingTop;
    if (!snappingBehavior.isEmpty())
        snapping[QLatin1String("Behavior")] = snappingBehavior;
    if (!snappingAppearance.isEmpty())
        snapping[QLatin1String("Appearance")] = snappingAppearance;
    if (!effects.isEmpty())
        snapping[QLatin1String("Effects")] = effects;
    if (!zoneSelector.isEmpty())
        snapping[QLatin1String("ZoneSelector")] = zoneSelector;
    if (!snappingGaps.isEmpty())
        snapping[QLatin1String("Gaps")] = snappingGaps;
    if (!snapping.isEmpty())
        root[QLatin1String("Snapping")] = snapping;

    // ── Performance ─────────────────────────────────────────────────────────
    QJsonObject performance;
    moveKey(v1Zones, QLatin1String("PollIntervalMs"), performance, QLatin1String("PollIntervalMs"));
    moveKey(v1Zones, QLatin1String("MinimumZoneSizePx"), performance, QLatin1String("MinimumZoneSizePx"));
    moveKey(v1Zones, QLatin1String("MinimumZoneDisplaySizePx"), performance, QLatin1String("MinimumZoneDisplaySizePx"));
    if (!performance.isEmpty())
        root[QLatin1String("Performance")] = performance;

    // ── Tiling ──────────────────────────────────────────────────────────────
    QJsonObject tilingTop;
    moveKey(v1Autotiling, QLatin1String("AutotileEnabled"), tilingTop, QLatin1String("Enabled"));

    QJsonObject tilingAlgo;
    moveKey(v1Autotiling, QLatin1String("DefaultAutotileAlgorithm"), tilingAlgo, QLatin1String("Default"));
    moveKey(v1Autotiling, QLatin1String("AutotileSplitRatio"), tilingAlgo, QLatin1String("SplitRatio"));
    moveKey(v1Autotiling, QLatin1String("AutotileSplitRatioStep"), tilingAlgo, QLatin1String("SplitRatioStep"));
    moveKey(v1Autotiling, QLatin1String("AutotileMasterCount"), tilingAlgo, QLatin1String("MasterCount"));
    moveKey(v1Autotiling, QLatin1String("AutotileMaxWindows"), tilingAlgo, QLatin1String("MaxWindows"));
    moveKey(v1Autotiling, QLatin1String("AutotilePerAlgorithmSettings"), tilingAlgo,
            QLatin1String("PerAlgorithmSettings"));

    QJsonObject tilingBehavior;
    moveKey(v1Autotiling, QLatin1String("AutotileInsertPosition"), tilingBehavior, QLatin1String("InsertPosition"));
    moveKey(v1Autotiling, QLatin1String("AutotileFocusNewWindows"), tilingBehavior, QLatin1String("FocusNewWindows"));
    moveKey(v1Autotiling, QLatin1String("AutotileFocusFollowsMouse"), tilingBehavior,
            QLatin1String("FocusFollowsMouse"));
    moveKey(v1Autotiling, QLatin1String("AutotileRespectMinimumSize"), tilingBehavior,
            QLatin1String("RespectMinimumSize"));
    moveKey(v1Autotiling, QLatin1String("AutotileStickyWindowHandling"), tilingBehavior,
            QLatin1String("StickyWindowHandling"));
    moveKey(v1Autotiling, QLatin1String("LockedScreens"), tilingBehavior, QLatin1String("LockedScreens"));

    QJsonObject tColors;
    moveKey(v1Autotiling, QLatin1String("AutotileUseSystemBorderColors"), tColors, QLatin1String("UseSystem"));
    moveKey(v1Autotiling, QLatin1String("AutotileBorderColor"), tColors, QLatin1String("Active"));
    moveKey(v1Autotiling, QLatin1String("AutotileInactiveBorderColor"), tColors, QLatin1String("Inactive"));

    QJsonObject tDecorations;
    moveKey(v1Autotiling, QLatin1String("AutotileHideTitleBars"), tDecorations, QLatin1String("HideTitleBars"));

    QJsonObject tBorders;
    moveKey(v1Autotiling, QLatin1String("AutotileShowBorder"), tBorders, QLatin1String("ShowBorder"));
    moveKey(v1Autotiling, QLatin1String("AutotileBorderWidth"), tBorders, QLatin1String("Width"));
    moveKey(v1Autotiling, QLatin1String("AutotileBorderRadius"), tBorders, QLatin1String("Radius"));

    QJsonObject tilingAppearance;
    if (!tColors.isEmpty())
        tilingAppearance[QLatin1String("Colors")] = tColors;
    if (!tDecorations.isEmpty())
        tilingAppearance[QLatin1String("Decorations")] = tDecorations;
    if (!tBorders.isEmpty())
        tilingAppearance[QLatin1String("Borders")] = tBorders;

    QJsonObject tilingGaps;
    moveKey(v1Autotiling, QLatin1String("AutotileInnerGap"), tilingGaps, QLatin1String("Inner"));
    moveKey(v1Autotiling, QLatin1String("AutotileOuterGap"), tilingGaps, QLatin1String("Outer"));
    moveKey(v1Autotiling, QLatin1String("AutotileUsePerSideOuterGap"), tilingGaps, QLatin1String("UsePerSide"));
    moveKey(v1Autotiling, QLatin1String("AutotileOuterGapTop"), tilingGaps, QLatin1String("Top"));
    moveKey(v1Autotiling, QLatin1String("AutotileOuterGapBottom"), tilingGaps, QLatin1String("Bottom"));
    moveKey(v1Autotiling, QLatin1String("AutotileOuterGapLeft"), tilingGaps, QLatin1String("Left"));
    moveKey(v1Autotiling, QLatin1String("AutotileOuterGapRight"), tilingGaps, QLatin1String("Right"));
    moveKey(v1Autotiling, QLatin1String("AutotileSmartGaps"), tilingGaps, QLatin1String("SmartGaps"));

    QJsonObject tiling = tilingTop;
    if (!tilingAlgo.isEmpty())
        tiling[QLatin1String("Algorithm")] = tilingAlgo;
    if (!tilingBehavior.isEmpty())
        tiling[QLatin1String("Behavior")] = tilingBehavior;
    if (!tilingAppearance.isEmpty())
        tiling[QLatin1String("Appearance")] = tilingAppearance;
    if (!tilingGaps.isEmpty())
        tiling[QLatin1String("Gaps")] = tilingGaps;
    if (!tiling.isEmpty())
        root[QLatin1String("Tiling")] = tiling;

    // ── Exclusions (key renames) ────────────────────────────────────────────
    QJsonObject exclusions;
    moveKey(v1Exclusions, QLatin1String("ExcludeTransientWindows"), exclusions, QLatin1String("TransientWindows"));
    moveKey(v1Exclusions, QLatin1String("MinimumWindowWidth"), exclusions, QLatin1String("MinimumWindowWidth"));
    moveKey(v1Exclusions, QLatin1String("MinimumWindowHeight"), exclusions, QLatin1String("MinimumWindowHeight"));
    moveKey(v1Exclusions, QLatin1String("Applications"), exclusions, QLatin1String("Applications"));
    moveKey(v1Exclusions, QLatin1String("WindowClasses"), exclusions, QLatin1String("WindowClasses"));
    if (!exclusions.isEmpty())
        root[QLatin1String("Exclusions")] = exclusions;

    // ── Rendering (key rename) ──────────────────────────────────────────────
    QJsonObject rendering;
    moveKey(v1Rendering, QLatin1String("RenderingBackend"), rendering, QLatin1String("Backend"));
    if (!rendering.isEmpty())
        root[QLatin1String("Rendering")] = rendering;

    // ── Shaders (key renames) ───────────────────────────────────────────────
    QJsonObject shaders;
    moveKey(v1Shaders, QLatin1String("EnableShaderEffects"), shaders, QLatin1String("Enabled"));
    moveKey(v1Shaders, QLatin1String("ShaderFrameRate"), shaders, QLatin1String("FrameRate"));
    moveKey(v1Shaders, QLatin1String("EnableAudioVisualizer"), shaders, QLatin1String("AudioVisualizer"));
    moveKey(v1Shaders, QLatin1String("AudioSpectrumBarCount"), shaders, QLatin1String("AudioSpectrumBarCount"));
    if (!shaders.isEmpty())
        root[QLatin1String("Shaders")] = shaders;

    // ── Animations (key renames + Phase-4 Profile-blob packing) ────────────
    // v1 stored five per-field animation keys + a standalone `AnimationsEnabled`
    // bool. v2's Phase-4-restructure packs the five per-field values into a
    // single `Profile` JSON blob (decision S) while keeping `Enabled` as an
    // orthogonal bool. Preserve v1 users' customisation by composing the
    // Profile blob inline here rather than dropping the fields.
    //
    // Both sides go through accessors: v2 via ConfigDefaults / Profile
    // constants (per CLAUDE.md rule: no inline QStringLiteral for config
    // keys) so a schema rename touches one accessor, and v1 via
    // ConfigKeys::Legacy::v1Animation*Key() so the migration is unambiguous about
    // "reading legacy field" — the v1 shape is stable by definition but
    // having a single source of truth keeps `grep "AnimationDuration"`
    // returning one accessor declaration instead of N call-sites.
    QJsonObject animations;
    moveKey(v1Animations, ConfigKeys::Legacy::v1AnimationsEnabledKey(), animations, ConfigDefaults::enabledKey());

    // Assemble Profile fields from the v1 keys (if present). We build
    // the JSON shape directly using Profile's public field-name
    // constants — matches `Profile::toJson` output without pulling
    // phosphor-animation runtime objects into the migration path
    // (which would bloat the daemon-startup dependency graph for a
    // transient code path). Sharing the constants guarantees that a
    // Profile field rename touches producer and migration together.
    // Every clampable field goes through `qBound` against the
    // ConfigDefaults Min/Max accessors. A v1 config with, say,
    // `AnimationDuration=-50` or `AnimationStaggerInterval=600000` would
    // otherwise land in the v2 blob verbatim — and `Profile::fromJson`
    // rejects out-of-range values at load time with a warning, so the
    // user silently loses their customisation AND the log gets noisy.
    // Clamping at migration time prevents both: the stored value is
    // always in-range, and `Profile::fromJson` accepts it cleanly.
    QJsonObject profile;
    if (v1Animations.contains(ConfigKeys::Legacy::v1AnimationDurationKey())) {
        const int raw = v1Animations.value(ConfigKeys::Legacy::v1AnimationDurationKey()).toInt();
        const int clamped = qBound(ConfigDefaults::animationDurationMin(), raw, ConfigDefaults::animationDurationMax());
        profile[QLatin1String(PhosphorAnimation::Profile::JsonFieldDuration)] = clamped;
    }
    if (v1Animations.contains(ConfigKeys::Legacy::v1AnimationEasingCurveKey())) {
        // Resolve the v1 friendly name (e.g. "easeOutCubic") through a
        // stack-local CurveRegistry so we store the canonical wire form
        // (e.g. "0.33,1.00,0.68,1.00") in the Profile blob. Without this
        // step, `Profile::fromJson` resolves the friendly name on read
        // via the consumer's registry, but the first UI save then
        // serialises the Curve back using `Curve::toString()` — which
        // always emits canonical wire form — causing a spurious config
        // rewrite on first post-migration interaction. Built-in curves
        // auto-register via the CurveRegistry constructor, so no
        // explicit registerBuiltins() call is needed here.
        //
        // Unknown specs (custom curve names from a v1 plugin that no
        // longer exists, typos in hand-edited v1 configs) are DROPPED
        // here rather than persisted. Persisting the raw string would
        // make `Profile::fromJson` emit the "curve spec … did not
        // resolve" warning on every daemon start forever; dropping the
        // field lets the library-default OutCubic apply and silences
        // the repeating diagnostic. The migration log below records
        // the dropped spec so an operator investigating a silent
        // curve change can find it.
        const QString v1Curve = v1Animations.value(ConfigKeys::Legacy::v1AnimationEasingCurveKey()).toString();
        PhosphorAnimation::CurveRegistry registry;
        if (const auto resolved = registry.tryCreate(v1Curve)) {
            profile[QLatin1String(PhosphorAnimation::Profile::JsonFieldCurve)] = resolved->toString();
        } else {
            qInfo("migrateV1ToV2: dropping unresolved v1 curve spec '%s' — library default (OutCubic) will apply",
                  qPrintable(v1Curve));
        }
    }
    if (v1Animations.contains(ConfigKeys::Legacy::v1AnimationMinDistanceKey())) {
        const int raw = v1Animations.value(ConfigKeys::Legacy::v1AnimationMinDistanceKey()).toInt();
        const int clamped =
            qBound(ConfigDefaults::animationMinDistanceMin(), raw, ConfigDefaults::animationMinDistanceMax());
        profile[QLatin1String(PhosphorAnimation::Profile::JsonFieldMinDistance)] = clamped;
    }
    if (v1Animations.contains(ConfigKeys::Legacy::v1AnimationSequenceModeKey())) {
        // SequenceMode is a closed enum (AllAtOnce=0, Stagger=1 as of v2).
        // Out-of-range values snap to the project default rather than
        // clamping to the nearest bound — clamping would silently alias
        // e.g. 999 onto Stagger, which is semantically different from
        // "the user's setting is meaningless, use the default".
        const int raw = v1Animations.value(ConfigKeys::Legacy::v1AnimationSequenceModeKey()).toInt();
        const int resolved =
            (raw >= ConfigDefaults::animationSequenceModeMin() && raw <= ConfigDefaults::animationSequenceModeMax())
            ? raw
            : ConfigDefaults::animationSequenceMode();
        profile[QLatin1String(PhosphorAnimation::Profile::JsonFieldSequenceMode)] = resolved;
    }
    if (v1Animations.contains(ConfigKeys::Legacy::v1AnimationStaggerIntervalKey())) {
        const int raw = v1Animations.value(ConfigKeys::Legacy::v1AnimationStaggerIntervalKey()).toInt();
        const int clamped =
            qBound(ConfigDefaults::animationStaggerIntervalMin(), raw, ConfigDefaults::animationStaggerIntervalMax());
        profile[QLatin1String(PhosphorAnimation::Profile::JsonFieldStaggerInterval)] = clamped;
    }
    if (!profile.isEmpty()) {
        // v1→v2 writes a stringified JSON blob even though the live schema
        // now stores AnimationProfile as a nested QVariantMap. Stringifying
        // here keeps the migrated value as a single scalar leaf so the
        // schema/migration cross-check
        // (`testSchemaCoversEveryMigrationDestinationKey`) sees one
        // declared key (`Animations/AnimationProfile`) instead of treating
        // the nested object as a sub-group of synthetic per-field keys.
        // The Settings layer's `Store::read<QVariantMap>` legacy-string
        // fallback parses this on first load and the next save normalises
        // it to a nested object.
        animations[ConfigDefaults::animationProfileKey()] =
            QString::fromUtf8(QJsonDocument(profile).toJson(QJsonDocument::Compact));
    }
    if (!animations.isEmpty())
        root[ConfigDefaults::animationsGroup()] = animations;

    // ── Shortcuts.Global (drop "Shortcut" suffix from some keys) ────────────
    QJsonObject globalShortcuts;
    moveKey(v1GlobalShortcuts, QLatin1String("OpenEditorShortcut"), globalShortcuts, QLatin1String("OpenEditor"));
    moveKey(v1GlobalShortcuts, QLatin1String("OpenSettingsShortcut"), globalShortcuts, QLatin1String("OpenSettings"));
    moveKey(v1GlobalShortcuts, QLatin1String("PreviousLayoutShortcut"), globalShortcuts,
            QLatin1String("PreviousLayout"));
    moveKey(v1GlobalShortcuts, QLatin1String("NextLayoutShortcut"), globalShortcuts, QLatin1String("NextLayout"));
    for (int i = 1; i <= 9; ++i) {
        moveKey(v1GlobalShortcuts, QStringLiteral("QuickLayout%1Shortcut").arg(i), globalShortcuts,
                QStringLiteral("QuickLayout%1").arg(i));
    }
    // Navigation keys — same names in v1 and v2
    for (const auto& key :
         {"MoveWindowLeft", "MoveWindowRight", "MoveWindowUp", "MoveWindowDown", "FocusZoneLeft", "FocusZoneRight",
          "FocusZoneUp", "FocusZoneDown", "PushToEmptyZone", "RestoreWindowSize", "ToggleWindowFloat", "SwapWindowLeft",
          "SwapWindowRight", "SwapWindowUp", "SwapWindowDown", "RotateWindowsClockwise",
          "RotateWindowsCounterclockwise", "CycleWindowForward", "CycleWindowBackward"}) {
        moveKey(v1GlobalShortcuts, QLatin1String(key), globalShortcuts, QLatin1String(key));
    }
    for (int i = 1; i <= 9; ++i) {
        const QString key = QStringLiteral("SnapToZone%1").arg(i);
        moveKey(v1GlobalShortcuts, key, globalShortcuts, key);
    }
    moveKey(v1GlobalShortcuts, QLatin1String("ResnapToNewLayoutShortcut"), globalShortcuts,
            QLatin1String("ResnapToNewLayout"));
    moveKey(v1GlobalShortcuts, QLatin1String("SnapAllWindowsShortcut"), globalShortcuts,
            QLatin1String("SnapAllWindows"));
    moveKey(v1GlobalShortcuts, QLatin1String("LayoutPickerShortcut"), globalShortcuts, QLatin1String("LayoutPicker"));
    moveKey(v1GlobalShortcuts, QLatin1String("ToggleLayoutLockShortcut"), globalShortcuts,
            QLatin1String("ToggleLayoutLock"));

    // ── Shortcuts.Tiling (drop "Shortcut" suffix) ───────────────────────────
    QJsonObject tilingShortcuts;
    moveKey(v1AutotileShortcuts, QLatin1String("ToggleShortcut"), tilingShortcuts, QLatin1String("Toggle"));
    moveKey(v1AutotileShortcuts, QLatin1String("FocusMasterShortcut"), tilingShortcuts, QLatin1String("FocusMaster"));
    moveKey(v1AutotileShortcuts, QLatin1String("SwapMasterShortcut"), tilingShortcuts, QLatin1String("SwapMaster"));
    moveKey(v1AutotileShortcuts, QLatin1String("IncMasterRatioShortcut"), tilingShortcuts,
            QLatin1String("IncMasterRatio"));
    moveKey(v1AutotileShortcuts, QLatin1String("DecMasterRatioShortcut"), tilingShortcuts,
            QLatin1String("DecMasterRatio"));
    moveKey(v1AutotileShortcuts, QLatin1String("IncMasterCountShortcut"), tilingShortcuts,
            QLatin1String("IncMasterCount"));
    moveKey(v1AutotileShortcuts, QLatin1String("DecMasterCountShortcut"), tilingShortcuts,
            QLatin1String("DecMasterCount"));
    moveKey(v1AutotileShortcuts, QLatin1String("RetileShortcut"), tilingShortcuts, QLatin1String("Retile"));

    QJsonObject shortcuts;
    if (!globalShortcuts.isEmpty())
        shortcuts[QLatin1String("Global")] = globalShortcuts;
    if (!tilingShortcuts.isEmpty())
        shortcuts[QLatin1String("Tiling")] = tilingShortcuts;
    if (!shortcuts.isEmpty())
        root[QLatin1String("Shortcuts")] = shortcuts;

    // ── Editor (split into sub-groups) ──────────────────────────────────────
    QJsonObject editorShortcuts;
    moveKey(v1Editor, QLatin1String("EditorDuplicateShortcut"), editorShortcuts, QLatin1String("Duplicate"));
    moveKey(v1Editor, QLatin1String("EditorSplitHorizontalShortcut"), editorShortcuts,
            QLatin1String("SplitHorizontal"));
    moveKey(v1Editor, QLatin1String("EditorSplitVerticalShortcut"), editorShortcuts, QLatin1String("SplitVertical"));
    moveKey(v1Editor, QLatin1String("EditorFillShortcut"), editorShortcuts, QLatin1String("Fill"));

    QJsonObject editorSnapping;
    moveKey(v1Editor, QLatin1String("GridSnappingEnabled"), editorSnapping, QLatin1String("GridEnabled"));
    moveKey(v1Editor, QLatin1String("EdgeSnappingEnabled"), editorSnapping, QLatin1String("EdgeEnabled"));
    moveKey(v1Editor, QLatin1String("SnapIntervalX"), editorSnapping, QLatin1String("IntervalX"));
    moveKey(v1Editor, QLatin1String("SnapIntervalY"), editorSnapping, QLatin1String("IntervalY"));
    // If per-axis intervals don't exist, propagate the unified SnapInterval to both.
    // Without this, configs that only set SnapInterval (no per-axis override) would
    // lose their value because the v2 load code reads IntervalX/IntervalY directly.
    if (!editorSnapping.contains(QLatin1String("IntervalX"))) {
        moveKey(v1Editor, QLatin1String("SnapInterval"), editorSnapping, QLatin1String("IntervalX"));
    }
    if (!editorSnapping.contains(QLatin1String("IntervalY"))) {
        moveKey(v1Editor, QLatin1String("SnapInterval"), editorSnapping, QLatin1String("IntervalY"));
    }
    moveKey(v1Editor, QLatin1String("SnapOverrideModifier"), editorSnapping, QLatin1String("OverrideModifier"));

    QJsonObject editorFillOnDrop;
    moveKey(v1Editor, QLatin1String("FillOnDropEnabled"), editorFillOnDrop, QLatin1String("Enabled"));
    moveKey(v1Editor, QLatin1String("FillOnDropModifier"), editorFillOnDrop, QLatin1String("Modifier"));

    QJsonObject editor;
    if (!editorShortcuts.isEmpty())
        editor[QLatin1String("Shortcuts")] = editorShortcuts;
    if (!editorSnapping.isEmpty())
        editor[QLatin1String("Snapping")] = editorSnapping;
    if (!editorFillOnDrop.isEmpty())
        editor[QLatin1String("FillOnDrop")] = editorFillOnDrop;
    if (!editor.isEmpty())
        root[QLatin1String("Editor")] = editor;

    // ── Ordering (keys unchanged) ───────────────────────────────────────────
    if (!v1Ordering.isEmpty())
        root[QLatin1String("Ordering")] = v1Ordering;

    // ── Extract WindowTracking (ephemeral session state) to session.json ──
    // In v2, session state lives in its own file to avoid write contention
    // between user preferences (config.json) and high-frequency window
    // tracking saves (session.json).
    //
    // Atomicity: only mutate `root` after the side-effect write succeeds.
    // If the out-of-tree write fails, leaving the keys in `root` lets
    // `config.json` retain the data so the next startup's migration can
    // retry. A partial commit here would silently lose session state.
    bool allSideEffectsSucceeded = true;

    const QString wtGroup = ConfigKeys::windowTrackingGroup();
    if (root.contains(wtGroup)) {
        QJsonObject sessionRoot;
        sessionRoot[wtGroup] = root.value(wtGroup);
        const QString sessionPath = ConfigDefaults::sessionFilePath();
        if (PhosphorConfig::JsonBackend::writeJsonAtomically(sessionPath, sessionRoot)) {
            root.remove(wtGroup);
        } else {
            qWarning(
                "ConfigMigration: failed to write session state to %s — "
                "aborting migration so the next run can retry",
                qPrintable(sessionPath));
            allSideEffectsSucceeded = false;
        }
    }

    // ── Extract Assignment/QuickLayouts to assignments.json ─────────────────
    // assignments.json is the v3 PhosphorZones::LayoutRegistry persistence file. It is
    // itself superseded in v4 by windowrules.json (see finalizeV4Conversion),
    // so this extraction is a stepping-stone that v3→v4 reads back out.
    {
        QJsonObject assignRoot;
        const QString assignPrefix = ConfigKeys::Legacy::v3assignmentGroupPrefix();
        QStringList keysToRemove;
        for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
            if (it.key().startsWith(assignPrefix)) {
                assignRoot[it.key()] = it.value();
                keysToRemove.append(it.key());
            }
        }
        const QString quickLayoutsKey = ConfigKeys::Legacy::v3quickLayoutsGroup();
        if (root.contains(quickLayoutsKey)) {
            assignRoot[quickLayoutsKey] = root.value(quickLayoutsKey);
            keysToRemove.append(quickLayoutsKey);
        }
        // ModeTracking is NOT extracted to assignments.json — in v3 it was
        // consumed by PhosphorZones::LayoutRegistry directly from config.json and deleted
        // after application. Extracting it would leave dead data in
        // assignments.json that nothing reads (and v4 doesn't read it either).
        const QString modeTrackingKey = ConfigKeys::Legacy::v3modeTrackingGroup();
        if (root.contains(modeTrackingKey)) {
            keysToRemove.append(modeTrackingKey);
        }

        bool assignmentsWritten = true;
        if (!assignRoot.isEmpty()) {
            const QString assignPath = legacyAssignmentsFilePath();
            if (!PhosphorConfig::JsonBackend::writeJsonAtomically(assignPath, assignRoot)) {
                qWarning(
                    "ConfigMigration: failed to write assignments to %s — "
                    "aborting migration so the next run can retry",
                    qPrintable(assignPath));
                assignmentsWritten = false;
                allSideEffectsSucceeded = false;
            }
        }
        // Only strip from root if the external file is committed (or there
        // was nothing to extract). ModeTracking is safe to drop unconditionally
        // — it's ephemeral and consumed in-memory only.
        if (assignmentsWritten) {
            for (const QString& key : keysToRemove) {
                root.remove(key);
            }
        } else if (root.contains(modeTrackingKey)) {
            // Still safe to drop ModeTracking even if assignments write failed.
            root.remove(modeTrackingKey);
        }
    }

    // ── Bump version ────────────────────────────────────────────────────────
    // Stamp literal 2, not ConfigSchemaVersion — prevents future version bumps
    // (e.g. to 3) from making this step stamp 3 and skipping a v2→v3 migration.
    //
    // Skip the bump when any side-effect write failed. MigrationRunner
    // detects the unbumped version, aborts the chain with a critical log,
    // and config.json is left untouched so the next startup retries.
    //
    // Note: the in-memory @p root has already been mutated extensively
    // above (v1 groups removed, dot-path hierarchy rebuilt) by the time
    // we get here. On a side-effect failure these mutations are silently
    // discarded by the caller — MigrationRunner::runOnFile sees that the
    // version key didn't advance and skips the disk write entirely, and
    // Store::Store's in-memory migration path likewise compares before vs.
    // after and skips the file rewrite. The on-disk file is therefore left
    // at v1 with the original layout, ready for the next startup to retry.
    if (allSideEffectsSucceeded) {
        root[ConfigKeys::versionKey()] = 2;
    }
}

// ── Schema migration: v2 → v3 ───────────────────────────────────────────────
// Splits the single "this monitor / desktop / activity is disabled in PlasmaZones"
// list into independent per-mode lists, and relocates them out of
// Snapping.Behavior.Display (which historically gated both modes despite the
// snapping-prefixed group name) into a mode-neutral Display group.
//
// Migration semantics: every entry in a v2 disabled list is copied into BOTH
// the snapping and autotile lists in v3. Rationale: v2 didn't distinguish
// modes, so the only safe interpretation of "the user disabled monitor X" is
// "the user wanted X off in PlasmaZones, period" — preserve that intent in
// both modes until the user explicitly re-enables one in the new UI.
//
// Side note: the v2 keys ShowOnAllMonitors and FilterByAspectRatio remain
// in Snapping.Behavior.Display untouched — only the three Disabled* keys move.

void ConfigMigration::migrateV2ToV3(QJsonObject& root)
{
    // Defense-in-depth idempotency guard, mirroring migrateV1ToV2. The
    // PhosphorConfig::MigrationRunner gates this on version == 2 and
    // `ensureJsonConfig` bails early when version >= ConfigSchemaVersion,
    // but a direct caller that hands us an already-v3 doc would otherwise
    // re-read v3-named groups as if they were v2 candidates. The v2→v3
    // step is largely empty-tolerant (each takeKey returns empty for
    // absent keys), but the asymmetry vs v1→v2's guard is a foot-gun.
    if (root.value(ConfigKeys::versionKey()).toInt(0) >= 3) {
        return;
    }

    // Walk the canonical v2 dot-path Snapping.Behavior.Display by splitting
    // the group accessor on '.' — this keeps the migration in lockstep with
    // the schema instead of duplicating segment names as bare literals.
    // The accessor still resolves to the v2 group because that group lives
    // on past v3 (it continues to hold ShowOnAllMonitors and
    // FilterByAspectRatio); only the three Disabled* keys move out.
    const QStringList v2GroupSegments =
        ConfigKeys::snappingBehaviorDisplayGroup().split(QLatin1Char('.'), Qt::SkipEmptyParts);
    Q_ASSERT(v2GroupSegments.size() == 3);
    const QString& snappingSeg = v2GroupSegments[0];
    const QString& behaviorSeg = v2GroupSegments[1];
    const QString& displaySeg = v2GroupSegments[2];

    QJsonObject snapping = root.value(snappingSeg).toObject();
    QJsonObject behavior = snapping.value(behaviorSeg).toObject();
    QJsonObject v2Display = behavior.value(displaySeg).toObject();

    // takeKey: read the v2 string value at @p key, drop the key from @p obj
    // unconditionally if present (even when the value isn't a string — we
    // don't want a hand-edited array or null lingering past the migration
    // and looking like live v2 data on a v3-stamped config), and return
    // the string representation when one is available.
    auto takeKey = [](QJsonObject& obj, const QString& key) -> QString {
        const auto it = obj.find(key);
        if (it == obj.end()) {
            return {};
        }
        const QJsonValue v = it.value();
        QString result;
        if (v.isString()) {
            result = v.toString();
        }
        obj.erase(it);
        return result;
    };

    const QString v2Monitors = takeKey(v2Display, ConfigKeys::Legacy::v2DisabledMonitorsKey());
    const QString v2Desktops = takeKey(v2Display, ConfigKeys::Legacy::v2DisabledDesktopsKey());
    const QString v2Activities = takeKey(v2Display, ConfigKeys::Legacy::v2DisabledActivitiesKey());

    // Write the duplicated lists into the new Display group. Skip empties so
    // a clean v2 config with no disabled entries doesn't grow noise keys.
    QJsonObject v3Display = root.value(ConfigKeys::displayGroup()).toObject();

    auto writeIfNonEmpty = [&v3Display](const QString& key, const QString& value) {
        if (!value.isEmpty()) {
            v3Display[key] = value;
        }
    };

    writeIfNonEmpty(ConfigKeys::Legacy::v3snappingDisabledMonitorsKey(), v2Monitors);
    writeIfNonEmpty(ConfigKeys::Legacy::v3autotileDisabledMonitorsKey(), v2Monitors);
    writeIfNonEmpty(ConfigKeys::Legacy::v3snappingDisabledDesktopsKey(), v2Desktops);
    writeIfNonEmpty(ConfigKeys::Legacy::v3autotileDisabledDesktopsKey(), v2Desktops);
    writeIfNonEmpty(ConfigKeys::Legacy::v3snappingDisabledActivitiesKey(), v2Activities);
    writeIfNonEmpty(ConfigKeys::Legacy::v3autotileDisabledActivitiesKey(), v2Activities);

    // Stitch the trimmed v2 Display object back into Snapping.Behavior, drop
    // the Display sub-object entirely if it became empty (no ShowOnAllMonitors
    // / FilterByAspectRatio either). Same for Snapping.Behavior itself.
    if (v2Display.isEmpty()) {
        behavior.remove(displaySeg);
    } else {
        behavior[displaySeg] = v2Display;
    }
    if (behavior.isEmpty()) {
        snapping.remove(behaviorSeg);
    } else {
        snapping[behaviorSeg] = behavior;
    }
    if (snapping.isEmpty()) {
        root.remove(snappingSeg);
    } else {
        root[snappingSeg] = snapping;
    }

    if (!v3Display.isEmpty()) {
        root[ConfigKeys::displayGroup()] = v3Display;
    }

    // Stamp literal 3 — see migrateV1ToV2 for why this isn't ConfigSchemaVersion.
    root[ConfigKeys::versionKey()] = 3;
}

// ── Schema migration: v3 → v4 ───────────────────────────────────────────────
// Window-rule consolidation — Phase 3.
//
// The v4 conversion produces the new windowrules.json store: every zone
// Assignment and per-mode disable entry becomes a context-only WindowRule.
// windowrules.json SUPERSEDES assignments.json and the config.json
// Display.*Disabled* keys — the runtime LayoutRegistry and Settings now read
// the rule store exclusively, so the v3 inputs are removed once converted.
//
// A MigrationStep is `void(QJsonObject&)` — it can only touch config.json.
// This step REMOVES the six Display.*Disabled* keys (their values are stashed
// into a temporary `_v4DisableStash` root key for finalizeV4Conversion to
// consume) and stamps `_version = 4`. finalizeV4Conversion (a post-chain step)
// reads that stash + assignments.json, writes windowrules.json, then deletes
// assignments.json as the irreversible commit.

namespace {
// The temporary root keys migrateV3ToV4 writes and finalizeV4Conversion
// consumes + strips. Not real schema keys — they never survive a completed
// conversion.
constexpr QLatin1String kV4DisableStashKey{"_v4DisableStash"};
// Carries the v4 `Animations.AnimationAppRules` array forward to
// finalizeV4Conversion, which converts each legacy entry into a WindowRule and
// appends it to windowrules.json. The source key is removed from the
// Animations group in migrateV3ToV4 so the unified rule store becomes the sole
// home for per-window animation overrides.
constexpr QLatin1String kV4AnimationRulesStashKey{"_v4AnimationRulesStash"};

// Inner field names inside the `_v4DisableStash` object. Shared between the
// writer (migrateV3ToV4) and the reader (finalizeV4Conversion) so a typo or
// rename can't silently drop a disable list on conversion.
constexpr QLatin1StringView kV3SnappingMonitorsStash{"snappingMonitors"};
constexpr QLatin1StringView kV3AutotileMonitorsStash{"autotileMonitors"};
constexpr QLatin1StringView kV3SnappingDesktopsStash{"snappingDesktops"};
constexpr QLatin1StringView kV3AutotileDesktopsStash{"autotileDesktops"};
constexpr QLatin1StringView kV3SnappingActivitiesStash{"snappingActivities"};
constexpr QLatin1StringView kV3AutotileActivitiesStash{"autotileActivities"};
} // namespace

void ConfigMigration::migrateV3ToV4(QJsonObject& root)
{
    // Schema-version-migration freeze policy: this function reads the v3
    // on-disk shape. All group/key accessors used here MUST be the frozen
    // `ConfigKeys::Legacy::v3*` accessors, NEVER the live ConfigDefaults
    // accessors. A future runtime rename of the live accessor would silently
    // retarget this migration to a path no v3 config ever had on disk; the
    // freeze decouples the migration's stable wire-format contract from the
    // live schema. See the comment block on `v4AnimationsGroup` in
    // configkeys.h for the same rationale applied to v4.
    //
    // Defense-in-depth idempotency guard, mirroring the earlier steps.
    if (root.value(ConfigKeys::versionKey()).toInt(0) >= 4) {
        return;
    }

    QJsonObject display = root.value(ConfigKeys::Legacy::v3DisplayGroup()).toObject();

    // Move the disable-list values out of config.json: stash the value, then
    // REMOVE the key. windowrules.json supersedes them — the runtime Settings
    // layer reads DisableEngine rules from the store now, never these keys.
    // finalizeV4Conversion consumes the stash and writes the rules.
    QJsonObject stash;
    const auto moveDisableKey = [&display, &stash](const QString& configKey, QLatin1StringView stashKey) {
        // Only stash a value when the v3 key actually carried one — an absent
        // or empty disable list contributes no rules, so a stash entry for it
        // would just be inert noise finalizeV4Conversion has to skip.
        const QString value = display.value(configKey).toString();
        if (!value.isEmpty()) {
            stash.insert(stashKey, value);
        }
        display.remove(configKey);
    };
    moveDisableKey(ConfigKeys::Legacy::v3snappingDisabledMonitorsKey(), kV3SnappingMonitorsStash);
    moveDisableKey(ConfigKeys::Legacy::v3autotileDisabledMonitorsKey(), kV3AutotileMonitorsStash);
    moveDisableKey(ConfigKeys::Legacy::v3snappingDisabledDesktopsKey(), kV3SnappingDesktopsStash);
    moveDisableKey(ConfigKeys::Legacy::v3autotileDisabledDesktopsKey(), kV3AutotileDesktopsStash);
    moveDisableKey(ConfigKeys::Legacy::v3snappingDisabledActivitiesKey(), kV3SnappingActivitiesStash);
    moveDisableKey(ConfigKeys::Legacy::v3autotileDisabledActivitiesKey(), kV3AutotileActivitiesStash);

    // Write the stripped Display group back; drop it entirely if now empty so
    // no husk object lingers.
    if (display.isEmpty()) {
        root.remove(ConfigKeys::Legacy::v3DisplayGroup());
    } else {
        root[ConfigKeys::Legacy::v3DisplayGroup()] = display;
    }

    // ── Stash hand-off to finalizeV4Conversion ─────────────────────────────
    // Both v4 stashes follow the same shape: persist the key only when
    // there's at least one entry to carry forward, so a clean v3 config
    // doesn't leave inert empty stashes for the finalizer to strip. The
    // finalizer reads a missing key as an empty input and no-ops on that
    // axis. The Animations group itself stays on disk when non-empty
    // (ShaderProfileTree still lives under it).
    //
    // Disable-list stash: each `moveDisableKey` above already skipped empty
    // values, so an empty `stash` here means the v3 config had no disable
    // lists at all.
    if (!stash.isEmpty()) {
        root[kV4DisableStashKey] = stash;
    }

    // Animation App Rule stash: v4 folds per-window animation overrides into
    // the unified windowrules.json store as `OverrideAnimationShader` /
    // `OverrideAnimationTiming` actions on a `WindowClass Contains <pattern>`
    // matcher. finalizeV4Conversion ports the bridge logic against the
    // stashed JSON and appends the resulting WindowRules to the same rule
    // set assignments/disable lists feed.
    QJsonObject animations = root.value(ConfigKeys::Legacy::v4AnimationsGroup()).toObject();
    const QJsonArray animationRules = animations.value(ConfigKeys::Legacy::v4AnimationAppRulesKey()).toArray();
    if (!animationRules.isEmpty()) {
        root[kV4AnimationRulesStashKey] = animationRules;
    }
    animations.remove(ConfigKeys::Legacy::v4AnimationAppRulesKey());
    if (animations.isEmpty()) {
        root.remove(ConfigKeys::Legacy::v4AnimationsGroup());
    } else {
        root[ConfigKeys::Legacy::v4AnimationsGroup()] = animations;
    }

    // Stamp literal 4 — see migrateV1ToV2 for why this isn't ConfigSchemaVersion.
    root[ConfigKeys::versionKey()] = 4;
}

// ── v4 finalizer: the two-file conversion ───────────────────────────────────

namespace {

/// Resolve a dot-path config group accessor (e.g. "Snapping.Behavior.WindowHandling")
/// against a nested JSON root and return the leaf-group object. Walking the
/// accessor's own segments keeps the migration in lockstep with the schema
/// instead of duplicating segment names as bare literals — the v1*-migration
/// literal exemption does NOT cover live v4 config keys.
QJsonObject groupObjectAtPath(const QJsonObject& root, const QString& dotPath)
{
    QJsonObject obj = root;
    for (const QString& segment : dotPath.split(QLatin1Char('.'), Qt::SkipEmptyParts)) {
        obj = obj.value(segment).toObject();
    }
    return obj;
}

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
PhosphorWindowRule::WindowRule disableRuleForMonitor(const QString& screenId, bool autotile)
{
    const QString name = disableRulePrefixFor(autotile) + screenId;
    return PhosphorWindowRule::ContextRuleBridge::makeDisableRule(name, screenId, 0, QString(), autotile);
}

/// Build a context rule from a v3 desktop disable-list entry (`screenId/N`).
/// Returns nullopt on a malformed entry.
///
/// Screen ids MUST NOT contain '/': the desktop number is the last '/'-segment
/// (split on `lastIndexOf('/')`), so a screen id with embedded slashes would be
/// truncated. This matches the `screenId/desktop` composite-key convention used
/// by Settings::writeDisableEntries.
std::optional<PhosphorWindowRule::WindowRule> disableRuleForDesktop(const QString& entry, bool autotile)
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
    const QString name = disableRulePrefixFor(autotile) + screenId + disableRuleDesktopSuffix(desktop);
    return PhosphorWindowRule::ContextRuleBridge::makeDisableRule(name, screenId, desktop, QString(), autotile);
}

/// Build a context rule from a v3 activity disable-list entry
/// (`screenId/activityUuid`). Returns nullopt on a malformed entry.
///
/// Screen ids MUST NOT contain '/': the screen id is the first '/'-segment
/// (split on `indexOf('/')`) and the activity uuid is the remainder, so a
/// screen id with embedded slashes would be truncated. This matches the
/// `screenId/activity` composite-key convention used by
/// Settings::writeDisableEntries.
std::optional<PhosphorWindowRule::WindowRule> disableRuleForActivity(const QString& entry, bool autotile)
{
    const int slash = entry.indexOf(QLatin1Char('/'));
    if (slash <= 0 || slash == entry.size() - 1) {
        return std::nullopt;
    }
    const QString screenId = entry.left(slash);
    const QString activity = entry.mid(slash + 1);
    const QString name = disableRulePrefixFor(autotile) + screenId + disableRuleActivitySuffix();
    return PhosphorWindowRule::ContextRuleBridge::makeDisableRule(name, screenId, 0, activity, autotile);
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
    const int actIdx = remainder.indexOf(QLatin1String(":Activity:"));
    if (actIdx >= 0) {
        const QString a = remainder.mid(actIdx + 10);
        if (!a.isEmpty()) {
            activity = a;
        }
        remainder = remainder.left(actIdx);
    }
    const int deskIdx = remainder.indexOf(QLatin1String(":Desktop:"));
    if (deskIdx >= 0) {
        bool ok = false;
        const int d = remainder.mid(deskIdx + 9).toInt(&ok);
        if (ok && d > 0) {
            desktop = d;
        }
        remainder = remainder.left(deskIdx);
    }
    screenId = remainder;
    return !screenId.isEmpty();
}

/// Human-readable label for a migrated assignment rule.
QString assignmentRuleName(const QString& screenId, int desktop, const QString& activity)
{
    QString name = screenId;
    if (desktop > 0) {
        name += QStringLiteral(" · Desktop ") + QString::number(desktop);
    }
    if (!activity.isEmpty()) {
        name += QStringLiteral(" · Activity");
    }
    return name;
}

// ─── Animation App Rule → WindowRule conversion ─────────────────────────────
// Ports the (now-deleted) PhosphorWindowRule::AnimationAppRuleBridge logic
// against the raw stash JSON. The legacy AnimationAppRule type is gone in v4+,
// so the conversion lives here — the migration is its sole remaining caller.

/// Fixed v5-UUID namespace for animation App-Rule identities. Inherited
/// verbatim from the legacy bridge so the migration's output is byte-stable
/// across re-runs (idempotent rule-id derivation).
const QUuid& animationAppRuleNamespaceUuid()
{
    static const QUuid ns(QStringLiteral("{b3f2c1a0-7d4e-5f6a-8b9c-0d1e2f3a4b5c}"));
    return ns;
}

// Legacy AnimationAppRule wire keys. The v3 on-disk format is frozen — the
// runtime accessors are deleted, so these literals are the migration's last
// reader of the shape. File-scope so the validate/build split below can share
// them without re-declaring.
constexpr QLatin1StringView kKeyClassPattern{"classPattern"};
constexpr QLatin1StringView kKeyEventPath{"eventPath"};
constexpr QLatin1StringView kKeyKind{"kind"};
constexpr QLatin1StringView kKeyEffectId{"effectId"};
constexpr QLatin1StringView kKeyShaderParams{"shaderParams"};
constexpr QLatin1StringView kKeyCurve{"curve"};
constexpr QLatin1StringView kKeyDurationMs{"durationMs"};
constexpr QLatin1StringView kKindShader{"shader"};
constexpr QLatin1StringView kKindTiming{"timing"};

/// True if @p source carries the minimum fields a non-discarded legacy
/// AnimationAppRule must have (non-empty classPattern + eventPath, and a
/// recognised `kind`). Mirrors the legacy rule-level loader's
/// drop-on-malformed contract: this is the predicate the caller uses to
/// filter the stash BEFORE assigning priorities, so dropped entries don't
/// leave gaps in the priority sequence the way they would if we filtered
/// in-loop with a pre-computed count.
bool isValidAnimationAppRuleSource(const QJsonObject& source)
{
    const QString classPattern = source.value(kKeyClassPattern).toString();
    const QString eventPath = source.value(kKeyEventPath).toString();
    if (classPattern.isEmpty() || eventPath.isEmpty()) {
        return false;
    }
    const QString kindStr = source.value(kKeyKind).toString();
    return kindStr.compare(kKindShader, Qt::CaseInsensitive) == 0
        || kindStr.compare(kKindTiming, Qt::CaseInsensitive) == 0;
}

/// Build a single WindowRule from a legacy AnimationAppRule JSON object
/// already known to pass @ref isValidAnimationAppRuleSource. The caller is
/// responsible for validating before calling; this function is total on
/// valid input.
///
/// @param i      zero-based index into the FILTERED (valid-only) source
///               list — used to derive `priority = count - i`.
/// @param count  total VALID source entries (priority floors at 1, reserving
///               0 for the provider-default catch-all band).
PhosphorWindowRule::WindowRule buildAnimationAppRule(const QJsonObject& source, int i, int count)
{
    namespace ActionParam = PhosphorWindowRule::ActionParam;

    const QString classPattern = source.value(kKeyClassPattern).toString();
    const QString eventPath = source.value(kKeyEventPath).toString();
    const QString kindStr = source.value(kKeyKind).toString();
    const bool isShader = kindStr.compare(kKindShader, Qt::CaseInsensitive) == 0;

    PhosphorWindowRule::RuleAction action;
    QJsonObject params;
    params.insert(ActionParam::Event, eventPath);
    if (isShader) {
        action.type = QString(PhosphorWindowRule::ActionType::OverrideAnimationShader);
        // effectId is always written — the empty string is the engaged-blocking
        // sentinel ("disable shader for matching windows"), distinct from an
        // unfilled slot ("no rule matched").
        params.insert(ActionParam::EffectId, source.value(kKeyEffectId).toString());
        // shaderParams round-trip note: the legacy bridge funneled the inner
        // params through QVariantMap (JSON → AnimationAppRule → QVariantMap →
        // QJsonObject), which silently lossy-coerces edge-case numeric types.
        // The migration ports the object verbatim instead — strictly more
        // type-faithful, and the only observable difference is for inputs the
        // QVariantMap round-trip would have corrupted anyway. A non-object
        // shaderParams payload (stray array / scalar) drops the inner block
        // and logs a warning, matching the legacy loader's diagnostic.
        const QJsonValue rawParams = source.value(kKeyShaderParams);
        if (rawParams.isObject()) {
            const QJsonObject paramsObj = rawParams.toObject();
            if (!paramsObj.isEmpty()) {
                params.insert(ActionParam::Params, paramsObj);
            }
        } else if (!rawParams.isUndefined() && !rawParams.isNull()) {
            qWarning(
                "ConfigMigration: shaderParams for AnimationAppRule[%d] (classPattern=\"%s\") is not a JSON "
                "object — payload dropped",
                i, qPrintable(classPattern));
        }
    } else {
        action.type = QString(PhosphorWindowRule::ActionType::OverrideAnimationTiming);
        const QString curve = source.value(kKeyCurve).toString();
        if (!curve.isEmpty()) {
            params.insert(ActionParam::Curve, curve);
        }
        // `durationMs <= 0` is the "inherit per-event default" sentinel —
        // omit the key entirely, mirroring AnimationAppRule::toJson. An
        // explicit `0` or negative value on disk falls into the same bucket
        // as an absent key.
        const int durationMs = source.value(kKeyDurationMs).toInt(0);
        if (durationMs > 0) {
            params.insert(ActionParam::DurationMs, durationMs);
        }
    }
    action.params = params;

    PhosphorWindowRule::WindowRule rule;
    // Deterministic id from the source identity tuple so repeated migrations
    // yield byte-identical rules — keeps the conversion idempotent under
    // crash-and-retry. The third segment uses the canonical lowercase kind
    // ("shader" / "timing"); hand-edited uppercase input on disk still
    // produces the same id since the kind-string compare above is
    // case-insensitive.
    rule.id = QUuid::createUuidV5(
        animationAppRuleNamespaceUuid(),
        PhosphorWindowRule::Detail::encodeSegment(classPattern) + PhosphorWindowRule::Detail::encodeSegment(eventPath)
            + PhosphorWindowRule::Detail::encodeSegment(isShader ? kKindShader : kKindTiming));
    rule.enabled = true;
    rule.priority = count - i;
    rule.match = PhosphorWindowRule::MatchExpression::makeLeaf(PhosphorWindowRule::Field::WindowClass,
                                                               PhosphorWindowRule::Operator::Contains, classPattern);
    rule.actions.append(action);
    return rule;
}

/// Drain the v4 animation-rule stash into @p rules. Malformed entries are
/// silently discarded — the legacy runtime loader did the same. The two-pass
/// shape (filter, then build) matches the legacy bridge byte-for-byte: the
/// priority `count - i` is computed against the POST-filter size, so dropped
/// entries don't leave gaps in the descending-by-list-order priority
/// sequence (`AnimationAppRuleList::fromJson` filtered first; `toRuleSet`
/// then used the filtered `entries.size()` as count).
void appendAnimationRulesFromStash(QList<PhosphorWindowRule::WindowRule>& rules, const QJsonArray& stash)
{
    QList<QJsonObject> valid;
    valid.reserve(stash.size());
    for (const QJsonValue& entry : stash) {
        if (!entry.isObject()) {
            continue;
        }
        const QJsonObject obj = entry.toObject();
        if (isValidAnimationAppRuleSource(obj)) {
            valid.append(obj);
        }
    }
    const int count = valid.size();
    rules.reserve(rules.size() + count);
    for (int i = 0; i < count; ++i) {
        rules.append(buildAnimationAppRule(valid.at(i), i, count));
    }
}

} // namespace

bool ConfigMigration::finalizeV4Conversion(const QString& jsonPath)
{
    const QString windowRulesPath = ConfigDefaults::windowRulesFilePath();
    const QString assignmentsPath = legacyAssignmentsFilePath();

    // ── Conversion-done vs cleanup-done — two SEPARATE concerns ─────────────
    // The conversion is multi-step: write windowrules.json (the irreversible
    // commit) → relocate QuickLayouts → strip config.json's `_v4DisableStash`
    // → delete assignments.json. These split into two questions that MUST NOT
    // be conflated:
    //
    //   "Is the v3→v4 conversion done?"  ⇒ does windowrules.json exist as a
    //       valid v4 WindowRuleSet? If so the conversion IS done — the rule
    //       store is authoritative and may have since been edited by the user
    //       (rule editor) or Settings (disable lists). It must NEVER be
    //       rebuilt-and-overwritten from the dead assignments.json again.
    //
    //   "Is post-conversion cleanup done?" ⇒ assignments.json removed AND
    //       `_v4DisableStash` stripped from config.json. These tail steps are
    //       safe and idempotent; if they failed (read-only fs, lock) they are
    //       retried on the next run — but the rebuild is NOT.
    //
    // Probe "conversion done" by actually loading windowrules.json as a
    // WindowRuleSet (named SchemaVersion check, not a bare `_version >= 4` on
    // an unrelated version namespace) — a file that parses as a v4 rule set is
    // by definition the completed conversion output.
    const bool windowRulesAlreadyConverted =
        QFile::exists(windowRulesPath) && PhosphorWindowRule::WindowRuleSet::loadFromFile(windowRulesPath).has_value();

    if (windowRulesAlreadyConverted) {
        // The conversion is complete. NEVER rebuild + overwrite windowrules.json
        // — doing so would silently destroy every user-authored rule and every
        // disable-list edit made since the first conversion. Only retry the
        // still-pending, idempotent cleanup steps.
        bool ok = true;

        // Strip the v4 scratch keys (`_v4DisableStash`, `_v4AnimationRulesStash`)
        // from config.json if any survived a partial earlier run.
        if (QFile::exists(jsonPath)) {
            QFile cf(jsonPath);
            if (cf.open(QIODevice::ReadOnly)) {
                QJsonParseError err;
                const QJsonDocument doc = QJsonDocument::fromJson(cf.readAll(), &err);
                cf.close();
                if (err.error == QJsonParseError::NoError && doc.isObject()
                    && (doc.object().contains(kV4DisableStashKey)
                        || doc.object().contains(kV4AnimationRulesStashKey))) {
                    QJsonObject configRoot = doc.object();
                    configRoot.remove(kV4DisableStashKey);
                    configRoot.remove(kV4AnimationRulesStashKey);
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

        return ok;
    }

    // From here down: windowrules.json does NOT yet exist as a valid v4 rule
    // set — a genuine first run, or a crash before windowrules.json was
    // written. Only this path rebuilds and writes the rule store.

    // Pre-flight the legacy assignments.json: a malformed sidecar must abort
    // BEFORE we write windowrules.json (otherwise we'd commit a
    // provider-default-only rule set that silently drops every assignment AND
    // the quick-layout slots, and then quarantine the corrupt original to
    // `.migrated` — masking the failure as a successful migration).
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

    const QJsonObject stash = configRoot.value(kV4DisableStashKey).toObject();
    const QJsonArray animationRulesStash = configRoot.value(kV4AnimationRulesStashKey).toArray();

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
        }
    }

    // windowrules.json is always written below (see "Write windowrules.json"),
    // regardless of how much v3 data was found. When there is nothing to
    // convert (no stash, no assignments file) the `rules` list stays empty and
    // an empty rule set is written — the daemon's store still needs a stable
    // file to exist on disk.

    QList<PhosphorWindowRule::WindowRule> rules;

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
            const bool autotile = (modeInt == 1);
            const QString snappingLayout = grp.value(ConfigKeys::Legacy::v3AssignmentLayout()).toString();
            const QString tilingAlgorithm = grp.value(ConfigKeys::Legacy::v3AssignmentAlgorithm()).toString();

            rules.append(PhosphorWindowRule::ContextRuleBridge::makeAssignmentRule(
                assignmentRuleName(screenId, desktop, activity), screenId, desktop, activity, autotile, snappingLayout,
                tilingAlgorithm));
        }
    }

    // ── Provider-default catch-all rule ────────────────────────────────────
    // The cascade falls through to a lowest-priority empty-All{} rule when no
    // pinned context rule matches. Both the snapping default AND the tiling
    // default are baked into the rule so the catch-all has a complete fallback
    // regardless of which engine the rule selects — the engine-mode action
    // chooses the active mode, but both layout/algorithm actions stand ready
    // so a manual mode toggle later (or another rule overriding the engine)
    // still resolves a sensible layout. Previously only one mode's default
    // was embedded, leaving the user's snapping default off the catch-all
    // when an autotile default was also present.
    {
        // Schema-migration freeze policy: read v3 on-disk paths through the
        // frozen `ConfigKeys::Legacy::v3*` accessors, NEVER the live
        // ConfigDefaults accessors. A future runtime rename of e.g.
        // `snappingBehaviorWindowHandlingGroup()` MUST NOT retarget the v3→v4
        // finalizer to a path that no v3 config ever had on disk. See
        // configkeys.h `Legacy` struct for the policy rationale.
        const QJsonObject windowHandling =
            groupObjectAtPath(configRoot, ConfigKeys::Legacy::v3SnappingBehaviorWindowHandlingGroup());
        const QString defaultLayoutId = windowHandling.value(ConfigKeys::Legacy::v3DefaultLayoutIdKey()).toString();

        const QJsonObject tilingAlgo = groupObjectAtPath(configRoot, ConfigKeys::Legacy::v3TilingAlgorithmGroup());
        const QString defaultAlgorithm = tilingAlgo.value(ConfigKeys::Legacy::v3DefaultKey()).toString();

        // Engine-mode preference: pick autotile only when the user has no
        // snapping default but does have a tiling default — snapping is the
        // historical default mode and stays selected whenever a snapping
        // layout is configured.
        const bool autotileDefault = defaultLayoutId.isEmpty() && !defaultAlgorithm.isEmpty();
        rules.append(PhosphorWindowRule::ContextRuleBridge::makeProviderDefaultRule(
            QStringLiteral("Default"), autotileDefault, defaultLayoutId, defaultAlgorithm));
    }

    // ── Disable-list rules ─────────────────────────────────────────────────
    // Collected into a separate list first so exact-duplicate
    // (mode, screen, desktop, activity) rules can be collapsed before being
    // merged into the final set — migrateV2ToV3 duplicates each v2 value into
    // both the snapping and autotile lists, so a stash carried forward from a
    // hand-edited or doubly-migrated config can hold the same entry twice.
    QList<PhosphorWindowRule::WindowRule> disableRules;
    auto appendMonitorRules = [&disableRules](const QString& csv, bool autotile) {
        for (const QString& entry : parseDisableList(csv)) {
            disableRules.append(disableRuleForMonitor(entry, autotile));
        }
    };
    auto appendDesktopRules = [&disableRules](const QString& csv, bool autotile) {
        for (const QString& entry : parseDisableList(csv)) {
            if (const auto rule = disableRuleForDesktop(entry, autotile)) {
                disableRules.append(*rule);
            }
        }
    };
    auto appendActivityRules = [&disableRules](const QString& csv, bool autotile) {
        for (const QString& entry : parseDisableList(csv)) {
            if (const auto rule = disableRuleForActivity(entry, autotile)) {
                disableRules.append(*rule);
            }
        }
    };
    appendMonitorRules(stash.value(kV3SnappingMonitorsStash).toString(), false);
    appendMonitorRules(stash.value(kV3AutotileMonitorsStash).toString(), true);
    appendDesktopRules(stash.value(kV3SnappingDesktopsStash).toString(), false);
    appendDesktopRules(stash.value(kV3AutotileDesktopsStash).toString(), true);
    appendActivityRules(stash.value(kV3SnappingActivitiesStash).toString(), false);
    appendActivityRules(stash.value(kV3AutotileActivitiesStash).toString(), true);

    // Collapse exact-duplicate disable rules: dedup on the semantic identity
    // (autotile-mode, screenId, desktop, activity) so the migrated store is no
    // noisier than necessary.
    {
        namespace CRB = PhosphorWindowRule::ContextRuleBridge;
        QSet<QString> seen;
        for (const PhosphorWindowRule::WindowRule& rule : std::as_const(disableRules)) {
            QString screenId;
            int desktop = 0;
            QString activity;
            CRB::contextDimsOf(rule.match, screenId, desktop, activity);
            const std::optional<bool> autotileMode = CRB::disableRuleAutotileMode(rule);
            const QString identity = (autotileMode && *autotileMode ? QLatin1String("A|") : QLatin1String("S|"))
                + screenId + QLatin1Char('|') + QString::number(desktop) + QLatin1Char('|') + activity;
            if (seen.contains(identity)) {
                continue;
            }
            seen.insert(identity);
            rules.append(rule);
        }
    }

    // ── Animation App Rules → WindowRules ──────────────────────────────────
    // Port the (now-deleted) AnimationAppRuleBridge logic against the stashed
    // legacy JSON. The animation rules target slot prefixes (`anim-shader:`,
    // `anim-timing:`) that no other rule type fills, so they coexist with the
    // assignment/disable rules above regardless of priority interleaving.
    appendAnimationRulesFromStash(rules, animationRulesStash);

    // ── Relocate QuickLayouts to the quicklayouts.json sidecar (FIRST) ─────
    // Quick-layout slots are NOT window rules — they belong in the sibling
    // sidecar LayoutRegistry reads (next to windowrules.json), not in the rule
    // store and not in config.json. The group's shape (slot number → layout
    // id) already matches the sidecar format verbatim.
    //
    // Write-order rationale (B4 data-loss fix): the sidecar MUST be durably
    // written BEFORE windowrules.json — windowrules.json is the irreversible
    // commit marker (its mere existence flips the `windowRulesAlreadyConverted`
    // probe on the next run, gating the rebuild path off forever). If the
    // sidecar write fails AFTER windowrules.json was committed, the next run
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
            if (!PhosphorConfig::JsonBackend::writeJsonAtomically(quickLayoutsPath, quickLayoutsToRelocate)) {
                qWarning(
                    "ConfigMigration: failed to write %s — aborting v4 conversion before committing windowrules.json",
                    qPrintable(quickLayoutsPath));
                return false;
            }
            qInfo("ConfigMigration: relocated %d quick-layout slots to %s",
                  static_cast<int>(quickLayoutsToRelocate.size()), qPrintable(quickLayoutsPath));
        }
    }

    // ── Write windowrules.json (atomic — the irreversible commit) ──────────
    // This is the marker that gates `windowRulesAlreadyConverted` on the next
    // run. It MUST go after the sidecar relocation (see comment above) — once
    // this file exists as a valid v4 rule set, the cleanup-only branch
    // short-circuits the rebuild forever.
    PhosphorWindowRule::WindowRuleSet ruleSet;
    ruleSet.setRules(rules);
    QDir().mkpath(QFileInfo(windowRulesPath).absolutePath());
    if (!ruleSet.saveToFile(windowRulesPath)) {
        qWarning("ConfigMigration: failed to write %s — aborting v4 conversion", qPrintable(windowRulesPath));
        return false;
    }
    qInfo("ConfigMigration: wrote %d window rules to %s", ruleSet.count(), qPrintable(windowRulesPath));

    // ── Rewrite config.json: strip the temporary stash keys ────────────────
    // The real Display.*Disabled* keys and Animations.AnimationAppRules array
    // were already removed by migrateV3ToV4; only the `_v4DisableStash` and
    // `_v4AnimationRulesStash` scratch keys remain to be cleaned up here.
    // Serialised under the cross-process QLockFile acquired in
    // ensureJsonConfig (see line 235): on a successful tryLock the rewrite
    // races no peer, so the value we read into configRoot at the top of this
    // function is still authoritative here. The lock is best-effort — a
    // tryLock failure logs the warning at line 242 and falls through, in
    // which case a concurrent peer COULD interleave; the warning is the
    // operator's signal that the serialisation guarantee was downgraded.
    // Predicate gates the rewrite so a clean config (no stash keys) isn't
    // needlessly touched.
    if (haveConfig && (configRoot.contains(kV4DisableStashKey) || configRoot.contains(kV4AnimationRulesStashKey))) {
        configRoot.remove(kV4DisableStashKey);
        configRoot.remove(kV4AnimationRulesStashKey);
        if (!PhosphorConfig::JsonBackend::writeJsonAtomically(jsonPath, configRoot)) {
            qWarning("ConfigMigration: failed to rewrite %s after v4 conversion", qPrintable(jsonPath));
            return false;
        }
    }

    // ── Retire assignments.json — the post-conversion cleanup tail ─────────
    // windowrules.json (and quicklayouts.json) now durably hold every datum
    // assignments.json carried; the runtime LayoutRegistry reads the rule
    // store exclusively. Retiring the legacy file LAST keeps the conversion
    // crash-recoverable. This step is non-fatal and idempotent: if it fails,
    // the next run sees windowrules.json already at the v4 WindowRuleSet
    // schema, takes the cleanup-only branch, and retries the retire — it does
    // NOT rebuild-and-overwrite the (possibly user-edited) rule store.
    retireLegacyAssignmentsFile(assignmentsPath);

    return true;
}

} // namespace PlasmaZones
