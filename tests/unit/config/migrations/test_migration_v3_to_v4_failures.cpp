// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_migration_v3_to_v4_failures.cpp
 * @brief v3 → v4 migration FAILURE paths: malformed input, unwritable
 *        sidecars, and the data-loss regressions each of those once caused.
 *
 * Two families, both about what happens when the inputs are not the happy
 * ones the conversion-fidelity suite feeds it:
 *
 *   - malformed VALUES inside well-formed JSON (the disable-list entries a
 *     user produces by hand-editing config.json), which must be dropped
 *     individually without taking a valid sibling with them, and
 *   - malformed or unwritable FILES (a corrupt rules.json or
 *     assignments.json, a stalled migration chain, a QuickLayouts sidecar
 *     that cannot be written), each of which must ABORT rather than commit a
 *     freshly-seeded store on top of the user's own rules.
 *
 * Split out of test_migration_v3_to_v4.cpp; the shared config/rules JSON
 * helpers live in MigrationV3V4Fixture.h.
 */

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QUuid>

#include <algorithm>

#include "config/configdefaults.h"
#include "config/configkeys.h"
#include "config/configmigration.h"
#include "helpers/IsolatedConfigGuard.h"

#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/RuleSet.h>

#include "MigrationV3V4Fixture.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
namespace CRB = PhosphorRules::ContextRuleBridge;

class TestMigrationV3ToV4Failures : public QObject, public MigrationV3V4Fixture
{
    Q_OBJECT

private Q_SLOTS:

    // ─── Malformed v3 INPUT VALUES ────────────────────────────────────────

    /// The migration's failure paths are well covered for malformed JSON (see
    /// the corrupt-file slots below), but the values INSIDE well-formed JSON
    /// are the ones a user actually produces by hand-editing config.json. Each
    /// bad entry must be dropped on its own without taking a valid sibling
    /// with it, and without emitting a rule that matches nothing.
    void testMalformedDisableListEntriesAreDroppedIndividually()
    {
        IsolatedConfigGuard guard;
        QJsonObject cfg = makeV3Config();
        QJsonObject display = cfg.value(QStringLiteral("Display")).toObject();
        // Trailing comma, an empty segment, a desktop entry with no '/', a
        // non-numeric desktop, and a zero desktop — mixed with valid ones.
        display.insert(QStringLiteral("SnappingDisabledMonitors"), QStringLiteral("DP-3,,DP-9,"));
        display.insert(QStringLiteral("SnappingDisabledDesktops"), QStringLiteral("DP-1/4,DP-2,DP-5/abc,DP-6/0"));
        cfg.insert(QStringLiteral("Display"), display);
        writeJson(ConfigDefaults::configFilePath(), cfg);
        writeJson(assignmentsPath(), makeAssignments());

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QList<QJsonObject> disabled = disableRules(rulesFromRules());

        // Both good monitor entries survive the empties around them.
        const auto hasMonitorRule = [&](const QString& screenId) {
            return std::any_of(disabled.cbegin(), disabled.cend(), [&](const QJsonObject& r) {
                return disableActionMode(r) == QLatin1String("snapping")
                    && matchLeafValue(r, QStringLiteral("screenId")) == screenId
                    && matchLeafValue(r, QStringLiteral("virtualDesktop")).isEmpty();
            });
        };
        QVERIFY2(hasMonitorRule(QStringLiteral("DP-3")), "a valid monitor entry must survive empty siblings");
        QVERIFY2(hasMonitorRule(QStringLiteral("DP-9")), "a valid monitor entry after an empty must survive");

        // The one good desktop entry survives; none of the three malformed
        // ones produces a rule.
        int desktopRules = 0;
        for (const QJsonObject& r : disabled) {
            if (!matchLeafValue(r, QStringLiteral("virtualDesktop")).isEmpty()) {
                ++desktopRules;
                QCOMPARE(matchLeafValue(r, QStringLiteral("screenId")), QStringLiteral("DP-1"));
                QCOMPARE(matchLeafValue(r, QStringLiteral("virtualDesktop")), QStringLiteral("4"));
            }
        }
        QCOMPARE(desktopRules, 1);

        // Pin the TOTAL too. Without it, a regression that let a malformed
        // desktop entry fall through to a screen-wide disable rule would be
        // invisible: such a rule has a non-empty screenId and no
        // virtualDesktop, so it is indistinguishable from the legitimate
        // monitor-list rules and both assertions above still hold.
        // Expected: snapping monitors DP-3 + DP-9, autotile monitors DP-3 +
        // HDMI-2, snapping desktop DP-1/4, autotile activity DP-1/act-uuid-7.
        QCOMPARE(disabled.size(), 6);

        // And nothing emitted a rule with an empty screen id, which would
        // match every window on every screen.
        for (const QJsonObject& r : disabled) {
            QVERIFY2(!matchLeafValue(r, QStringLiteral("screenId")).isEmpty(),
                     "a malformed entry must not become a match-everything rule");
        }
    }

    // ─── Data-loss regression: delete-failure must not clobber the store ──
    //
    // Simulates the scenario where the assignments.json retire step fails (a
    // read-only filesystem, a lock, a permissions error) so the legacy file is
    // still present on the next startup. The conversion is already complete —
    // rules.json exists as a valid v4 RuleSet, and the user has
    // since authored an extra rule via the rule editor. Re-running
    // finalizeV4Conversion MUST NOT rebuild-and-overwrite rules.json from
    // the dead assignments.json: the user's rule must survive.
    //
    // The fix gates the rebuild on `!rulesAlreadyConverted` (probed by
    // actually loading rules.json as a RuleSet), NOT on
    // assignments.json's absence — so a permanently-undeletable assignments.json
    // can no longer clobber the rule store on every launch.
    void testDeleteFailure_doesNotOverwriteUserRules()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());

        // First run: full conversion produces rules.json.
        QVERIFY(ConfigMigration::ensureJsonConfig());
        QVERIFY(QFile::exists(ConfigDefaults::rulesFilePath()));

        // The user authors a new rule via the rule editor — load the store,
        // append a rule, persist it. This rule exists ONLY in rules.json;
        // it has no counterpart in assignments.json.
        auto setWithUserRule = PhosphorRules::RuleSet::loadFromFile(ConfigDefaults::rulesFilePath());
        QVERIFY2(setWithUserRule.has_value(), "rules.json must parse as a v4 rule set");
        const PhosphorRules::Rule userRule =
            CRB::makeDisableRule(QStringLiteral("User-authored · DP-9"), QStringLiteral("DP-9"),
                                 /*virtualDesktop=*/0, QString(), QStringLiteral("snapping"),
                                 PhosphorRules::ContextRuleBridge::kContextBandBase);
        const QUuid userRuleId = userRule.id;
        QVERIFY(setWithUserRule->addRule(userRule));
        QVERIFY(setWithUserRule->saveToFile(ConfigDefaults::rulesFilePath()));
        const int countWithUserRule = setWithUserRule->count();
        QVERIFY(countWithUserRule > 0);

        // Simulate the retire-step failure: assignments.json is still present
        // on disk (as if QFile::remove / rename had failed on the first run).
        // Without the fix, the old idempotency guard — gated on
        // assignments.json's absence — would NOT short-circuit, so the rebuild
        // path would re-run and overwrite rules.json, destroying the
        // user's rule.
        writeJson(assignmentsPath(), makeAssignments());
        QVERIFY(QFile::exists(assignmentsPath()));

        // Re-run the migration against the already-converted tree.
        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY(ConfigMigration::ensureJsonConfig());

        // The user's rule MUST survive — rules.json was not rebuilt.
        auto afterRerun = PhosphorRules::RuleSet::loadFromFile(ConfigDefaults::rulesFilePath());
        QVERIFY2(afterRerun.has_value(), "rules.json must still parse as a v4 rule set after the re-run");
        QVERIFY2(afterRerun->ruleById(userRuleId).has_value(),
                 "the user-authored rule must survive a re-run with assignments.json still present");
        QCOMPARE(afterRerun->count(), countWithUserRule);

        // The cleanup-only branch still retires the leftover assignments.json
        // (quarantined to .migrated, or deleted) so it cannot loop forever.
        QVERIFY2(!QFile::exists(assignmentsPath()),
                 "the cleanup-only branch must retire the leftover assignments.json");
    }

    // ─── Data-loss regression: malformed rules.json aborts ─────────
    //
    // Sibling of testMalformedAssignmentsJsonAborts for the rebuild path's
    // rules.json prevalidate. When rules.json exists but doesn't
    // parse, the "already converted" probe (loadFromFile().has_value()) drops
    // into the rebuild path, which would otherwise overwrite the corrupt-but-
    // recoverable original with a stub seed-only rule set — destroying
    // every user-authored rule. The new prevalidateRulesFile fires
    // FIRST: quarantines to .corrupt.bak, refuses to commit, returns false.
    /// A rules.json that is perfectly well-formed JSON but does NOT LOAD as a
    /// v4 rule set must be quarantined, not rebuilt over.
    ///
    /// The pre-flight used to reject only on a JSON parse failure while its
    /// caller gated on `RuleSet::loadFromFile(...).has_value()`. Anything in
    /// the gap between those two predicates — a `_version` of the wrong type,
    /// a schema version written by a newer build the user then downgraded
    /// from, a file over the loader's size cap — sailed past the pre-flight
    /// and fell through to the rebuild, which overwrote every user-authored
    /// rule with a freshly seeded set. Reachable by hand-editing, and
    /// reachable without any hand-editing at all by running a newer build once.
    void testWellFormedButUnloadableRulesJsonAborts()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());

        const QString rulesPath = ConfigDefaults::rulesFilePath();
        QDir().mkpath(QFileInfo(rulesPath).absolutePath());
        // Valid JSON, a real object, carrying a rule the user authored — and a
        // schema version this build does not know.
        const QByteArray future = QByteArrayLiteral(
            "{ \"_version\": 9999, \"rules\": [ { \"id\": \"{2f1c8e44-2a7b-5d93-8e10-4b2c9a7f1d35}\", "
            "\"name\": \"Mine\", \"enabled\": true, \"priority\": 250, "
            "\"match\": { \"field\": \"appId\", \"op\": \"appIdMatches\", \"value\": \"firefox\" }, "
            "\"actions\": [ { \"type\": \"float\" } ] } ] }");
        {
            QFile f(rulesPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(future), static_cast<qint64>(future.size()));
        }
        // It really is well-formed JSON — otherwise this would be testing the
        // parse-failure path the slot below already covers.
        QJsonParseError err;
        QJsonDocument::fromJson(future, &err);
        QCOMPARE(err.error, QJsonParseError::NoError);

        QVERIFY2(!ConfigMigration::ensureJsonConfig(), "ensureJsonConfig must refuse a rules.json it cannot load");

        // Quarantined with the user's bytes intact, and NOT replaced by a
        // seeded stub.
        const QString corruptBak = rulesPath + QStringLiteral(".corrupt.bak");
        QVERIFY2(QFile::exists(corruptBak), "an unloadable rules.json must be quarantined");
        QFile kept(corruptBak);
        QVERIFY(kept.open(QIODevice::ReadOnly));
        QCOMPARE(kept.readAll(), future);
        QVERIFY2(!QFile::exists(rulesPath), "no stub rules.json may be written over the quarantined one");
    }

    void testMalformedRulesJsonAborts()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        // No legacy assignments file — this isolates the rules-only
        // corruption path. A fresh install with a corrupt rules.json
        // is the cleanest reproduction.
        const QString corruptPath = ConfigDefaults::rulesFilePath();
        QDir().mkpath(QFileInfo(corruptPath).absolutePath());
        const QByteArray corruptBytes = QByteArrayLiteral("{ \"_version\": 4, \"rules\": [");
        {
            QFile f(corruptPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(corruptBytes), static_cast<qint64>(corruptBytes.size()));
        }

        QVERIFY2(!ConfigMigration::ensureJsonConfig(), "ensureJsonConfig must return false on a malformed rules.json");

        // The corrupt file was quarantined to .corrupt.bak with bytes preserved.
        const QString corruptBak = corruptPath + QStringLiteral(".corrupt.bak");
        QVERIFY2(QFile::exists(corruptBak), "the malformed rules.json must be quarantined to .corrupt.bak");
        {
            QFile f(corruptBak);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(f.readAll(), corruptBytes);
        }

        // Original is gone (renamed). A new stub rules.json must NOT
        // have been written — that's the data-loss class the guard exists for.
        QVERIFY(!QFile::exists(corruptPath));

        // config.json's chain step (migrateV3ToV4) DID run before finalize —
        // it stamps `_version=4` and stashes any disable-list / animation-rule
        // data. The chain step's idempotency guard then short-circuits the
        // next attempt; the rebuild branch at finalize takes over (rules.json
        // doesn't exist after quarantine, so the "already converted" probe
        // returns false and rebuild retries from the stash). Both paths
        // surface as a follow-up run after the user repairs the quarantine.
        const QJsonObject cfg = readJson(ConfigDefaults::configFilePath());
        // The migration chain now runs v3 → v4 → v5, so config.json lands at
        // the current schema version (the v3→v4 step still stamps 4 mid-chain).
        QCOMPARE(cfg.value(QStringLiteral("_version")).toInt(), PlasmaZones::ConfigSchemaVersion);
    }

    // ─── Data-loss regression (B5): malformed assignments.json aborts ─────
    //
    // A corrupt assignments.json (truncation, power-loss, hand-edit error)
    // must NOT silently produce a rules.json holding only the disable + seed
    // rules — that would lose every pinned assignment AND
    // the quick-layout slots. The migration aborts loudly: the corrupt file
    // is quarantined to `.corrupt.bak` (NOT `.migrated`, which would imply a
    // successful migration), rules.json is NOT written, and config.json
    // keeps its v3 stamp so the next run can re-attempt after the user
    // repairs the sidecar.
    void testMalformedAssignmentsJsonAborts()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());

        // Truncated / hand-edited corruption: a non-empty payload that fails
        // to parse as JSON. Whitespace-only is intentionally NOT what we
        // simulate — that case is treated as a fresh install (no assignments
        // to migrate), not corruption.
        const QString corruptPath = assignmentsPath();
        QDir().mkpath(QFileInfo(corruptPath).absolutePath());
        const QByteArray corruptBytes = QByteArrayLiteral("{ \"Assignment:DP-2\": { \"Mode\": 1, ");
        {
            QFile f(corruptPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(corruptBytes), static_cast<qint64>(corruptBytes.size()));
        }

        // Migration MUST abort.
        QVERIFY2(!ConfigMigration::ensureJsonConfig(),
                 "ensureJsonConfig must return false on a malformed assignments.json");

        // rules.json must NOT have been created — silently writing a
        // disable-only rule set would mask the data-loss.
        QVERIFY2(!QFile::exists(ConfigDefaults::rulesFilePath()),
                 "rules.json must not be written when the legacy sidecar is corrupt");

        // The corrupt file was quarantined to .corrupt.bak with its original
        // bytes preserved — the user can inspect and repair it.
        const QString corruptBak = corruptPath + QStringLiteral(".corrupt.bak");
        QVERIFY2(QFile::exists(corruptBak), "the malformed assignments.json must be quarantined to .corrupt.bak");
        {
            QFile f(corruptBak);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(f.readAll(), corruptBytes);
        }

        // NOT `.migrated`: that suffix implies a successful migration and
        // would mask the failure on next inspection.
        QVERIFY2(!QFile::exists(corruptPath + QStringLiteral(".migrated")),
                 "the corrupt file must NOT be quarantined as .migrated");

        // The original assignments.json no longer exists at its primary path
        // (it was renamed to .corrupt.bak).
        QVERIFY(!QFile::exists(corruptPath));

        // config.json keeps its v3 stamp — the on-disk schema version was
        // NOT bumped, so the next run will re-attempt the migration. The
        // user's path forward: repair `.corrupt.bak`, rename it back to
        // assignments.json, and re-run.
        const QJsonObject cfg = readJson(ConfigDefaults::configFilePath());
        QCOMPARE(cfg.value(QStringLiteral("_version")).toInt(), 3);
    }

    // ─── Data-loss regression: stalled chain refuses to commit ──────────
    //
    // Stalled-chain gate: when config.json is stamped at a pre-v4 version
    // (chain stalled — e.g. migrateV1ToV2's side-effect writes failed and
    // MigrationRunner::runOnFile returned true for a no-op chain),
    // finalizeV4Conversion must refuse to commit a stub rules.json.
    // Otherwise the next successful run would hit `rulesAlreadyConverted
    // = true` in the cleanup branch and strip `_v4DisableStash` /
    // `_v4AnimationRulesStash` without porting them into rules — silently
    // losing the user's disable lists and animation app rules forever.
    void testFinalizeV4Conversion_refusesToCommitWhenChainStalled()
    {
        IsolatedConfigGuard guard;

        // Construct a v3-stamped config.json carrying both v4 scratch keys —
        // the shape a partially-advanced chain would produce after migrateV3ToV4
        // ran but the chain failed to bump version to 4 (synthetic, but exactly
        // the on-disk shape the gate must catch).
        QJsonObject cfg;
        cfg.insert(ConfigKeys::versionKey(), 3);
        QJsonObject disableStash;
        // Use the real v4 stash wire shape: production moveDisableKey calls
        // `display.value(configKey).toString()` and stashes that string verbatim
        // (CSV), not an array. A downstream test that asserts "stash content
        // was consumed into rules" would silently get a false positive if we
        // used an array here — toString() returns "" for a JSON array, and
        // appendMonitorRules's empty-input early-return would no-op.
        disableStash.insert(QStringLiteral("snappingMonitors"), QStringLiteral("DP-1"));
        disableStash.insert(QStringLiteral("autotileMonitors"), QStringLiteral("DP-1"));
        cfg.insert(QStringLiteral("_v4DisableStash"), disableStash);
        QJsonArray animStash;
        QJsonObject animEntry;
        animEntry.insert(QStringLiteral("classPattern"), QStringLiteral("firefox"));
        animEntry.insert(QStringLiteral("eventPath"), QStringLiteral("window.open"));
        animEntry.insert(QStringLiteral("kind"), QStringLiteral("shader"));
        animEntry.insert(QStringLiteral("effectId"), QStringLiteral("dissolve"));
        animStash.append(animEntry);
        cfg.insert(QStringLiteral("_v4AnimationRulesStash"), animStash);
        writeJson(ConfigDefaults::configFilePath(), cfg);

        // Call finalizeV4Conversion DIRECTLY so the chain doesn't get a
        // chance to advance _version to 4 before finalize runs. This is
        // the only way to exercise the chain-stalled gate at
        // configmigration.cpp's `if (configVersion < ConfigSchemaVersion)`
        // branch — ensureJsonConfig() runs the chain first, which on a
        // clean fixture lands at v4 and routes finalize through the
        // already-converted path.
        const bool ok = ConfigMigration::finalizeV4Conversion(ConfigDefaults::configFilePath());
        QVERIFY2(!ok, "finalizeV4Conversion must return false when _version < ConfigSchemaVersion");
        QVERIFY2(!QFile::exists(ConfigDefaults::rulesFilePath()),
                 "rules.json must NOT be committed when config.json is still below v4");
        const QJsonObject onDisk = readJson(ConfigDefaults::configFilePath());
        QVERIFY2(onDisk.value(ConfigKeys::versionKey()).toInt(0) == 3,
                 "the v3 stamp must survive — finalize is not allowed to advance the version");
        QVERIFY2(onDisk.contains(QStringLiteral("_v4DisableStash")),
                 "stash keys must survive on disk so the next run can retry");
        QVERIFY2(onDisk.contains(QStringLiteral("_v4AnimationRulesStash")),
                 "animation stash must survive on disk so the next run can retry");
    }

    // ─── Data-loss regression (B4): QuickLayouts write failure is recoverable ─
    //
    // The v3→v4 conversion writes two files: quicklayouts.json (sidecar) and
    // rules.json (the irreversible commit marker). Writing the sidecar
    // FIRST means a sidecar failure aborts BEFORE committing rules.json
    // — the user's slots stay recoverable from assignments.json on the next
    // attempt. Writing rules.json first would gate the rebuild path off
    // forever on the next run (the cleanup-only branch never re-attempts the
    // QuickLayouts relocation), losing the slots to the .migrated quarantine.
    //
    // We simulate the sidecar write failure by pre-creating a DIRECTORY at
    // the quicklayouts.json path: QSaveFile cannot replace a directory with a
    // file, so the write fails deterministically.
    void testQuickLayoutsWriteFailureRecoverable()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());

        // Wedge the sidecar write: a non-empty directory at the target path
        // makes the atomic-write step fail. QSaveFile's commit() renames a
        // temp file onto `quicklayouts.json`; renaming a file onto a non-empty
        // directory fails deterministically on POSIX (ENOTEMPTY/EISDIR), and
        // the directory stays failed for the duration of the first attempt.
        const QString quickLayoutsPath = ConfigDefaults::quickLayoutsFilePath();
        QDir().mkpath(QFileInfo(quickLayoutsPath).absolutePath());
        QVERIFY(QDir().mkpath(quickLayoutsPath));
        QVERIFY(QFileInfo(quickLayoutsPath).isDir());
        // Pin the wedge: a child file makes the directory non-empty so
        // rename() can't succeed on any filesystem.
        {
            QFile pin(quickLayoutsPath + QStringLiteral("/.pin"));
            QVERIFY(pin.open(QIODevice::WriteOnly));
            QCOMPARE(pin.write("x"), static_cast<qint64>(1));
        }

        // First attempt: the sidecar write fails, migration aborts BEFORE
        // committing rules.json. The legacy sidecar must still be on
        // disk (we never reach the retire step), so the data is recoverable.
        QVERIFY2(!ConfigMigration::ensureJsonConfig(),
                 "ensureJsonConfig must return false when the QuickLayouts sidecar write fails");
        QVERIFY2(!QFile::exists(ConfigDefaults::rulesFilePath()),
                 "rules.json must NOT be written when the sidecar write fails");
        QVERIFY2(QFile::exists(assignmentsPath()),
                 "assignments.json must remain on disk so the user can re-attempt the migration");

        // Recovery: remove the wedge (delete the pin file, then the
        // directory) so the next run can write the sidecar. The user's
        // environment is otherwise unchanged.
        QVERIFY(QFile::remove(quickLayoutsPath + QStringLiteral("/.pin")));
        QVERIFY(QDir().rmdir(quickLayoutsPath));
        QVERIFY(!QFileInfo::exists(quickLayoutsPath));

        // Second attempt: full migration succeeds. rules.json is
        // written, the sidecar is populated, and the legacy file is retired.
        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY2(ConfigMigration::ensureJsonConfig(),
                 "the migration must succeed once the QuickLayouts sidecar write can complete");
        QVERIFY(QFile::exists(ConfigDefaults::rulesFilePath()));
        QVERIFY2(QFile::exists(quickLayoutsPath), "the QuickLayouts sidecar must be populated on the second attempt");
        const QJsonObject slots = readJson(quickLayoutsPath);
        QCOMPARE(slots.value(QStringLiteral("snapping")).toObject().value(QStringLiteral("3")).toString(),
                 QStringLiteral("{quick-layout-id}"));
        QVERIFY2(!QFile::exists(assignmentsPath()),
                 "assignments.json must be retired once the full conversion completes");
    }
};

QTEST_MAIN(TestMigrationV3ToV4Failures)
#include "test_migration_v3_to_v4_failures.moc"
