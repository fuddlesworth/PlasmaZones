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
#include "core/constants.h"
#include "../helpers/StubSettings.h"
#include "../helpers/IsolatedConfigGuard.h"

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
        auto* mgr = new PhosphorZones::LayoutRegistry(PlasmaZones::createAssignmentsBackend(),
                                                      QStringLiteral("plasmazones/layouts"), parent);
        QString layoutDir = m_guards.back()->dataPath() + QStringLiteral("/plasmazones/layouts");
        QDir().mkpath(layoutDir);
        mgr->setLayoutDirectory(layoutDir);
        return mgr;
    }

    std::vector<std::unique_ptr<IsolatedConfigGuard>> m_guards;

private Q_SLOTS:

    void cleanup()
    {
        m_guards.clear();
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

    // ═══════════════════════════════════════════════════════════════════════════
    // P2: Quick layout slots
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayoutManager_quickLayoutSlot_validRange_1to9()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* layout = createTestLayout(QStringLiteral("Quick"));
        mgr->addLayout(layout);

        QString layoutId = layout->id().toString();

        mgr->setQuickLayoutSlot(1, layoutId);
        QVERIFY(mgr->quickLayoutSlots().contains(1));

        mgr->setQuickLayoutSlot(9, layoutId);
        QVERIFY(mgr->quickLayoutSlots().contains(9));

        mgr->setQuickLayoutSlot(0, layoutId);
        QVERIFY(!mgr->quickLayoutSlots().contains(0));

        mgr->setQuickLayoutSlot(10, layoutId);
        QVERIFY(!mgr->quickLayoutSlots().contains(10));

        mgr->setQuickLayoutSlot(1, QString());
        QVERIFY(!mgr->quickLayoutSlots().contains(1));
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

        settings->setTestDefaultLayoutId(second->id().toString());
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

        settings->setTestDefaultLayoutId(QUuid::createUuid().toString());
        QCOMPARE(mgr->defaultLayout()->name(), QStringLiteral("OnlyLayout"));

        settings->setTestDefaultLayoutId(QString());
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
        mgr->setQuickLayoutSlot(3, idA);

        // Save
        mgr->saveAssignments();

        // Create a new manager and load — same config file sees the data
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr2(new PhosphorZones::LayoutRegistry(
            PlasmaZones::createAssignmentsBackend(), QStringLiteral("plasmazones/layouts")));
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
        QCOMPARE(mgr2->quickLayoutSlots().value(3), idA);
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
    // Default-assignment-entry provider (issue #368)
    //
    // The cascade falls through to a settings-derived default entry when no
    // stored assignment matches a (screen, desktop, activity) tuple. Pinning
    // the four mode-priority cases (autotile-only, snap-only, both, neither)
    // and the explicit-stored-entry-takes-precedence case so a fresh virtual
    // desktop inherits the user's global mode rather than silently defaulting
    // to whatever defaultLayout() resolves to.
    // ═══════════════════════════════════════════════════════════════════════════

    void testDefaultAssignmentEntryProvider_autotileOnly_synthesizesAutotile()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        mgr->setDefaultAssignmentEntryProvider([]() {
            PhosphorZones::AssignmentEntry e;
            e.mode = PhosphorZones::AssignmentEntry::Autotile;
            e.tilingAlgorithm = QStringLiteral("bsp");
            return e;
        });

        // No stored entry for desktop 4 — cascade falls through to provider.
        auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(entry.tilingAlgorithm, QStringLiteral("bsp"));
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), QStringLiteral("autotile:bsp"));
    }

    void testDefaultAssignmentEntryProvider_snapOnly_synthesizesSnap()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Snap"));
        mgr->addLayout(layout);

        const QString layoutId = layout->id().toString();
        mgr->setDefaultAssignmentEntryProvider([layoutId]() {
            PhosphorZones::AssignmentEntry e;
            e.mode = PhosphorZones::AssignmentEntry::Snapping;
            e.snappingLayout = layoutId;
            return e;
        });

        auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry.snappingLayout, layoutId);
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), layoutId);
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 4), layout);
    }

    void testDefaultAssignmentEntryProvider_neither_returnsNoEntry()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        mgr->setDefaultAssignmentEntryProvider([]() {
            // Neither mode configured — provider returns an empty entry,
            // which the cascade should treat as "no assignment" (matching
            // pre-368 behaviour).
            return PhosphorZones::AssignmentEntry{};
        });

        QVERIFY(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4).isEmpty());
        auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4);
        QVERIFY(!entry.isValid());
    }

    void testDefaultAssignmentEntryProvider_storedEntryTakesPrecedence()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("PerDesktop"));
        mgr->addLayout(layout);

        // Provider would return autotile, but the per-desktop snapping
        // assignment must win — explicit configuration is authoritative.
        mgr->setDefaultAssignmentEntryProvider([]() {
            PhosphorZones::AssignmentEntry e;
            e.mode = PhosphorZones::AssignmentEntry::Autotile;
            e.tilingAlgorithm = QStringLiteral("bsp");
            return e;
        });
        mgr->assignLayout(QStringLiteral("DP-1"), 1, QString(), layout);

        auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 1);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry.snappingLayout, layout->id().toString());

        // Other desktops still get the synthesized autotile default.
        auto desk2 = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 2);
        QCOMPARE(desk2.mode, PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(desk2.tilingAlgorithm, QStringLiteral("bsp"));
    }

    void testDefaultAssignmentEntryProvider_emptyProvider_preservesPre368Behaviour()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        // No provider set — cascade miss returns default-constructed entry,
        // matching the historical behaviour callers rely on.
        QVERIFY(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4).isEmpty());
        auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4);
        QVERIFY(!entry.isValid());
    }

    void testDefaultAssignmentEntryProvider_layoutForScreen_resolvesSnapDefault()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* fallback = createTestLayout(QStringLiteral("Fallback"));
        mgr->addLayout(fallback);

        const QString layoutId = fallback->id().toString();
        mgr->setDefaultAssignmentEntryProvider([layoutId]() {
            PhosphorZones::AssignmentEntry e;
            e.mode = PhosphorZones::AssignmentEntry::Snapping;
            e.snappingLayout = layoutId;
            return e;
        });

        // No stored assignment — layoutForScreen should resolve via the
        // provider's snap default (preferred over defaultLayout()).
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 4), fallback);
    }

    void testDefaultAssignmentEntryProvider_snapWithUnknownUuid_fallsThroughToDefaultLayout()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* registered = createTestLayout(QStringLiteral("Registered"));
        mgr->addLayout(registered);

        // Provider points at a UUID that's NOT in the registry. layoutForScreen
        // must fall through to defaultLayout() rather than returning nullptr —
        // a stale defaultLayoutId in settings shouldn't break overlay/drag.
        const QString bogusId = QUuid::createUuid().toString();
        mgr->setDefaultAssignmentEntryProvider([bogusId]() {
            PhosphorZones::AssignmentEntry e;
            e.mode = PhosphorZones::AssignmentEntry::Snapping;
            e.snappingLayout = bogusId;
            return e;
        });

        // assignmentIdForScreen surfaces the raw stored string (matches the
        // method's documented contract — KCM/UI sees the dangling reference
        // and can warn/clear it).
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), bogusId);
        // assignmentEntryForScreen agrees (gated on activeLayoutId() non-empty
        // — bogusId is non-empty so it propagates).
        QCOMPARE(mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4).snappingLayout, bogusId);
        // layoutForScreen, however, must not return nullptr — it falls
        // through to defaultLayout() when the synth UUID can't be resolved.
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 4), registered);
    }

    void testDefaultAssignmentEntryProvider_autotileWithEmptyAlgorithm_yieldsBareAutotileId()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        // Mode-only autotile (empty algorithm = "use engine default") is the
        // KCM's representation of "autotile mode, no specific algorithm"
        // — activeLayoutId() must yield "autotile:" so the cascade visitor
        // accepts it and downstream callers route via LayoutId::isAutotile.
        mgr->setDefaultAssignmentEntryProvider([]() {
            PhosphorZones::AssignmentEntry e;
            e.mode = PhosphorZones::AssignmentEntry::Autotile;
            e.tilingAlgorithm = QString(); // explicitly empty
            return e;
        });

        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), QStringLiteral("autotile:"));
        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Autotile);
        QVERIFY(entry.tilingAlgorithm.isEmpty());
        // layoutForScreen rejects autotile entries (no Layout* to resolve)
        // and falls through to defaultLayout(); registry has no layouts
        // here so the result is nullptr.
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 4), nullptr);
    }

    void testDefaultAssignmentEntryProvider_partialSnapEntry_treatedAsNoEntry()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        // Snap enabled but no defaultLayoutId set — provider returns a
        // partial entry. All three cascade views must agree this is "no
        // entry" (matching the activeLayoutId()-empty rule the cascade
        // visitor uses internally), preserving pre-368 behaviour for
        // callers that expect default-constructed.
        mgr->setDefaultAssignmentEntryProvider([]() {
            PhosphorZones::AssignmentEntry e;
            e.mode = PhosphorZones::AssignmentEntry::Snapping;
            e.snappingLayout = QString(); // no UUID configured
            return e;
        });

        QVERIFY(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4).isEmpty());
        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4);
        QVERIFY(!entry.isValid());
        QCOMPARE(entry.snappingLayout, QString());
    }

    void testDefaultAssignmentEntryProvider_cascadeHitSkipsProvider()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Stored"));
        mgr->addLayout(layout);
        mgr->assignLayout(QStringLiteral("DP-1"), 1, QString(), layout);

        // shared_ptr so the lambda copy and the test reach the same counter.
        auto callCount = std::make_shared<int>(0);
        mgr->setDefaultAssignmentEntryProvider([callCount]() {
            ++(*callCount);
            PhosphorZones::AssignmentEntry e;
            e.mode = PhosphorZones::AssignmentEntry::Autotile;
            e.tilingAlgorithm = QStringLiteral("bsp");
            return e;
        });

        // Exact-key cascade hit — provider must not be invoked.
        auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 1);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry.snappingLayout, layout->id().toString());
        QCOMPARE(*callCount, 0);

        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 1), layout->id().toString());
        QCOMPARE(*callCount, 0);

        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 1), layout);
        QCOMPARE(*callCount, 0);

        // Cascade miss on a different desktop — provider IS invoked once
        // per cascade-querying method.
        (void)mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 2);
        QCOMPARE(*callCount, 1);
    }

    void testDefaultAssignmentEntryProvider_replaceProviderReplacesBehaviour()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        // First provider — autotile.
        mgr->setDefaultAssignmentEntryProvider([]() {
            PhosphorZones::AssignmentEntry e;
            e.mode = PhosphorZones::AssignmentEntry::Autotile;
            e.tilingAlgorithm = QStringLiteral("bsp");
            return e;
        });
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), QStringLiteral("autotile:bsp"));

        // Replace with a different provider — second value must win, not stack.
        mgr->setDefaultAssignmentEntryProvider([]() {
            PhosphorZones::AssignmentEntry e;
            e.mode = PhosphorZones::AssignmentEntry::Autotile;
            e.tilingAlgorithm = QStringLiteral("dwindle");
            return e;
        });
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), QStringLiteral("autotile:dwindle"));

        // Clearing the provider restores pre-368 cascade behaviour.
        mgr->setDefaultAssignmentEntryProvider({});
        QVERIFY(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4).isEmpty());
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
