// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configmigration.h"

#include "configbackends.h"
#include "configdefaults.h"
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
        {4, &ConfigMigration::migrateV4ToV5},
    };
    return s;
}
} // namespace

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
} // namespace

/// The legacy assignments.json path. rules.json supersedes it in v4 —
/// migrateV1ToV2 still writes it (a v2 artifact) and finalizeV4Conversion
/// reads then deletes it; no live runtime code touches it, so the path lives
/// here rather than on the public ConfigDefaults surface. It sits beside
/// rules.json (the same plasmazones config directory).
QString legacyAssignmentsFilePath()
{
    return QFileInfo(ConfigDefaults::rulesFilePath()).absolutePath() + QStringLiteral("/assignments.json");
}

/// Pre-flight check for the legacy assignments.json sidecar: if the file
/// exists but fails to parse (truncation, power-loss, hand-edit error), abort
/// the v3→v4 conversion BEFORE anything irreversible runs.
///
/// Rationale (B5 data-loss fix): silently treating a corrupt assignments.json
/// as "no assignments" would let the conversion write a rules.json
/// holding only the seeded built-in rules, then quarantine the
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

/// Mirror of prevalidateLegacyAssignmentsFile for rules.json.
///
/// finalizeV4Conversion's "already converted" gate probes via
/// `RuleSet::loadFromFile(...).has_value()`. If the file exists but
/// is corrupt (truncation, hand-edit error, power-loss), that probe returns
/// nullopt — the gate falls through to the rebuild path, which mints a
/// freshly-seeded rule set and writes it on top of the corrupt-but-
/// recoverable original. Every user-authored rule is destroyed without
/// warning and without a backup.
///
/// On detected corruption: quarantine to `rules.json.corrupt.bak`,
/// log at critical, and return false so the caller aborts before any
/// stub-rule write happens. Mirrors the assignments.json contract.
///
/// Returns true if the file is absent, empty, parses as a v4 rule set, or
/// parses as any JSON object the rule loader will inspect downstream. The
/// only false case is a file that exists, is non-empty, but fails to parse
/// as a JSON object — that's the data-loss trigger we exist to prevent.
bool prevalidateRulesFile(const QString& rulesPath)
{
    if (!QFile::exists(rulesPath)) {
        return true;
    }
    QFile wf(rulesPath);
    if (!wf.open(QIODevice::ReadOnly)) {
        // Mirrors the assignments prevalidate — we can't read to decide, so
        // surface a warning and let the downstream path's open-failure
        // handling take its course.
        qWarning("ConfigMigration: could not open %s for prevalidation: %s", qPrintable(rulesPath),
                 qPrintable(wf.errorString()));
        return true;
    }
    const QByteArray bytes = wf.readAll();
    wf.close();
    if (bytes.trimmed().isEmpty()) {
        return true;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error == QJsonParseError::NoError && doc.isObject()) {
        return true;
    }

    const QString corruptBak = rulesPath + QStringLiteral(".corrupt.bak");
    QFile::remove(corruptBak);
    if (QFile::rename(rulesPath, corruptBak)) {
        qCritical(
            "ConfigMigration: %s is malformed (%s) — quarantined to %s. "
            "Aborting v4 conversion to prevent destroying user-authored "
            "rules. Inspect/repair the file and rename it back to "
            "rules.json, then re-run.",
            qPrintable(rulesPath), qPrintable(err.errorString()), qPrintable(corruptBak));
    } else {
        qCritical(
            "ConfigMigration: %s is malformed (%s) — also failed to "
            "quarantine to %s. Aborting v4 conversion. Move or repair "
            "the file by hand.",
            qPrintable(rulesPath), qPrintable(err.errorString()), qPrintable(corruptBak));
    }
    return false;
}

/// Retire the superseded assignments.json once rules.json holds every
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
        // let ensureJsonConfigImpl re-check the on-disk state. If the peer
        // is still running its migration when we proceed, both processes
        // race the read-migrate-write sequence; the later atomic-rename
        // wins, and session.json / assignments.json are written by both —
        // either survives, but a session-state inconsistency between them
        // is possible until the next save() reconciles.
        qWarning(
            "ConfigMigration: could not acquire migration lock within 5s — proceeding without lock; "
            "if the lock-holding peer is still migrating, the later atomic-rename wins");
    }

    // Re-read the on-disk file under the lock. `ensureJsonConfigImpl`
    // short-circuits when the file is already at the current schema
    // version (its "Already at OR above current version" branch),
    // sparing the parse + migration work we'd otherwise duplicate
    // against a peer who completed its own migration while we were
    // waiting on the lock. The s_migrated flag at the top of this
    // function only catches the same-process case; cross-process
    // peers reach the version-check inside Impl.
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
                        // disable-list, animation-rules, snap-exclusion, and
                        // animation-exclusion data under the four `_v4*Stash`
                        // root keys. The multi-step finalizer (reading
                        // assignments.json + the four stashes, writing
                        // rules.json + quicklayouts.json, stripping the
                        // stash keys, retiring assignments.json) happens
                        // here, after the chain. Idempotent — safe to always run.
                        return finalizeV4Conversion(jsonPath);
                    }
                    // Already at OR above current version — finalizeV4Conversion's
                    // cleanup-only branch runs idempotently: it strips any leftover
                    // assignments.json artifacts or `_v4*Stash` keys that a prior
                    // crash may have left behind (and prunes the retired
                    // provider-default rule from rules.json), a no-op once clean.
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
        // into rules.json rather than left orphaned.
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
    // The in-memory chain above ran through migrateV4ToV5, a pure config→config
    // transform (it folds the per-mode appearance/gap values into the unified
    // "Windows" / "Gaps" groups in-place and creates no rules), so only the v4
    // finalizer runs here. finalizeV4Conversion also adopts a legacy
    // windowrules.json as rules.json (a first-step, all-paths action) and prunes
    // the retired provider-default rule.
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

    // Verify the chain ran to completion before persisting. The v1→v2
    // step has side-effect writes (session.json, assignments.json) and
    // SKIPS its version bump when those fail — see migrateV1ToV2's
    // `allSideEffectsSucceeded` guard. Without this check, a failed
    // side-effect write would write a v1-stamped, partially-mutated
    // JSON to disk; `ensureJsonConfigImpl` would then rename the
    // original INI to `.bak` and finalizeV4Conversion would proceed
    // against the half-migrated root — silently losing both the
    // original INI and most of the v1 groups. Stricter than
    // `MigrationRunner::runOnFile` above: `runOnFile` persists any
    // advance (it only short-circuits when `newVersion == oldVersion`),
    // but the INI→JSON path refuses any partial advance below
    // `ConfigSchemaVersion` because the INI source is about to be
    // renamed to `.bak` and a half-migrated root would be unrecoverable.
    const int finalVersion = root.value(ConfigKeys::versionKey()).toInt(0);
    if (finalVersion < ConfigSchemaVersion) {
        qWarning(
            "ConfigMigration::migrateIniToJson: chain did not advance to v%d (stopped at v%d) — "
            "refusing disk write to avoid persisting a partially-migrated root",
            ConfigSchemaVersion, finalVersion);
        return false;
    }

    return PhosphorConfig::JsonBackend::writeJsonAtomically(jsonPath, root);
}

QJsonObject ConfigMigration::iniMapToJson(const QMap<QString, QVariant>& flatMap)
{
    QJsonObject root;

    const QString renderingGroup = ConfigDefaults::renderingGroup();
    const QString generalGroup = ConfigDefaults::generalGroup();
    const QString renderingKey = ConfigKeys::Legacy::v1RenderingBackendKey();
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

} // namespace PlasmaZones
