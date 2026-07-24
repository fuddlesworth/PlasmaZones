// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layoutmanager_assignment.cpp
 * @brief Unit tests for PhosphorZones::LayoutRegistry fallback cascade, default layout, quick slots
 */

#include <QTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopedPointer>
#include <QUuid>
#include "config/configbackends.h"
#include "config/configdefaults.h"
#include <memory>
#include <vector>

#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleStore.h>
#include "core/types/constants.h"
#include "helpers/StubSettings.h"
#include "LayoutManagerAssignmentFixture.h"

using namespace PlasmaZones;

class TestLayoutManagerAssignment : public LayoutManagerAssignmentFixture
{
    Q_OBJECT

private Q_SLOTS:

    // ─── Combined batch API ──────────────────────────────────────────────
    //
    // setAllCombinedAssignments / combinedAssignments are the triple-axis
    // sibling of the Desktop / Activity batches. Pin the round-trip: a
    // Combined rule survives, gets its enabled flag preserved, and its
    // edit isolation from pure-Activity / pure-Desktop / Monitor rules.
    void testCombinedBatchRoundTrip()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layoutA = createTestLayout(QStringLiteral("LayoutA"));
        mgr->addLayout(layoutA);
        auto* layoutB = createTestLayout(QStringLiteral("LayoutB"));
        mgr->addLayout(layoutB);

        // Seed: one Combined, one pure-Activity, one pure-Desktop. The
        // batch must touch ONLY the Combined.
        mgr->assignLayout(QStringLiteral("DP-1"), 3, QStringLiteral("work"), layoutA);
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QStringLiteral("work"), layoutB);
        mgr->assignLayout(QStringLiteral("DP-1"), 5, QString(), layoutB);

        // Reader returns only the Combined rule.
        const auto combined = mgr->combinedAssignments();
        QCOMPARE(combined.size(), 1);
        PhosphorZones::CombinedAssignmentKey key{QStringLiteral("DP-1"), 3, QStringLiteral("work")};
        QVERIFY(combined.contains(key));
        QCOMPARE(combined.value(key), layoutA->id().toString());

        // Round-trip: re-assign the same hash → state must be byte-identical.
        mgr->setAllCombinedAssignments(combined);
        const auto roundTripped = mgr->combinedAssignments();
        QCOMPARE(roundTripped, combined);

        // Resolution check at the (screen, desktop, activity) tuple. Precedence
        // is plain priority now (no specificity): both the Combined rule
        // (DP-1, 3, work → LayoutA) and the broader Activity rule (DP-1, work →
        // LayoutB) match this query, and the Activity rule was authored later so
        // it seeded a higher priority and wins. The transposed-arg regression
        // this used to guard is still caught by the round-trip hash equality
        // above (a mis-built Combined rule reads back a different key).
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 3, QStringLiteral("work"))->name(),
                 QStringLiteral("LayoutB"));

        // The pure-Activity rule resolves at a desktop with no Combined entry.
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 1, QStringLiteral("work"))->name(),
                 QStringLiteral("LayoutB"));
        // The pure-Desktop rule survives untouched.
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 5, QString())->name(), QStringLiteral("LayoutB"));
    }

    // NOTE on enabled-flag preservation: setAllCombinedAssignments mirrors
    // applyBatchAssignments's OldEntrySnapshot capture (Pass 1 P2 audit
    // finding), so disabled→enabled regression is structurally shared with
    // the Activity / Desktop / Screen batches. A dedicated test would need
    // a public RuleStore accessor on LayoutRegistry to flip the
    // rule's enabled flag — none exists today, and adding one solely for
    // test scaffolding would be an SRP violation. The shared
    // applyBatchAssignments + OldEntrySnapshot path covers the four
    // sibling APIs uniformly.

    // ─── Combined-rule preservation regression ───────────────────────────
    //
    // setAllActivityAssignments / activityAssignments operate on a
    // (screen, activity) hash key with no desktop dimension. Combined
    // rules (screen+desktop+activity) used to be matched by both the
    // batch reader and the family classifier — which meant a Combined
    // rule was read into the (screen, activity) hash (silently
    // overwriting any pure-Activity entry on the same pair), then on
    // round-trip rebuilt at desktop=0, permanently losing its desktop
    // pin. The fix narrows both reader and family classifier to STRICT
    // per-Activity (Activity-only, no Combined), so Combined rules
    // survive untouched in the rule store across an Activity-batch
    // round-trip.
    void testCombinedRulesSurviveActivityBatchRoundTrip()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* activityLayout = createTestLayout(QStringLiteral("ActivityLayout"));
        mgr->addLayout(activityLayout);
        auto* combinedLayout = createTestLayout(QStringLiteral("CombinedLayout"));
        mgr->addLayout(combinedLayout);

        const QString screen = QStringLiteral("DP-1");
        const QString activity = QStringLiteral("activity-work");

        // Pure-Activity rule and a Combined rule (same screen+activity but
        // pinned to desktop 3). Pre-fix: the Activity batch reader would
        // see both, key them under the same (screen, activity) pair, and
        // the QHash insert order would silently drop one.
        mgr->assignLayout(screen, 0, activity, activityLayout);
        mgr->assignLayout(screen, 3, activity, combinedLayout);

        // Reader sees ONLY the pure-Activity rule.
        const auto projection = mgr->activityAssignments();
        QCOMPARE(projection.size(), 1);
        QVERIFY(projection.contains(qMakePair(screen, activity)));

        // Round-trip the projection back through setAllActivityAssignments.
        // The Combined rule must still be reachable on desktop 3, untouched
        // by the Activity batch family classifier.
        mgr->setAllActivityAssignments(projection);
        QCOMPARE(mgr->layoutForScreen(screen, 3, activity)->name(), QStringLiteral("CombinedLayout"));
        QCOMPARE(mgr->layoutForScreen(screen, 1, activity)->name(), QStringLiteral("ActivityLayout"));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P1: layoutForScreen fallback cascade
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayoutManager_layoutForScreen_fallbackCascade()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* defaultLayout = createTestLayout(QStringLiteral("Default"));
        mgr->addLayout(defaultLayout);

        auto* screenLayout = createTestLayout(QStringLiteral("ScreenSpecific"));
        mgr->addLayout(screenLayout);

        auto* desktopLayout = createTestLayout(QStringLiteral("DesktopSpecific"));
        mgr->addLayout(desktopLayout);

        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), screenLayout);
        mgr->assignLayout(QStringLiteral("DP-1"), 2, QString(), desktopLayout);

        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 2)->name(), QStringLiteral("DesktopSpecific"));
        // Desktop 1 has no explicit entry — cascades to display default
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 1)->name(), QStringLiteral("ScreenSpecific"));

        PhosphorZones::Layout* fallback = mgr->layoutForScreen(QStringLiteral("HDMI-1"));
        QVERIFY(fallback != nullptr);
        QCOMPARE(fallback->name(), QStringLiteral("Default"));
    }

    // Per-activity assignments (stored at virtualDesktop=0 with a
    // non-empty activity) must be reachable through the cascade and must
    // win over the monitor-only default. Discussion #413 reported that
    // toggling activity assignments did nothing — monitor assignments
    // kept overriding them — because the cascade jumped from
    // (screen, desktop, activity) straight to (screen, desktop, "") and
    // (screen, 0, "") with no level matching the way activity entries
    // are persisted by `setAllActivityAssignments` ((screen, 0, activity)).
    void testLayoutManager_layoutForScreen_perActivityCascade()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* monitorLayout = createTestLayout(QStringLiteral("MonitorDefault"));
        mgr->addLayout(monitorLayout);
        auto* activityLayout = createTestLayout(QStringLiteral("WorkActivity"));
        mgr->addLayout(activityLayout);

        const QString screen = QStringLiteral("DP-1");
        const QString workActivity = QStringLiteral("activity-work");
        const QString playActivity = QStringLiteral("activity-play");

        // Mirror the storage shape `setAllActivityAssignments` uses.
        mgr->assignLayout(screen, 0, QString(), monitorLayout);
        mgr->assignLayout(screen, 0, workActivity, activityLayout);

        // In the work activity, on any desktop, activity entry wins.
        QCOMPARE(mgr->layoutForScreen(screen, 1, workActivity)->name(), QStringLiteral("WorkActivity"));
        QCOMPARE(mgr->layoutForScreen(screen, 5, workActivity)->name(), QStringLiteral("WorkActivity"));

        // An activity without a per-activity entry falls through to the
        // monitor default (level 4).
        QCOMPARE(mgr->layoutForScreen(screen, 1, playActivity)->name(), QStringLiteral("MonitorDefault"));

        // Empty activity (e.g. ActivityManager not initialised) skips
        // the activity level and lands on the monitor default.
        QCOMPARE(mgr->layoutForScreen(screen, 1, QString())->name(), QStringLiteral("MonitorDefault"));
    }

    // Per-activity entries must outrank per-desktop entries when both
    // are present and the user is in a configured activity. Activities
    // are a higher-level workspace context than virtual desktops in
    // KDE Plasma, so the cascade picks activity first.
    void testLayoutManager_layoutForScreen_activityWinsOverDesktop()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* desktopLayout = createTestLayout(QStringLiteral("DesktopTwo"));
        mgr->addLayout(desktopLayout);
        auto* activityLayout = createTestLayout(QStringLiteral("ActivityWork"));
        mgr->addLayout(activityLayout);

        const QString screen = QStringLiteral("DP-1");
        const QString workActivity = QStringLiteral("activity-work");

        mgr->assignLayout(screen, 2, QString(), desktopLayout);
        mgr->assignLayout(screen, 0, workActivity, activityLayout);

        QCOMPARE(mgr->layoutForScreen(screen, 2, workActivity)->name(), QStringLiteral("ActivityWork"));
        // No activity → desktop entry applies.
        QCOMPARE(mgr->layoutForScreen(screen, 2, QString())->name(), QStringLiteral("DesktopTwo"));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P2: Quick layout slots
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayoutManager_quickLayoutSlot_validRange_1to9()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* layout = createTestLayout(QStringLiteral("Quick"));
        mgr->addLayout(layout);

        QString layoutId = layout->id().toString();
        const auto snapping = PhosphorZones::AssignmentEntry::Snapping;

        mgr->setQuickLayoutSlot(snapping, 1, layoutId);
        QVERIFY(mgr->quickLayoutSlots(snapping).contains(1));

        mgr->setQuickLayoutSlot(snapping, 9, layoutId);
        QVERIFY(mgr->quickLayoutSlots(snapping).contains(9));

        mgr->setQuickLayoutSlot(snapping, 0, layoutId);
        QVERIFY(!mgr->quickLayoutSlots(snapping).contains(0));

        mgr->setQuickLayoutSlot(snapping, 10, layoutId);
        QVERIFY(!mgr->quickLayoutSlots(snapping).contains(10));

        mgr->setQuickLayoutSlot(snapping, 1, QString());
        QVERIFY(!mgr->quickLayoutSlots(snapping).contains(1));
    }

    void testLayoutManager_quickLayoutSlot_perModeIndependent()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* layout = createTestLayout(QStringLiteral("Quick"));
        mgr->addLayout(layout);
        const QString layoutId = layout->id().toString();
        const auto snapping = PhosphorZones::AssignmentEntry::Snapping;
        const auto autotile = PhosphorZones::AssignmentEntry::Autotile;

        // Same slot number in each mode holds an independent binding: a manual
        // layout UUID for snapping, an autotile algorithm ID for tiling.
        mgr->setQuickLayoutSlot(snapping, 1, layoutId);
        mgr->setQuickLayoutSlot(autotile, 1, QStringLiteral("autotile:bsp"));

        QCOMPARE(mgr->quickLayoutSlots(snapping).value(1), layoutId);
        QCOMPARE(mgr->quickLayoutSlots(autotile).value(1), QStringLiteral("autotile:bsp"));

        // Clearing one mode's slot leaves the other mode untouched.
        mgr->setQuickLayoutSlot(snapping, 1, QString());
        QVERIFY(!mgr->quickLayoutSlots(snapping).contains(1));
        QCOMPARE(mgr->quickLayoutSlots(autotile).value(1), QStringLiteral("autotile:bsp"));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P2: Default layout resolution
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayoutManager_defaultLayout_settingsIdTakesPrecedence()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* settings = new StubSettings(mgr.data());
        mgr->setDefaultLayoutIdProvider([settings]() {
            return settings->defaultLayoutId();
        });

        auto* first = createTestLayout(QStringLiteral("First"));
        mgr->addLayout(first);

        auto* second = createTestLayout(QStringLiteral("Second"));
        mgr->addLayout(second);

        QCOMPARE(mgr->defaultLayout()->name(), QStringLiteral("First"));

        settings->setDefaultLayoutId(second->id().toString());
        QCOMPARE(mgr->defaultLayout()->name(), QStringLiteral("Second"));
    }

    void testLayoutManager_defaultLayout_fallbackToFirstLayout()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* settings = new StubSettings(mgr.data());
        mgr->setDefaultLayoutIdProvider([settings]() {
            return settings->defaultLayoutId();
        });

        auto* layout = createTestLayout(QStringLiteral("OnlyLayout"));
        mgr->addLayout(layout);

        settings->setDefaultLayoutId(QUuid::createUuid().toString());
        QCOMPARE(mgr->defaultLayout()->name(), QStringLiteral("OnlyLayout"));

        settings->setDefaultLayoutId(QString());
        QCOMPARE(mgr->defaultLayout()->name(), QStringLiteral("OnlyLayout"));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P3: PhosphorZones::AssignmentEntry explicit fields
    // ═══════════════════════════════════════════════════════════════════════════

    void testAssignmentEntry_snappingAssignment_setsFields()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* layout = createTestLayout(QStringLiteral("Manual"));
        mgr->addLayout(layout);

        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layout);

        auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 0);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry.snappingLayout, layout->id().toString());
        QVERIFY(entry.tilingAlgorithm.isEmpty());
        QCOMPARE(entry.activeLayoutId(), layout->id().toString());
    }

    void testAssignmentEntry_autotileAssignment_setsFields()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* layout = createTestLayout(QStringLiteral("Manual"));
        mgr->addLayout(layout);

        // First assign a snapping layout
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layout);

        // Then assign autotile — should preserve snappingLayout
        mgr->assignLayoutById(QStringLiteral("DP-1"), 0, QString(), QStringLiteral("autotile:wide"));

        auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 0);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(entry.tilingAlgorithm, QStringLiteral("wide"));
        // snappingLayout should be preserved from the earlier assignment
        QCOMPARE(entry.snappingLayout, layout->id().toString());
        QCOMPARE(entry.activeLayoutId(), QStringLiteral("autotile:wide"));
    }

    void testAssignmentEntry_togglePreservesBothFields()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* layout = createTestLayout(QStringLiteral("Manual"));
        mgr->addLayout(layout);

        // Set snapping layout
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layout);
        // Set autotile (snapping preserved)
        mgr->assignLayoutById(QStringLiteral("DP-1"), 0, QString(), QStringLiteral("autotile:dwindle"));

        auto entry1 = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 0);
        QCOMPARE(entry1.mode, PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(entry1.snappingLayout, layout->id().toString());
        QCOMPARE(entry1.tilingAlgorithm, QStringLiteral("dwindle"));

        // Toggle back to snapping (tilingAlgorithm preserved)
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layout);

        auto entry2 = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 0);
        QCOMPARE(entry2.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry2.snappingLayout, layout->id().toString());
        QCOMPARE(entry2.tilingAlgorithm, QStringLiteral("dwindle"));
        QCOMPARE(entry2.activeLayoutId(), layout->id().toString());
    }

    void testAssignmentEntry_modeForScreen_delegates()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* layout = createTestLayout(QStringLiteral("Manual"));
        mgr->addLayout(layout);

        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layout);
        QCOMPARE(mgr->modeForScreen(QStringLiteral("DP-1"), 0), PhosphorZones::AssignmentEntry::Snapping);

        mgr->assignLayoutById(QStringLiteral("DP-1"), 0, QString(), QStringLiteral("autotile:wide"));
        QCOMPARE(mgr->modeForScreen(QStringLiteral("DP-1"), 0), PhosphorZones::AssignmentEntry::Autotile);
    }

    void testAssignmentEntry_snappingLayoutForScreen_returnsField()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* layout = createTestLayout(QStringLiteral("Manual"));
        mgr->addLayout(layout);

        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layout);
        mgr->assignLayoutById(QStringLiteral("DP-1"), 0, QString(), QStringLiteral("autotile:wide"));

        // Even in autotile mode, snappingLayoutForScreen returns the preserved layout
        QCOMPARE(mgr->snappingLayoutForScreen(QStringLiteral("DP-1"), 0), layout->id().toString());
    }

    void testAssignmentEntry_tilingAlgorithmForScreen_returnsField()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* layout = createTestLayout(QStringLiteral("Manual"));
        mgr->addLayout(layout);

        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layout);
        mgr->assignLayoutById(QStringLiteral("DP-1"), 0, QString(), QStringLiteral("autotile:wide"));

        // Switch back to snapping — tilingAlgorithm is still preserved
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layout);
        QCOMPARE(mgr->tilingAlgorithmForScreen(QStringLiteral("DP-1"), 0), QStringLiteral("wide"));
    }

    void testAssignmentEntry_perDesktop_independentEntries()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* layoutA = createTestLayout(QStringLiteral("LayoutA"));
        mgr->addLayout(layoutA);
        auto* layoutB = createTestLayout(QStringLiteral("LayoutB"));
        mgr->addLayout(layoutB);

        // Desktop 1: snapping with layoutA
        mgr->assignLayout(QStringLiteral("DP-1"), 1, QString(), layoutA);
        // Desktop 2: autotile with layoutB as snapping fallback
        mgr->assignLayout(QStringLiteral("DP-1"), 2, QString(), layoutB);
        mgr->assignLayoutById(QStringLiteral("DP-1"), 2, QString(), QStringLiteral("autotile:dwindle"));

        auto entry1 = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 1);
        QCOMPARE(entry1.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry1.snappingLayout, layoutA->id().toString());

        auto entry2 = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 2);
        QCOMPARE(entry2.mode, PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(entry2.snappingLayout, layoutB->id().toString());
        QCOMPARE(entry2.tilingAlgorithm, QStringLiteral("dwindle"));
    }

    void testAssignmentEntry_clearAutotile_flipsToSnapping()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* layout = createTestLayout(QStringLiteral("Manual"));
        mgr->addLayout(layout);

        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layout);
        mgr->assignLayoutById(QStringLiteral("DP-1"), 0, QString(), QStringLiteral("autotile:wide"));

        // clearAutotileAssignments should flip mode to Snapping, preserving both fields
        mgr->clearAutotileAssignments();

        auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 0);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry.snappingLayout, layout->id().toString());
        QCOMPARE(entry.tilingAlgorithm, QStringLiteral("wide"));
    }

    // Regression for the mode-only autotile cascade bug: the KCM stores
    // mode=Autotile with empty tilingAlgorithm via setAssignmentEntryDirect
    // to mean "autotile mode, use the default algorithm". Before the fix to
    // LayoutId::makeAutotileId's empty-algorithm handling, activeLayoutId()
    // returned empty for this entry — the cascade visitors in
    // assignmentIdForScreen / assignmentEntryForScreen rejected it, and
    // modeForScreen wrongly reported Snapping. Pin the correct behaviour so
    // a future change to makeAutotileId doesn't silently regress the KCM
    // mode-only workflow again.
    void testAssignmentEntry_modeOnlyAutotile_cascadeAccepts()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        PhosphorZones::AssignmentEntry modeOnly;
        modeOnly.mode = PhosphorZones::AssignmentEntry::Autotile;
        // snappingLayout + tilingAlgorithm both left empty — KCM wire format
        // for "autotile, pick the default algorithm".
        mgr->setAssignmentEntryDirect(QStringLiteral("DP-1"), 0, QString(), modeOnly);

        // activeLayoutId() returns the bare prefix — non-empty, so the
        // cascade visitor accepts it.
        QCOMPARE(modeOnly.activeLayoutId(), QStringLiteral("autotile:"));
        QVERIFY(!modeOnly.activeLayoutId().isEmpty());

        // Both cascade paths must agree that this entry routes as Autotile.
        QCOMPARE(mgr->modeForScreen(QStringLiteral("DP-1"), 0), PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 0), QStringLiteral("autotile:"));

        auto roundTrip = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 0);
        QCOMPARE(roundTrip.mode, PhosphorZones::AssignmentEntry::Autotile);
        QVERIFY(roundTrip.tilingAlgorithm.isEmpty());
        QVERIFY(roundTrip.snappingLayout.isEmpty());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P6: Config round-trip
    // ═══════════════════════════════════════════════════════════════════════════

    void testConfigRoundTrip_saveAndLoad_preservesAllFields()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* layoutA = createTestLayout(QStringLiteral("LayoutA"));
        mgr->addLayout(layoutA);
        auto* layoutB = createTestLayout(QStringLiteral("LayoutB"));
        mgr->addLayout(layoutB);

        QString idA = layoutA->id().toString();
        QString idB = layoutB->id().toString();

        // Base screen: autotile with snapping preserved
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layoutA);
        mgr->assignLayoutById(QStringLiteral("DP-1"), 0, QString(), QStringLiteral("autotile:wide"));

        // Per-desktop: snapping with tiling preserved
        mgr->assignLayout(QStringLiteral("DP-1"), 2, QString(), layoutB);
        mgr->assignLayoutById(QStringLiteral("DP-1"), 2, QString(), QStringLiteral("autotile:dwindle"));
        mgr->assignLayout(QStringLiteral("DP-1"), 2, QString(), layoutB); // flip back to snapping

        // Per-activity: pure autotile
        mgr->assignLayoutById(QStringLiteral("DP-1"), 0, QStringLiteral("act-123"), QStringLiteral("autotile:tall"));

        // Quick layout slot
        mgr->setQuickLayoutSlot(PhosphorZones::AssignmentEntry::Snapping, 3, idA);

        // Save
        mgr->saveAssignments();

        // Create a new manager and load — same config file sees the data
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr2(
            PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts")));
        mgr2->addLayout(createTestLayout(QStringLiteral("LayoutA")));
        mgr2->addLayout(createTestLayout(QStringLiteral("LayoutB")));
        QString layoutDir2 = m_guards.back()->dataPath() + QStringLiteral("/plasmazones/layouts2");
        QDir().mkpath(layoutDir2);
        mgr2->setLayoutDirectory(layoutDir2);
        mgr2->loadAssignments();

        // Verify base screen
        auto base = mgr2->assignmentEntryForScreen(QStringLiteral("DP-1"), 0);
        QCOMPARE(base.mode, PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(base.snappingLayout, idA);
        QCOMPARE(base.tilingAlgorithm, QStringLiteral("wide"));

        // Verify per-desktop
        auto desk2 = mgr2->assignmentEntryForScreen(QStringLiteral("DP-1"), 2);
        QCOMPARE(desk2.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(desk2.snappingLayout, idB);
        QCOMPARE(desk2.tilingAlgorithm, QStringLiteral("dwindle"));

        // Verify per-activity
        auto act = mgr2->assignmentEntryForScreen(QStringLiteral("DP-1"), 0, QStringLiteral("act-123"));
        QCOMPARE(act.mode, PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(act.tilingAlgorithm, QStringLiteral("tall"));

        // Verify quick layout slot
        QCOMPARE(mgr2->quickLayoutSlots(PhosphorZones::AssignmentEntry::Snapping).value(3), idA);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P6: PhosphorZones::AssignmentEntry::fromLayoutId static factory
    // ═══════════════════════════════════════════════════════════════════════════

    void testAssignmentEntry_fromLayoutId_autotile()
    {
        auto entry = PhosphorZones::AssignmentEntry::fromLayoutId(QStringLiteral("autotile:wide"));
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(entry.tilingAlgorithm, QStringLiteral("wide"));
        QVERIFY(entry.snappingLayout.isEmpty());
    }

    void testAssignmentEntry_fromLayoutId_snapping()
    {
        QString uuid = QUuid::createUuid().toString();
        auto entry = PhosphorZones::AssignmentEntry::fromLayoutId(uuid);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry.snappingLayout, uuid);
        QVERIFY(entry.tilingAlgorithm.isEmpty());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P7: PhosphorZones::LayoutAssignmentKey::fromGroupName parser
    // ═══════════════════════════════════════════════════════════════════════════

    void testFromGroupName_fullKey_parsesAllFields()
    {
        auto key = PhosphorZones::LayoutAssignmentKey::fromGroupName(
            QStringLiteral("Assignment:eDP-1:Desktop:2:Activity:abc-123"), QStringLiteral("Assignment:"));
        QCOMPARE(key.screenId, QStringLiteral("eDP-1"));
        QCOMPARE(key.virtualDesktop, 2);
        QCOMPARE(key.activity, QStringLiteral("abc-123"));
    }

    void testFromGroupName_screenOnly_parsesScreenId()
    {
        auto key = PhosphorZones::LayoutAssignmentKey::fromGroupName(QStringLiteral("Assignment:HDMI-A-1"),
                                                                     QStringLiteral("Assignment:"));
        QCOMPARE(key.screenId, QStringLiteral("HDMI-A-1"));
        QCOMPARE(key.virtualDesktop, 0);
        QVERIFY(key.activity.isEmpty());
    }

    void testFromGroupName_noPrefix_returnsEmpty()
    {
        auto key = PhosphorZones::LayoutAssignmentKey::fromGroupName(QStringLiteral("Snapping.Behavior"),
                                                                     QStringLiteral("Assignment:"));
        QVERIFY(key.screenId.isEmpty());
    }

    void testFromGroupName_emptyAfterPrefix_returnsEmpty()
    {
        auto key = PhosphorZones::LayoutAssignmentKey::fromGroupName(QStringLiteral("Assignment:"),
                                                                     QStringLiteral("Assignment:"));
        QVERIFY(key.screenId.isEmpty());
    }

    void testFromGroupName_emptyActivity_treatedAsAllActivities()
    {
        auto key = PhosphorZones::LayoutAssignmentKey::fromGroupName(QStringLiteral("Assignment:eDP-1:Activity:"),
                                                                     QStringLiteral("Assignment:"));
        QCOMPARE(key.screenId, QStringLiteral("eDP-1"));
        QVERIFY(key.activity.isEmpty());
    }

    void testFromGroupName_invalidDesktop_treatedAsAllDesktops()
    {
        auto key = PhosphorZones::LayoutAssignmentKey::fromGroupName(QStringLiteral("Assignment:eDP-1:Desktop:abc"),
                                                                     QStringLiteral("Assignment:"));
        QCOMPARE(key.screenId, QStringLiteral("eDP-1"));
        QCOMPARE(key.virtualDesktop, 0);
    }

    void testFromGroupName_negativeDesktop_treatedAsAllDesktops()
    {
        auto key = PhosphorZones::LayoutAssignmentKey::fromGroupName(QStringLiteral("Assignment:eDP-1:Desktop:-1"),
                                                                     QStringLiteral("Assignment:"));
        QCOMPARE(key.screenId, QStringLiteral("eDP-1"));
        QCOMPARE(key.virtualDesktop, 0);
    }

    void testFromGroupName_zeroDesktop_treatedAsAllDesktops()
    {
        auto key = PhosphorZones::LayoutAssignmentKey::fromGroupName(QStringLiteral("Assignment:eDP-1:Desktop:0"),
                                                                     QStringLiteral("Assignment:"));
        QCOMPARE(key.screenId, QStringLiteral("eDP-1"));
        QCOMPARE(key.virtualDesktop, 0);
    }

    void testFromGroupName_desktopOnly_parsesDesktop()
    {
        auto key = PhosphorZones::LayoutAssignmentKey::fromGroupName(QStringLiteral("Assignment:DP-2:Desktop:3"),
                                                                     QStringLiteral("Assignment:"));
        QCOMPARE(key.screenId, QStringLiteral("DP-2"));
        QCOMPARE(key.virtualDesktop, 3);
        QVERIFY(key.activity.isEmpty());
    }

    void testAssignmentEntry_fromLayoutId_setsModeSetsField_preservesOther()
    {
        PhosphorZones::AssignmentEntry existing;
        existing.mode = PhosphorZones::AssignmentEntry::Autotile;
        existing.tilingAlgorithm = QStringLiteral("dwindle");
        existing.snappingLayout = QStringLiteral("{some-uuid}");

        // Update snapping field — mode switches to Snapping, tiling preserved
        auto entry = PhosphorZones::AssignmentEntry::fromLayoutId(QStringLiteral("{new-uuid}"), existing);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry.snappingLayout, QStringLiteral("{new-uuid}"));
        QCOMPARE(entry.tilingAlgorithm, QStringLiteral("dwindle")); // preserved

        // Update tiling field — mode switches to Autotile, snapping preserved
        auto entry2 = PhosphorZones::AssignmentEntry::fromLayoutId(QStringLiteral("autotile:wide"), existing);
        QCOMPARE(entry2.mode, PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(entry2.tilingAlgorithm, QStringLiteral("wide"));
        QCOMPARE(entry2.snappingLayout, QStringLiteral("{some-uuid}")); // preserved
    }
};

QTEST_MAIN(TestLayoutManagerAssignment)
#include "test_layoutmanager_assignment.moc"
