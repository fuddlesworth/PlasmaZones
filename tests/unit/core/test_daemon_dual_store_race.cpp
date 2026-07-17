// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_daemon_dual_store_race.cpp
 * @brief Regression guard for the in-process dual-RuleStore data-loss race.
 *
 * Pre-fix, the daemon constructed a @c RuleStore for @c LayoutRegistry /
 * @c RuleAdaptor and a SECOND store inside @c Settings, both pointed at
 * @c rules.json. Both loaded the same snapshot on boot; neither watched
 * the file. Once one store wrote (e.g., an assignment rule added via the KCM
 * D-Bus path → @c LayoutRegistry → store ① ), the other store still held the
 * pre-write snapshot. When @c Settings::writeDisableEntries later rebuilt its
 * @c kept rule list from store ② 's stale @c ruleSet(), the resulting
 * @c setAllRules call wrote @c kept (which no longer contained the freshly-
 * added assignment rule) back to disk, silently dropping it.
 *
 * The fix routes the daemon's single store through @c Settings via the
 * borrowing ctor — every in-process writer mutates one @c RuleSet, so
 * no rebuild can ever see a stale snapshot of its peer's rule.
 *
 * This test instantiates the daemon's exact composition (one owned store,
 * @c Settings constructed with the borrow ctor) and runs the failing
 * pre-fix scenario. The assertions verify both writes survive an on-disk
 * round-trip, which would fail with the pre-fix dual-store layout.
 */

#include <QDir>
#include <QTest>
#include <QUuid>
#include <memory>

#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleStore.h>

#include "../../../src/config/configbackends.h"
#include "../../../src/config/configdefaults.h"
#include "../../../src/config/settings.h"

#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
using Mode = PhosphorZones::AssignmentEntry::Mode;

namespace {

// Stable-id-shaped screen names, for the same reason spelled out at the top of
// tests/unit/config/test_settings_disable_per_mode.cpp: every assertion below
// compares a screen name the code stored and handed back through
// Settings::disabledMonitors, which resolves connector names on EVERY read.
//
// `resolveScreenId` only rewrites CONNECTOR names, which ScreenIdentity defines
// as "contains no ':'", so a "Manuf:Model:Serial" string canonicalizes to itself
// on every machine. A connector name like "DP-2" would instead resolve against
// whatever is actually plugged into the box running the suite, and
// `QCOMPARE(disabledMonitors(...), {"DP-2"})` would come back as that monitor's
// EDID id. Under QT_QPA_PLATFORM=offscreen no connector matches and the name
// falls through untouched, which is why such a test passes ctest and fails a
// bare run on a host that owns the connector.
const QString kDisabledScreenSnapping = QStringLiteral("TestVendor:PanelA:0001");
const QString kDisabledScreenAutotile = QStringLiteral("TestVendor:PanelB:0002");
const QString kDisabledScreenBorrow = QStringLiteral("TestVendor:PanelC:0003");

/// Mint a deterministic assignment rule representing what the daemon's
/// LayoutRegistry / RuleAdaptor writes when the KCM saves a per-screen
/// layout choice. The exact action shape doesn't matter for the race test;
/// what matters is that this rule exists in the in-memory set BEFORE the
/// Settings-side disable write runs.
PhosphorRules::Rule makeTestAssignmentRule()
{
    return PhosphorRules::ContextRuleBridge::makeAssignmentRule(
        QStringLiteral("Test assignment rule"), QStringLiteral("DP-1"), 0, QString(), QStringLiteral("snapping"),
        QUuid::createUuid().toString(), QString(), PhosphorRules::ContextRuleBridge::kContextBandBase);
}

/// True iff @p store contains a rule with the assignment-action shape — i.e.,
/// not a per-mode disable rule. Used as the "the assignment write survived"
/// predicate without coupling the test to the rule's exact id.
bool hasAssignmentRule(const PhosphorRules::RuleStore& store)
{
    for (const PhosphorRules::Rule& rule : store.ruleSet().rules()) {
        if (!PhosphorRules::ContextRuleBridge::disableRuleMode(rule)) {
            return true;
        }
    }
    return false;
}

} // namespace

class TestDaemonDualStoreRace : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Pre-fix repro: an assignment write through the daemon-owned store
    /// followed by a per-mode disable write through Settings dropped the
    /// assignment rule when both stores pointed at the same file. With the
    /// borrow ctor in place both writes hit one store, so the disable
    /// rewrite carries the assignment rule along.
    void testDualStoreRace_assignmentSurvivesDisableWrite()
    {
        IsolatedConfigGuard guard;

        // The daemon's composition root: one backend, one RuleStore.
        auto backend = createDefaultConfigBackend();
        auto daemonStore = std::make_unique<PhosphorRules::RuleStore>(ConfigDefaults::rulesFilePath(), nullptr);
        daemonStore->load();

        // Settings shares the daemon's store via the borrow ctor. Mirrors
        // Daemon::Daemon line passing m_ruleStore.get() — when this
        // pointer is non-null Settings does NOT mint a second store.
        Settings settings(backend.get(), /*curveRegistry=*/nullptr, daemonStore.get(), nullptr);

        QCOMPARE(daemonStore->count(), 0);

        // (1) Daemon-side writer adds an assignment rule. This is the
        // canonical KCM-saves-an-assignment path: LayoutRegistry /
        // RuleAdaptor mutate the daemon's store directly.
        const PhosphorRules::Rule assignment = makeTestAssignmentRule();
        QVERIFY(daemonStore->addRule(assignment));
        QCOMPARE(daemonStore->count(), 1);
        QVERIFY(hasAssignmentRule(*daemonStore));

        // (2) Settings-side writer toggles a per-mode disable. Pre-fix this
        // rebuilt `kept` from Settings's own stale snapshot (empty — taken
        // before step 1) and called setAllRules({disableRule}), clobbering
        // the assignment from disk. With one shared store, the rebuild
        // sees the assignment in ruleSet() and carries it forward.
        const QString& disabledScreen = kDisabledScreenSnapping;
        settings.setDisabledMonitors(Mode::Snapping, {disabledScreen});

        // In-memory invariant — both writes are present in the one live
        // ruleset. Pre-fix this would already be wrong because Settings's
        // separate store would only contain the disable rule.
        QCOMPARE(daemonStore->count(), 2);
        QVERIFY2(hasAssignmentRule(*daemonStore),
                 "assignment rule was dropped from the in-memory set by the disable write");
        QCOMPARE(settings.disabledMonitors(Mode::Snapping), QStringList{disabledScreen});

        // (3) On-disk round-trip — the most useful assertion. Load a fresh
        // store from the same file and verify both writes are present.
        // Pre-fix Settings's setAllRules wrote `{disableRule}` only, so
        // this reload would see one rule and hasAssignmentRule() would be
        // false. With the fix, the on-disk file holds both rules.
        PhosphorRules::RuleStore reloaded(ConfigDefaults::rulesFilePath(), nullptr);
        reloaded.load();
        QCOMPARE(reloaded.count(), 2);
        QVERIFY2(hasAssignmentRule(reloaded), "assignment rule was dropped from rules.json by the dual-store race");
    }

    /// Symmetrical case: Settings writes the disable first, daemon writes
    /// the assignment second. Pre-fix this lost the disable rule because
    /// LayoutRegistry's setAllRules rebuilt from its own pre-disable
    /// snapshot. The borrow ctor fixes both orderings — verify the second
    /// one explicitly so a future regression that only repairs one side
    /// fails here.
    void testDualStoreRace_disableSurvivesAssignmentWrite()
    {
        IsolatedConfigGuard guard;

        auto backend = createDefaultConfigBackend();
        auto daemonStore = std::make_unique<PhosphorRules::RuleStore>(ConfigDefaults::rulesFilePath(), nullptr);
        daemonStore->load();
        Settings settings(backend.get(), /*curveRegistry=*/nullptr, daemonStore.get(), nullptr);

        // (1) Settings-side writer first.
        const QString& disabledScreen = kDisabledScreenAutotile;
        settings.setDisabledMonitors(Mode::Autotile, {disabledScreen});
        QCOMPARE(daemonStore->count(), 1);

        // (2) Daemon-side writer second. The race direction here is
        // hypothetical with the borrow ctor (one store can't race itself),
        // but `addRule` operating on a stale ruleset would have been the
        // failure mode if LayoutRegistry held its own copy. Verifying the
        // shared-store assertion pins the symmetric guarantee.
        QVERIFY(daemonStore->addRule(makeTestAssignmentRule()));
        QCOMPARE(daemonStore->count(), 2);

        // On-disk round-trip.
        PhosphorRules::RuleStore reloaded(ConfigDefaults::rulesFilePath(), nullptr);
        reloaded.load();
        QCOMPARE(reloaded.count(), 2);
        QVERIFY(hasAssignmentRule(reloaded));
        QCOMPARE(settings.disabledMonitors(Mode::Autotile), QStringList{disabledScreen});
    }

    /// Smoke check for the borrow contract itself: when Settings is
    /// constructed with a non-null store pointer, mutations to that store
    /// are visible through Settings's accessors WITHOUT calling
    /// settings.load(). The pre-fix layout could not satisfy this because
    /// Settings's separate store never saw the daemon's writes.
    void testBorrowCtor_sharedRulesetVisibleThroughSettings()
    {
        IsolatedConfigGuard guard;

        auto backend = createDefaultConfigBackend();
        auto daemonStore = std::make_unique<PhosphorRules::RuleStore>(ConfigDefaults::rulesFilePath(), nullptr);
        daemonStore->load();
        Settings settings(backend.get(), nullptr, daemonStore.get(), nullptr);

        // Inject a disable rule directly through the daemon-owned store
        // (the path RuleAdaptor takes when KCM edits a rule by id).
        const QString& disabledScreen = kDisabledScreenBorrow;
        const auto rule = PhosphorRules::ContextRuleBridge::makeDisableRule(
            QStringLiteral("Snapping off · %1").arg(disabledScreen), disabledScreen, 0, QString(),
            QStringLiteral("snapping"), PhosphorRules::ContextRuleBridge::kContextBandBase);
        QVERIFY(daemonStore->addRule(rule));

        // Settings reads through the same store, so the disable is visible
        // without a manual load().
        QCOMPARE(settings.disabledMonitors(Mode::Snapping), QStringList{disabledScreen});
    }
};

QTEST_MAIN(TestDaemonDualStoreRace)
#include "test_daemon_dual_store_race.moc"
