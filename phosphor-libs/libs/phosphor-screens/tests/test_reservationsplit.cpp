// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "reservationsplit.h"

#include <QTest>

using PhosphorScreens::Detail::EdgeSplit;
using PhosphorScreens::Detail::splitReservation;

class TestReservationSplit : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void noReservation()
    {
        const EdgeSplit s = splitReservation(0, 32, 53);
        QCOMPARE(s.first, 0);
        QCOMPARE(s.second, 0);
    }

    void noClaims()
    {
        const EdgeSplit s = splitReservation(32, 0, 0);
        QCOMPARE(s.first, 0);
        QCOMPARE(s.second, 0);
    }

    void singleClaimingEdgeTakesAll()
    {
        const EdgeSplit s = splitReservation(32, 32, 0);
        QCOMPARE(s.first, 32);
        QCOMPARE(s.second, 0);
    }

    // Discussion #985: a 32 px top panel that reserves, plus a bottom dock
    // that claims 53 px but is hidden and reserves nothing. The top edge
    // matches the sensor total exactly, so it takes all of it — the old
    // proportional split gave top=12 / bottom=20, which pushed zones under
    // the top panel and left a 20 px gap at the bottom of the screen.
    void hiddenOppositePanelDoesNotStealReservation()
    {
        const EdgeSplit s = splitReservation(32, 32, 53);
        QCOMPARE(s.first, 32);
        QCOMPARE(s.second, 0);
    }

    void hiddenTopPanelDoesNotStealFromBottom()
    {
        const EdgeSplit s = splitReservation(53, 32, 53);
        QCOMPARE(s.first, 0);
        QCOMPARE(s.second, 53);
    }

    void bothEdgesReserving()
    {
        const EdgeSplit s = splitReservation(76, 32, 44);
        QCOMPARE(s.first, 32);
        QCOMPARE(s.second, 44);
    }

    // A pixel of rounding slack between plasmashell's geometry and the
    // compositor's configure still resolves to the claiming edge, and the
    // split always sums to the sensor total.
    void roundingSlackStillAttributes()
    {
        const EdgeSplit s = splitReservation(32, 33, 53);
        QCOMPARE(s.first, 32);
        QCOMPARE(s.second, 0);
    }

    // Equal claims on both edges with only one of them reserving is genuinely
    // ambiguous: fall back to the proportional split rather than guessing an
    // edge. The total is still preserved.
    void ambiguousEqualClaimsFallBackToProportional()
    {
        const EdgeSplit s = splitReservation(32, 32, 32);
        QCOMPARE(s.first, 16);
        QCOMPARE(s.second, 16);
    }

    // No combination is anywhere near the total (stale panel data, or a
    // non-Plasma layer surface reserving alongside). Proportional split.
    void unattributableFallsBackToProportional()
    {
        const EdgeSplit s = splitReservation(100, 20, 30);
        QCOMPARE(s.first, 40);
        QCOMPARE(s.second, 60);
    }

    void splitAlwaysSumsToReserved()
    {
        for (int reserved = 1; reserved <= 120; ++reserved) {
            for (int a = 0; a <= 60; a += 7) {
                for (int b = 0; b <= 60; b += 5) {
                    const EdgeSplit s = splitReservation(reserved, a, b);
                    if (a + b == 0) {
                        QCOMPARE(s.first + s.second, 0);
                    } else {
                        QCOMPARE(s.first + s.second, reserved);
                    }
                    QVERIFY(s.first >= 0);
                    QVERIFY(s.second >= 0);
                }
            }
        }
    }
};

QTEST_MAIN(TestReservationSplit)
#include "test_reservationsplit.moc"
