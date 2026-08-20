// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_unified_layout_controller_none.cpp
 * @brief The picker's generic None card on snapping/autotile contexts.
 *
 * The Templates screens' None card has its own suite
 * (test_unified_layout_controller_scrolling.cpp); this one pins the GENERIC
 * twin the other two families share:
 *
 *  - the card is present in the controller's list for a non-Templates
 *    context and sorts last (an escape from the list, not a member of it);
 *  - a press on a Snapping context stores the reserved word in the snapping
 *    slot, keeps the mode, and resolves to NO layout even though the
 *    registry-wide default fallback exists;
 *  - a press on an Autotile context stores the word in the algorithm slot
 *    as "autotile:none", keeping the mode;
 *  - displayIdForAssignment translates the stored wire ids back to the
 *    card's own bare id so the picker highlight and the cycle anchor land
 *    on it.
 *
 * Same DI-lean construction as the scrolling suite: null ScreenManager /
 * algorithm registry / autotile engine, since no path under test needs them.
 *
 * KNOWN GAP: the "engine never receives the reserved word" invariant is
 * enforced daemon-side, in translation/skip sites no test target compiles
 * (updateEngineScreens' autotile:none skip, the mode-toggle restore arms,
 * the OSD gates, the overlay suppression). This suite pins the registry and
 * controller halves only; the daemon halves are covered by the code-level
 * gates at those sites plus manual verification, the same standing gap the
 * scrolling sentinel shipped with.
 */

#include <QScopedPointer>
#include <QTest>

#include <algorithm>

#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>

#include "config/settings.h"
#include "core/utils/unifiedlayoutlist.h"
#include "daemon/controllers/unifiedlayoutcontroller.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"

#include <QDir>
#include <memory>
#include <vector>

using namespace PlasmaZones;
using LayoutSupport = PhosphorEngine::IPlacementEngine::LayoutSupport;

class TestUnifiedLayoutControllerNone : public QObject
{
    Q_OBJECT

private:
    std::vector<std::unique_ptr<TestHelpers::IsolatedConfigGuard>> m_guards;

    PhosphorZones::Layout* createTestLayout(const QString& name)
    {
        auto* layout = new PhosphorZones::Layout(name);
        auto* zone = new PhosphorZones::Zone();
        zone->setRelativeGeometry(QRectF(0, 0, 1, 1));
        layout->addZone(zone);
        return layout;
    }

    PhosphorZones::LayoutRegistry* createManager()
    {
        m_guards.emplace_back(std::make_unique<TestHelpers::IsolatedConfigGuard>());
        auto* mgr = TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        const QString layoutDir = m_guards.back()->dataPath() + QStringLiteral("/plasmazones/layouts");
        QDir().mkpath(layoutDir);
        mgr->setLayoutDirectory(layoutDir);
        return mgr;
    }

private Q_SLOTS:
    void cleanup()
    {
        m_guards.clear();
    }

    void applyEntry_noneCard_optsASnappingContextOut()
    {
        // The full press path on a snapping screen: the card is listed,
        // pinned last, accepted despite not naming a Layout, stored as the
        // reserved word, and the context then resolves to NO layout even
        // though every cascade miss normally lands on defaultLayout().
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        Settings settings(nullptr);
        // Named to bracket the None row alphabetically ("Alpha" < "None" <
        // "Zulu"), so a row taking its alphabetical seat instead of the
        // pinned tail would land in the middle and be caught.
        auto* alpha = createTestLayout(QStringLiteral("Alpha"));
        mgr->addLayout(alpha);
        auto* zulu = createTestLayout(QStringLiteral("Zulu"));
        mgr->addLayout(zulu);

        UnifiedLayoutController controller(mgr.data(), &settings, nullptr, nullptr);
        controller.setCurrentScreenName(QStringLiteral("DP-1"));
        controller.setCurrentLayoutSupport(LayoutSupport::Placement);
        controller.setLayoutFilter(/*manual=*/true, /*autotile=*/false, /*templates=*/false);

        const int desktop = mgr->currentVirtualDesktopForScreen(QStringLiteral("DP-1"));
        mgr->assignLayout(QStringLiteral("DP-1"), desktop, QString(), alpha);

        const QString noneId = QString(PhosphorZones::NoSnappingLayout);
        const auto listed = controller.layouts();
        QVERIFY(PhosphorZones::LayoutUtils::findLayout(listed, noneId) != nullptr);
        QCOMPARE(listed.last().id, noneId);
        // Exactly one None row. findLayout plus a pinned-last check both pass
        // when a regression appends a SECOND copy in alphabetical position,
        // so count the row rather than just locating one.
        QCOMPARE(std::count_if(listed.cbegin(), listed.cend(),
                               [&noneId](const auto& p) {
                                   return p.id == noneId;
                               }),
                 1);

        QVERIFY(controller.applyLayoutById(noneId));
        // The controller's live id property is the card's own bare id — the
        // picker highlight and the cycle anchor both bind it, so a press
        // that stored the opt-out without moving currentLayoutId would leave
        // the highlight on the old card and cycle stepping from it.
        QCOMPARE(controller.currentLayoutId(), noneId);
        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), desktop);
        // The mode survives: a None press opts the context out of layouts,
        // it does not flip engines.
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry.snappingLayout, noneId);
        // The point of the reserved word: no layout resolves, default
        // fallback notwithstanding.
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), desktop, QString()), nullptr);
        // The picker highlight lands back on the card.
        QCOMPARE(controller.displayIdForAssignment(QStringLiteral("DP-1"),
                                                   mgr->assignmentIdForScreen(QStringLiteral("DP-1"), desktop)),
                 noneId);

        // Reversible from the same picker — and from CYCLE: a forward press
        // from the opted-out state lands on a real layout rather than
        // sticking, so the opt-out is never a trap state for the keyboard
        // path. (Wrap-from-the-pinned-tail and the unmatched-forward arm
        // both land on the first row here, so this pins the behavior, not
        // which arm produced it.)
        controller.cycleNext();
        QVERIFY2(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), desktop) != noneId,
                 "cycling forward off the None card must move to a real layout");
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), desktop, QString()), alpha);

        QVERIFY(controller.applyLayoutById(zulu->id().toString()));
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), desktop, QString()), zulu);
    }

    void cycle_fromTheLastRealLayout_landsOnTheNoneCard()
    {
        // Cycling ONTO the opt-out, the direction the suite did not pin. The
        // None row is a list member pinned last, and a forward cycle from a
        // matched current index takes the plain modular step, so a press from
        // the last real layout applies the opt-out. That is deliberate — it
        // matches the shipped scrolling None precedent, and it is SILENT by
        // design — but nothing asserted it, so a skip arm added to the
        // matched-index path (the shape the two unmatched arms already use)
        // would change keyboard behaviour with no test failing.
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        Settings settings(nullptr);
        auto* alpha = createTestLayout(QStringLiteral("Alpha"));
        mgr->addLayout(alpha);
        auto* zulu = createTestLayout(QStringLiteral("Zulu"));
        mgr->addLayout(zulu);

        UnifiedLayoutController controller(mgr.data(), &settings, nullptr, nullptr);
        controller.setCurrentScreenName(QStringLiteral("DP-1"));
        controller.setCurrentLayoutSupport(LayoutSupport::Placement);
        controller.setLayoutFilter(/*manual=*/true, /*autotile=*/false, /*templates=*/false);

        const int desktop = mgr->currentVirtualDesktopForScreen(QStringLiteral("DP-1"));
        const QString noneId = QString(PhosphorZones::NoSnappingLayout);

        // Sit on the last REAL row, so the next forward step is the pinned
        // None card rather than a wrap to the first layout.
        const auto listed = controller.layouts();
        QCOMPARE(listed.last().id, noneId);
        const QString lastReal = listed.at(listed.size() - 2).id;
        QVERIFY(controller.applyLayoutById(lastReal));
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), desktop), lastReal);

        controller.cycleNext();
        QCOMPARE(mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), desktop).snappingLayout, noneId);
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), desktop, QString()), nullptr);
        QCOMPARE(controller.currentLayoutId(), noneId);
    }

    void applyEntry_noneCard_optsAnAutotileContextOut()
    {
        // The same press on an autotile context writes the ALGORITHM slot:
        // "autotile:none" on the wire, mode preserved, and the snapping
        // sibling untouched (the lossless-toggle contract).
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        Settings settings(nullptr);
        auto* layout = createTestLayout(QStringLiteral("Manual"));
        mgr->addLayout(layout);

        UnifiedLayoutController controller(mgr.data(), &settings, nullptr, nullptr);
        controller.setCurrentScreenName(QStringLiteral("DP-1"));
        controller.setCurrentLayoutSupport(LayoutSupport::Placement);
        // The filter the daemon actually computes for an autotile-active
        // screen: manual OFF. updateLayoutFilterForScreen derives
        // includeManual as "not scrolling and (manual or not autotile)", which
        // is false once autotile is the only active family, and the quick-slot
        // path builds the same pair. Passing manual=true here tested a
        // combination no screen ever runs under.
        controller.setLayoutFilter(/*manual=*/false, /*autotile=*/true, /*templates=*/false);

        const int desktop = mgr->currentVirtualDesktopForScreen(QStringLiteral("DP-1"));
        mgr->assignLayout(QStringLiteral("DP-1"), desktop, QString(), layout);
        mgr->assignLayoutById(QStringLiteral("DP-1"), desktop, QString(), QStringLiteral("autotile:bsp"));

        // The None row is appended outside both the manual and autotile
        // blocks, so it survives this filter too, still pinned last. Note the
        // fixture wires no algorithm registry, so the list carries no autotile
        // rows to sit in front of it — what is pinned here is the row's seat
        // relative to whatever real rows exist, not a mixed-family ordering.
        const QString noneId = QString(PhosphorZones::NoSnappingLayout);
        const auto listed = controller.layouts();
        QVERIFY(PhosphorZones::LayoutUtils::findLayout(listed, noneId) != nullptr);
        QCOMPARE(listed.last().id, noneId);
        QCOMPARE(std::count_if(listed.cbegin(), listed.cend(),
                               [&noneId](const auto& p) {
                                   return p.id == noneId;
                               }),
                 1);

        QVERIFY(controller.applyLayoutById(QString(PhosphorZones::NoSnappingLayout)));
        QCOMPARE(controller.currentLayoutId(), noneId);
        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), desktop);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(entry.tilingAlgorithm, QString(PhosphorZones::NoTilingAlgorithm));
        QCOMPARE(entry.snappingLayout, layout->id().toString());
        QCOMPARE(entry.activeLayoutId(), QStringLiteral("autotile:none"));

        // The wire id translates back to the card's bare id for the
        // highlight, exactly like the scrolling sentinel substitution.
        QCOMPARE(controller.displayIdForAssignment(QStringLiteral("DP-1"), QStringLiteral("autotile:none")),
                 QString(PhosphorZones::NoSnappingLayout));
        // Everything else stays identity.
        QCOMPARE(controller.displayIdForAssignment(QStringLiteral("DP-1"), QStringLiteral("autotile:bsp")),
                 QStringLiteral("autotile:bsp"));
    }
};

QTEST_MAIN(TestUnifiedLayoutControllerNone)
#include "test_unified_layout_controller_none.moc"
