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
#include <PhosphorWindowRules/ContextRuleBridge.h>
#include <PhosphorWindowRules/IdentityKey.h>
#include <PhosphorWindowRules/MatchExpression.h>
#include <PhosphorWindowRules/MatchTypes.h>
#include <PhosphorWindowRules/RuleAction.h>
#include <PhosphorWindowRules/WindowRule.h>
#include <PhosphorWindowRules/WindowRuleSet.h>

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

/// Mirror of prevalidateLegacyAssignmentsFile for windowrules.json.
///
/// finalizeV4Conversion's "already converted" gate probes via
/// `WindowRuleSet::loadFromFile(...).has_value()`. If the file exists but
/// is corrupt (truncation, hand-edit error, power-loss), that probe returns
/// nullopt — the gate falls through to the rebuild path, which mints a
/// provider-default-only rule set and writes it on top of the corrupt-but-
/// recoverable original. Every user-authored rule is destroyed without
/// warning and without a backup.
///
/// On detected corruption: quarantine to `windowrules.json.corrupt.bak`,
/// log at critical, and return false so the caller aborts before any
/// stub-rule write happens. Mirrors the assignments.json contract.
///
/// Returns true if the file is absent, empty, parses as a v4 rule set, or
/// parses as any JSON object the rule loader will inspect downstream. The
/// only false case is a file that exists, is non-empty, but fails to parse
/// as a JSON object — that's the data-loss trigger we exist to prevent.
bool prevalidateWindowRulesFile(const QString& windowRulesPath)
{
    if (!QFile::exists(windowRulesPath)) {
        return true;
    }
    QFile wf(windowRulesPath);
    if (!wf.open(QIODevice::ReadOnly)) {
        // Mirrors the assignments prevalidate — we can't read to decide, so
        // surface a warning and let the downstream path's open-failure
        // handling take its course.
        qWarning("ConfigMigration: could not open %s for prevalidation: %s", qPrintable(windowRulesPath),
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

    const QString corruptBak = windowRulesPath + QStringLiteral(".corrupt.bak");
    QFile::remove(corruptBak);
    if (QFile::rename(windowRulesPath, corruptBak)) {
        qCritical(
            "ConfigMigration: %s is malformed (%s) — quarantined to %s. "
            "Aborting v4 conversion to prevent destroying user-authored "
            "rules. Inspect/repair the file and rename it back to "
            "windowrules.json, then re-run.",
            qPrintable(windowRulesPath), qPrintable(err.errorString()), qPrintable(corruptBak));
    } else {
        qCritical(
            "ConfigMigration: %s is malformed (%s) — also failed to "
            "quarantine to %s. Aborting v4 conversion. Move or repair "
            "the file by hand.",
            qPrintable(windowRulesPath), qPrintable(err.errorString()), qPrintable(corruptBak));
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
                        // windowrules.json + quicklayouts.json, stripping the
                        // stash keys, retiring assignments.json) happens
                        // here, after the chain. Idempotent — safe to always run.
                        return finalizeV4Conversion(jsonPath);
                    }
                    // Already at OR above current version — finalizeV4Conversion's
                    // cleanup-only branch runs idempotently: it strips any leftover
                    // assignments.json artifacts or `_v4*Stash` keys that a prior
                    // crash may have left behind, and is a no-op once the file is
                    // already clean.
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
        root[ConfigKeys::Legacy::v2SnappingGroup()] = snapping;

    // ── Performance ─────────────────────────────────────────────────────────
    QJsonObject performance;
    moveKey(v1Zones, QLatin1String("PollIntervalMs"), performance, QLatin1String("PollIntervalMs"));
    moveKey(v1Zones, QLatin1String("MinimumZoneSizePx"), performance, QLatin1String("MinimumZoneSizePx"));
    moveKey(v1Zones, QLatin1String("MinimumZoneDisplaySizePx"), performance, QLatin1String("MinimumZoneDisplaySizePx"));
    if (!performance.isEmpty())
        root[ConfigKeys::Legacy::v2PerformanceGroup()] = performance;

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
        root[ConfigKeys::Legacy::v2TilingGroup()] = tiling;

    // ── Exclusions (key renames) ────────────────────────────────────────────
    QJsonObject exclusions;
    moveKey(v1Exclusions, QLatin1String("ExcludeTransientWindows"), exclusions, QLatin1String("TransientWindows"));
    moveKey(v1Exclusions, QLatin1String("MinimumWindowWidth"), exclusions, QLatin1String("MinimumWindowWidth"));
    moveKey(v1Exclusions, QLatin1String("MinimumWindowHeight"), exclusions, QLatin1String("MinimumWindowHeight"));
    moveKey(v1Exclusions, QLatin1String("Applications"), exclusions, QLatin1String("Applications"));
    moveKey(v1Exclusions, QLatin1String("WindowClasses"), exclusions, QLatin1String("WindowClasses"));
    if (!exclusions.isEmpty())
        root[ConfigKeys::Legacy::v2ExclusionsGroup()] = exclusions;

    // ── Rendering (key rename) ──────────────────────────────────────────────
    QJsonObject rendering;
    moveKey(v1Rendering, ConfigKeys::Legacy::v1RenderingBackendKey(), rendering, QLatin1String("Backend"));
    if (!rendering.isEmpty())
        root[ConfigKeys::Legacy::v2RenderingGroup()] = rendering;

    // ── Shaders (key renames) ───────────────────────────────────────────────
    QJsonObject shaders;
    moveKey(v1Shaders, QLatin1String("EnableShaderEffects"), shaders, QLatin1String("Enabled"));
    moveKey(v1Shaders, QLatin1String("ShaderFrameRate"), shaders, QLatin1String("FrameRate"));
    moveKey(v1Shaders, QLatin1String("EnableAudioVisualizer"), shaders, QLatin1String("AudioVisualizer"));
    moveKey(v1Shaders, QLatin1String("AudioSpectrumBarCount"), shaders, QLatin1String("AudioSpectrumBarCount"));
    if (!shaders.isEmpty())
        root[ConfigKeys::Legacy::v2ShadersGroup()] = shaders;

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
        // now stores the animation profile as a nested QVariantMap.
        // Stringifying here keeps the migrated value as a single scalar
        // leaf so the schema/migration cross-check
        // (`testSchemaCoversEveryMigrationDestinationKey`) sees one
        // declared key (`Animations/Profile` — `animationProfileKey()`
        // returns "Profile") instead of treating the nested object as a
        // sub-group of synthetic per-field keys.
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
        moveKey(v1GlobalShortcuts, ConfigKeys::Legacy::v1QuickLayoutShortcutKey(i), globalShortcuts,
                ConfigKeys::Legacy::v2QuickLayoutKey(i));
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
        const QString key = ConfigKeys::Legacy::v2SnapToZoneKey(i);
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
        root[ConfigKeys::Legacy::v2ShortcutsGroup()] = shortcuts;

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
        root[ConfigKeys::Legacy::v2EditorGroup()] = editor;

    // ── Ordering (keys unchanged) ───────────────────────────────────────────
    if (!v1Ordering.isEmpty())
        root[ConfigKeys::Legacy::v2OrderingGroup()] = v1Ordering;

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

    // Source read uses the frozen v1 group accessor per the configkeys.h
    // freeze policy — a future runtime rename of `windowTrackingGroup`
    // must not silently miss this read. Destination write into
    // session.json's root uses the live runtime group accessor — that
    // file is the live shape's home.
    const QString srcWtGroup = ConfigKeys::Legacy::v1WindowTrackingGroup();
    const QString dstWtGroup = ConfigKeys::windowTrackingGroup();
    if (root.contains(srcWtGroup)) {
        QJsonObject sessionRoot;
        sessionRoot[dstWtGroup] = root.value(srcWtGroup);
        const QString sessionPath = ConfigDefaults::sessionFilePath();
        if (PhosphorConfig::JsonBackend::writeJsonAtomically(sessionPath, sessionRoot)) {
            root.remove(srcWtGroup);
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

        // "Commit-ok": true when the assignments file was successfully
        // written, OR when there was nothing to write in the first place.
        // Drives the keysToRemove drain below — only strip from root once
        // the external destination is durable (or vacuously durable).
        bool assignmentsCommitOk = true;
        if (!assignRoot.isEmpty()) {
            const QString assignPath = legacyAssignmentsFilePath();
            if (!PhosphorConfig::JsonBackend::writeJsonAtomically(assignPath, assignRoot)) {
                qWarning(
                    "ConfigMigration: failed to write assignments to %s — "
                    "aborting migration so the next run can retry",
                    qPrintable(assignPath));
                assignmentsCommitOk = false;
                allSideEffectsSucceeded = false;
            }
        }
        // Only strip from root if the external file is committed (or there
        // was nothing to extract). ModeTracking is safe to drop unconditionally
        // — it's ephemeral and consumed in-memory only.
        if (assignmentsCommitOk) {
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
    // the FROZEN v2 accessor on '.' — this keeps the migration in lockstep
    // with the schema instead of duplicating segment names as bare literals.
    // Using the LIVE `snappingBehaviorDisplayGroup()` here would silently
    // retarget the v2→v3 step to whatever path a future rename of that
    // accessor points to, which by definition isn't where v2 configs ever
    // wrote. This mirrors the same freeze policy applied at the v3→v4 step
    // and at the v3-write site below.
    const QStringList v2GroupSegments =
        ConfigKeys::Legacy::v2SnappingBehaviorDisplayGroup().split(QLatin1Char('.'), Qt::SkipEmptyParts);
    // Frozen-literal invariant: `"Snapping.Behavior.Display"` is exactly three
    // dot-segments. Q_ASSERT documents the contract at the test bench; the
    // release-build guard below prevents an out-of-bounds segment access if a
    // future freeze-policy violation ever lands the literal at a different
    // shape. Without the runtime guard, the `v2GroupSegments[0..2]` indexing
    // would be UB in release builds — the Q_ASSERT alone is asymmetric
    // coverage.
    Q_ASSERT(v2GroupSegments.size() == 3);
    if (v2GroupSegments.size() != 3) {
        qCritical("migrateV2ToV3: frozen v2 group accessor split into %lld segments (expected 3) — aborting step",
                  static_cast<long long>(v2GroupSegments.size()));
        return;
    }
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
    // the string representation when one is available. Logs a warning
    // when a non-string value is erased so a user reviewing the log can
    // recover their hand-edit by hand.
    auto takeKey = [](QJsonObject& obj, const QString& key) -> QString {
        const auto it = obj.find(key);
        if (it == obj.end()) {
            return {};
        }
        const QJsonValue v = it.value();
        QString result;
        if (v.isString()) {
            result = v.toString();
        } else if (!v.isNull() && !v.isUndefined()) {
            qWarning(
                "ConfigMigration::migrateV2ToV3: discarding non-string value at v2 key %s — hand-edited "
                "values that don't match the v2 wire format do not survive migration",
                qPrintable(key));
        }
        obj.erase(it);
        return result;
    };

    const QString v2Monitors = takeKey(v2Display, ConfigKeys::Legacy::v2DisabledMonitorsKey());
    const QString v2Desktops = takeKey(v2Display, ConfigKeys::Legacy::v2DisabledDesktopsKey());
    const QString v2Activities = takeKey(v2Display, ConfigKeys::Legacy::v2DisabledActivitiesKey());

    // Write the duplicated lists into the new Display group. Skip empties so
    // a clean v2 config with no disabled entries doesn't grow noise keys.
    // Group name must come from the FROZEN v3 accessor — `displayGroup()`
    // is the LIVE accessor that follows future renames, so using it here
    // would silently retarget the v2→v3 step to a path no v3 config ever
    // had on disk after a future rename. The v3→v4 step downstream
    // (`migrateV3ToV4`) already uses `Legacy::v3DisplayGroup()` for the
    // same reason; this site was the outlier.
    QJsonObject v3Display = root.value(ConfigKeys::Legacy::v3DisplayGroup()).toObject();

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
        // Use the FROZEN v3 accessor — see comment above on the matching
        // `Legacy::v3DisplayGroup()` read for why the live accessor would
        // break the chain.
        root[ConfigKeys::Legacy::v3DisplayGroup()] = v3Display;
    }

    // Stamp literal 3 — see migrateV1ToV2 for why this isn't ConfigSchemaVersion.
    root[ConfigKeys::versionKey()] = 3;
}

// ── Schema migration: v3 → v4 ───────────────────────────────────────────────
// Window-rule consolidation — Phase 3.
//
// The v4 conversion produces the new windowrules.json store: every zone
// Assignment, per-mode disable entry, animation app rule, exclusion list
// entry, AND animation exclusion list entry becomes a WindowRule.
// windowrules.json SUPERSEDES assignments.json and the config.json
// Display.*Disabled* / Exclusions.* / Animations.AnimationAppRules /
// Animations.WindowFiltering.{Applications,WindowClasses} keys — the
// runtime LayoutRegistry, Settings, SnapEngine and effect now read the
// rule store exclusively, so the v3 inputs are removed once converted.
//
// Each migration step has signature `void(QJsonObject&)` — it can only touch
// config.json. This step REMOVES the six Display.*Disabled* keys, the
// Animations.AnimationAppRules array, both Exclusions.{Applications,
// WindowClasses} leaf keys, and both Animations.WindowFiltering.{Applications,
// WindowClasses} leaf keys. The values are stashed into four temporary
// root keys (`_v4DisableStash`, `_v4AnimationRulesStash`, `_v4ExclusionStash`,
// `_v4AnimationExclusionStash`) for finalizeV4Conversion to consume,
// and stamps `_version = 4`. finalizeV4Conversion (a post-chain step)
// reads that stash + assignments.json, writes windowrules.json, then deletes
// assignments.json as the irreversible commit.

namespace {
// The temporary root keys migrateV3ToV4 writes and finalizeV4Conversion
// consumes + strips. Not real schema keys — they never survive a completed
// conversion. Aliased to the frozen accessors in `ConfigKeys::Legacy` so
// settings.cpp::purgeStaleKeys (which preserves them across save() cycles
// when the chain stalls) reads from the same SSoT.
inline QString kV4DisableStashKey()
{
    return ConfigKeys::Legacy::v4DisableStashKey();
}
// Carries the v4 `Animations.AnimationAppRules` array forward to
// finalizeV4Conversion, which converts each legacy entry into a WindowRule and
// appends it to windowrules.json. The source key is removed from the
// Animations group in migrateV3ToV4 so the unified rule store becomes the sole
// home for per-window animation overrides.
inline QString kV4AnimationRulesStashKey()
{
    return ConfigKeys::Legacy::v4AnimationRulesStashKey();
}
// Sibling aliases for the v4 exclusion stashes added when the v3 Exclusions /
// Animations.WindowFiltering lists were folded into Exclude / ExcludeAnimations
// WindowRules. Same alias-policy as the two above: every stash key reads
// through a short shim in this TU so the call sites stay symmetric and the
// purgeStaleKeys preservation list still resolves through ConfigKeys::Legacy.
inline QString kV4ExclusionStashKey()
{
    return ConfigKeys::Legacy::v4ExclusionStashKey();
}
inline QString kV4AnimationExclusionStashKey()
{
    return ConfigKeys::Legacy::v4AnimationExclusionStashKey();
}

// Inner field names inside the `_v4DisableStash` object. Shared between the
// writer (migrateV3ToV4) and the reader (finalizeV4Conversion) so a typo or
// rename can't silently drop a disable list on conversion.
//
// Naming note: these are field names inside the v4 stash object, not v4 wire
// keys themselves — the "v3"-shaped data they describe (snapping vs.
// autotile, monitors vs. desktops vs. activities) is what gives the
// constants their names. The stash itself is a v4 artifact that bridges the
// chain step to the finalizer.
constexpr QLatin1StringView kStashSnappingMonitorsField{"snappingMonitors"};
constexpr QLatin1StringView kStashAutotileMonitorsField{"autotileMonitors"};
constexpr QLatin1StringView kStashSnappingDesktopsField{"snappingDesktops"};
constexpr QLatin1StringView kStashAutotileDesktopsField{"autotileDesktops"};
constexpr QLatin1StringView kStashSnappingActivitiesField{"snappingActivities"};
constexpr QLatin1StringView kStashAutotileActivitiesField{"autotileActivities"};

// Forward declaration — defined alongside the other dot-path JSON helpers in
// the anonymous namespace below `migrateV3ToV4`. migrateV3ToV4 calls it to
// rename the Snapping.Appearance.* zone-overlay groups to Snapping.Zones.*.
void moveGroupAtPath(QJsonObject& root, const QString& fromDotPath, const QString& toDotPath);
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
        //
        // Surface non-string disk values (hand-edit / disk-corruption /
        // doubly-migrated remnants) before we discard them. `toString()`
        // silently returns "" for numbers / nulls / arrays / bools so the
        // stash-then-remove path would otherwise drop the data without a
        // trace. Mirrors the qWarning in migrateV2ToV3::takeKey.
        const QJsonValue raw = display.value(configKey);
        if (display.contains(configKey) && !raw.isString() && !raw.isNull() && !raw.isUndefined()) {
            qWarning(
                "ConfigMigration::migrateV3ToV4: discarding non-string value at Display.%s "
                "(type=%d) — only string disable-lists are migrated.",
                qPrintable(configKey), static_cast<int>(raw.type()));
        }
        const QString value = raw.toString();
        if (!value.isEmpty()) {
            stash.insert(stashKey, value);
        }
        display.remove(configKey);
    };
    moveDisableKey(ConfigKeys::Legacy::v3snappingDisabledMonitorsKey(), kStashSnappingMonitorsField);
    moveDisableKey(ConfigKeys::Legacy::v3autotileDisabledMonitorsKey(), kStashAutotileMonitorsField);
    moveDisableKey(ConfigKeys::Legacy::v3snappingDisabledDesktopsKey(), kStashSnappingDesktopsField);
    moveDisableKey(ConfigKeys::Legacy::v3autotileDisabledDesktopsKey(), kStashAutotileDesktopsField);
    moveDisableKey(ConfigKeys::Legacy::v3snappingDisabledActivitiesKey(), kStashSnappingActivitiesField);
    moveDisableKey(ConfigKeys::Legacy::v3autotileDisabledActivitiesKey(), kStashAutotileActivitiesField);

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
    //
    // Re-entry note: the v3-version-gate at the top of this function
    // (`if (root[versionKey].toInt(0) >= 4) return;`) means a fully-stamped
    // v4 config never re-enters here, so the `stash` we build is for THIS
    // run only. A user who hand-edits their v3 config to re-add a
    // Display.*Disabled* key after a partial earlier run would land back
    // here, and the `root[kV4DisableStashKey()] = stash;` overwrite is
    // intentional: the prior stash represented the prior on-disk state,
    // and the user's hand-edit is the new authoritative input. Merging
    // would carry forward values the user just removed from disk. Spelt
    // out so a future maintainer doesn't "fix" this by accident.
    if (!stash.isEmpty()) {
        root[kV4DisableStashKey()] = stash;
    }

    // Animation App Rule stash: v4 folds per-window animation overrides into
    // the unified windowrules.json store as `OverrideAnimationShader` /
    // `OverrideAnimationTiming` actions on a `WindowClass Contains <pattern>`
    // matcher. finalizeV4Conversion ports the bridge logic against the
    // stashed JSON and appends the resulting WindowRules to the same rule
    // set assignments/disable lists feed.
    QJsonObject animations = root.value(ConfigKeys::Legacy::v4AnimationsGroup()).toObject();
    const QJsonValue rawAnimationRules = animations.value(ConfigKeys::Legacy::v4AnimationAppRulesKey());
    // Surface a non-array AnimationAppRules value before discarding it,
    // matching the moveDisableKey diagnostic above. Without this log, a
    // hand-edited or disk-corrupted value silently vanishes during
    // migration (toArray() returns empty for non-array QJsonValues).
    if (animations.contains(ConfigKeys::Legacy::v4AnimationAppRulesKey()) && !rawAnimationRules.isArray()
        && !rawAnimationRules.isNull() && !rawAnimationRules.isUndefined()) {
        qWarning(
            "ConfigMigration::migrateV3ToV4: discarding non-array value at Animations.%s "
            "(type=%d) — only arrays of animation app rules are migrated.",
            qPrintable(ConfigKeys::Legacy::v4AnimationAppRulesKey()), static_cast<int>(rawAnimationRules.type()));
    }
    const QJsonArray animationRules = rawAnimationRules.toArray();
    if (!animationRules.isEmpty()) {
        root[kV4AnimationRulesStashKey()] = animationRules;
    }
    animations.remove(ConfigKeys::Legacy::v4AnimationAppRulesKey());
    if (animations.isEmpty()) {
        root.remove(ConfigKeys::Legacy::v4AnimationsGroup());
    } else {
        root[ConfigKeys::Legacy::v4AnimationsGroup()] = animations;
    }

    // Exclusions stash: the legacy `Exclusions.Applications` and
    // `Exclusions.WindowClasses` keys hold comma-joined pattern lists that the
    // runtime previously folded into terminal `Exclude` rules at evaluation
    // time via the (now-deleted) legacy bridge — see git history for
    // `ExclusionListBridge` if forensics on the pre-v4 builder are needed.
    // v4 promotes those into
    // first-class WindowRules: finalizeV4Conversion appends one
    // `AppId AppIdMatches <pattern> → Exclude` rule per surviving pattern to
    // windowrules.json, so the daemon's runtime exclusion behaviour for an
    // upgrading user does not change. Read both raw values, surface non-string
    // disk values (same diagnostic shape as the moveDisableKey logger above),
    // strip the keys, and drop the group entirely if it's now empty.
    // Single helper for both the snapping-side and animation-side exclusion
    // list stash drains — same shape, same diagnostic on non-string disk
    // values, same skip-on-empty. The `sourceLabel` argument is the dotted
    // group path the warning text uses for operator forensics; the rest is
    // mechanical. Takes `groupSrc`/`stashDst` by reference so the helper
    // works as either a free read-modify-write on the source group OR (in
    // the dot-path animation case) on the nested object the caller already
    // extracted.
    const auto stashListEntry = [](QJsonObject& groupSrc, QJsonObject& stashDst, const QString& configKey,
                                   const char* sourceLabel) {
        const QJsonValue raw = groupSrc.value(configKey);
        if (groupSrc.contains(configKey) && !raw.isString() && !raw.isNull() && !raw.isUndefined()) {
            qWarning(
                "ConfigMigration::migrateV3ToV4: discarding non-string value at %s.%s "
                "(type=%d) — only comma-joined string pattern lists are migrated.",
                sourceLabel, qPrintable(configKey), static_cast<int>(raw.type()));
        }
        const QString value = raw.toString();
        if (!value.isEmpty()) {
            // Field name = source-key name (Applications / WindowClasses).
            // finalizeV4Conversion uses the field name to choose the rule's
            // match-leaf field (DesktopFile / WindowClass for the animation
            // fold; both feed AppId for the snapping fold).
            stashDst.insert(configKey, value);
        }
        groupSrc.remove(configKey);
    };

    QJsonObject exclusions = root.value(ConfigKeys::Legacy::v3ExclusionsGroup()).toObject();
    QJsonObject exclusionStash;
    stashListEntry(exclusions, exclusionStash, ConfigKeys::Legacy::v3ExcludedApplicationsKey(), "Exclusions");
    stashListEntry(exclusions, exclusionStash, ConfigKeys::Legacy::v3ExcludedWindowClassesKey(), "Exclusions");
    if (exclusions.isEmpty()) {
        root.remove(ConfigKeys::Legacy::v3ExclusionsGroup());
    } else {
        root[ConfigKeys::Legacy::v3ExclusionsGroup()] = exclusions;
    }
    if (!exclusionStash.isEmpty()) {
        root[kV4ExclusionStashKey()] = exclusionStash;
    }

    // Animation exclusion stash: the legacy
    // `Animations.WindowFiltering.{Applications,WindowClasses}` lists
    // historically fed the effect's `m_animationExclusionRuleSet` via the
    // bridge's `Contains`-leaf builder. v4 promotes those into first-class
    // `ExcludeAnimations` WindowRules so the effect can drop both the
    // QStringList settings and the per-effect rebuild. Same shape as the
    // snapping-side stash above — read raw, surface non-string disk
    // values, strip the keys, and drop the (dot-path) group if it's now
    // empty. The "Animations" parent segment routes through
    // `Legacy::v4AnimationsGroup()` and the "WindowFiltering" leaf segment
    // routes through `Legacy::v4WindowFilteringSegment()` so this block
    // stays in lockstep with the AnimationAppRules block above; a future
    // rename of either frozen accessor flows through every site.
    QJsonObject animationsForFiltering = root.value(ConfigKeys::Legacy::v4AnimationsGroup()).toObject();
    QJsonObject animationFiltering =
        animationsForFiltering.value(ConfigKeys::Legacy::v4WindowFilteringSegment()).toObject();
    QJsonObject animationExclusionStash;
    stashListEntry(animationFiltering, animationExclusionStash, ConfigKeys::Legacy::v3ExcludedApplicationsKey(),
                   "Animations.WindowFiltering");
    stashListEntry(animationFiltering, animationExclusionStash, ConfigKeys::Legacy::v3ExcludedWindowClassesKey(),
                   "Animations.WindowFiltering");
    if (animationFiltering.isEmpty()) {
        animationsForFiltering.remove(ConfigKeys::Legacy::v4WindowFilteringSegment());
    } else {
        animationsForFiltering[ConfigKeys::Legacy::v4WindowFilteringSegment()] = animationFiltering;
    }
    if (animationsForFiltering.isEmpty()) {
        root.remove(ConfigKeys::Legacy::v4AnimationsGroup());
    } else {
        root[ConfigKeys::Legacy::v4AnimationsGroup()] = animationsForFiltering;
    }
    if (!animationExclusionStash.isEmpty()) {
        root[kV4AnimationExclusionStashKey()] = animationExclusionStash;
    }

    // v3.1 renamed the "Snapping › Appearance" page to "Zones" (it configures the
    // drag-time zone overlay). Move its config groups Snapping.Appearance.* ->
    // Snapping.Zones.* so the freed Snapping.Appearance.* namespace can hold the
    // new snapped-window appearance settings (mirroring Tiling.Appearance.*).
    // Both source and destination use FROZEN Legacy accessors — never the live
    // ConfigDefaults::snappingZones*Group() accessors — so a future rename of
    // those live group names can't silently retarget this historical step to a
    // path no migrated config ever produced (same freeze policy as v2→v3).
    moveGroupAtPath(root, ConfigKeys::Legacy::v3SnappingAppearanceColorsGroup(),
                    ConfigKeys::Legacy::v4SnappingZonesColorsGroup());
    moveGroupAtPath(root, ConfigKeys::Legacy::v3SnappingAppearanceOpacityGroup(),
                    ConfigKeys::Legacy::v4SnappingZonesOpacityGroup());
    moveGroupAtPath(root, ConfigKeys::Legacy::v3SnappingAppearanceBorderGroup(),
                    ConfigKeys::Legacy::v4SnappingZonesBorderGroup());
    moveGroupAtPath(root, ConfigKeys::Legacy::v3SnappingAppearanceLabelsGroup(),
                    ConfigKeys::Legacy::v4SnappingZonesLabelsGroup());

    // Stamp literal 4 — see migrateV1ToV2 for why this isn't ConfigSchemaVersion.
    root[ConfigKeys::versionKey()] = 4;
}

// ── v4 finalizer: the multi-step cross-file conversion ─────────────────────

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

/// Set the nested object at @p segments within @p root to @p value, reading
/// out, mutating, and writing back each level so sibling sub-groups at any
/// ancestor are preserved and intermediate objects are created on demand.
void setGroupAtSegments(QJsonObject& root, const QStringList& segments, const QJsonObject& value)
{
    // Materialise the chain of ancestor objects top-down so each can be
    // rewritten bottom-up with its mutated child (QJsonObject is a value type;
    // value() returns copies, so we must reassign back up the chain).
    QList<QJsonObject> chain;
    chain.reserve(segments.size());
    QJsonObject node = root;
    for (int i = 0; i < segments.size() - 1; ++i) {
        chain.append(node);
        node = node.value(segments.at(i)).toObject();
    }
    chain.append(node);

    // Bottom-up rebuild: place the value at the leaf, then fold each level
    // back into its parent.
    chain.last()[segments.last()] = value;
    for (int i = segments.size() - 2; i >= 0; --i) {
        chain[i][segments.at(i)] = chain.at(i + 1);
    }
    root = chain.first();
}

/// Remove the nested object at @p segments within @p root, then prune any
/// ancestor object that became empty as a result — but never an ancestor that
/// still holds other sub-groups (e.g. don't drop "Snapping" while
/// Behavior/Effects remain).
void removeGroupAtSegments(QJsonObject& root, const QStringList& segments)
{
    QList<QJsonObject> chain;
    chain.reserve(segments.size());
    QJsonObject node = root;
    for (int i = 0; i < segments.size() - 1; ++i) {
        chain.append(node);
        node = node.value(segments.at(i)).toObject();
    }
    chain.append(node);

    chain.last().remove(segments.last());
    for (int i = segments.size() - 2; i >= 0; --i) {
        if (chain.at(i + 1).isEmpty()) {
            chain[i].remove(segments.at(i));
        } else {
            chain[i][segments.at(i)] = chain.at(i + 1);
        }
    }
    root = chain.first();
}

/// Move the nested object at @p fromDotPath to @p toDotPath within @p root,
/// creating destination ancestors and pruning now-empty source ancestors.
/// No-op when the source object is absent/empty. Used by migrateV3ToV4 to
/// rename Snapping.Appearance.* zone-overlay groups to Snapping.Zones.*.
/// If the destination already exists it is overwritten wholesale (source
/// wins) — this cannot arise for a genuine v3 config since the destination
/// namespace did not exist before v4.
void moveGroupAtPath(QJsonObject& root, const QString& fromDotPath, const QString& toDotPath)
{
    const QStringList fromSegments = fromDotPath.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    const QStringList toSegments = toDotPath.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (fromSegments.isEmpty() || toSegments.isEmpty()) {
        return;
    }

    // Read the source leaf via segment navigation. An absent or empty object
    // contributes nothing and (per the no-op contract) must not create a
    // husk at the destination — bail before touching anything.
    const QJsonObject leaf = groupObjectAtPath(root, fromDotPath);
    if (leaf.isEmpty()) {
        return;
    }

    setGroupAtSegments(root, toSegments, leaf);
    removeGroupAtSegments(root, fromSegments);
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
PhosphorWindowRules::WindowRule disableRuleForMonitor(const QString& screenId,
                                                      PhosphorZones::AssignmentEntry::Mode mode)
{
    const QString name = disableRulePrefixFor(mode) + screenId;
    return PhosphorWindowRules::ContextRuleBridge::makeDisableRule(name, screenId, 0, QString(),
                                                                   PhosphorZones::modeToWireString(mode));
}

/// Build a context rule from a v3 desktop disable-list entry (`screenId/N`).
/// Returns nullopt on a malformed entry.
///
/// Screen ids MUST NOT contain '/': the desktop number is the last '/'-segment
/// (split on `lastIndexOf('/')`), so a screen id with embedded slashes would be
/// truncated. This matches the `screenId/desktop` composite-key convention used
/// by Settings::writeDisableEntries.
std::optional<PhosphorWindowRules::WindowRule> disableRuleForDesktop(const QString& entry,
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
    return PhosphorWindowRules::ContextRuleBridge::makeDisableRule(name, screenId, desktop, QString(),
                                                                   PhosphorZones::modeToWireString(mode));
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
std::optional<PhosphorWindowRules::WindowRule> disableRuleForActivity(const QString& entry,
                                                                      PhosphorZones::AssignmentEntry::Mode mode)
{
    const int slash = entry.lastIndexOf(QLatin1Char('/'));
    if (slash <= 0 || slash == entry.size() - 1) {
        return std::nullopt;
    }
    const QString screenId = entry.left(slash);
    const QString activity = entry.mid(slash + 1);
    const QString name = disableRulePrefixFor(mode) + screenId + disableRuleActivitySuffix();
    return PhosphorWindowRules::ContextRuleBridge::makeDisableRule(name, screenId, 0, activity,
                                                                   PhosphorZones::modeToWireString(mode));
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

// ─── Animation App Rule → WindowRule conversion ─────────────────────────────
// Ports the (now-deleted) PhosphorWindowRules::AnimationAppRuleBridge logic
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
PhosphorWindowRules::WindowRule buildAnimationAppRule(const QJsonObject& source, int i, int count)
{
    namespace ActionParam = PhosphorWindowRules::ActionParam;

    const QString classPattern = source.value(kKeyClassPattern).toString();
    const QString eventPath = source.value(kKeyEventPath).toString();
    const QString kindStr = source.value(kKeyKind).toString();
    const bool isShader = kindStr.compare(kKindShader, Qt::CaseInsensitive) == 0;

    PhosphorWindowRules::RuleAction action;
    QJsonObject params;
    params.insert(ActionParam::Event, eventPath);
    if (isShader) {
        action.type = QString(PhosphorWindowRules::ActionType::OverrideAnimationShader);
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
        action.type = QString(PhosphorWindowRules::ActionType::OverrideAnimationTiming);
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

    PhosphorWindowRules::WindowRule rule;
    // Deterministic id from the source identity tuple so repeated migrations
    // yield byte-identical rules — keeps the conversion idempotent under
    // crash-and-retry. The third segment uses the canonical lowercase kind
    // ("shader" / "timing"); hand-edited uppercase input on disk still
    // produces the same id since the kind-string compare above is
    // case-insensitive.
    rule.id = QUuid::createUuidV5(
        animationAppRuleNamespaceUuid(),
        PhosphorWindowRules::Detail::encodeSegment(classPattern) + PhosphorWindowRules::Detail::encodeSegment(eventPath)
            + PhosphorWindowRules::Detail::encodeSegment(isShader ? kKindShader : kKindTiming));
    rule.enabled = true;
    rule.priority = count - i;
    rule.match = PhosphorWindowRules::MatchExpression::makeLeaf(PhosphorWindowRules::Field::WindowClass,
                                                                PhosphorWindowRules::Operator::Contains, classPattern);
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
void appendAnimationRulesFromStash(QList<PhosphorWindowRules::WindowRule>& rules, const QJsonArray& stash)
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

/// Fixed v5-UUID namespace for migrated exclusion-rule identities. Pinned
/// here as the canonical SSoT since the helper layer's
/// `ExclusionRules::detail::namespaceUuid` retired alongside the legacy
/// list-builder. The constant is byte-identical to the one the legacy
/// runtime bridge used (deleted alongside the v4 fold — see git history
/// for `ExclusionListBridge`) so a daemon that bridge-built a rule from
/// the same `(field, op, pattern)` tuple at runtime (pre-v4) produces
/// the same id post-migration. Two consequences of the deterministic
/// derivation:
///   - within a single rebuild, two source patterns that derive the same
///     id collapse via `WindowRuleSet::setRules`' id-dedup (so the snapping
///     fold of identical patterns across both v3 lists yields one rule);
///     cross-RUN idempotency is provided separately by the
///     `windowRulesAlreadyConverted` existence probe in the finalizer, which
///     refuses to rebuild once windowrules.json exists, so the rebuild path
///     never runs twice, and
///   - the LEGACY runtime bridge's id (pre-v4 daemons that built the
///     same rule from the same lists at runtime) matches the migration's
///     id, so a v4 store that somehow saw both producers stays consistent.
/// This is NOT a dedup against hand-authored rules: a user authoring an
/// `AppId AppIdMatches firefox → Exclude` rule through the Window Rules
/// page receives a fresh `QUuid::createUuid()` random id at allocation
/// time (windowrulecontroller.cpp / WindowRule default-constructed `id`),
/// not the deterministic UUIDv5 the migration derives. The migration's
/// rule and the user's rule will coexist as two distinct entries with
/// semantically-equivalent matches — the store dedups on id, not on
/// match-leaf identity. Acceptable: both rules resolve to Excluded for
/// the same window, so the user-visible behaviour is correct, just
/// slightly redundant; the picker shows two entries the user can
/// reconcile manually.
inline const QUuid& exclusionMigrationNamespace()
{
    static const QUuid ns(QStringLiteral("{d5f4e3c2-9b60-7182-0abe-2f3a4b5c6d7e}"));
    return ns;
}

// The migrated exclusion-rule ids below are UUIDv5-derived from the integer
// values of the Field/Operator enumerators (encoded as decimal strings). Those
// integers are therefore a wire format: renumbering any of them silently
// changes every previously-migrated exclusion-rule id and breaks the
// collision-with-self idempotency guarantee — the same failure mode the
// ExcludeAnimations wire-string static_assert below guards against. MatchTypes.h
// already documents "keeping enum values stable across versions"; pin the exact
// values the derivation depends on so a renumber breaks the build instead.
static_assert(static_cast<int>(PhosphorWindowRules::Field::AppId) == 0
                  && static_cast<int>(PhosphorWindowRules::Field::WindowClass) == 1
                  && static_cast<int>(PhosphorWindowRules::Field::DesktopFile) == 2,
              "Field enum values feed migrated exclusion-rule UUIDs — renumbering them silently "
              "changes every migrated rule id. Bump the schema version and write a v4→v5 migration "
              "if a renumber is truly needed.");
static_assert(static_cast<int>(PhosphorWindowRules::Operator::Contains) == 1
                  && static_cast<int>(PhosphorWindowRules::Operator::AppIdMatches) == 5,
              "Operator enum values feed migrated exclusion-rule UUIDs — renumbering them silently "
              "changes every migrated rule id. Bump the schema version and write a v4→v5 migration "
              "if a renumber is truly needed.");

/// Drain the v4 exclusion stash into @p rules. The stash carries two
/// comma-joined string fields (`Applications` and `WindowClasses`) — the v3
/// schema split them by intent (one matched against desktopFileName, one
/// against windowClass) but the daemon's runtime bridge always folded BOTH
/// against the resolved `appId` using the segment-aware `AppIdMatches`
/// operator. Preserve that bridge-flavoured semantics here so the migration
/// is behaviour-preserving: each surviving pattern, from either list, becomes
/// one `AppId AppIdMatches <pattern>` matcher with a terminal `Exclude`
/// action. Empty / whitespace-only patterns are dropped, mirroring the
/// runtime bridge's `pattern.trimmed().isEmpty()` skip.
void appendExclusionRulesFromStash(QList<PhosphorWindowRules::WindowRule>& rules, const QJsonObject& stash)
{
    using namespace PhosphorWindowRules;
    const auto appendOne = [&rules](const QString& rawCsv) {
        for (const QString& part : rawCsv.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QString pattern = part.trimmed();
            if (pattern.isEmpty()) {
                continue;
            }
            WindowRule rule;
            // Deterministic id keyed off `(field, op, pattern)` — byte-
            // identical namespace + segment encoding to the (now-retired)
            // legacy runtime bridge so any pre-v4 daemon that bridge-built
            // a rule from the same `(field, op, pattern)` tuple produced
            // the same id, and an upgrading user carries the same UUIDs
            // across the migration.
            rule.id = QUuid::createUuidV5(
                exclusionMigrationNamespace(),
                Detail::encodeSegment(QString::number(static_cast<int>(Field::AppId)))
                    + Detail::encodeSegment(QString::number(static_cast<int>(Operator::AppIdMatches)))
                    + Detail::encodeSegment(pattern));
            rule.enabled = true;
            // priority = 0 leaves the rule at the bottom of the
            // controller-renormalized list within its band. The user can
            // drag-reorder it in the Window Rules page if precedence matters.
            rule.priority = 0;
            rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, pattern);
            RuleAction action;
            action.type = QString(ActionType::Exclude);
            rule.actions.append(action);
            rules.append(rule);
        }
    };
    // Both lists feed AppId rules — see the comment block above.
    appendOne(stash.value(ConfigKeys::Legacy::v3ExcludedApplicationsKey()).toString());
    appendOne(stash.value(ConfigKeys::Legacy::v3ExcludedWindowClassesKey()).toString());
}

/// Seed the premade "Steam" Window Rule into a freshly-built v4 rule set.
///
/// Steam is a CEF/XWayland client that spawns most of its UI — the Friends
/// List, the self-drawn `notificationtoasts_<N>_desktop` popups, Settings, and
/// chat windows — as separate top-level windows. They all share the `steam`
/// window class but report a title other than the main library window's
/// `Steam`. The transient/popup/menu members are already filtered structurally
/// by the effect's `shouldHandleWindow()` (see the `transientFor()` /
/// `isStructurallyUnmanageableWindowType()` net referenced in discussion #461),
/// but the Normal-type top-levels (Friends List, the notification toasts) slip
/// that filter and get auto-tiled — the long-standing "Steam breaks tiling"
/// bug other compositors ship rules for.
///
/// The rule excludes every `steam`-class window whose title is NOT exactly
/// `Steam`, leaving the main library window tileable (the Hyprland
/// `title:^(?!Steam$).*` idiom). `Exclude` is enforced at the effect's
/// `shouldHandleWindow()` gate, which evaluates the FULL WindowQuery
/// (windowClass + title) — so the composite match resolves there even though
/// the daemon-side appId-only fast paths (`isAppIdExcluded`, pending-restore
/// prune) ignore non-AppId leaves; those gate keyboard navigation / state
/// cleanup, not whether the window is tiled.
///
/// `WindowClass Contains "steam"` matches KWin's raw `"resourceName
/// resourceClass"` string (e.g. `"steam Steam"`, `"steamwebhelper Steam"`)
/// case-insensitively; the `Title Equals "Steam"` guard is likewise
/// case-insensitive (see MatchTypes operator semantics). The id is a fixed
/// deterministic UUIDv5 so a re-run never produces a duplicate.
void appendSteamDefaultRule(QList<PhosphorWindowRules::WindowRule>& rules)
{
    using namespace PhosphorWindowRules;
    WindowRule rule;
    rule.id = QUuid::createUuidV5(exclusionMigrationNamespace(),
                                  Detail::encodeSegment(QStringLiteral("steam-default-exclude")));
    rule.name = QStringLiteral("Steam");
    rule.enabled = true;
    // priority 0 mirrors the migrated exclusion rules: an Exclude rule's
    // precedence is irrelevant to the boolean exclusion slice the effect
    // evaluates, and the controller renormalizes display order on load.
    rule.priority = 0;
    rule.match = MatchExpression::makeAll(
        {MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("steam")),
         MatchExpression::makeNone(
             {MatchExpression::makeLeaf(Field::Title, Operator::Equals, QStringLiteral("Steam"))})});
    RuleAction action;
    action.type = QString(ActionType::Exclude);
    rule.actions.append(action);
    rules.append(rule);
}

/// Drain the v4 ANIMATION exclusion stash into @p rules. Mirrors
/// `appendExclusionRulesFromStash` but produces `ExcludeAnimations`-action
/// rules with `DesktopFile Contains <pattern>` / `WindowClass Contains
/// <pattern>` leaves — the same match semantics the legacy effect-side
/// helper produced for the animation pipeline, so the effect's
/// shouldAnimateWindow gate stays behaviour-preserving for an upgrading
/// user. The Application/WindowClass split mirrors the legacy lists'
/// match-field distinction (unlike the snapping-side migration above,
/// which folded both into AppId rules because the daemon's runtime
/// bridge already collapsed the distinction).
void appendAnimationExclusionRulesFromStash(QList<PhosphorWindowRules::WindowRule>& rules, const QJsonObject& stash)
{
    using namespace PhosphorWindowRules;
    // Pin the wire-string for ExcludeAnimations against a future rename.
    // The animation-exclusion rule id is derived as
    // `UUIDv5(namespace, "<field>" + "<op>" + "<pattern>" + "<actionType>")`
    // — so renaming the wire-string from "excludeAnimations" to anything
    // else would silently change every existing migrated rule's id and
    // break the migration's collision-with-self idempotency guarantee.
    // The same rule's `RuleAction::type` field below also stores the wire-
    // string verbatim, so any rename has to update both producers AND the
    // testAnimationExclusions_idempotentRuleIds golden hash in lockstep.
    // QLatin1StringView's operator== is not constexpr in this Qt minimum;
    // the std::string_view bridge IS constexpr-comparable and gives a
    // build-break at compile time on rename.
    static_assert(std::string_view(ActionType::ExcludeAnimations.data(),
                                   static_cast<std::size_t>(ActionType::ExcludeAnimations.size()))
                      == std::string_view("excludeAnimations"),
                  "Renaming the ExcludeAnimations wire-string is a migration-breaking "
                  "change — every previously-migrated animation-exclude rule id is derived "
                  "from this exact byte sequence. Bump the schema version and write a "
                  "v4→v5 migration if you really need to rename.");
    const auto appendOne = [&rules](const QString& rawCsv, Field field) {
        for (const QString& part : rawCsv.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QString pattern = part.trimmed();
            if (pattern.isEmpty()) {
                continue;
            }
            WindowRule rule;
            // Deterministic id keyed off `(field, op, pattern, action)` — same
            // namespace + segment encoding as the snapping-side exclusion
            // migration so identical inputs collapse to identical ids on
            // re-runs. The snapping side always uses AppId/AppIdMatches while
            // this side uses DesktopFile|WindowClass/Contains, so the two folds
            // can never collide on the field/op/pattern triple anyway; the
            // appended action segment is defensive and additionally keeps this
            // fold's own entries distinct should the encodings ever converge.
            rule.id = QUuid::createUuidV5(
                exclusionMigrationNamespace(),
                Detail::encodeSegment(QString::number(static_cast<int>(field)))
                    + Detail::encodeSegment(QString::number(static_cast<int>(Operator::Contains)))
                    + Detail::encodeSegment(pattern) + Detail::encodeSegment(QString(ActionType::ExcludeAnimations)));
            rule.enabled = true;
            rule.priority = 0;
            rule.match = MatchExpression::makeLeaf(field, Operator::Contains, pattern);
            RuleAction action;
            action.type = QString(ActionType::ExcludeAnimations);
            rule.actions.append(action);
            rules.append(rule);
        }
    };
    appendOne(stash.value(ConfigKeys::Legacy::v3ExcludedApplicationsKey()).toString(), Field::DesktopFile);
    appendOne(stash.value(ConfigKeys::Legacy::v3ExcludedWindowClassesKey()).toString(), Field::WindowClass);
}

/// Fixed v5-UUID namespace for migrated SnapToZone-rule identities — distinct
/// from the exclusion namespace so the two folds can never collide on id.
inline const QUuid& snapToZoneMigrationNamespace()
{
    static const QUuid ns(QStringLiteral("{6f1c8e44-2a7b-5d93-8e10-4b2c9a7f1d35}"));
    return ns;
}

// Frozen on-disk keys for the legacy per-layout `appRules` array — the dead v3
// zone app-assignment format this fold is the last reader of. Local literals
// (NOT the live `ZoneJsonKeys::` accessors) so a future rename of those live
// keys can never retarget this migration away from what v3 wrote to disk.
constexpr QLatin1String kLayoutAppRulesKey{"appRules"};
constexpr QLatin1String kLayoutAppRulePattern{"pattern"};
constexpr QLatin1String kLayoutAppRuleZoneNumber{"zoneNumber"};

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
constexpr std::array<QLatin1String, 13> kLayoutSettingKeys{{
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
    QLatin1String{"useFullScreenGeometry"},
    QLatin1String{"shaderId"},
    QLatin1String{"shaderParams"},
}};

/// Convert each layout file's legacy per-layout `appRules` into first-class
/// SnapToZone WindowRules. v3 stored app→zone assignments on the Layout
/// (`Layout::appRules`: a `{pattern, zoneNumber, targetScreen}` triple, single
/// zone); v4 unifies them into the window-rule store. Each becomes
/// `AppId AppIdMatches <pattern> → SnapToZone [zoneNumber]`. AppId / AppIdMatches
/// mirrors the retired `Layout::matchAppRule` (which matched the pattern against
/// the window's appId via segment-aware `appIdMatches`) and the daemon placement
/// path, which resolves the query on appId — WindowClass is not tracked daemon-
/// side, so a WindowClass leaf would never match.
///
/// The legacy `targetScreen` (a connector name like "DP-1") is intentionally NOT
/// carried over as a `ScreenId` constraint. v4 resolves a SnapToZone rule on the
/// window's CURRENT screen, and a `ScreenId Equals` leaf would have to match the
/// canonical screen-id form the daemon reports at runtime (EDID-form
/// "Manuf:Model:Serial", what the settings screen-picker also stores), not a
/// connector name. Translating connector→canonical here would couple this pure
/// JSON transform to live screen state and make the deterministic rule id depend
/// on which monitors are connected. So a migrated app snaps to its zone on
/// whatever screen it opens on (per-monitor pinning is dropped; v3's cross-screen
/// routing was already retired by this PR).
///
/// Patterns are deduped across layouts — a SnapToZone ordinal rule fires
/// regardless of which layout is active, so one pattern can map to only one
/// placement; on a same-pattern / different-zone conflict the first wins and the
/// rest are logged. Layout files are visited in name order for deterministic
/// "first wins".
void appendLayoutAppRulesAsSnapToZone(QList<PhosphorWindowRules::WindowRule>& rules, const QString& layoutsDir)
{
    using namespace PhosphorWindowRules;
    QDir dir(layoutsDir);
    if (!dir.exists()) {
        return;
    }
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    QSet<QString> seenPatterns;
    for (const QString& fileName : files) {
        QFile f(dir.filePath(fileName));
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        f.close();
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }
        const QJsonArray appRules = doc.object().value(kLayoutAppRulesKey).toArray();
        for (const QJsonValue& entry : appRules) {
            if (!entry.isObject()) {
                continue;
            }
            const QJsonObject ar = entry.toObject();
            const QString pattern = ar.value(kLayoutAppRulePattern).toString().trimmed();
            const int zoneNumber = ar.value(kLayoutAppRuleZoneNumber).toInt(0);
            if (pattern.isEmpty() || zoneNumber < 1) {
                continue;
            }
            // The SnapToZone action validator caps ordinals at MaxZoneOrdinal, so a
            // legacy zoneNumber beyond it would be silently dropped by the loader's
            // validator. Skip it here with a visible warning instead of emitting a
            // rule that vanishes on the next load.
            if (zoneNumber > MaxZoneOrdinal) {
                qWarning(
                    "ConfigMigration: app->zone pattern '%s' targets zone %d beyond the max ordinal (%d) — "
                    "dropping the assignment.",
                    qPrintable(pattern), zoneNumber, MaxZoneOrdinal);
                continue;
            }
            const QString patternKey = pattern.toLower();
            if (seenPatterns.contains(patternKey)) {
                qWarning(
                    "ConfigMigration: duplicate app->zone pattern '%s' across layouts — keeping the first, "
                    "dropping zone %d (a SnapToZone ordinal rule fires regardless of the active layout, so a "
                    "pattern can map to only one placement).",
                    qPrintable(pattern), zoneNumber);
                continue;
            }
            seenPatterns.insert(patternKey);

            WindowRule rule;
            // Deterministic id from (pattern, zone) so a crash-and-retry
            // conversion yields byte-identical rules.
            rule.id = QUuid::createUuidV5(snapToZoneMigrationNamespace(),
                                          Detail::encodeSegment(pattern)
                                              + Detail::encodeSegment(QString::number(zoneNumber)));
            rule.enabled = true;
            // priority 0 mirrors the other migrated rules; the controller
            // renormalizes display order on load and the user can reorder.
            rule.priority = 0;
            rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, pattern);
            RuleAction action;
            action.type = QString(ActionType::SnapToZone);
            QJsonObject params;
            params.insert(QString(ActionParam::Zones), QJsonArray{zoneNumber});
            action.params = params;
            rule.actions.append(action);
            rules.append(rule);
        }
    }
}

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
    QJsonArray zones = structural.value(kLayoutZonesKey).toArray();
    for (int i = 0; i < zones.size(); ++i) {
        QJsonObject zone = zones.at(i).toObject();
        zone.remove(kLayoutAppearanceKey);
        zones.replace(i, zone);
    }
    structural.insert(QString(kLayoutZonesKey), zones);
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

    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    bool sidecarDirty = false;
    bool allOk = true;
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

        // Rewrite the layout file stripped of its settings.
        if (!PhosphorConfig::JsonBackend::writeJsonAtomically(path, stripLayoutSettings(full))) {
            qWarning("ConfigMigration: layout-settings relocation failed to rewrite %s", qPrintable(path));
            allOk = false;
        }
    }

    if (sidecarDirty) {
        sidecar.insert(QString(kSettingsVersionKey), kLayoutSettingsSchemaVersion);
        QDir().mkpath(QFileInfo(sidecarPath).absolutePath());
        if (!PhosphorConfig::JsonBackend::writeJsonAtomically(sidecarPath, sidecar)) {
            qWarning("ConfigMigration: layout-settings relocation failed to write %s", qPrintable(sidecarPath));
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
    const QString windowRulesPath = ConfigDefaults::windowRulesFilePath();
    const QString assignmentsPath = legacyAssignmentsFilePath();

    // ── Relocate per-layout settings out of the layout files (v4) ──────────
    // Independent of the windowrules/assignments machinery below: split each
    // layout file's embedded settings into the layout-settings.json sidecar and
    // slim the file. Idempotent (already-slim files are skipped) and best-effort
    // — a relocation failure leaves the settings embedded (still read by the
    // runtime store) and must not abort the v4 conversion, so its result does
    // not gate the return value.
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
    //       all four `_v4*Stash` scratch keys (`_v4DisableStash`,
    //       `_v4AnimationRulesStash`, `_v4ExclusionStash`,
    //       `_v4AnimationExclusionStash`) stripped from config.json. These
    //       tail steps are safe and idempotent; if they failed (read-only
    //       fs, lock) they are retried on the next run — but the rebuild
    //       is NOT.
    //
    // Probe "conversion done" by actually loading windowrules.json as a
    // WindowRuleSet (named SchemaVersion check, not a bare `_version >= 4` on
    // an unrelated version namespace) — a file that parses as a v4 rule set is
    // by definition the completed conversion output.
    const bool windowRulesAlreadyConverted =
        QFile::exists(windowRulesPath) && PhosphorWindowRules::WindowRuleSet::loadFromFile(windowRulesPath).has_value();

    if (windowRulesAlreadyConverted) {
        // The conversion is complete. NEVER rebuild + overwrite windowrules.json
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

        return ok;
    }

    // From here down: windowrules.json does NOT yet exist as a valid v4 rule
    // set — a genuine first run, or a crash before windowrules.json was
    // written. Only this path rebuilds and writes the rule store.

    // Pre-flight windowrules.json itself: the "already converted" probe
    // above gates on a `WindowRuleSet::loadFromFile(...).has_value()` parse
    // check. If the file EXISTS but the loader returned nullopt (malformed
    // JSON, truncated write, hand-edit error), we'd otherwise fall through
    // to a rebuild that overwrites the corrupt-but-recoverable original
    // with a stub provider-default rule set — destroying every user-authored
    // rule. Quarantine to `.corrupt.bak` and abort instead, mirroring the
    // assignments-prevalidate contract below.
    if (!prevalidateWindowRulesFile(windowRulesPath)) {
        return false;
    }

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

    // Refuse to commit a v4 marker (the eventual windowrules.json write
    // below) when config.json has not actually reached v4. If a prior
    // run's migration chain stalled (e.g. migrateV1ToV2's side-effect
    // writes failed, leaving the disk-side _version at 1),
    // `MigrationRunner::runOnFile` returns `true` for a no-op chain —
    // ensureJsonConfigImpl then proceeds here with a still-v1
    // configRoot. Writing an empty/stub windowrules.json now would set
    // `windowRulesAlreadyConverted=true` on the next run; the cleanup-
    // only branch above would then strip all four stash keys
    // (populated by a later successful chain run) without porting them
    // into rules — permanently losing the user's disable lists,
    // animation app rules, snapping exclusion lists, AND animation
    // exclusion lists. Bail out and let the next run retry the chain.
    if (haveConfig) {
        const int configVersion = configRoot.value(ConfigKeys::versionKey()).toInt(0);
        if (configVersion < ConfigSchemaVersion) {
            qWarning(
                "ConfigMigration::finalizeV4Conversion: refusing to commit windowrules.json — "
                "config.json is still at v%d (target v%d). The migration chain did not advance; "
                "a stub windowrules.json now would mask the stalled chain on the next run and "
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

    // windowrules.json is always written below (see "Write windowrules.json"),
    // regardless of how much v3 data was found. When there is nothing to
    // convert (no stash, no assignments file) the `rules` list stays empty and
    // an empty rule set is written — the daemon's store still needs a stable
    // file to exist on disk.

    QList<PhosphorWindowRules::WindowRule> rules;

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

            rules.append(PhosphorWindowRules::ContextRuleBridge::makeAssignmentRule(
                assignmentRuleName(screenId, desktop, activity), screenId, desktop, activity,
                PhosphorZones::modeToWireString(mode), snappingLayout, tilingAlgorithm));
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
        const auto defaultMode = (defaultLayoutId.isEmpty() && !defaultAlgorithm.isEmpty())
            ? PhosphorZones::AssignmentEntry::Autotile
            : PhosphorZones::AssignmentEntry::Snapping;
        rules.append(PhosphorWindowRules::ContextRuleBridge::makeProviderDefaultRule(
            QStringLiteral("Default"), PhosphorZones::modeToWireString(defaultMode), defaultLayoutId,
            defaultAlgorithm));
    }

    // ── Disable-list rules ─────────────────────────────────────────────────
    // Collected into a separate list first so exact-duplicate
    // (mode, screen, desktop, activity) rules can be collapsed before being
    // merged into the final set — migrateV2ToV3 duplicates each v2 value into
    // both the snapping and autotile lists, so a stash carried forward from a
    // hand-edited or doubly-migrated config can hold the same entry twice.
    QList<PhosphorWindowRules::WindowRule> disableRules;
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
        namespace CRB = PhosphorWindowRules::ContextRuleBridge;
        QSet<QString> seen;
        for (const PhosphorWindowRules::WindowRule& rule : std::as_const(disableRules)) {
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

    // ── Animation App Rules → WindowRules ──────────────────────────────────
    // Port the (now-deleted) AnimationAppRuleBridge logic against the stashed
    // legacy JSON. The animation rules target slot prefixes (`anim-shader:`,
    // `anim-timing:`) that no other rule type fills, so they coexist with the
    // assignment/disable rules above regardless of priority interleaving.
    appendAnimationRulesFromStash(rules, animationRulesStash);

    // ── Exclusions → WindowRules ───────────────────────────────────────────
    // Promote the legacy `Exclusions.{Applications,WindowClasses}` lists into
    // first-class WindowRules so the runtime no longer needs the bridge that
    // re-built them on every settings change. Each surviving pattern becomes
    // an Application-subject `AppId AppIdMatches <pattern>` matcher with a
    // terminal `Exclude` action — the same shape the legacy runtime bridge
    // produced for the daemon's navigation gates (see
    // `appendExclusionRulesFromStash` for the builder), so behaviour is
    // preserved for an upgrading user.
    appendExclusionRulesFromStash(rules, exclusionStash);

    // ── Animation exclusions → WindowRules ────────────────────────────────
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
    // `windowRulesAlreadyConverted` gate at the top of this function keeps the
    // rebuild path from re-seeding it (or resurrecting it after a user deletes
    // it) on any later run. See `appendSteamDefaultRule` for the match/enforcement
    // rationale.
    appendSteamDefaultRule(rules);

    // ── Per-layout app rules → SnapToZone WindowRules ──────────────────────
    // v3 stored app→zone assignments on each Layout (`Layout::appRules`); v4
    // unifies them into the window-rule store. Read every layout file's legacy
    // `appRules` array and emit one SnapToZone rule per assignment, so an
    // upgrading user's pinned apps keep snapping to their zone(s) through the
    // new single system. Layouts live in the user-writable data dir (separate
    // from config.json / windowrules.json), so resolve that path directly. This
    // runs only on the first conversion (the `windowRulesAlreadyConverted` gate
    // above), which every real v3→v4 upgrade hits exactly once.
    {
        const QString layoutsDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QLatin1Char('/') + ConfigDefaults::layoutsSubdir();
        appendLayoutAppRulesAsSnapToZone(rules, layoutsDir);
    }

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
    PhosphorWindowRules::WindowRuleSet ruleSet;
    const int inputRuleCount = rules.size();
    ruleSet.setRules(rules);
    const int storedRuleCount = ruleSet.count();
    QDir().mkpath(QFileInfo(windowRulesPath).absolutePath());
    if (!ruleSet.saveToFile(windowRulesPath)) {
        qWarning("ConfigMigration: failed to write %s — aborting v4 conversion", qPrintable(windowRulesPath));
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
    //   2. Validator rejection. WindowRuleSet::setRules silently drops
    //      rules whose `WindowRule::isValid()` returns false (null id,
    //      invalid match, zero actions, or action-validator failure).
    //      The migration's builders should never produce such rules
    //      today; a non-zero delta with no UUIDv5 collision in the
    //      preceding setRules warnings points at a builder regression.
    // Surfacing the delta makes either case forensically visible.
    if (storedRuleCount != inputRuleCount) {
        qInfo(
            "ConfigMigration: wrote %d window rules to %s (dropped %d of %d candidates — UUIDv5 collision OR "
            "validator rejection; see preceding setRules warnings to discriminate)",
            storedRuleCount, qPrintable(windowRulesPath), inputRuleCount - storedRuleCount, inputRuleCount);
    } else {
        qInfo("ConfigMigration: wrote %d window rules to %s", storedRuleCount, qPrintable(windowRulesPath));
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
