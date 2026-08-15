// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layoutmanager_assignment.cpp
 * @brief Unit tests for PhosphorZones::LayoutRegistry fallback cascade, default layout, quick slots
 */

#include <QTest>
#include <QDir>
#include <QScopedPointer>
#include <QUuid>

#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
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

    // storedAssignmentIdForScreen: same cascade as assignmentIdForScreen but a
    // miss must stay a miss — no default-layout synthesis. Discussion #858:
    // the editor asked "what layout is on DP-2", got the registry-wide default
    // (the only layout, UW1) back as if DP-2 owned it, opened it for in-place
    // editing, and the user's save overwrote UW1.
    void testLayoutManager_storedAssignmentIdForScreen_noDefaultSynthesis()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* uw1 = createTestLayout(QStringLiteral("UW1"));
        mgr->addLayout(uw1);
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), uw1);

        // Assigned screen: the stored query answers like the resolvers.
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1")), uw1->id().toString());
        QCOMPARE(mgr->storedAssignmentIdForScreen(QStringLiteral("DP-1")), uw1->id().toString());

        // The cascade itself still applies: a desktop-specific query on the
        // assigned screen falls through to the base entry.
        QCOMPARE(mgr->storedAssignmentIdForScreen(QStringLiteral("DP-1"), 3), uw1->id().toString());

        // Unassigned screen: layoutForScreen keeps handing out the default
        // (first) layout, but the stored query must report empty so callers
        // can tell "DP-2 has no layout of its own".
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-2")), uw1);
        QVERIFY(mgr->storedAssignmentIdForScreen(QStringLiteral("DP-2")).isEmpty());
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

    // The global autotile disable walks EVERY context; the daemon's re-enable only
    // ever wrote the current desktop per screen, so an off/on round trip stranded
    // desktops 2..N in Snapping with no way back. restoreAutotileAssignments is the
    // inverse of clearAutotileAssignments and closes that.
    void testRestoreAutotileAssignments_revivesEveryContextTheDisableFlipped()
    {
        // createManager(), NOT makeLayoutRegistry directly: the factory installs the
        // IsolatedConfigGuard that redirects the rule store and the layout directory
        // into a temporary XDG root. Constructing the registry bare writes to the
        // developer's REAL ~/.config/plasmazones/rules.json and layout directory.
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        // Two different desktops on one screen, both autotile, different algorithms.
        // Each is given a snap layout FIRST, which is what a real screen carries:
        // the mode-toggle losslessness invariant means an autotile entry keeps the
        // snapping layout it had before. It also matters for what this test can
        // observe — a Snapping entry with no snappingLayout has an empty
        // activeLayoutId, and the cascade visitors reject those, so the post-clear
        // state would be invisible to assignmentEntryForScreen and the assertions
        // below would read the level-1 default instead of the flipped entry.
        auto* snapA = createTestLayout(QStringLiteral("SnapA"));
        mgr->addLayout(snapA);
        mgr->assignLayout(QStringLiteral("DP-1"), 1, QString(), snapA);
        mgr->assignLayout(QStringLiteral("DP-1"), 2, QString(), snapA);
        mgr->assignLayoutById(QStringLiteral("DP-1"), 1, QString(), QStringLiteral("autotile:wide"));
        mgr->assignLayoutById(QStringLiteral("DP-1"), 2, QString(), QStringLiteral("autotile:dwindle"));
        QCOMPARE(mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 1).mode,
                 PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 2).mode,
                 PhosphorZones::AssignmentEntry::Autotile);

        // An autotile quick-layout slot the disable will wipe. The wipe is
        // documented as ONE-WAY (layoutregistry_batch.cpp): the restore rebuilds
        // assignment rules and has nothing to rebuild slots from, so the slot must
        // still be gone after it runs. Pinning it here keeps a future "make the
        // restore symmetric" change from happening by accident.
        mgr->setQuickLayoutSlot(PhosphorZones::AssignmentEntry::Autotile, 1, QStringLiteral("autotile:bsp"));
        QCOMPARE(mgr->quickLayoutSlots(PhosphorZones::AssignmentEntry::Autotile).value(1),
                 QStringLiteral("autotile:bsp"));

        mgr->clearAutotileAssignments();
        QVERIFY2(!mgr->quickLayoutSlots(PhosphorZones::AssignmentEntry::Autotile).contains(1),
                 "the disable wipes autotile quick-layout slots");
        // Both flipped, both algorithms preserved — that preservation is what the
        // restore keys on, and what the disable's own comment promises.
        QCOMPARE(mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 1).mode,
                 PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 2).mode,
                 PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 2).tilingAlgorithm, QStringLiteral("dwindle"));

        QCOMPARE(mgr->restoreAutotileAssignments(), 2);

        // BOTH desktops come back, each on its OWN algorithm — the whole point.
        // Desktop 2 is the one the old per-current-desktop enable could never reach.
        const auto d1 = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 1);
        const auto d2 = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 2);
        QCOMPARE(d1.mode, PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(d1.tilingAlgorithm, QStringLiteral("wide"));
        QCOMPARE(d2.mode, PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(d2.tilingAlgorithm, QStringLiteral("dwindle"));

        // The slot the disable wiped stays wiped — the restore is one-way there.
        QVERIFY2(!mgr->quickLayoutSlots(PhosphorZones::AssignmentEntry::Autotile).contains(1),
                 "restoreAutotileAssignments rebuilds assignment rules only, never quick-layout slots");
    }

    // An activity-pinned context is a Combined (screen + desktop + activity) rule,
    // which the cascade-family union admits, so the global disable reaches it and
    // the restore must reach it back — on its OWN algorithm, not the global
    // default. This is the axis the daemon's old per-current-desktop re-enable
    // could never address at all.
    void testRestoreAutotileAssignments_revivesActivityPinnedContext()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* snap = createTestLayout(QStringLiteral("SnapWork"));
        mgr->addLayout(snap);

        const QString screen = QStringLiteral("DP-1");
        const QString activity = QStringLiteral("work");
        mgr->assignLayout(screen, 1, activity, snap);
        mgr->assignLayoutById(screen, 1, activity, QStringLiteral("autotile:tall"));
        QCOMPARE(mgr->assignmentEntryForScreen(screen, 1, activity).mode, PhosphorZones::AssignmentEntry::Autotile);

        mgr->clearAutotileAssignments();
        QCOMPARE(mgr->assignmentEntryForScreen(screen, 1, activity).mode, PhosphorZones::AssignmentEntry::Snapping);

        QCOMPARE(mgr->restoreAutotileAssignments(), 1);

        const auto revived = mgr->assignmentEntryForScreen(screen, 1, activity);
        QCOMPARE(revived.mode, PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(revived.tilingAlgorithm, QStringLiteral("tall"));
        QCOMPARE(revived.snappingLayout, snap->id().toString());
    }

    // A window-property rule that happens to carry a SetEngineMode action must be
    // left alone: its SetEngineMode is a per-window intent, not a context
    // assignment, so a global disable/enable has no business flipping it. The
    // isContextAssignmentRule guard is what keeps it out of the walk, and the
    // rule authored here would otherwise qualify for the flip: Snapping mode
    // with a surviving tiling algorithm is exactly the discriminator the
    // restore keys on.
    void testRestoreAutotileAssignments_leavesWindowRuleWithEngineModeIntact()
    {
        namespace PWR = PhosphorRules;
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* store = mgr->findChild<PWR::RuleStore*>();
        QVERIFY(store != nullptr);

        PWR::Rule rule;
        rule.id = QUuid::createUuid();
        rule.name = QStringLiteral("test-window-rule-with-mode");
        rule.enabled = true;
        // A window-property match, NOT the (screen, desktop, activity) shape.
        rule.match =
            PWR::MatchExpression::makeLeaf(PWR::Field::AppId, PWR::Operator::AppIdMatches, QStringLiteral("konsole"));
        rule.actions = PWR::ContextRuleBridge::makeAssignmentActions(QStringLiteral("snapping"), QString(),
                                                                     QStringLiteral("dwindle"));
        PWR::RuleAction floatAction;
        floatAction.type = QString(PWR::ActionType::Float);
        rule.actions.append(floatAction);
        QVERIFY(store->addRule(rule));

        QCOMPARE(mgr->restoreAutotileAssignments(), 0);

        // The rule survives with every action it was authored with, the Float
        // included — a rebuild would have dropped it.
        const auto rules = store->ruleSet().rules();
        bool found = false;
        for (const PWR::Rule& stored : rules) {
            if (stored.id != rule.id) {
                continue;
            }
            found = true;
            QCOMPARE(stored.actions.size(), rule.actions.size());
            bool hasFloat = false;
            bool stillSnapping = false;
            for (const PWR::RuleAction& action : stored.actions) {
                if (action.type == QLatin1String(PWR::ActionType::Float)) {
                    hasFloat = true;
                } else if (action.type == QLatin1String(PWR::ActionType::SetEngineMode)) {
                    stillSnapping = action.params.value(PWR::ActionParam::Mode).toString() == QLatin1String("snapping");
                }
            }
            QVERIFY2(hasFloat, "the rule's non-assignment action must survive the restore walk");
            QVERIFY2(stillSnapping, "a window rule's engine mode must not be flipped to autotile");
        }
        QVERIFY(found);
    }

    // A MIXED context-assignment rule — the (screen, desktop) context shape plus
    // an action that is nobody's assignment slot — must keep that extra action
    // across BOTH legs of the flip. flipAssignmentModes rebuilds the three
    // assignment slots from makeAssignmentActions and then re-appends every
    // surviving non-slot action in its original relative order, so the extra
    // action lands last. Assigning makeAssignmentActions' output wholesale
    // (the shape before the fix) would delete it on the way down and again on
    // the way back up.
    void testClearRestoreAutotile_mixedRuleKeepsNonAssignmentActions()
    {
        namespace PWR = PhosphorRules;
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* store = mgr->findChild<PWR::RuleStore*>();
        QVERIFY(store != nullptr);

        auto* snap = createTestLayout(QStringLiteral("SnapMixed"));
        mgr->addLayout(snap);

        // An exact-context autotile assignment (DP-1, desktop 1) carrying both
        // layout fields, so makeAssignmentActions emits all three slots on
        // every rebuild and the action count stays comparable across the flips.
        PWR::Rule rule = PWR::ContextRuleBridge::makeAssignmentRule(QString(), QStringLiteral("DP-1"), 1, QString(),
                                                                    QStringLiteral("autotile"), snap->id().toString(),
                                                                    QStringLiteral("dwindle"), /*priority=*/100);
        // TWO non-assignment actions, so the assertions can discriminate the
        // preserved order AMONG survivors (SetOpacity before Float), not merely
        // survival: a rebuild that reversed or re-sorted survivors would keep
        // the count and lose the pair's order.
        PWR::RuleAction opacityAction;
        opacityAction.type = QString(PWR::ActionType::SetOpacity);
        // SetOpacity's validator requires a wire-encoded [0.0, 1.0] Value.
        opacityAction.params.insert(QString(PWR::ActionParam::Value), 0.8);
        rule.actions.append(opacityAction);
        PWR::RuleAction floatAction;
        floatAction.type = QString(PWR::ActionType::Float);
        rule.actions.append(floatAction);
        QCOMPARE(rule.actions.size(), 5);
        QVERIFY(store->addRule(rule));

        // Read the stored rule back and report (engine mode, action count,
        // whether the survivors still trail in authored order).
        const auto inspect = [store, &rule](QString& modeOut, int& countOut, bool& survivorsInOrderOut) {
            modeOut.clear();
            countOut = -1;
            survivorsInOrderOut = false;
            const auto stored = store->ruleSet().ruleById(rule.id);
            QVERIFY(stored.has_value());
            countOut = stored->actions.size();
            survivorsInOrderOut = stored->actions.size() >= 2
                && stored->actions.at(stored->actions.size() - 2).type == QLatin1String(PWR::ActionType::SetOpacity)
                && stored->actions.last().type == QLatin1String(PWR::ActionType::Float);
            for (const PWR::RuleAction& action : stored->actions) {
                if (action.type == QLatin1String(PWR::ActionType::SetEngineMode)) {
                    modeOut = action.params.value(PWR::ActionParam::Mode).toString();
                }
            }
        };

        QString mode;
        int actionCount = -1;
        bool survivorsInOrder = false;

        mgr->clearAutotileAssignments();
        inspect(mode, actionCount, survivorsInOrder);
        QCOMPARE(mode, QStringLiteral("snapping"));
        QCOMPARE(actionCount, 5);
        QVERIFY2(survivorsInOrder,
                 "the disable must re-append the rule's non-assignment actions, in authored order, after the slots");

        QCOMPARE(mgr->restoreAutotileAssignments(), 1);
        inspect(mode, actionCount, survivorsInOrder);
        QCOMPARE(mode, QStringLiteral("autotile"));
        QCOMPARE(actionCount, 5);
        QVERIFY2(survivorsInOrder,
                 "the restore must re-append the rule's non-assignment actions, in authored order, after the slots");
    }

    // The discriminating half of "algorithm memory": a context the user switched
    // back to Snapping BY HAND still carries its tiling algorithm, and the restore
    // revives it too. That is the intended semantics stated in
    // layoutregistry_batch.cpp — the global enable brings autotile back everywhere
    // it has ever been configured, not only where the last global disable took it
    // away. No global disable runs in this test, so nothing but the surviving
    // algorithm can be driving the flip.
    void testRestoreAutotileAssignments_revivesHandSwitchedContext()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* snap = createTestLayout(QStringLiteral("SnapChosen"));
        mgr->addLayout(snap);

        mgr->assignLayout(QStringLiteral("DP-3"), 1, QString(), snap);
        mgr->assignLayoutById(QStringLiteral("DP-3"), 1, QString(), QStringLiteral("autotile:wide"));
        // The user picks the snapping layout again — a hand switch, not a disable.
        mgr->assignLayout(QStringLiteral("DP-3"), 1, QString(), snap);

        const auto handSwitched = mgr->assignmentEntryForScreen(QStringLiteral("DP-3"), 1);
        QCOMPARE(handSwitched.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(handSwitched.tilingAlgorithm, QStringLiteral("wide")); // algorithm memory survives the switch

        QCOMPARE(mgr->restoreAutotileAssignments(), 1);

        const auto revived = mgr->assignmentEntryForScreen(QStringLiteral("DP-3"), 1);
        QCOMPARE(revived.mode, PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(revived.tilingAlgorithm, QStringLiteral("wide"));
    }

    // The restore must not sweep up a context the user genuinely chose Snapping
    // for. Only an entry still carrying a tilingAlgorithm — the marker a disable
    // leaves behind — is revived.
    void testRestoreAutotileAssignments_leavesGenuineSnappingContextsAlone()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("SnapOnly"));
        mgr->addLayout(layout);
        mgr->assignLayout(QStringLiteral("DP-2"), 1, QString(), layout);

        const auto before = mgr->assignmentEntryForScreen(QStringLiteral("DP-2"), 1);
        QCOMPARE(before.mode, PhosphorZones::AssignmentEntry::Snapping);
        QVERIFY2(before.tilingAlgorithm.isEmpty(), "a never-autotiled context must carry no algorithm");

        QCOMPARE(mgr->restoreAutotileAssignments(), 0);
        QCOMPARE(mgr->assignmentEntryForScreen(QStringLiteral("DP-2"), 1).mode,
                 PhosphorZones::AssignmentEntry::Snapping);
    }
};

QTEST_MAIN(TestLayoutManagerAssignment)
#include "test_layoutmanager_assignment.moc"
