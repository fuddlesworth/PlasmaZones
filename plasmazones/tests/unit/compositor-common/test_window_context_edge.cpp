// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_window_context_edge.cpp
 * @brief The classification of one edit to a window's desktop set, which the
 *        desktop handler's arms are a function of. Each row pins one cell of
 *        the model: a move, a set that grew or shrank, a stick and an un-stick,
 *        seen from the desktop in view.
 */

#include <QTest>

#include "plasmazoneseffect/windowcontextedge.h"

using namespace PlasmaZones::WindowContextEdge;

namespace {
const QString d1 = QStringLiteral("d1");
const QString d2 = QStringLiteral("d2");
const QString d3 = QStringLiteral("d3");

/// Classify @p previous → @p current with d1 in view on both axes.
Edge edit(const IdSet& previous, const IdSet& current, bool otherAxisInView = true)
{
    return classify(previous, /*hadPrevious=*/true, current, d1, otherAxisInView);
}
} // namespace

class TestWindowContextEdge : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void moveOffViewIsAGenuineDeparture()
    {
        const Edge edge = edit({d1}, {d2});
        QCOMPARE(edge.kind, Kind::Departed);
        QVERIFY(edge.genuineMove);
        QVERIFY(edge.leftAnId);
    }

    // F399: the window is still on d2, where it keeps its tile.
    void uncheckingTheDesktopInViewIsNotAMove()
    {
        const Edge edge = edit({d1, d2}, {d2});
        QCOMPARE(edge.kind, Kind::Departed);
        QVERIFY(!edge.genuineMove);
    }

    // F581: leaving "every desktop" is never a move.
    void unstickOntoAHiddenDesktopIsNotAMove()
    {
        const Edge edge = edit({}, {d2});
        QCOMPARE(edge.kind, Kind::Departed);
        QVERIFY(!edge.genuineMove);
        QVERIFY(edge.leftEverywhere);
    }

    void moveIntoViewIsAGenuineArrival()
    {
        const Edge edge = edit({d2}, {d1});
        QCOMPARE(edge.kind, Kind::Arrived);
        QVERIFY(edge.genuineMove);
        QVERIFY(edge.onlyInView);
    }

    void growIntoViewIsAnArrival()
    {
        const Edge edge = edit({d2}, {d1, d2});
        QCOMPARE(edge.kind, Kind::Arrived);
        QVERIFY(!edge.genuineMove);
        QVERIFY(!edge.leftAnId);
    }

    // F520: a window made sticky from a hidden desktop is in view now.
    void stickFromAHiddenDesktopIsAnArrival()
    {
        const Edge edge = edit({d2}, {});
        QCOMPARE(edge.kind, Kind::Arrived);
        QVERIFY(edge.becameEverywhere);
        QVERIFY(!edge.genuineMove);
        QVERIFY(!edge.leftAnId);
    }

    void stickFromTheDesktopInViewStaysInView()
    {
        const Edge edge = edit({d1}, {});
        QCOMPARE(edge.kind, Kind::StayedInView);
        QVERIFY(edge.becameEverywhere);
    }

    // F410: un-stuck onto the desktop in view, where it was all along.
    void unstickOntoTheDesktopInViewStaysInView()
    {
        const Edge edge = edit({}, {d1});
        QCOMPARE(edge.kind, Kind::StayedInView);
        QVERIFY(edge.leftEverywhere);
        QVERIFY(edge.onlyInView);
    }

    // F553
    void shrinkToTheDesktopInViewStaysInView()
    {
        const Edge edge = edit({d1, d2}, {d1});
        QCOMPARE(edge.kind, Kind::StayedInView);
        QVERIFY(edge.onlyInView);
        QVERIFY(edge.leftAnId);
    }

    // F563: a move nobody sees still leaves the desktop it was held on.
    void moveBetweenHiddenDesktopsLeavesAnId()
    {
        const Edge edge = edit({d2}, {d3});
        QCOMPARE(edge.kind, Kind::StayedHidden);
        QVERIFY(edge.genuineMove);
        QVERIFY(edge.leftAnId);
    }

    void growBetweenHiddenDesktopsLeavesNone()
    {
        const Edge edge = edit({d2}, {d2, d3});
        QCOMPARE(edge.kind, Kind::StayedHidden);
        QVERIFY(!edge.leftAnId);
    }

    // A window on another activity is never in view, whatever its desktop.
    void otherAxisHiddenNeverArrives()
    {
        const Edge edge = edit({d2}, {d1}, /*otherAxisInView=*/false);
        QCOMPARE(edge.kind, Kind::StayedHidden);
    }

    void unseededOrNoViewIsUnclassifiable()
    {
        QCOMPARE(classify({d2}, /*hadPrevious=*/false, {d1}, d1, true).kind, Kind::Unclassifiable);
        QCOMPARE(classify({d2}, /*hadPrevious=*/true, {d1}, QString(), true).kind, Kind::Unclassifiable);
    }
};

QTEST_GUILESS_MAIN(TestWindowContextEdge)
#include "test_window_context_edge.moc"
