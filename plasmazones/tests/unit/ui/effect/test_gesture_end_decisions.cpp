// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_gesture_end_decisions.cpp
 * @brief The end-of-resize rules (plasmazoneseffect/gestureenddecisions.h):
 *        when the client has committed the size it was dragged to, what a
 *        held resize crossed, and when the neighbour-reflow report is sent
 *        (F486, F701).
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
};

QTEST_MAIN(TestGestureEndDecisions)
#include "test_gesture_end_decisions.moc"
