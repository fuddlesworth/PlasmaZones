// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// classifyStripSelectorPoint — the pure classification math behind the
// strip-mode zone selector hit-test (selector_strip.cpp reads rendered card
// rects back from QML and hands them here). Pins the gap-priority rules,
// the half split, the tabbed whole-card answer, and the un-laid-out-card
// exclusions, without a QML scene.

#include "core/types/stripselectorhittest.h"

#include <QtTest>

using PlasmaZones::classifyStripSelectorPoint;
using PlasmaZones::StripSelectorHit;

namespace {

// Three uniform cards, 200 wide, 100 tall, 20 apart: [0,200] [220,420]
// [440,640], all at y 0..100. Inflate 10 into each neighbour.
QVector<QRectF> threeCards()
{
    return {QRectF(0, 0, 200, 100), QRectF(220, 0, 200, 100), QRectF(440, 0, 200, 100)};
}

constexpr qreal kInflate = 10.0;

} // namespace

class TestStripSelectorHitTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void interiorGapHits()
    {
        const QVector<bool> tabbed{false, false, false};
        // Dead centre of the first spacing strip (x 210) → boundary 1.
        StripSelectorHit hit = classifyStripSelectorPoint(threeCards(), tabbed, QPointF(210, 50), kInflate);
        QCOMPARE(hit.gapIndex, 1);
        QVERIFY(hit.columnIndex < 0);
        // Inflated reach: 5px INSIDE card 1's left edge still answers the gap.
        hit = classifyStripSelectorPoint(threeCards(), tabbed, QPointF(225, 50), kInflate);
        QCOMPARE(hit.gapIndex, 1);
        // Past the inflation it is the card again.
        hit = classifyStripSelectorPoint(threeCards(), tabbed, QPointF(240, 20), kInflate);
        QCOMPARE(hit.gapIndex, -1);
        QCOMPARE(hit.columnIndex, 1);
    }

    void outerBoundaries()
    {
        const QVector<bool> tabbed{false, false, false};
        // Just left of the first card → leading boundary 0.
        StripSelectorHit hit = classifyStripSelectorPoint(threeCards(), tabbed, QPointF(-4, 50), kInflate);
        QCOMPARE(hit.gapIndex, 0);
        // Just right of the last card → trailing boundary 3.
        hit = classifyStripSelectorPoint(threeCards(), tabbed, QPointF(645, 50), kInflate);
        QCOMPARE(hit.gapIndex, 3);
    }

    void halvesSplitAtTheMidline()
    {
        const QVector<bool> tabbed{false, false, false};
        StripSelectorHit hit = classifyStripSelectorPoint(threeCards(), tabbed, QPointF(320, 20), kInflate);
        QCOMPARE(hit.columnIndex, 1);
        QCOMPARE(hit.half, 0);
        hit = classifyStripSelectorPoint(threeCards(), tabbed, QPointF(320, 80), kInflate);
        QCOMPARE(hit.columnIndex, 1);
        QCOMPARE(hit.half, 1);
        // The midline itself belongs to the TOP half (<=, not <): pin the
        // boundary the slot is named for, so a comparator flip cannot pass.
        hit = classifyStripSelectorPoint(threeCards(), tabbed, QPointF(320, 50), kInflate);
        QCOMPARE(hit.columnIndex, 1);
        QCOMPARE(hit.half, 0);
    }

    void gapBandCoversTheNeighboursYUnion()
    {
        // Card 1 is taller and offset (y 10..130) while card 0 is y 0..100:
        // the boundary band between them spans the UNION (0..130), so a
        // probe below card 0's extent but inside card 1's must still answer
        // the gap. A band derived from the left card alone would miss it.
        const QVector<QRectF> cards{QRectF(0, 0, 200, 100), QRectF(220, 10, 200, 120), QRectF(440, 0, 200, 100)};
        const QVector<bool> tabbed{false, false, false};
        const StripSelectorHit hit = classifyStripSelectorPoint(cards, tabbed, QPointF(210, 120), kInflate);
        QCOMPARE(hit.gapIndex, 1);
    }

    void shortTabbedVectorDegradesToHalves()
    {
        // The tabbed vector deliberately covers only card 0: the surplus
        // cards degrade to plain top/bottom halves rather than asserting.
        const QVector<bool> tabbed{true};
        StripSelectorHit hit = classifyStripSelectorPoint(threeCards(), tabbed, QPointF(560, 20), kInflate);
        QCOMPARE(hit.columnIndex, 2);
        QCOMPARE(hit.half, 0);
        hit = classifyStripSelectorPoint(threeCards(), tabbed, QPointF(560, 80), kInflate);
        QCOMPARE(hit.columnIndex, 2);
        QCOMPARE(hit.half, 1);
        // The one covered card still answers whole-card.
        hit = classifyStripSelectorPoint(threeCards(), tabbed, QPointF(100, 50), kInflate);
        QCOMPARE(hit.columnIndex, 0);
        QCOMPARE(hit.half, 2);
    }

    void zeroInflateKeepsTheBareSpacingStrip()
    {
        // gapInflate 0: the bands stop reaching into the cards (5px inside a
        // card answers the card), while the bare spacing strip between two
        // cards is still a gap.
        const QVector<bool> tabbed{false, false, false};
        StripSelectorHit hit = classifyStripSelectorPoint(threeCards(), tabbed, QPointF(225, 20), 0.0);
        QCOMPARE(hit.gapIndex, -1);
        QCOMPARE(hit.columnIndex, 1);
        hit = classifyStripSelectorPoint(threeCards(), tabbed, QPointF(210, 50), 0.0);
        QCOMPARE(hit.gapIndex, 1);
    }

    void tabbedCardIsOneWholeTarget()
    {
        const QVector<bool> tabbed{false, true, false};
        // Both halves of a tabbed card answer whole-card (2): the join IS
        // the tab dock, there is no top/bottom distinction to offer.
        StripSelectorHit top = classifyStripSelectorPoint(threeCards(), tabbed, QPointF(320, 20), kInflate);
        StripSelectorHit bottom = classifyStripSelectorPoint(threeCards(), tabbed, QPointF(320, 80), kInflate);
        QCOMPARE(top.half, 2);
        QCOMPARE(bottom.half, 2);
        QCOMPARE(top.columnIndex, 1);
        QCOMPARE(bottom.columnIndex, 1);
    }

    void missAnswersInvalid()
    {
        const QVector<bool> tabbed{false, false, false};
        const StripSelectorHit hit = classifyStripSelectorPoint(threeCards(), tabbed, QPointF(320, 400), kInflate);
        QVERIFY(!hit.isValid());
    }

    void unlaidCardIsUnhittable()
    {
        // Card 1 not laid out (scrolled out / first frame): its body is a
        // miss, and the boundary between 0 and 2 cannot be derived from it.
        QVector<QRectF> cards = threeCards();
        cards[1] = QRectF();
        const QVector<bool> tabbed{false, false, false};
        StripSelectorHit hit = classifyStripSelectorPoint(cards, tabbed, QPointF(320, 50), kInflate);
        // A full miss, not merely "not card 1": both interior boundaries lose
        // their un-laid-out neighbour, the outer bands cover only the
        // laid-out ends, and no card body contains the point.
        QVERIFY(!hit.isValid());
        // The laid-out neighbours still answer their own bodies.
        hit = classifyStripSelectorPoint(cards, tabbed, QPointF(100, 20), kInflate);
        QCOMPARE(hit.columnIndex, 0);
        QCOMPARE(hit.half, 0);
    }

    void singleNarrowCardKeepsAHittableMiddle()
    {
        // A 12px-wide card with 10px inflation: the clamp (width/3 = 4)
        // keeps the middle 4px answering the card. The width is chosen BELOW
        // 2 * gapInflate on purpose — with the clamp deleted outright the
        // two bands ([-10, 10] and [2, 22]) would cover the whole card and
        // this probe would answer a boundary, so the fixture fails on the
        // no-clamp mutation, not only on a divisor change.
        const QVector<QRectF> cards{QRectF(0, 0, 12, 100)};
        const QVector<bool> tabbed{false};
        const StripSelectorHit hit = classifyStripSelectorPoint(cards, tabbed, QPointF(6, 20), kInflate);
        QCOMPARE(hit.gapIndex, -1);
        QCOMPARE(hit.columnIndex, 0);
        QCOMPARE(hit.half, 0);
    }

    void unlaidEndsRenumberTheOuterBands()
    {
        // The REALISTIC un-laid-out case (a horizontally scrolled row only
        // ever un-lays the ends): the outer bands anchor on the first / last
        // LAID-OUT card and carry its renumbered gap index, not 0 / count.
        const QVector<bool> tabbed{false, false, false};
        QVector<QRectF> cards = threeCards();
        cards[0] = QRectF();
        StripSelectorHit hit = classifyStripSelectorPoint(cards, tabbed, QPointF(215, 50), kInflate);
        QCOMPARE(hit.gapIndex, 1);
        cards = threeCards();
        cards[2] = QRectF();
        hit = classifyStripSelectorPoint(cards, tabbed, QPointF(425, 50), kInflate);
        QCOMPARE(hit.gapIndex, 2);
    }

    void allUnlaidAnswersInvalid()
    {
        // Every rect null (nothing laid out yet): no band may anchor on a
        // missing card, and nothing may read past the empty scan.
        const QVector<QRectF> cards{QRectF(), QRectF(), QRectF()};
        const QVector<bool> tabbed{false, false, false};
        const StripSelectorHit hit = classifyStripSelectorPoint(cards, tabbed, QPointF(100, 50), kInflate);
        QVERIFY(!hit.isValid());
    }

    void emptyListAnswersInvalid()
    {
        const StripSelectorHit hit = classifyStripSelectorPoint({}, {}, QPointF(10, 10), kInflate);
        QVERIFY(!hit.isValid());
    }
};

QTEST_APPLESS_MAIN(TestStripSelectorHitTest)
#include "test_strip_selector_hittest.moc"
