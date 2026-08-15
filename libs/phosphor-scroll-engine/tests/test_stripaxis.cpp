// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/StripAxis.h>

#include <QTest>

using PhosphorProtocol::ScrollAxis;
using PhosphorScrollEngine::StripAxis;

/// StripAxis is pure arithmetic with no engine state, so it is pinned
/// directly rather than through a relayout. Everything else in the
/// transposition rewrite routes through this type, which is exactly why it
/// needs its own test: a compensating error INSIDE the mapper would otherwise
/// be invisible to every test that reaches geometry through it.
class TestStripAxis : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Horizontal is the historical layout, so every accessor must be the
    /// plain identity. This is the property that lets the rewrite land
    /// behaviour-preserving ahead of any vertical support.
    void horizontalIsTheIdentityMapping()
    {
        constexpr StripAxis axis = StripAxis::horizontal();
        const QRect r(10, 20, 300, 400);

        QCOMPARE(axis.mainPos(r), 10);
        QCOMPARE(axis.crossPos(r), 20);
        QCOMPARE(axis.mainSize(r), 300);
        QCOMPARE(axis.crossSize(r), 400);
        QCOMPARE(axis.mainLow(r), r.left());
        QCOMPARE(axis.mainHigh(r), r.right());
        QCOMPARE(axis.crossLow(r), r.top());
        QCOMPARE(axis.crossHigh(r), r.bottom());
        QCOMPARE(axis.makeRect(10, 20, 300, 400), r);
        QCOMPARE(axis.translatedMain(r, 25), r.translated(25, 0));
    }

    /// Vertical reads the same rect through swapped roles. Note the rect
    /// itself is NOT transposed here — the accessors are what change, because
    /// a rect is always in screen coordinates.
    void verticalSwapsMainAndCross()
    {
        constexpr StripAxis axis = StripAxis::vertical();
        const QRect r(10, 20, 300, 400);

        QCOMPARE(axis.mainPos(r), 20);
        QCOMPARE(axis.crossPos(r), 10);
        QCOMPARE(axis.mainSize(r), 400);
        QCOMPARE(axis.crossSize(r), 300);
        QCOMPARE(axis.mainLow(r), r.top());
        QCOMPARE(axis.mainHigh(r), r.bottom());
        QCOMPARE(axis.crossLow(r), r.left());
        QCOMPARE(axis.crossHigh(r), r.right());
    }

    /// makeRect is the one rect constructor the relayout should use, so its
    /// role-to-screen mapping is pinned in both directions.
    void makeRectPlacesRolesOnTheRightScreenAxis()
    {
        QCOMPARE(StripAxis::horizontal().makeRect(5, 7, 100, 200), QRect(5, 7, 100, 200));
        QCOMPARE(StripAxis::vertical().makeRect(5, 7, 100, 200), QRect(7, 5, 200, 100));
    }

    /// THE REASON THIS TYPE EXISTS, in the shape the view-delta classifier
    /// needs it: a main-axis translate must move the main coordinate and leave
    /// the cross one exactly alone, under both orientations.
    void translatedMainMovesOnlyTheMainAxis()
    {
        const QRect r(10, 20, 300, 400);

        const QRect h = StripAxis::horizontal().translatedMain(r, -60);
        QCOMPARE(h, QRect(-50, 20, 300, 400));
        QCOMPARE(h.y(), r.y());

        const QRect v = StripAxis::vertical().translatedMain(r, -60);
        QCOMPARE(v, QRect(10, -40, 300, 400));
        QCOMPARE(v.x(), r.x());
    }

    /// setMainLow MOVES the low edge while HOLDING the high edge, so it
    /// resizes; moveMain repositions without resizing. The straddle clamp
    /// depends on that difference, so pin it rather than leaving it to
    /// whoever next reads QRect's documentation.
    void lowEdgeSettersResizeWhileMoversDoNot()
    {
        const QRect start(10, 20, 300, 400);

        QRect resized = start;
        StripAxis::horizontal().setMainLow(resized, 60);
        QCOMPARE(resized, QRect(60, 20, 250, 400));
        QCOMPARE(resized.right(), start.right());

        QRect moved = start;
        StripAxis::horizontal().moveMain(moved, 60);
        QCOMPARE(moved, QRect(60, 20, 300, 400));

        QRect resizedV = start;
        StripAxis::vertical().setMainLow(resizedV, 70);
        QCOMPARE(resizedV, QRect(10, 70, 300, 350));
        QCOMPARE(resizedV.bottom(), start.bottom());

        QRect movedV = start;
        StripAxis::vertical().moveMain(movedV, 70);
        QCOMPARE(movedV, QRect(10, 70, 300, 400));
    }

    void sizeAndPointOverloadsFollowTheSameRoles()
    {
        const QSize s(300, 400);
        const QPoint p(10, 20);

        QCOMPARE(StripAxis::horizontal().mainSize(s), 300);
        QCOMPARE(StripAxis::horizontal().crossSize(s), 400);
        QCOMPARE(StripAxis::vertical().mainSize(s), 400);
        QCOMPARE(StripAxis::vertical().crossSize(s), 300);

        QCOMPARE(StripAxis::horizontal().mainPos(p), 10);
        QCOMPARE(StripAxis::vertical().mainPos(p), 20);
        QCOMPARE(StripAxis::horizontal().crossPos(p), 20);
        QCOMPARE(StripAxis::vertical().crossPos(p), 10);
    }

    void transposedExchangesTheRolesAndIsAnInvolution()
    {
        QCOMPARE(StripAxis::horizontal().transposed(), StripAxis::vertical());
        QCOMPARE(StripAxis::vertical().transposed(), StripAxis::horizontal());
        QCOMPARE(StripAxis::vertical().transposed().transposed(), StripAxis::vertical());
    }

    /// Horizontal must be the default-constructed value and must be zero.
    /// A value-initialized wire struct, an absent JSON key and an absent
    /// entry in the effect's vertical-axis list all lean on that at once.
    void defaultIsHorizontalAndHorizontalIsZero()
    {
        QCOMPARE(StripAxis(), StripAxis::horizontal());
        QVERIFY(StripAxis().isHorizontal());
        QCOMPARE(static_cast<int>(ScrollAxis::Horizontal), 0);
        QCOMPARE(static_cast<int>(PhosphorProtocol::scrollAxisFromInt(0)), 0);
    }

    /// Out-of-range ints (version skew, a malformed caller) degrade to the
    /// historical behaviour rather than to a vertical strip.
    void outOfRangeIntsFallBackToHorizontal()
    {
        QCOMPARE(PhosphorProtocol::scrollAxisFromInt(1), ScrollAxis::Vertical);
        QCOMPARE(PhosphorProtocol::scrollAxisFromInt(2), ScrollAxis::Horizontal);
        QCOMPARE(PhosphorProtocol::scrollAxisFromInt(-1), ScrollAxis::Horizontal);
        QVERIFY(!PhosphorProtocol::isValidScrollAxis(2));
        QVERIFY(PhosphorProtocol::isValidScrollAxis(0));
        QVERIFY(PhosphorProtocol::isValidScrollAxis(1));
    }
};

QTEST_APPLESS_MAIN(TestStripAxis)
#include "test_stripaxis.moc"
