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
#include "core/constants.h"
#include "../helpers/StubSettings.h"
#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/LayoutRegistryTestHelpers.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestLayoutManagerAssignment : public QObject
{
    Q_OBJECT

private:
    PhosphorZones::Layout* createTestLayout(const QString& name, QObject* parent = nullptr)
    {
        auto* layout = new PhosphorZones::Layout(name, parent);
        auto* zone = new PhosphorZones::Zone();
        zone->setRelativeGeometry(QRectF(0, 0, 1, 1));
        layout->addZone(zone);
        return layout;
    }

    PhosphorZones::LayoutRegistry* createManager(QObject* parent = nullptr)
    {
        m_guards.emplace_back(std::make_unique<IsolatedConfigGuard>());
        auto* mgr = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"), parent);
        QString layoutDir = m_guards.back()->dataPath() + QStringLiteral("/plasmazones/layouts");
        QDir().mkpath(layoutDir);
        mgr->setLayoutDirectory(layoutDir);
        return mgr;
    }

    /// Author a per-context DefaultLayoutAssignment override rule into the
    /// registry's owned rule store. @p allow true forces the synthesized default
    /// through for the context; false suppresses it. Mirrors the shape
    /// ContextRuleBridge::makeDisableRule produces for the per-mode disable case.
    void addDefaultAssignmentRule(PhosphorZones::LayoutRegistry* mgr, const QString& screenId, int virtualDesktop,
                                  const QString& activity, bool allow)
    {
        namespace PWR = PhosphorRules;
        auto* store = mgr->findChild<PWR::RuleStore*>();
        QVERIFY(store != nullptr);

        PWR::Rule rule;
        rule.id = QUuid::createUuid();
        rule.name = QStringLiteral("test-default-assignment");
        rule.enabled = true;
        rule.priority =
            PWR::ContextRuleBridge::contextPriority(!screenId.isEmpty(), virtualDesktop > 0, !activity.isEmpty());
        rule.match = PWR::ContextRuleBridge::makeContextMatch(screenId, virtualDesktop, activity);

        PWR::RuleAction action;
        action.type = QString(PWR::ActionType::DefaultLayoutAssignment);
        action.params.insert(PWR::ActionParam::Value, allow);
        rule.actions.append(action);

        QVERIFY(store->addRule(rule));
    }

    /// Author a PINNED engine-mode assignment rule (no layout) — the shape a user
    /// gets from "set this monitor to snapping/autotile" without picking a layout.
    void addEngineModeRule(PhosphorZones::LayoutRegistry* mgr, const QString& screenId, int virtualDesktop,
                           const QString& activity, const QString& modeToken)
    {
        namespace PWR = PhosphorRules;
        auto* store = mgr->findChild<PWR::RuleStore*>();
        QVERIFY(store != nullptr);
        const PWR::Rule rule = PWR::ContextRuleBridge::makeAssignmentRule(
            QStringLiteral("test-mode"), screenId, virtualDesktop, activity, modeToken, QString(), QString());
        QVERIFY(store->addRule(rule));
    }

    std::vector<std::unique_ptr<IsolatedConfigGuard>> m_guards;

private Q_SLOTS:

    void cleanup()
    {
        m_guards.clear();
    }

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

        // Cascade-resolution check: the rebuilt Combined rule must resolve
        // through the live cascade at its (screen, desktop, activity) tuple.
        // A regression that mis-builds the rule with a transposed-arg
        // signature would still produce a hash-equal projection (because
        // the reader re-reads from the same wrong tuple); this assertion
        // catches that class of bug.
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 3, QStringLiteral("work"))->name(),
                 QStringLiteral("LayoutA"));

        // The pure-Activity rule survives untouched.
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

    // ═══════════════════════════════════════════════════════════════════════════
    // Level-1 cascade default — symmetric snap + autotile providers (issue #368)
    //
    // The level-1 (global) tier of the assignment hierarchy is two pass-through
    // providers: setDefaultLayoutIdProvider (snap UUID) and
    // setDefaultAutotileAlgorithmProvider (autotile algorithm). Composition
    // roots gate each on its own enabled flag; the library decides precedence
    // (snap > autotile) so the daemon stays free of mode-priority logic. Pin
    // the four enabled-flag combinations and the cascade-takes-precedence rule.
    // ═══════════════════════════════════════════════════════════════════════════

    void testLevel1Default_autotileOnly_synthesizesAutotile()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        // Snap disabled (provider returns empty), autotile provider returns
        // an algorithm — the cascade should resolve autotile.
        mgr->setDefaultLayoutIdProvider([]() {
            return QString();
        });
        mgr->setDefaultAutotileAlgorithmProvider([]() {
            return QStringLiteral("bsp");
        });

        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(entry.tilingAlgorithm, QStringLiteral("bsp"));
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), QStringLiteral("autotile:bsp"));
    }

    void testLevel1Default_snapOnly_synthesizesSnap()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Snap"));
        mgr->addLayout(layout);

        const QString layoutId = layout->id().toString();
        mgr->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });
        mgr->setDefaultAutotileAlgorithmProvider([]() {
            return QString();
        });

        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry.snappingLayout, layoutId);
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), layoutId);
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 4), layout);
    }

    void testLevel1Default_bothEmpty_returnsNoEntry()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        // Both providers return empty — neither snap nor autotile is the
        // user's active default. Cascade should treat as "no assignment".
        mgr->setDefaultLayoutIdProvider([]() {
            return QString();
        });
        mgr->setDefaultAutotileAlgorithmProvider([]() {
            return QString();
        });

        QVERIFY(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4).isEmpty());
        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4);
        QVERIFY(!entry.isValid());
    }

    void testLevel1Default_bothSet_snapWinsByLibraryPrecedence()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Snap"));
        mgr->addLayout(layout);

        const QString layoutId = layout->id().toString();
        // Both providers return non-empty — library precedence picks snap.
        // Composition roots that wire both provider lambdas without
        // gating on enabled flags will see snap consistently.
        mgr->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });
        mgr->setDefaultAutotileAlgorithmProvider([]() {
            return QStringLiteral("bsp");
        });

        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry.snappingLayout, layoutId);
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), layoutId);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Suppress default layout assignment — the global gate + per-context override.
    //
    // The global setDefaultAssignmentSuppressedProvider gate makes an unassigned
    // context resolve to NO active layout (the same empty-entry state as having no
    // providers). A per-context DefaultLayoutAssignment rule overrides the global
    // baseline either way: false suppresses one context even when global allows,
    // true forces the default through even when global suppresses. Explicit
    // assignments are never affected — suppression only gates the synthesized
    // default.
    // ═══════════════════════════════════════════════════════════════════════════

    void testSuppressDefault_globalOn_suppressesSynthesizedDefault()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Snap"));
        mgr->addLayout(layout);

        const QString layoutId = layout->id().toString();
        mgr->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });
        // Global suppress on — the snap default must NOT be synthesized.
        mgr->setDefaultAssignmentSuppressedProvider([]() {
            return true;
        });

        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4);
        QVERIFY(!entry.isValid());
        QVERIFY(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4).isEmpty());
    }

    void testSuppressDefault_globalOff_synthesizesAsBefore()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Snap"));
        mgr->addLayout(layout);

        const QString layoutId = layout->id().toString();
        mgr->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });
        // Provider wired but reports off — behaviour is unchanged from today.
        mgr->setDefaultAssignmentSuppressedProvider([]() {
            return false;
        });

        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4);
        QCOMPARE(entry.snappingLayout, layoutId);
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), layoutId);
    }

    void testSuppressDefault_perContextSuppress_overridesGlobalAllow()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Snap"));
        mgr->addLayout(layout);

        const QString layoutId = layout->id().toString();
        mgr->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });
        // Global allows the default (off). A per-context rule suppresses DP-1.
        mgr->setDefaultAssignmentSuppressedProvider([]() {
            return false;
        });
        addDefaultAssignmentRule(mgr.data(), QStringLiteral("DP-1"), 0, QString(), /*allow=*/false);

        // DP-1 is suppressed by its rule.
        QVERIFY(!mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4).isValid());
        QVERIFY(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4).isEmpty());
        // DP-2 (no rule) still gets the global default.
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-2"), 4), layoutId);
    }

    void testSuppressDefault_perContextAllow_overridesGlobalSuppress()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Snap"));
        mgr->addLayout(layout);

        const QString layoutId = layout->id().toString();
        mgr->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });
        // Global suppresses everywhere. A per-context rule re-enables DP-1.
        mgr->setDefaultAssignmentSuppressedProvider([]() {
            return true;
        });
        addDefaultAssignmentRule(mgr.data(), QStringLiteral("DP-1"), 0, QString(), /*allow=*/true);

        // DP-1 forces the default through.
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), layoutId);
        // DP-2 (no rule) stays suppressed by the global setting.
        QVERIFY(mgr->assignmentIdForScreen(QStringLiteral("DP-2"), 4).isEmpty());
    }

    void testSuppressDefault_explicitAssignmentUnaffected()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Manual"));
        mgr->addLayout(layout);

        // Global suppress on, but the user has an explicit assignment on DP-1.
        mgr->setDefaultAssignmentSuppressedProvider([]() {
            return true;
        });
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layout);

        // The explicit assignment wins — suppression only gates the synthesized
        // default, never a stored assignment.
        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 0);
        QCOMPARE(entry.snappingLayout, layout->id().toString());
        QCOMPARE(entry.activeLayoutId(), layout->id().toString());
    }

    // isContextActiveLayoutSuppressed — the daemon's "no active layout because of
    // suppression" gate. Must fire ONLY when suppression is the cause, never for
    // other empty-assignment states (e.g. snapping enabled with no default id),
    // so the daemon's defaultLayout() fallbacks are preserved there.

    void testIsContextSuppressed_globalOn_unassigned_true()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Snap"));
        mgr->addLayout(layout);
        const QString layoutId = layout->id().toString();
        mgr->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });
        mgr->setDefaultAssignmentSuppressedProvider([]() {
            return true;
        });
        QVERIFY(mgr->isContextActiveLayoutSuppressed(QStringLiteral("DP-1"), 4));
    }

    void testIsContextSuppressed_globalOff_noDefault_false()
    {
        // Regression guard: no providers wired (no default configured) is NOT
        // suppression — the daemon must keep its defaultLayout() fallback here.
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        mgr->setDefaultAssignmentSuppressedProvider([]() {
            return false;
        });
        QVERIFY(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4).isEmpty());
        QVERIFY(!mgr->isContextActiveLayoutSuppressed(QStringLiteral("DP-1"), 4));
    }

    void testIsContextSuppressed_explicitAssignment_false()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Manual"));
        mgr->addLayout(layout);
        mgr->setDefaultAssignmentSuppressedProvider([]() {
            return true;
        });
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layout);
        QVERIFY(!mgr->isContextActiveLayoutSuppressed(QStringLiteral("DP-1"), 0));
    }

    void testIsContextSuppressed_perContextOverrides()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Snap"));
        mgr->addLayout(layout);
        const QString layoutId = layout->id().toString();
        mgr->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });
        // Global off, but a per-context suppress rule on DP-1.
        mgr->setDefaultAssignmentSuppressedProvider([]() {
            return false;
        });
        addDefaultAssignmentRule(mgr.data(), QStringLiteral("DP-1"), 0, QString(), /*allow=*/false);
        QVERIFY(mgr->isContextActiveLayoutSuppressed(QStringLiteral("DP-1"), 4));
        // DP-2 (no rule) follows the global off → not suppressed.
        QVERIFY(!mgr->isContextActiveLayoutSuppressed(QStringLiteral("DP-2"), 4));
    }

    void testIsContextSuppressed_pinnedModeOnlyRule_overridesGlobalSuppress()
    {
        // A rule that sets only the engine mode (no layout) on a monitor
        // must override the global suppress setting — the context stays active so
        // the overlay / zone selector show and the layout falls back to default.
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Snap"));
        mgr->addLayout(layout);
        mgr->setDefaultAssignmentSuppressedProvider([]() {
            return true;
        });
        addEngineModeRule(mgr.data(), QStringLiteral("DP-1"), 2, QString(), QStringLiteral("snapping"));
        // DP-1 has a pinned mode rule → not suppressed despite global suppress.
        QVERIFY(!mgr->isContextActiveLayoutSuppressed(QStringLiteral("DP-1"), 2));
        // DP-2 (no rule) → still suppressed by the global setting.
        QVERIFY(mgr->isContextActiveLayoutSuppressed(QStringLiteral("DP-2"), 2));
    }

    void testDefaultSuppressedForContext_bareAutotileModeRule_stillSuppressed()
    {
        // The autotile gate's primitive. A mode-only autotile rule sets the mode
        // but draws its algorithm from the suppressed global default. So the
        // context is NOT "active-layout-suppressed" (the mode rule covers it),
        // yet its DEFAULT *is* suppressed — which is what the autotile activation
        // gate checks to refuse tiling a bare "autotile:" context.
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        mgr->setDefaultAssignmentSuppressedProvider([]() {
            return true;
        });
        addEngineModeRule(mgr.data(), QStringLiteral("DP-1"), 2, QString(), QStringLiteral("autotile"));
        // The mode rule covers the context → not active-layout-suppressed.
        QVERIFY(!mgr->isContextActiveLayoutSuppressed(QStringLiteral("DP-1"), 2));
        // But the synthesized default IS suppressed → bare autotile must not tile.
        QVERIFY(mgr->isDefaultAssignmentSuppressedForContext(QStringLiteral("DP-1"), 2));
        // A per-context allow override flips it back on.
        addDefaultAssignmentRule(mgr.data(), QStringLiteral("DP-1"), 2, QString(), /*allow=*/true);
        QVERIFY(!mgr->isDefaultAssignmentSuppressedForContext(QStringLiteral("DP-1"), 2));
    }

    void testLevel1Default_storedEntryTakesPrecedence()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("PerDesktop"));
        mgr->addLayout(layout);

        // Autotile is the global default, but desktop 1 has an explicit
        // snap assignment — explicit cascade entries always win.
        mgr->setDefaultAutotileAlgorithmProvider([]() {
            return QStringLiteral("bsp");
        });
        mgr->assignLayout(QStringLiteral("DP-1"), 1, QString(), layout);

        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 1);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry.snappingLayout, layout->id().toString());

        // Other desktops still get the synthesized autotile default.
        const auto desk2 = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 2);
        QCOMPARE(desk2.mode, PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(desk2.tilingAlgorithm, QStringLiteral("bsp"));
    }

    void testLevel1Default_noProviders_preservesPre368Behaviour()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        // No providers set at all — cascade miss returns default-constructed
        // entry, matching the historical behaviour callers rely on.
        QVERIFY(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4).isEmpty());
        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4);
        QVERIFY(!entry.isValid());
    }

    void testLevel1Default_snapWithUnknownUuid_layoutForScreenFallsThrough()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* registered = createTestLayout(QStringLiteral("Registered"));
        mgr->addLayout(registered);

        // Snap provider returns a UUID that's NOT in the registry — a stale
        // defaultLayoutId from settings. layoutForScreen must still resolve
        // to a real Layout* (defaultLayout falls back to first by defaultOrder).
        const QString bogusId = QUuid::createUuid().toString();
        mgr->setDefaultLayoutIdProvider([bogusId]() {
            return bogusId;
        });

        // assignmentIdForScreen surfaces the raw stored string — KCM/UI
        // can warn or clear it.
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), bogusId);
        QCOMPARE(mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4).snappingLayout, bogusId);
        // layoutForScreen must not return nullptr.
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 4), registered);
    }

    void testLevel1Default_autotileWithEmptyAlgorithm_treatedAsNoAutotile()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        // Provider returns empty algorithm — composition root would only
        // do this if autotile is disabled, so the cascade should treat as
        // "no level-1 autotile default" and fall through to no entry.
        mgr->setDefaultAutotileAlgorithmProvider([]() {
            return QString();
        });

        QVERIFY(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4).isEmpty());
        QVERIFY(!mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4).isValid());
    }

    void testLevel1Default_cascadeHitSkipsProviders()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Stored"));
        mgr->addLayout(layout);
        mgr->assignLayout(QStringLiteral("DP-1"), 1, QString(), layout);

        auto snapCalls = std::make_shared<int>(0);
        auto autoCalls = std::make_shared<int>(0);
        mgr->setDefaultLayoutIdProvider([snapCalls]() {
            ++(*snapCalls);
            return QString(); // empty so autotile would be consulted
        });
        mgr->setDefaultAutotileAlgorithmProvider([autoCalls]() {
            ++(*autoCalls);
            return QStringLiteral("bsp");
        });

        // Exact-key cascade hit — neither provider should be invoked.
        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 1);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(*snapCalls, 0);
        QCOMPARE(*autoCalls, 0);

        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 1), layout->id().toString());
        QCOMPARE(*snapCalls, 0);
        QCOMPARE(*autoCalls, 0);

        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 1), layout);
        QCOMPARE(*snapCalls, 0);
        QCOMPARE(*autoCalls, 0);

        // Cascade miss on a different desktop — both providers consulted in
        // priority order: snap first (returns empty), then autotile (wins).
        (void)mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 2);
        QCOMPARE(*snapCalls, 1);
        QCOMPARE(*autoCalls, 1);
    }

    void testLevel1Default_replaceProviderReplacesBehaviour()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        mgr->setDefaultAutotileAlgorithmProvider([]() {
            return QStringLiteral("bsp");
        });
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), QStringLiteral("autotile:bsp"));

        // Replace — second value wins, not stacks.
        mgr->setDefaultAutotileAlgorithmProvider([]() {
            return QStringLiteral("dwindle");
        });
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), QStringLiteral("autotile:dwindle"));

        // Clearing restores pre-368 cascade behaviour.
        mgr->setDefaultAutotileAlgorithmProvider({});
        QVERIFY(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4).isEmpty());
    }

    // The daemon's snap provider returns m_settings->defaultLayoutId() directly
    // when snappingEnabled is true. If defaultLayoutId() is empty (e.g. a
    // user has snap on but never picked a default layout), the snap provider
    // returns empty and the cascade falls through to the autotile branch. The
    // user explicitly enabled snap, but they get autotile on unconfigured
    // contexts. This pins that behaviour so anyone changing it has to
    // explicitly decide whether the silent mode-swap is desirable; a future
    // tightening (snap mode with empty layout treated as a sentinel "snap, no
    // zones") would update this expectation.
    void testLevel1Default_snapEnabledEmptyId_autotileEnabled_autotileWins()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        mgr->setDefaultLayoutIdProvider([]() {
            return QString();
        });
        mgr->setDefaultAutotileAlgorithmProvider([]() {
            return QStringLiteral("bsp");
        });

        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(entry.tilingAlgorithm, QStringLiteral("bsp"));
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), QStringLiteral("autotile:bsp"));
    }

    // Cascade level-2 (per-screen base entry, key = (screen, 0, "")) must
    // hide level-1 providers. Without this, a per-screen base assignment
    // would be silently shadowed by the user's global default whenever the
    // queried desktop didn't have its own explicit entry. Companion to
    // testLevel1Default_cascadeHitSkipsProviders, which covers the level-3
    // exact-key case; this one covers the more interesting level-2 case.
    void testLevel1Default_perScreenBaseEntryHidesProviders()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* baseLayout = createTestLayout(QStringLiteral("PerScreenBase"));
        mgr->addLayout(baseLayout);

        // Per-screen base entry on DP-1 (no per-desktop entries).
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), baseLayout);

        // Both providers wired with non-empty values that would otherwise
        // win at level-1 if the cascade missed.
        auto snapCalls = std::make_shared<int>(0);
        auto autoCalls = std::make_shared<int>(0);
        mgr->setDefaultLayoutIdProvider([snapCalls]() {
            ++(*snapCalls);
            return QStringLiteral("{ffffffff-ffff-ffff-ffff-ffffffffffff}");
        });
        mgr->setDefaultAutotileAlgorithmProvider([autoCalls]() {
            ++(*autoCalls);
            return QStringLiteral("bsp");
        });

        // Querying any desktop on DP-1 should resolve to the per-screen
        // base entry — the cascade hits at level-2 and providers are not
        // consulted. Pinning the call counter at 0 catches future
        // refactors that would re-order resolution.
        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 5);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry.snappingLayout, baseLayout->id().toString());
        QCOMPARE(*snapCalls, 0);
        QCOMPARE(*autoCalls, 0);

        // Same query via the id-only and Layout* paths, same expectation.
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 5), baseLayout->id().toString());
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 5), baseLayout);
        QCOMPARE(*snapCalls, 0);
        QCOMPARE(*autoCalls, 0);

        // A different screen with no entry DOES fall through to providers.
        (void)mgr->assignmentEntryForScreen(QStringLiteral("HDMI-2"), 5);
        QCOMPARE(*snapCalls, 1);
        // Snap provider returned non-empty (bogus uuid), so autotile not consulted.
        QCOMPARE(*autoCalls, 0);
    }

    // hasExplicitAssignment must distinguish "stored" from "synthesized
    // fallback". This is the building block consumers like the D-Bus
    // getAllScreenAssignments JSON readback rely on to avoid round-tripping
    // the synthesized default back into stored state (which would shadow
    // future global-default changes). The KCM JSON gate added in
    // src/dbus/layoutadaptor/assignment.cpp is correct only as long as
    // this invariant holds — pin it here so a future refactor that
    // accidentally makes hasExplicitAssignment provider-aware would fail
    // this test.
    void testLevel1Default_hasExplicitAssignmentIgnoresSynth()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        mgr->setDefaultAutotileAlgorithmProvider([]() {
            return QStringLiteral("bsp");
        });

        // Cascade miss returns a synthesized entry…
        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 0);
        QVERIFY(entry.isValid());
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Autotile);

        // …but hasExplicitAssignment correctly reports "no stored entry".
        QVERIFY(!mgr->hasExplicitAssignment(QStringLiteral("DP-1"), 0, QString()));
        QVERIFY(!mgr->hasExplicitAssignment(QStringLiteral("DP-1"), 5, QString()));

        // After an explicit assignment, hasExplicitAssignment flips true
        // for that exact key only.
        auto* layout = createTestLayout(QStringLiteral("Stored"));
        mgr->addLayout(layout);
        mgr->assignLayout(QStringLiteral("DP-1"), 1, QString(), layout);

        QVERIFY(mgr->hasExplicitAssignment(QStringLiteral("DP-1"), 1, QString()));
        // Sibling desktop is still synth-only, not explicit.
        QVERIFY(!mgr->hasExplicitAssignment(QStringLiteral("DP-1"), 2, QString()));
        // Different screen is also still synth-only.
        QVERIFY(!mgr->hasExplicitAssignment(QStringLiteral("HDMI-2"), 1, QString()));
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
