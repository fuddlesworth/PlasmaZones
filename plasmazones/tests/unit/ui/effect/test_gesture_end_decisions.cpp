// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_gesture_end_decisions.cpp
 * @brief The end-of-resize and crossing rules (plasmazoneseffect/gestureenddecisions.h):
 *        when the client has committed the size it was dragged to, what a
 *        held resize crossed, when the neighbour-reflow report is sent, and
 *        when a frame change moved nothing, and where a size restored
 *        mid-move puts the window, whether a move KWin ended was reverted,
 *        where a pointerless drag's grab point is, how a drag's held
 *        crossing settles at the drop, and what an output settle reports as
 *        KWin's alone (F486, F701, F248, F702, F752, F774, F713, F731, F1014,
 *        F1009).
 */

#include "plasmazoneseffect/gestureenddecisions.h"

#include <QTest>

using namespace PlasmaZones::GestureEndDecisions;

class TestGestureEndDecisions : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // The request is fractional and the committed frame is whole pixels, so a
    // pixel either way answers it; anything more does not.
    void frameAnswersRequest_withinAPixel()
    {
        QVERIFY(frameAnswersRequest(QSizeF(800, 600), QSizeF(800.4, 599.6)));
        QVERIFY(!frameAnswersRequest(QSizeF(800, 600), QSizeF(802, 600)));
        QVERIFY(!frameAnswersRequest(QSizeF(800, 600), QSizeF(800, 598)));
    }

    void classify_noCrossing()
    {
        QCOMPARE(classify(QStringLiteral("DP-1/vs:0"), QStringLiteral("DP-1/vs:0")), Crossing::None);
        QCOMPARE(classify(QString(), QStringLiteral("DP-1")), Crossing::None);
        QCOMPARE(classify(QStringLiteral("DP-1"), QString()), Crossing::None);
    }

    void classify_virtualScreen()
    {
        QCOMPARE(classify(QStringLiteral("DP-1/vs:0"), QStringLiteral("DP-1/vs:1")), Crossing::VirtualScreen);
    }

    void classify_output()
    {
        QCOMPARE(classify(QStringLiteral("DP-1/vs:0"), QStringLiteral("DP-2")), Crossing::Output);
        QCOMPARE(classify(QStringLiteral("DP-1"), QStringLiteral("DP-2")), Crossing::Output);
    }

    // A resize that ended on another screen does not describe the tiles it
    // started among.
    void reportsResize_onlyWithoutCrossing()
    {
        QVERIFY(reportsResize(Crossing::None));
        QVERIFY(!reportsResize(Crossing::VirtualScreen));
        QVERIFY(!reportsResize(Crossing::Output));
    }

    // A frame that kept its top-left and changed only its size has not moved,
    // so it crosses no screen (F248). One that moved is judged by its centre.
    void sizeOnly_grownFromTheCorner()
    {
        QVERIFY(isSizeOnlyChange(QRectF(0, 0, 800, 600), QRectF(0, 0, 1100, 700)));
    }
    void sizeOnly_subPixelCornerDrift()
    {
        QVERIFY(isSizeOnlyChange(QRectF(100, 100, 800, 600), QRectF(100.6, 100, 900, 600)));
    }
    void sizeOnly_moved()
    {
        QVERIFY(!isSizeOnlyChange(QRectF(0, 0, 800, 600), QRectF(40, 0, 800, 600)));
    }
    void sizeOnly_movedAndResized()
    {
        QVERIFY(!isSizeOnlyChange(QRectF(0, 0, 800, 600), QRectF(0, 40, 900, 600)));
    }
    void sizeOnly_unchanged()
    {
        QVERIFY(!isSizeOnlyChange(QRectF(0, 0, 800, 600), QRectF(0, 0, 800, 600)));
    }

    // A size restored during a move keeps the grab point at the same fraction
    // of the window (F702).
    void frameUnderGrab_centredGrabStaysCentred()
    {
        QCOMPARE(frameUnderGrab(QRectF(0, 0, 1920, 1080), QPointF(960, 540), QSize(800, 600)),
                 QRect(560, 240, 800, 600));
    }
    void frameUnderGrab_grabNearTheRightEdge()
    {
        // 90% across a 500-wide zone stays 90% across the 150-wide window, and
        // halfway down stays halfway down.
        const QRect r = frameUnderGrab(QRectF(8, 8, 500, 752), QPointF(458, 384), QSize(150, 178));
        QCOMPARE(r, QRect(323, 295, 150, 178));
        QVERIFY(r.contains(458, 384));
    }
    void frameUnderGrab_grabOutsideIsClampedToTheEdge()
    {
        QCOMPARE(frameUnderGrab(QRectF(100, 100, 400, 300), QPointF(600, 50), QSize(200, 100)),
                 QRect(400, 50, 200, 100));
    }
    // A move KWin put back at its start (Escape) is cancelled; one accepted
    // elsewhere (Return, a lifted finger) is not (F752).
    void moveReverted_escapeRevertIsCancelled()
    {
        QVERIFY(moveReverted(QRectF(8, 8, 329, 752), QRectF(8, 8, 329, 752), 0, 0, true));
    }
    void moveReverted_acceptedKeyboardMoveIsNotCancelled()
    {
        QVERIFY(!moveReverted(QRectF(8, 8, 329, 752), QRectF(48, 8, 329, 752), 0, 0, true));
    }
    void moveReverted_fractionalResidueStillReverted()
    {
        QVERIFY(moveReverted(QRectF(10.4, 10.4, 300, 300), QRectF(10, 10, 300, 300), 0, 0, true));
    }
    // Escape on a Monocle tile KWin restored on drag: the cancel re-applies the
    // maximize, which commands the maximize area, not the gap-inset tile rect
    // the move started on (F1014).
    void moveReverted_cancelReMaximizesTheMonocleTile()
    {
        QVERIFY(moveReverted(QRectF(8, 8, 1008, 752), QRectF(0, 0, 1024, 768), 3, 3, true));
    }
    // A keyboard or touch move of the same tile taken to another output's top
    // edge is maximized there by KWin's electric border and accepted: no cancel
    // (F1072).
    void moveReverted_edgeMaximizeOnAnotherOutputIsNotCancelled()
    {
        QVERIFY(!moveReverted(QRectF(8, 8, 1008, 752), QRectF(1024, 0, 1920, 1080), 3, 3, false));
    }
    // A vertically maximized window moved sideways with the keyboard keeps its
    // maximize, KWin leaving an axis maximize through a move along the free
    // axis: accepted with Return, it is no cancel (F1057, live run 2).
    void moveReverted_axisMaximizeMovedAlongItsFreeAxisIsNotCancelled()
    {
        QVERIFY(!moveReverted(QRectF(56, 0, 400, 768), QRectF(584, 0, 400, 768), 1, 1, true));
    }
    // The same move accepted elsewhere keeps KWin's restore and is no cancel.
    void moveReverted_restoredOnDragAndAcceptedIsNotCancelled()
    {
        QVERIFY(!moveReverted(QRectF(8, 8, 1008, 752), QRectF(352, 220, 400, 328), 3, 0, true));
    }
    // A move that started restored is judged by its rect alone.
    void moveReverted_restoredStartIgnoresTheMode()
    {
        QVERIFY(!moveReverted(QRectF(8, 8, 400, 300), QRectF(48, 8, 400, 300), 0, 0, true));
    }
    // The grab point of a pointerless drag travels with the window (F774).
    void grabPointAt_followsTheWindow()
    {
        QCOMPARE(grabPointAt(QPointF(100, 100), QPointF(50, 50), QPointF(450, 250)), QPointF(500, 300));
    }
    void frameUnderGrab_emptyFrameKeepsTheTopLeft()
    {
        QCOMPARE(frameUnderGrab(QRectF(30, 40, 0, 0), QPointF(500, 500), QSize(200, 100)), QRect(30, 40, 200, 100));
    }

    // A dragged window's crossing settles once at the drop, by what the drop
    // did (F713, F731): a float is never announced where it lands.
    void settleDragCrossing_floatTracksOnly()
    {
        QCOMPARE(settleDragCrossing(DropAnswer::Floated, /*tracked=*/true, /*landedManaged=*/true),
                 DragCrossingSettle::TrackOnly);
        QCOMPARE(settleDragCrossing(DropAnswer::Floated, /*tracked=*/false, /*landedManaged=*/true),
                 DragCrossingSettle::TrackOnly);
    }
    // The daemon re-inserted a tracked window: no release relay (F753).
    void settleDragCrossing_daemonPlacedTrackedReKeys()
    {
        QCOMPARE(settleDragCrossing(DropAnswer::DaemonPlaced, /*tracked=*/true, /*landedManaged=*/true),
                 DragCrossingSettle::ReKeyOnly);
    }
    // A plain drop onto a tiling monitor floats there (l18_plain_drop_on_tiling):
    // tracked, never announced.
    void settleDragCrossing_daemonPlacedUntrackedTracksOnly()
    {
        QCOMPARE(settleDragCrossing(DropAnswer::DaemonPlaced, /*tracked=*/false, /*landedManaged=*/true),
                 DragCrossingSettle::TrackOnly);
    }
    void settleDragCrossing_daemonPlacedOnSnappingRunsBody()
    {
        QCOMPARE(settleDragCrossing(DropAnswer::DaemonPlaced, /*tracked=*/true, /*landedManaged=*/false),
                 DragCrossingSettle::RunBody);
    }
    void settleDragCrossing_snapRunsBody()
    {
        QCOMPARE(settleDragCrossing(DropAnswer::Snapped, /*tracked=*/true, /*landedManaged=*/false),
                 DragCrossingSettle::RunBody);
    }
    // No answer: the crossing runs as it did mid-drag, once, and is reported.
    void settleDragCrossing_unansweredRunsBodyAndReports()
    {
        QCOMPARE(settleDragCrossing(DropAnswer::Unanswered, /*tracked=*/true, /*landedManaged=*/true),
                 DragCrossingSettle::RunBodyAndReport);
        QCOMPARE(settleDragCrossing(DropAnswer::Unanswered, /*tracked=*/false, /*landedManaged=*/true),
                 DragCrossingSettle::RunBodyAndReport);
    }

    // A window KWin's output-update cascade moved, and nothing after it, is
    // reported as KWin's alone (F1009).
    void settleRowKwinOnly_cascadeMoveIsKwins()
    {
        QVERIFY(settleRowKwinOnly(4, 4, false, false, QStringLiteral("uuid-B"), QStringLiteral("uuid-B")));
    }
    // A Move to Screen from the keyboard after the cascade ended moves no
    // interactive count, and is the user's all the same.
    void settleRowKwinOnly_keyboardMoveAfterTheCascadeIsNot()
    {
        QVERIFY(!settleRowKwinOnly(4, 4, false, false, QStringLiteral("uuid-B"), QStringLiteral("uuid-C")));
    }
    // One made between two cascades, which the later one found at its start.
    void settleRowKwinOnly_moveBetweenCascadesIsNot()
    {
        QVERIFY(!settleRowKwinOnly(4, 4, false, true, QStringLiteral("uuid-C"), QStringLiteral("uuid-C")));
    }
    // No cascade sampled the window: the interactive count decides.
    void settleRowKwinOnly_unsampledFallsBackToTheCount()
    {
        QVERIFY(settleRowKwinOnly(4, 4, false, false, QString(), QStringLiteral("uuid-B")));
        QVERIFY(!settleRowKwinOnly(4, 5, false, false, QString(), QStringLiteral("uuid-B")));
    }
    // A cancelled drag of a tile the effect floated at its start has the
    // daemon re-emit it: the tiling screen retiles, a strip re-emits the
    // window (F1015).
    void cancelledDragReassert_tilingRetiles()
    {
        QCOMPARE(cancelledDragReassert(true, true, false, true, false), CancelReassert::Retile);
    }
    void cancelledDragReassert_stripReemitsTheWindow()
    {
        QCOMPARE(cancelledDragReassert(true, true, false, true, true), CancelReassert::ReapplyWindow);
    }
    // A Reorder-mode tile that crossed a snapping screen mid-drag was
    // suspended there, which shed its marks the same way (F1058).
    void cancelledDragReassert_suspendedTileRetiles()
    {
        QCOMPARE(cancelledDragReassert(true, false, true, true, false), CancelReassert::Retile);
        QCOMPARE(cancelledDragReassert(true, false, true, true, true), CancelReassert::ReapplyWindow);
    }
    // A drop that was not cancelled, a window floating before the drag (or kept
    // tiled by Reorder on its screen), and an untracked window ask for nothing.
    void cancelledDragReassert_otherwiseNothing()
    {
        QCOMPARE(cancelledDragReassert(false, true, true, true, false), CancelReassert::None);
        QCOMPARE(cancelledDragReassert(true, false, false, true, false), CancelReassert::None);
        QCOMPARE(cancelledDragReassert(true, true, true, false, false), CancelReassert::None);
    }

    void settleRowKwinOnly_draggedIsNot()
    {
        QVERIFY(!settleRowKwinOnly(4, 4, true, false, QStringLiteral("uuid-B"), QStringLiteral("uuid-B")));
    }
};

QTEST_MAIN(TestGestureEndDecisions)
#include "test_gesture_end_decisions.moc"
