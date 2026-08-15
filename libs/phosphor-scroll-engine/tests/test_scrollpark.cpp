// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "scrollengine/scrollpark_p.h"

#include <QTest>

using PhosphorProtocol::ScrollAxis;
using PhosphorScrollEngine::StripAxis;
using namespace PhosphorScrollEngine::Detail;

/// The tile placement decision, driven directly rather than through a whole
/// engine batch. Until it was extracted this was the most intricate geometry
/// in the engine with no test path of its own.
///
/// Every case runs on BOTH axes from one table, and the expectations are
/// stated in ROLE terms, so a slip that keys an arm on a physical axis fails
/// on exactly one row.
class TestScrollPark : public QObject
{
    Q_OBJECT

private:
    /// A 1200x800 screen horizontally, transposed to 800x1200 vertically, so
    /// the MAIN extent is 1200 either way and every literal below keeps its
    /// meaning across the transpose.
    static QRect screenFor(ScrollAxis a)
    {
        return a == ScrollAxis::Horizontal ? QRect(0, 0, 1200, 800) : QRect(0, 0, 800, 1200);
    }

    /// Build a rect from role coordinates, so a case reads the same on both
    /// axes.
    static QRect roleRect(ScrollAxis a, int mainPos, int mainLen)
    {
        const StripAxis axis{a};
        return axis.makeRect(mainPos, 0, mainLen, axis.crossSize(screenFor(a)));
    }

    static ParkInputs inputsFor(ScrollAxis a, const QRect& tile)
    {
        ParkInputs in;
        in.tileRect = tile;
        in.columnRect = tile;
        in.workArea = screenFor(a);
        in.screenRect = screenFor(a);
        in.axis = StripAxis{a};
        in.parkTop = 5000;
        return in;
    }

private Q_SLOTS:
    void offTheTrailingEndParksWithTheTrailingEdge_data()
    {
        QTest::addColumn<int>("axisValue");
        QTest::addColumn<QString>("expectedEdge");
        QTest::newRow("horizontal") << int(ScrollAxis::Horizontal) << QStringLiteral("right");
        QTest::newRow("vertical") << int(ScrollAxis::Vertical) << QStringLiteral("bottom");
    }

    /// A MAIN-axis departure always names the end it left by. This is the
    /// positive half of the invariant, and the suite had only the negative
    /// half before.
    void offTheTrailingEndParksWithTheTrailingEdge()
    {
        QFETCH(int, axisValue);
        QFETCH(QString, expectedEdge);
        const auto a = static_cast<ScrollAxis>(axisValue);

        // Entirely past the trailing end of the viewport.
        const ParkResult r = resolveTilePlacement(inputsFor(a, roleRect(a, 1400, 300)), QString());
        QVERIFY(r.parked);
        QCOMPARE(r.emittedEdge, expectedEdge);
        QCOMPARE(r.rememberedEdge.value_or(QString()), expectedEdge);
    }

    void offTheLeadingEndParksWithTheLeadingEdge_data()
    {
        QTest::addColumn<int>("axisValue");
        QTest::addColumn<QString>("expectedEdge");
        QTest::newRow("horizontal") << int(ScrollAxis::Horizontal) << QStringLiteral("left");
        QTest::newRow("vertical") << int(ScrollAxis::Vertical) << QStringLiteral("top");
    }

    void offTheLeadingEndParksWithTheLeadingEdge()
    {
        QFETCH(int, axisValue);
        QFETCH(QString, expectedEdge);
        const auto a = static_cast<ScrollAxis>(axisValue);

        const ParkResult r = resolveTilePlacement(inputsFor(a, roleRect(a, -500, 300)), QString());
        QVERIFY(r.parked);
        QCOMPARE(r.emittedEdge, expectedEdge);
    }

    void crossAxisOverflowParksWithoutAnEdge_data()
    {
        QTest::addColumn<int>("axisValue");
        QTest::newRow("horizontal") << int(ScrollAxis::Horizontal);
        QTest::newRow("vertical") << int(ScrollAxis::Vertical);
    }

    /// THE ROLE PAIR. A park caused by stack overflow carries NO edge, because
    /// the strip made no departure — enforced in both crop and clamp mode.
    /// Under transposition this arm swaps physical axes with the one above, so
    /// keying it on x-vs-y rather than main-vs-cross fails exactly one row.
    void crossAxisOverflowParksWithoutAnEdge()
    {
        QFETCH(int, axisValue);
        const auto a = static_cast<ScrollAxis>(axisValue);
        const StripAxis axis{a};

        // On screen along the strip, but entirely beyond the cross extent.
        QRect tile = axis.makeRect(100, axis.crossSize(screenFor(a)) + 200, 300, 100);
        ParkInputs in = inputsFor(a, tile);
        in.columnRect = tile;
        // Crop mode ON: the cross-axis arm must still enforce, because crop
        // mode opts out of the MAIN-axis straddle only.
        in.cropStraddlers = true;

        const ParkResult r = resolveTilePlacement(in, QString());
        QVERIFY2(r.parked, "cross-axis overflow must park even in crop mode");
        QVERIFY2(r.emittedEdge.isEmpty(), "a cross-axis park carries no edge: the strip made no departure");
    }

    void aCrossAxisStraddlerClampsToTheCrossEdge_data()
    {
        QTest::addColumn<int>("axisValue");
        QTest::newRow("horizontal") << int(ScrollAxis::Horizontal);
        QTest::newRow("vertical") << int(ScrollAxis::Vertical);
    }

    /// The clamping half of the cross-axis arm, which the parking cases above
    /// cannot distinguish: an entirely-beyond tile parks either way, so only a
    /// STRADDLER with enough visible pins down which edge gets moved. Added
    /// after a mutation of the cross-axis test survived the parking cases.
    void aCrossAxisStraddlerClampsToTheCrossEdge()
    {
        QFETCH(int, axisValue);
        const auto a = static_cast<ScrollAxis>(axisValue);
        const StripAxis axis{a};
        const QRect screen = screenFor(a);

        // Starts well inside the cross extent and runs 200px past its end, so
        // the visible remainder clears the 48px floor and the tile clamps.
        const int crossStart = axis.crossSize(screen) - 300;
        const QRect tile = axis.makeRect(100, crossStart, 300, 500);
        ParkInputs in = inputsFor(a, tile);
        in.columnRect = tile;

        const ParkResult r = resolveTilePlacement(in, QString());
        QVERIFY2(!r.parked, "a cross-axis straddler with room to spare clamps rather than parking");
        QCOMPARE(axis.crossHigh(r.rect), axis.crossHigh(screen));
        // The MAIN extent is untouched: this arm only trims across the strip.
        QCOMPARE(axis.mainPos(r.rect), 100);
        QCOMPARE(axis.mainSize(r.rect), 300);
        QVERIFY(r.emittedEdge.isEmpty());
    }

    void cropModeOptsOutOfTheMainAxisClampOnly_data()
    {
        QTest::addColumn<int>("axisValue");
        QTest::newRow("horizontal") << int(ScrollAxis::Horizontal);
        QTest::newRow("vertical") << int(ScrollAxis::Vertical);
    }

    /// The other half of the same asymmetry: a main-axis straddler keeps its
    /// TRUE rect under crop mode, where the cross-axis case above does not.
    void cropModeOptsOutOfTheMainAxisClampOnly()
    {
        QFETCH(int, axisValue);
        const auto a = static_cast<ScrollAxis>(axisValue);
        const StripAxis axis{a};

        // Straddles the trailing end with plenty visible.
        const QRect tile = roleRect(a, 1000, 400);

        ParkInputs clamped = inputsFor(a, tile);
        const ParkResult clampResult = resolveTilePlacement(clamped, QString());
        QVERIFY(!clampResult.parked);
        QCOMPARE(axis.mainHigh(clampResult.rect), axis.mainHigh(screenFor(a)));

        ParkInputs cropped = inputsFor(a, tile);
        cropped.cropStraddlers = true;
        const ParkResult cropResult = resolveTilePlacement(cropped, QString());
        QVERIFY(!cropResult.parked);
        QCOMPARE(cropResult.rect, tile); // true rect kept
    }

    void aThinRemainderParksInsteadOfPeeking_data()
    {
        QTest::addColumn<int>("axisValue");
        QTest::newRow("horizontal") << int(ScrollAxis::Horizontal);
        QTest::newRow("vertical") << int(ScrollAxis::Vertical);
    }

    /// Below the peek floor the straddler parks rather than committing a
    /// sliver the client's minimum would immediately fight.
    void aThinRemainderParksInsteadOfPeeking()
    {
        QFETCH(int, axisValue);
        const auto a = static_cast<ScrollAxis>(axisValue);

        // Only 10px of main extent left on screen, under the 48px floor.
        const ParkResult r = resolveTilePlacement(inputsFor(a, roleRect(a, 1190, 400)), QString());
        QVERIFY(r.parked);
        QVERIFY(!r.emittedEdge.isEmpty());
    }

    void arrivingOnScreenConsumesTheRememberedEdge_data()
    {
        QTest::addColumn<int>("axisValue");
        QTest::newRow("horizontal") << int(ScrollAxis::Horizontal);
        QTest::newRow("vertical") << int(ScrollAxis::Vertical);
    }

    /// A window coming back on screen emits the edge it left by, and the
    /// memory is consumed so a later unrelated move is not re-anchored to it.
    void arrivingOnScreenConsumesTheRememberedEdge()
    {
        QFETCH(int, axisValue);
        const auto a = static_cast<ScrollAxis>(axisValue);

        const ParkResult r =
            resolveTilePlacement(inputsFor(a, roleRect(a, 100, 300)), QStringLiteral("remembered-edge"));
        QVERIFY(!r.parked);
        QCOMPARE(r.emittedEdge, QStringLiteral("remembered-edge"));
        QVERIFY2(!r.rememberedEdge.has_value(), "arrival must erase the memory, not keep it");
    }

    void aCrossAxisParkHandsBackAConsumedMainEdge_data()
    {
        QTest::addColumn<int>("axisValue");
        QTest::newRow("horizontal") << int(ScrollAxis::Horizontal);
        QTest::newRow("vertical") << int(ScrollAxis::Vertical);
    }

    /// The subtle one. A tile arriving on screen consumes its remembered
    /// main-axis edge, and then overflows the cross axis and parks. That park
    /// has no side to animate from, so the edge must be put BACK rather than
    /// emitted — it still describes the eventual main-axis arrival.
    void aCrossAxisParkHandsBackAConsumedMainEdge()
    {
        QFETCH(int, axisValue);
        const auto a = static_cast<ScrollAxis>(axisValue);
        const StripAxis axis{a};

        QRect tile = axis.makeRect(100, axis.crossSize(screenFor(a)) + 200, 300, 100);
        ParkInputs in = inputsFor(a, tile);
        in.columnRect = tile;

        const ParkResult r = resolveTilePlacement(in, QStringLiteral("carried"));
        QVERIFY(r.parked);
        QVERIFY2(r.emittedEdge.isEmpty(), "a cross-axis park emits no edge");
        QCOMPARE(r.rememberedEdge.value_or(QString()), QStringLiteral("carried"));
    }

    /// parkRect is deliberately physical on both axes: its safety claim is
    /// "below every output", which is about the desktop, not the strip.
    void parkIsPhysicalOnBothAxes()
    {
        for (const auto a : {ScrollAxis::Horizontal, ScrollAxis::Vertical}) {
            QRect rect(100, 100, 300, 200);
            parkRect(rect, screenFor(a), 5000);
            QCOMPARE(rect.top(), 5000);
            QCOMPARE(rect.size(), QSize(300, 200));
        }
    }
};

QTEST_APPLESS_MAIN(TestScrollPark)
#include "test_scrollpark.moc"
