// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>

#include "tilinghandler/centringdecisions.h"

using PlasmaZones::CentringDecisions::commandStillInTile;

/**
 * @brief The centring pass's witness: whether the window's commanded origin
 *        still belongs to the tile its centring target names (F536).
 */
class TestCentringDecisions : public QObject
{
    Q_OBJECT

private:
    // A 400x300 tile at (100, 50), with gapless neighbours to its right at
    // (500, 50) and below at (100, 350).
    static QRectF zone()
    {
        return QRectF(100.0, 50.0, 400.0, 300.0);
    }

private Q_SLOTS:
    // A tile apply commands the zone origin.
    void zoneOrigin_inside()
    {
        QVERIFY(commandStillInTile(zone(), QPointF(100.0, 50.0)));
    }

    // A client that committed 300x200 is centred at (150, 100), inside.
    void centredOrigin_inside()
    {
        QVERIFY(commandStillInTile(zone(), QPointF(150.0, 100.0)));
    }

    // A move into the gapless neighbour to the right lands exactly on this
    // zone's right edge. The old witness grew the zone by a pixel on every
    // side and read it as still in this tile.
    void gaplessRightNeighbourOrigin_outside()
    {
        QVERIFY(!commandStillInTile(zone(), QPointF(500.0, 50.0)));
    }

    // The same for the gapless neighbour below.
    void gaplessBelowNeighbourOrigin_outside()
    {
        QVERIFY(!commandStillInTile(zone(), QPointF(100.0, 350.0)));
    }

    // A neighbour one pixel away also sat inside the old grown zone.
    void onePixelGapNeighbour_outside()
    {
        QVERIFY(!commandStillInTile(zone(), QPointF(501.0, 50.0)));
        QVERIFY(!commandStillInTile(zone(), QPointF(100.0, 351.0)));
    }

    // A pixel of rounding above or left of the zone origin still counts.
    void minusOneJitter_inside()
    {
        QVERIFY(commandStillInTile(zone(), QPointF(99.0, 49.0)));
        QVERIFY(commandStillInTile(zone(), QPointF(99.5, 50.0)));
    }
};

QTEST_GUILESS_MAIN(TestCentringDecisions)
#include "test_centring_decisions.moc"
