// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_gesture_end_decisions.cpp
 * @brief The end-of-resize and crossing rules (plasmazoneseffect/gestureenddecisions.h):
 *        when the client has committed the size it was dragged to, what a
 *        held resize crossed, when the neighbour-reflow report is sent, and
 *        when a frame change moved nothing, and where a size restored
 *        mid-move puts the window (F486, F701, F248, F702).
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
    void frameUnderGrab_emptyFrameKeepsTheTopLeft()
    {
        QCOMPARE(frameUnderGrab(QRectF(30, 40, 0, 0), QPointF(500, 500), QSize(200, 100)), QRect(30, 40, 200, 100));
    }
};

QTEST_MAIN(TestGestureEndDecisions)
#include "test_gesture_end_decisions.moc"
