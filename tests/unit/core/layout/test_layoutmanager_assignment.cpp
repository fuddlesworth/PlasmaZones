// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layoutmanager_assignment.cpp
 * @brief Unit tests for PhosphorZones::LayoutRegistry fallback cascade, default layout, quick slots
 *
 * The AssignmentEntry-shape half (the "scrolling:" sentinel, the config round
 * trip, the fromLayoutId factory and the LayoutAssignmentKey parser) lives in
 * the sibling test_layoutmanager_assignment_entry.cpp, split off at the P6
 * banner when this file passed the 1150-line ceiling. Both share
 * LayoutManagerAssignmentFixture.
 */

#include <QTest>
#include <QDir>
#include <QScopedPointer>
#include <QUuid>

#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "helpers/StubSettings.h"
#include "LayoutManagerAssignmentFixture.h"

using namespace PlasmaZones;

class TestLayoutManagerAssignment : public LayoutManagerAssignmentFixture
{
    Q_OBJECT

private:
    /// The projection readers return full AssignmentEntry values; the batch
    /// setters accept id strings. This maps a projection back to the setter
    /// shape (activeLayoutId per key) for the round-trip legs.
    template<typename K>
    static QHash<K, QString> assignmentIds(const QHash<K, PhosphorZones::AssignmentEntry>& entries)
    {
        QHash<K, QString> out;
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            out.insert(it.key(), it.value().activeLayoutId());
        }
        return out;
    }

private Q_SLOTS:

    // ─── Mixed-rule survival through the assignment paths ────────────────
    //
    // A context assignment rule the user also edited in the Rules editor —
    // a name, managed=true, and a non-assignment action (SetOpacity) — must
    // survive every assignment write shape: upsert, batch rebuild, the
    // clear's STRIP path, and a re-assign after the strip. Every rebuild
    // routes through makeAssignmentRule, which emits only the assignment
    // slot actions, so each path must carry the rest across
    // (carryOverNonAssignmentActions) or the user's edits silently die.
    void testMixedRuleSurvivesAssignmentRoundTrips()
    {
        namespace PWR = PhosphorRules;
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layoutA = createTestLayout(QStringLiteral("LayoutA"));
        mgr->addLayout(layoutA);
        auto* layoutB = createTestLayout(QStringLiteral("LayoutB"));
        mgr->addLayout(layoutB);
        auto* store = mgr->findChild<PWR::RuleStore*>();
        QVERIFY(store != nullptr);

        // Seed the assignment, then edit it the way the Rules editor can.
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layoutA);
        const QUuid ruleId = PWR::ContextRuleBridge::assignmentRuleIdFor(QStringLiteral("DP-1"), 0, QString());
        std::optional<PWR::Rule> seeded = store->ruleSet().ruleById(ruleId);
        QVERIFY(seeded.has_value());
        PWR::RuleAction opacity;
        opacity.type = QString(PWR::ActionType::SetOpacity);
        opacity.params.insert(QString(PWR::ActionParam::Value), 0.9);
        seeded->actions.append(opacity);
        seeded->name = QStringLiteral("Work monitor");
        seeded->managed = true;
        QVERIFY(store->updateRule(*seeded));

        const auto ruleHasOpacity = [&](const std::optional<PWR::Rule>& r) {
            if (!r) {
                return false;
            }
            for (const PWR::RuleAction& a : r->actions) {
                if (a.type == QLatin1String(PWR::ActionType::SetOpacity)) {
                    return true;
                }
            }
            return false;
        };

        // UPSERT: change the layout on the Monitors page.
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layoutB);
        std::optional<PWR::Rule> afterUpsert = store->ruleSet().ruleById(ruleId);
        QVERIFY(afterUpsert.has_value());
        QVERIFY2(ruleHasOpacity(afterUpsert), "upsert must carry the non-assignment action across");
        QCOMPARE(afterUpsert->name, QStringLiteral("Work monitor"));
        QVERIFY(afterUpsert->managed);

        // BATCH: an "apply all" over the monitor axis.
        QHash<QString, QString> screenMap;
        screenMap.insert(QStringLiteral("DP-1"), layoutA->id().toString());
        mgr->setAllScreenAssignments(screenMap);
        std::optional<PWR::Rule> afterBatch = store->ruleSet().ruleById(ruleId);
        QVERIFY(afterBatch.has_value());
        QVERIFY2(ruleHasOpacity(afterBatch), "the batch rebuild must carry the non-assignment action across");
        QCOMPARE(afterBatch->name, QStringLiteral("Work monitor"));
        QVERIFY2(afterBatch->managed, "the batch rebuild must not clear the managed flag");

        // CLEAR: strips the assignment slots, keeps the rule alive for the
        // opacity it still carries.
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), nullptr);
        QVERIFY(!mgr->hasExplicitAssignment(QStringLiteral("DP-1"), 0, QString()));
        std::optional<PWR::Rule> afterClear = store->ruleSet().ruleById(ruleId);
        QVERIFY2(afterClear.has_value(), "clearing the assignment must not delete a rule carrying other actions");
        QVERIFY(ruleHasOpacity(afterClear));
        for (const PWR::RuleAction& a : afterClear->actions) {
            QVERIFY2(a.type != QLatin1String(PWR::ActionType::SetEngineMode)
                         && a.type != QLatin1String(PWR::ActionType::SetSnappingLayout)
                         && a.type != QLatin1String(PWR::ActionType::SetTilingAlgorithm)
                         && a.type != QLatin1String(PWR::ActionType::SetScrollingTemplate),
                     "the clear must strip all four assignment slots");
        }

        // RE-ASSIGN after the strip: the deterministic id is occupied by the
        // stripped rule, so the write must merge onto it, not silently no-op
        // on an addRule id collision.
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layoutA);
        QVERIFY2(mgr->hasExplicitAssignment(QStringLiteral("DP-1"), 0, QString()),
                 "re-assigning after a strip must take effect, not no-op on the id collision");
        std::optional<PWR::Rule> reassigned = store->ruleSet().ruleById(ruleId);
        QVERIFY(reassigned.has_value());
        QVERIFY(ruleHasOpacity(reassigned));

        // CONTROL: a PURE assignment rule is deleted outright on clear.
        mgr->assignLayout(QStringLiteral("DP-2"), 0, QString(), layoutA);
        const QUuid pureId = PWR::ContextRuleBridge::assignmentRuleIdFor(QStringLiteral("DP-2"), 0, QString());
        QVERIFY(store->ruleSet().ruleById(pureId).has_value());
        mgr->assignLayout(QStringLiteral("DP-2"), 0, QString(), nullptr);
        QVERIFY2(!store->ruleSet().ruleById(pureId).has_value(),
                 "a rule whose only actions were the assignment slots is deleted outright");
    }

    // ─── Combined batch API ──────────────────────────────────────────────
    //
    // setAllCombinedAssignments / combinedAssignments are the triple-axis
    // sibling of the Desktop / Activity batches. Pin the round-trip: a
    // Combined rule survives, and stays isolated from pure-Activity,
    // pure-Desktop and Monitor rules. (Enabled-flag preservation is not
    // asserted here — see the NOTE below this test for why.)
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

        // Reader returns only the Combined rule, as a full entry.
        const auto combined = mgr->combinedAssignments();
        QCOMPARE(combined.size(), 1);
        PhosphorZones::CombinedAssignmentKey key{QStringLiteral("DP-1"), 3, QStringLiteral("work")};
        QVERIFY(combined.contains(key));
        QCOMPARE(combined.value(key).activeLayoutId(), layoutA->id().toString());

        // Round-trip: re-assign the projected ids → state must be identical.
        mgr->setAllCombinedAssignments(assignmentIds(combined));
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

        // Round-trip the projected ids back through setAllActivityAssignments.
        // The Combined rule must still be reachable on desktop 3, untouched
        // by the Activity batch family classifier.
        mgr->setAllActivityAssignments(assignmentIds(projection));
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
        const auto scrolling = PhosphorZones::AssignmentEntry::Scrolling;

        // Same slot number in each mode holds an independent binding: a manual
        // layout UUID for snapping, an autotile algorithm ID for tiling.
        mgr->setQuickLayoutSlot(snapping, 1, layoutId);
        mgr->setQuickLayoutSlot(autotile, 1, QStringLiteral("autotile:bsp"));

        QCOMPARE(mgr->quickLayoutSlots(snapping).value(1), layoutId);
        QCOMPARE(mgr->quickLayoutSlots(autotile).value(1), QStringLiteral("autotile:bsp"));

        // Scrolling owns its OWN slot array since the native-template pivot:
        // its slots hold ScrollingTemplate ids, so a scrolling write never
        // lands in the snapping array and vice versa. The "scrolling:"
        // sentinel is not a bindable slot value (it names no template) and
        // its rejection must not disturb any array.
        auto* store = attachTemplateStore(mgr.get());
        const QUuid templId = createTestTemplate(store, QStringLiteral("SlotTemplate"));
        mgr->setQuickLayoutSlot(scrolling, 1, QStringLiteral("scrolling:"));
        QVERIFY(!mgr->quickLayoutSlots(scrolling).contains(1));
        mgr->setQuickLayoutSlot(scrolling, 1, templId.toString());
        QCOMPARE(mgr->quickLayoutSlots(scrolling).value(1), templId.toString());
        // Scrolling slots resolve no Layout* (template ids have none).
        QCOMPARE(mgr->layoutForShortcut(scrolling, 1), nullptr);
        QCOMPARE(mgr->quickLayoutSlots(snapping).value(1), layoutId);
        QCOMPARE(mgr->quickLayoutSlots(autotile).value(1), QStringLiteral("autotile:bsp"));
        // A manual-layout uuid is refused in a scrolling slot (unknown to
        // the template store), leaving the existing binding untouched.
        mgr->setQuickLayoutSlot(scrolling, 1, layoutId);
        QCOMPARE(mgr->quickLayoutSlots(scrolling).value(1), templId.toString());

        // Clearing one mode's slot leaves the other modes untouched.
        mgr->setQuickLayoutSlot(snapping, 1, QString());
        QVERIFY(!mgr->quickLayoutSlots(snapping).contains(1));
        QCOMPARE(mgr->quickLayoutSlots(autotile).value(1), QStringLiteral("autotile:bsp"));
        QCOMPARE(mgr->quickLayoutSlots(scrolling).value(1), templId.toString());
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

    void testAssignmentEntry_scrollingTemplate_roundTrip()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* layout = createTestLayout(QStringLiteral("Manual"));
        mgr->addLayout(layout);
        auto* store = attachTemplateStore(mgr.get());
        const QUuid templId = createTestTemplate(store, QStringLiteral("Template"));

        // Seed a snapping assignment, then assign a scrolling template: the
        // mode flips to Scrolling, the template lands in its own field, and
        // the snapping choice survives (lossless-toggle contract).
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layout);
        mgr->assignScrollingTemplate(QStringLiteral("DP-1"), 0, QString(), templId.toString());

        auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 0);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Scrolling);
        QCOMPARE(entry.scrollingTemplateLayout, templId.toString());
        QCOMPARE(entry.snappingLayout, layout->id().toString());
        // The sentinel stays payload-free: rules matching ActiveLayout
        // Equals "scrolling:" must keep working with a template assigned.
        QCOMPARE(entry.activeLayoutId(), QString(PhosphorLayout::LayoutId::ScrollingId));

        QCOMPARE(mgr->scrollingTemplateForContext(QStringLiteral("DP-1"), 0, QString()).id, templId);

        // Toggling back to snapping preserves the template for the return trip.
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layout);
        auto back = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 0);
        QCOMPARE(back.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(back.scrollingTemplateLayout, templId.toString());
        // ...the mode-gated resolver answers invalid for a non-Scrolling
        // context, while the RAW field getter still reads the dormant
        // template — parity with snappingLayoutForScreen returning a
        // preserved layout in autotile mode.
        QVERIFY(!mgr->scrollingTemplateForContext(QStringLiteral("DP-1"), 0, QString()).isValid());
        QCOMPARE(mgr->scrollingTemplateLayoutForScreen(QStringLiteral("DP-1"), 0), templId.toString());
    }

    void testAssignmentEntry_scrollingTemplate_deleteScrubs()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* store = attachTemplateStore(mgr.get());
        const QUuid parsed = createTestTemplate(store, QStringLiteral("Template"));
        const QString templId = parsed.toString();

        mgr->assignScrollingTemplate(QStringLiteral("DP-1"), 0, QString(), templId);
        QCOMPARE(mgr->scrollingTemplateForContext(QStringLiteral("DP-1"), 0, QString()).id, parsed);

        // Deleting the template (store delete + the id-keyed purge the D-Bus
        // delete verb drives) scrubs the SetScrollingTemplate reference; the
        // context stays Scrolling (mode is intent, the template was data).
        QVERIFY(store->removeTemplate(parsed));
        // The purge reports whether it changed anything, and a reference to
        // scrub exists here, so a false return means the scrub never ran and
        // the assertions below would be passing for the wrong reason.
        QVERIFY(mgr->purgeLayoutIdFromAssignments(templId));
        auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 0);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Scrolling);
        QVERIFY(entry.scrollingTemplateLayout.isEmpty());
        // Degrade check: with the reference scrubbed the mode-gated resolver
        // has nothing to answer with, so a Scrolling context reports invalid
        // rather than a stale template.
        QVERIFY(!mgr->scrollingTemplateForContext(QStringLiteral("DP-1"), 0, QString()).isValid());
    }

    void testAssignmentEntry_scrollingTemplate_deleteScrubOnSnappingContext()
    {
        // The template survives a toggle back to Snapping (stored-but-dormant
        // per the lossless contract). Deleting the template layout THERE must
        // scrub the dormant reference while the live snapping assignment
        // survives untouched — the purge's drop test must not misread a
        // Snapping rule that carries a template as droppable.
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* layout = createTestLayout(QStringLiteral("Manual"));
        mgr->addLayout(layout);
        auto* store = attachTemplateStore(mgr.get());
        const QUuid templUuid = createTestTemplate(store, QStringLiteral("Template"));

        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layout);
        mgr->assignScrollingTemplate(QStringLiteral("DP-1"), 0, QString(), templUuid.toString());
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layout); // back to Snapping
        auto seeded = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 0);
        QCOMPARE(seeded.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(seeded.scrollingTemplateLayout, templUuid.toString());

        QVERIFY(store->removeTemplate(templUuid));
        QVERIFY(mgr->purgeLayoutIdFromAssignments(templUuid.toString()));
        auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 0);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QVERIFY(entry.scrollingTemplateLayout.isEmpty());
        QCOMPARE(entry.snappingLayout, layout->id().toString());
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 0, QString()), layout);
    }

    void testApplyQuickLayout_onScrollingScreen_routesToTemplate()
    {
        // A quick-slot press on a Scrolling screen swaps the TEMPLATE and
        // never flips the engine: scrolling slots hold native template ids
        // in their OWN array, and applyQuickLayout's scrolling arm routes
        // through applyScrollingTemplateToScreen.
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* store = attachTemplateStore(mgr.get());
        const QUuid templB = createTestTemplate(store, QStringLiteral("TemplateB"));

        // applyScrollingTemplateToScreen writes to the screen's CURRENT
        // desktop, so the seed and the assertions must resolve the same one.
        const int desktop = mgr->currentVirtualDesktopForScreen(QStringLiteral("DP-1"));
        mgr->assignLayoutById(QStringLiteral("DP-1"), desktop, QString(),
                              QString(PhosphorLayout::LayoutId::ScrollingId));
        QCOMPARE(mgr->modeForScreen(QStringLiteral("DP-1"), desktop), PhosphorZones::AssignmentEntry::Scrolling);

        mgr->setQuickLayoutSlot(PhosphorZones::AssignmentEntry::Scrolling, 1, templB.toString());
        mgr->applyQuickLayout(PhosphorZones::AssignmentEntry::Scrolling, 1, QStringLiteral("DP-1"));

        QCOMPARE(mgr->modeForScreen(QStringLiteral("DP-1"), desktop), PhosphorZones::AssignmentEntry::Scrolling);
        QCOMPARE(mgr->scrollingTemplateForContext(QStringLiteral("DP-1"), desktop, QString()).id, templB);
    }

    void testCycleToNextLayout_onScrollingScreen_stepsTemplate()
    {
        // The library-level cycle steps the native TEMPLATE store on a
        // Scrolling screen (name-sorted order), anchored on the currently
        // assigned template, and leaves the mode alone.
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* store = attachTemplateStore(mgr.get());
        const QUuid templA = createTestTemplate(store, QStringLiteral("TemplateA"));
        const QUuid templB = createTestTemplate(store, QStringLiteral("TemplateB"));

        // The cycle writes to the screen's CURRENT desktop; seed and assert
        // against the same one.
        const int desktop = mgr->currentVirtualDesktopForScreen(QStringLiteral("DP-1"));
        mgr->assignLayoutById(QStringLiteral("DP-1"), desktop, QString(),
                              QString(PhosphorLayout::LayoutId::ScrollingId));
        mgr->assignScrollingTemplate(QStringLiteral("DP-1"), desktop, QString(), templA.toString());

        mgr->cycleToNextLayout(QStringLiteral("DP-1"));
        QCOMPARE(mgr->modeForScreen(QStringLiteral("DP-1"), desktop), PhosphorZones::AssignmentEntry::Scrolling);
        QCOMPARE(mgr->scrollingTemplateForContext(QStringLiteral("DP-1"), desktop, QString()).id, templB);

        mgr->cycleToNextLayout(QStringLiteral("DP-1"));
        QCOMPARE(mgr->scrollingTemplateForContext(QStringLiteral("DP-1"), desktop, QString()).id, templA);
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

        // Both cascade paths must agree that this entry routes as Autotile.
        QCOMPARE(mgr->modeForScreen(QStringLiteral("DP-1"), 0), PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 0), QStringLiteral("autotile:"));

        auto roundTrip = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 0);
        QCOMPARE(roundTrip.mode, PhosphorZones::AssignmentEntry::Autotile);
        QVERIFY(roundTrip.tilingAlgorithm.isEmpty());
        QVERIFY(roundTrip.snappingLayout.isEmpty());
    }

    void testAssignmentEntry_modeOnlySnapping_explicitPinHolds()
    {
        // The Monitors-page mode-only Snapping pin (staged when leaving
        // Scrolling while the context suppresses the default layout) must
        // be cascade-VISIBLE despite its empty activeLayoutId. The
        // discriminator: an autotile-flavoured default tier — without the
        // hasExplicitSnappingModePin carve-out the empty-id rejection falls
        // through to the default and reports Autotile.
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        mgr->setDefaultAutotileAlgorithmProvider([] {
            return QStringLiteral("bsp");
        });
        mgr->setSnappingPreferredProvider([] {
            return false;
        });

        PhosphorZones::AssignmentEntry modeOnly;
        modeOnly.mode = PhosphorZones::AssignmentEntry::Snapping;
        mgr->setAssignmentEntryDirect(QStringLiteral("DP-1"), 0, QString(), modeOnly);
        QVERIFY(modeOnly.activeLayoutId().isEmpty());

        // BOTH cascade APIs settle on the explicit pin, consistently:
        // Snapping mode, and an EMPTY id (there is no layout identity to
        // report — never the default tier's autotile id).
        QCOMPARE(mgr->modeForScreen(QStringLiteral("DP-1"), 0), PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 0), QString());
    }

    void testAssignmentEntry_modeOnlySnapping_disabledRuleFallsThrough()
    {
        // A DISABLED exact-context rule is not an explicit pin. This pins
        // the FIRST guard layer: the evaluator skips disabled rules, so the
        // resolution falls through to the default tier before the pin
        // predicate is ever consulted. (hasExplicitSnappingModePin's own
        // `exact->enabled` conjunct is a second, belt-and-braces layer —
        // see its definition comment; it has no independently reachable
        // observable here.)
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        mgr->setDefaultAutotileAlgorithmProvider([] {
            return QStringLiteral("bsp");
        });
        mgr->setSnappingPreferredProvider([] {
            return false;
        });

        PhosphorZones::AssignmentEntry modeOnly;
        modeOnly.mode = PhosphorZones::AssignmentEntry::Snapping;
        mgr->setAssignmentEntryDirect(QStringLiteral("DP-1"), 0, QString(), modeOnly);

        auto* store = mgr->findChild<PhosphorRules::RuleStore*>();
        QVERIFY(store);
        const auto rules = store->ruleSet().rules();
        QCOMPARE(rules.size(), 1);
        QVERIFY(store->setRuleEnabled(rules.first().id, false));

        // Layer 1: only the disabled rule exists → default tier (autotile).
        QCOMPARE(mgr->modeForScreen(QStringLiteral("DP-1"), 0), PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 0), QStringLiteral("autotile:bsp"));
    }

    // A mode-only pin has an EMPTY activeLayoutId, and the batch setters
    // receive exactly that empty string on an "apply all" pass. Before the
    // fix, shouldSkipLayoutAssignment's empty-id arm made the driver `continue`
    // — but step 2 has already dropped the whole rule family, so the skip was
    // a DELETE: every mode-only Snapping pin silently evaporated the first
    // time the KCM saved. Pin that an empty id with a prior rule rebuilds from
    // the snapshot, and that an empty id with NO prior rule is still a no-op.
    void testBatch_emptyLayoutId_preservesModeOnlyPin()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        mgr->setDefaultAutotileAlgorithmProvider([] {
            return QStringLiteral("bsp");
        });
        mgr->setSnappingPreferredProvider([] {
            return false;
        });

        // A mode-only Snapping pin on DP-1: mode set, no layout fields.
        PhosphorZones::AssignmentEntry modeOnly;
        modeOnly.mode = PhosphorZones::AssignmentEntry::Snapping;
        mgr->setAssignmentEntryDirect(QStringLiteral("DP-1"), 0, QString(), modeOnly);
        QVERIFY(modeOnly.activeLayoutId().isEmpty());
        QCOMPARE(mgr->modeForScreen(QStringLiteral("DP-1"), 0), PhosphorZones::AssignmentEntry::Snapping);

        // The "apply all" the KCM sends: DP-1 carries the pin's empty id,
        // DP-2 is a context that was never pinned at all.
        QHash<QString, QString> apply;
        apply.insert(QStringLiteral("DP-1"), QString());
        apply.insert(QStringLiteral("DP-2"), QString());
        mgr->setAllScreenAssignments(apply);

        // DP-1's pin survived: still explicit, still Snapping, still empty-id.
        // A regression reports Autotile here (the default tier's mode).
        QVERIFY(mgr->hasExplicitAssignment(QStringLiteral("DP-1"), 0, QString()));
        QCOMPARE(mgr->modeForScreen(QStringLiteral("DP-1"), 0), PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 0), QString());

        // DP-2 had no prior rule, so the empty id created nothing.
        QVERIFY(!mgr->hasExplicitAssignment(QStringLiteral("DP-2"), 0, QString()));
        QCOMPARE(mgr->modeForScreen(QStringLiteral("DP-2"), 0), PhosphorZones::AssignmentEntry::Autotile);
    }

    // The same preservation on the per-desktop batch, driven through the
    // projection reader the KCM actually round-trips (desktopAssignments →
    // setAllDesktopAssignments), so the empty string is produced by the code
    // under test rather than hand-written by the test.
    void testBatch_desktopRoundTrip_preservesModeOnlyPin()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        // An autotile-flavoured default tier, so a lost pin reads back as
        // Autotile rather than coinciding with Snapping by default.
        mgr->setDefaultAutotileAlgorithmProvider([] {
            return QStringLiteral("bsp");
        });
        mgr->setSnappingPreferredProvider([] {
            return false;
        });

        PhosphorZones::AssignmentEntry modeOnly;
        modeOnly.mode = PhosphorZones::AssignmentEntry::Snapping;
        mgr->setAssignmentEntryDirect(QStringLiteral("DP-1"), 4, QString(), modeOnly);

        const auto projection = mgr->desktopAssignments();
        QCOMPARE(projection.size(), 1);
        QCOMPARE(projection.value(qMakePair(QStringLiteral("DP-1"), 4)).activeLayoutId(), QString());

        mgr->setAllDesktopAssignments(assignmentIds(projection));

        QVERIFY(mgr->hasExplicitAssignment(QStringLiteral("DP-1"), 4, QString()));
        QCOMPARE(mgr->modeForScreen(QStringLiteral("DP-1"), 4), PhosphorZones::AssignmentEntry::Snapping);
        // Still exactly one rule — the rebuild replaced it, it did not duplicate.
        QCOMPARE(mgr->desktopAssignments().size(), 1);
    }

    void testProjections_carryScrollingTemplate()
    {
        // The projection readers expose the full entry, so a templated
        // Scrolling context projects the bare sentinel AND its template —
        // the D-Bus getters' {layoutId, scrollingTemplate} value shape.
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        // A real template out of the store, not a manual-layout uuid standing
        // in for one: the production path only ever puts template ids in this
        // slot, and the store is what makes the id resolvable.
        auto* store = attachTemplateStore(mgr.get());
        const QUuid templId = createTestTemplate(store, QStringLiteral("Template"));

        mgr->assignLayoutById(QStringLiteral("DP-1"), 2, QString(), QString(PhosphorLayout::LayoutId::ScrollingId));
        mgr->assignScrollingTemplate(QStringLiteral("DP-1"), 2, QString(), templId.toString());

        const auto projection = mgr->desktopAssignments();
        const auto entry = projection.value(qMakePair(QStringLiteral("DP-1"), 2));
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Scrolling);
        QCOMPARE(entry.activeLayoutId(), QString(PhosphorLayout::LayoutId::ScrollingId));
        QCOMPARE(entry.scrollingTemplateLayout, templId.toString());
    }

    // A LAYOUT-ONLY exact-context rule (no SetEngineMode) is claimed by
    // findExactContextRule's shape fallback but SPARED by the batch driver's
    // family drop, which only removes rules carrying SetEngineMode. The batch
    // must therefore rebuild it under its own id and replace it in place. If
    // the rebuild is appended instead, the surviving original sits at the same
    // priority, wins the list-order tie-break, and a duplicate accumulates on
    // every apply.
    void testBatch_layoutOnlyRule_rebuiltInPlaceNotDuplicated()
    {
        namespace PWR = PhosphorRules;
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layoutA = createTestLayout(QStringLiteral("LayoutA"));
        mgr->addLayout(layoutA);
        auto* layoutB = createTestLayout(QStringLiteral("LayoutB"));
        mgr->addLayout(layoutB);

        auto* store = mgr->findChild<PWR::RuleStore*>();
        QVERIFY(store != nullptr);

        // Hand-author the layout-only rule with a settings-UI style uuid (not
        // the deterministic id), so the shape fallback is the path that finds it.
        PWR::Rule layoutOnly;
        layoutOnly.id = QUuid::createUuid();
        layoutOnly.name = QStringLiteral("test-layout-only");
        layoutOnly.enabled = true;
        layoutOnly.priority = PWR::ContextRuleBridge::kContextBandBase;
        layoutOnly.match = PWR::ContextRuleBridge::makeContextMatch(QStringLiteral("DP-1"), 0, QString());
        PWR::RuleAction setLayout;
        setLayout.type = QString(PWR::ActionType::SetSnappingLayout);
        setLayout.params.insert(PWR::ActionParam::LayoutId, layoutA->id().toString());
        layoutOnly.actions.append(setLayout);
        QVERIFY(store->addRule(layoutOnly));
        QCOMPARE(store->ruleSet().rules().size(), 1);

        QHash<QString, QString> apply;
        apply.insert(QStringLiteral("DP-1"), layoutB->id().toString());

        // Two applies — the duplicate-accumulation shape grows the set each time.
        mgr->setAllScreenAssignments(apply);
        QCOMPARE(store->ruleSet().rules().size(), 1);
        mgr->setAllScreenAssignments(apply);
        QCOMPARE(store->ruleSet().rules().size(), 1);

        // The NEW layout wins; the shadowed original would have resolved LayoutA.
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 0), layoutB->id().toString());
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 0)->name(), QStringLiteral("LayoutB"));
    }
};

QTEST_MAIN(TestLayoutManagerAssignment)
#include "test_layoutmanager_assignment.moc"
