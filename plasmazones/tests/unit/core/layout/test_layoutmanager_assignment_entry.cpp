// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layoutmanager_assignment_entry.cpp
 * @brief The AssignmentEntry-shape half of the LayoutRegistry assignment
 *        suite: the bare "scrolling:" sentinel, the config round trip, the
 *        fromLayoutId classification factory, and the LayoutAssignmentKey
 *        group-name parser.
 *
 * Split out of test_layoutmanager_assignment.cpp at the P6 banner when that
 * file passed the 1150-line ceiling. Both halves share
 * LayoutManagerAssignmentFixture, so a helper added for one is available to
 * the other and neither carries its own registry setup.
 */

#include <QTest>
#include <QDir>
#include <QScopedPointer>
#include <QUuid>

#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "LayoutManagerAssignmentFixture.h"

using namespace PlasmaZones;

class TestLayoutManagerAssignmentEntry : public LayoutManagerAssignmentFixture
{
    Q_OBJECT

private Q_SLOTS:

    // ═══════════════════════════════════════════════════════════════════════════
    // P6: The "scrolling:" sentinel
    // ═══════════════════════════════════════════════════════════════════════════
    //
    // A Scrolling context's activeLayoutId is the bare sentinel (its
    // TEMPLATE layout lives in a separate assignment slot and never rides
    // the id). Unlike "autotile:", the sentinel carries NO payload, and the
    // classifier is an exact compare rather than a prefix test. Pin both
    // halves: the sentinel routes as Scrolling end to end, and a
    // prefix-shaped impostor ("scrolling:junk") does NOT.

    void testScrollingSentinel_assignsScrollingModeWithEmptyLayout()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        mgr->assignLayoutById(QStringLiteral("DP-1"), 0, QString(), QString(PhosphorLayout::LayoutId::ScrollingId));

        QCOMPARE(mgr->modeForScreen(QStringLiteral("DP-1"), 0), PhosphorZones::AssignmentEntry::Scrolling);
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 0), QStringLiteral("scrolling:"));

        // A fresh sentinel assign carries no payload in ANY slot — including
        // the template, whose emptiness is the canary for a rebuild path
        // inventing or duplicating SetScrollingTemplate actions.
        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 0);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Scrolling);
        QVERIFY(entry.snappingLayout.isEmpty());
        QVERIFY(entry.tilingAlgorithm.isEmpty());
        QVERIFY(entry.scrollingTemplateLayout.isEmpty());
    }

    void testScrollingSentinel_prefixImpostorIsNotScrolling()
    {
        // "scrolling:junk" must NOT classify as Scrolling. A startsWith test
        // would accept it and silently drop the "junk" in every consumer, so
        // the classifier is an exact compare and the impostor falls through as
        // an ordinary (non-autotile, non-scrolling) layout id — which the
        // Snapping arm then owns.
        QVERIFY(!PhosphorLayout::LayoutId::isScrolling(QStringLiteral("scrolling:junk")));

        const auto entry = PhosphorZones::AssignmentEntry::fromLayoutId(QStringLiteral("scrolling:junk"));
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry.snappingLayout, QStringLiteral("scrolling:junk"));
        QVERIFY(entry.tilingAlgorithm.isEmpty());

        // The bare sentinel, by contrast, is accepted.
        QVERIFY(PhosphorLayout::LayoutId::isScrolling(QStringLiteral("scrolling:")));
    }

    void testScrollingFamily_queryStampHelpers()
    {
        // The rules-visible template stamp lives in its own helper family:
        // isScrollingFamily is the PREFIX classifier (rules side), while
        // isScrolling stays the exact-compare ASSIGNMENT classifier. The two
        // must keep disagreeing on the prefixed form.
        namespace LayoutId = PhosphorLayout::LayoutId;
        const QString uuid = QStringLiteral("{11111111-2222-3333-4444-555555555555}");

        QCOMPARE(LayoutId::makeScrollingId(uuid), QStringLiteral("scrolling:") + uuid);
        QCOMPARE(LayoutId::extractTemplateId(LayoutId::makeScrollingId(uuid)), uuid);
        // Empty payload round-trips to the bare sentinel, mirroring
        // makeAutotileId's empty-algorithm contract.
        QCOMPARE(LayoutId::makeScrollingId(QString()), QString(LayoutId::ScrollingId));
        QCOMPARE(LayoutId::extractTemplateId(QString(LayoutId::ScrollingId)), QString());

        QVERIFY(LayoutId::isScrollingFamily(QString(LayoutId::ScrollingId)));
        QVERIFY(LayoutId::isScrollingFamily(LayoutId::makeScrollingId(uuid)));
        QVERIFY(LayoutId::isScrollingFamily(QStringLiteral("scrolling:junk")));
        QVERIFY(!LayoutId::isScrollingFamily(QStringLiteral("autotile:bsp")));
        // The assignment classifier keeps rejecting every payload-carrying form.
        QVERIFY(!LayoutId::isScrolling(LayoutId::makeScrollingId(uuid)));
    }

    void testAssignmentEntry_fromLayoutId_scrollingRoundTrips()
    {
        // The mode cascade — AssignmentEntry::fromLayoutId is the ONE place a
        // wire id becomes a mode. Round-trip the sentinel through it and back
        // out via activeLayoutId().
        const auto fresh = PhosphorZones::AssignmentEntry::fromLayoutId(QStringLiteral("scrolling:"));
        QCOMPARE(fresh.mode, PhosphorZones::AssignmentEntry::Scrolling);
        QCOMPARE(fresh.activeLayoutId(), QStringLiteral("scrolling:"));

        // Lossless toggle: flipping an existing entry to Scrolling keeps BOTH
        // sibling layout fields, so toggling back cannot degrade the
        // assignment into Snapping-pointing-at-a-bogus-id.
        PhosphorZones::AssignmentEntry existing;
        existing.mode = PhosphorZones::AssignmentEntry::Autotile;
        existing.snappingLayout = QStringLiteral("{some-uuid}");
        existing.tilingAlgorithm = QStringLiteral("dwindle");

        const auto scrolled = PhosphorZones::AssignmentEntry::fromLayoutId(QStringLiteral("scrolling:"), existing);
        QCOMPARE(scrolled.mode, PhosphorZones::AssignmentEntry::Scrolling);
        QCOMPARE(scrolled.snappingLayout, QStringLiteral("{some-uuid}"));
        QCOMPARE(scrolled.tilingAlgorithm, QStringLiteral("dwindle"));
        QCOMPARE(scrolled.activeLayoutId(), QStringLiteral("scrolling:"));

        // And back out to Snapping — the preserved uuid is what returns.
        const auto backToSnap = PhosphorZones::AssignmentEntry::fromLayoutId(scrolled.snappingLayout, scrolled);
        QCOMPARE(backToSnap.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(backToSnap.activeLayoutId(), QStringLiteral("{some-uuid}"));
        QCOMPARE(backToSnap.tilingAlgorithm, QStringLiteral("dwindle"));
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

        // The dormant template is a real store template, the only kind the
        // production paths ever write into this slot.
        auto* store = attachTemplateStore(mgr.get());
        const QString templId = createTestTemplate(store, QStringLiteral("DormantTemplate")).toString();

        // Base screen: autotile with snapping AND a dormant template preserved
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layoutA);
        mgr->assignScrollingTemplate(QStringLiteral("DP-1"), 0, QString(), templId);
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

        // Verify base screen — including the dormant template, whose survival
        // through save/load is the canary for the whole four-slot rule shape.
        auto base = mgr2->assignmentEntryForScreen(QStringLiteral("DP-1"), 0);
        QCOMPARE(base.mode, PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(base.snappingLayout, idA);
        QCOMPARE(base.tilingAlgorithm, QStringLiteral("wide"));
        QCOMPARE(base.scrollingTemplateLayout, templId);

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

    // ═══════════════════════════════════════════════════════════════════════════
    // The picker-highlight id: three states out of one query
    // ═══════════════════════════════════════════════════════════════════════════

    void testScrollingDisplayId_answersTheThreeHighlightStates()
    {
        // scrollingDisplayIdForContext is the shared authority behind every
        // picker-highlight site, and it answers one of three shapes: a
        // template's own id, the reserved no-template word, or the bare
        // "scrolling:" sentinel that matches no card at all. Each drives a
        // different thing on screen — a highlighted template card, a
        // highlighted None card, and nothing highlighted — so collapsing any
        // two of them is a visible bug rather than an internal detail.
        //
        // Driven through a real LayoutRegistry on purpose. The function is a
        // non-virtual convenience over scrollingTemplateForContext and the
        // virtual scrollingTemplateExplicitlyNone, and that second virtual
        // has a `return false` default on the interface — a lightweight stub
        // would inherit it, making the None arm unreachable and this test
        // green while proving nothing about it.
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* store = attachTemplateStore(mgr.get());
        const QUuid templId = createTestTemplate(store, QStringLiteral("Picked"));

        const QString screen = QStringLiteral("DP-1");
        mgr->assignLayoutById(screen, 0, QString(), QString(PhosphorLayout::LayoutId::ScrollingId));

        // 1. No template of its own and no default to inherit: the sentinel,
        // which matches no card, so the picker highlights nothing.
        QCOMPARE(mgr->scrollingDisplayIdForContext(screen, 0, QString()),
                 QString(PhosphorLayout::LayoutId::ScrollingId));

        // 2. A template assigned: its own id, which is the card to highlight.
        mgr->assignScrollingTemplate(screen, 0, QString(), templId.toString());
        QCOMPARE(mgr->scrollingDisplayIdForContext(screen, 0, QString()), templId.toString());

        // 3. Explicitly opted out: the reserved word, so the None card is the
        // one highlighted.
        mgr->assignScrollingTemplate(screen, 0, QString(), QString(PhosphorZones::NoScrollingTemplate));
        QCOMPARE(mgr->scrollingDisplayIdForContext(screen, 0, QString()), QString(PhosphorZones::NoScrollingTemplate));

        // The state the reserved word exists to be distinguishable FROM: an
        // empty slot with a default configured inherits that default and
        // highlights its card, where the opt-out above highlights None. Both
        // resolve "no template of this screen's own", and before the reserved
        // word they shared the empty spelling and could not be told apart.
        const QUuid defaultId = createTestTemplate(store, QStringLiteral("Inherited"));
        mgr->setDefaultScrollingTemplateProvider([defaultId] {
            return defaultId.toString();
        });
        // The opt-out still wins over the freshly configured default.
        QCOMPARE(mgr->scrollingDisplayIdForContext(screen, 0, QString()), QString(PhosphorZones::NoScrollingTemplate));

        mgr->assignScrollingTemplate(screen, 0, QString(), QString());
        QCOMPARE(mgr->scrollingDisplayIdForContext(screen, 0, QString()), defaultId.toString());
    }
};

QTEST_MAIN(TestLayoutManagerAssignmentEntry)
#include "test_layoutmanager_assignment_entry.moc"
