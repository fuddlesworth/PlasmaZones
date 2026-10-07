// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_snap_navigation.cpp
 * @brief The snap keyboard verbs on two outputs: where a move lands when it
 *        reaches the edge of a layout, and what it commits there.
 */

#include "wta_snap_nav_fixture.h"

#include <QSignalSpy>

using PhosphorEngine::NavigationContext;
using PhosphorSnapEngine::SnapEngine;

class TestWtaSnapNavigation : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // A move off the right edge of DP-1 enters DP-2's layout and commits
    // there, with no membership left behind on DP-1 (F841).
    void moveIntoTheNeighbourOutputCommitsThere()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        const QString w = f.live(QStringLiteral("nav-1"), QRect(1300, 100, 400, 300));
        f.snapOn(w, {f.zone(2)}, kLeft);
        f.cross.outputs.insert(kLeft + QStringLiteral("|right"), kRight);
        f.focus(w, kLeft);
        QSignalSpy applies(f.snap.get(), &SnapEngine::applyGeometryRequested);
        f.snap->moveFocusedInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(f.snap->screenForTrackedWindow(w), kRight);
        QCOMPARE(applies.count(), 1);
        QCOMPARE(applies.first().at(6).toString(), kRight);
        const PhosphorSnapEngine::SnapState* left =
            static_cast<const PhosphorSnapEngine::SnapState*>(f.snap->stateForScreen(kLeft));
        QVERIFY(!left || !f.snap->holdsWindowInState(w, left));
    }

    // A tiling neighbour output takes the window before the next desktop is
    // tried (F841).
    void tilingNeighbourOutputIsTriedBeforeTheDesktop()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        const QString w = f.live(QStringLiteral("nav-2"), QRect(1300, 100, 400, 300));
        f.snapOn(w, {f.zone(2)}, kLeft);
        f.cross.outputs.insert(kLeft + QStringLiteral("|right"), kRight);
        f.cross.desktopCount = 2;
        f.setMode(kRight, 1, PhosphorZones::AssignmentEntry::Autotile);
        f.focus(w, kLeft);
        QSignalSpy crossMode(f.snap.get(), &PhosphorEngine::PlacementEngineBase::crossModeMoveRequested);
        QSignalSpy desktopMove(f.snap.get(), &PhosphorEngine::PlacementEngineBase::windowDesktopMoveRequested);
        f.snap->moveFocusedInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(crossMode.count(), 1);
        QCOMPARE(desktopMove.count(), 0);
    }
};

QTEST_MAIN(TestWtaSnapNavigation)
#include "test_wta_snap_navigation.moc"
