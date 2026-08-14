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
        // A 24px-wide card with 10px inflation: the clamp (width/3) keeps
        // the middle 8px answering the card, not a boundary.
        const QVector<QRectF> cards{QRectF(0, 0, 24, 100)};
        const QVector<bool> tabbed{false};
        const StripSelectorHit hit = classifyStripSelectorPoint(cards, tabbed, QPointF(12, 20), kInflate);
        QCOMPARE(hit.gapIndex, -1);
        QCOMPARE(hit.columnIndex, 0);
        QCOMPARE(hit.half, 0);
    }

    void emptyListAnswersInvalid()
    {
        const StripSelectorHit hit = classifyStripSelectorPoint({}, {}, QPointF(10, 10), kInflate);
        QVERIFY(!hit.isValid());
    }
};

QTEST_APPLESS_MAIN(TestStripSelectorHitTest)
#include "test_strip_selector_hittest.moc"
