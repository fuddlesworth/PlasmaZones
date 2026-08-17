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

    /// The dual-axis table every slot below runs on. It was copied out
    /// verbatim eight times, so a row rename had eight places to miss.
    static void addAxisRows()
    {
        QTest::addColumn<int>("axisValue");
        QTest::newRow("horizontal") << int(ScrollAxis::Horizontal);
        QTest::newRow("vertical") << int(ScrollAxis::Vertical);
    }

    /// The same table plus the edge name the strip's TRAILING end carries on
    /// each axis, which is the whole point of running these slots twice.
    static void addAxisTrailingEdgeRows()
    {
        QTest::addColumn<int>("axisValue");
        QTest::addColumn<QString>("expectedEdge");
        QTest::newRow("horizontal") << int(ScrollAxis::Horizontal) << QStringLiteral("right");
        QTest::newRow("vertical") << int(ScrollAxis::Vertical) << QStringLiteral("bottom");
    }

    static void addAxisLeadingEdgeRows()
    {
        QTest::addColumn<int>("axisValue");
        QTest::addColumn<QString>("expectedEdge");
        QTest::newRow("horizontal") << int(ScrollAxis::Horizontal) << QStringLiteral("left");
        QTest::newRow("vertical") << int(ScrollAxis::Vertical) << QStringLiteral("top");
    }

private Q_SLOTS:
    void offTheTrailingEndParksWithTheTrailingEdge_data()
    {
        addAxisTrailingEdgeRows();
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
        addAxisLeadingEdgeRows();
    }

    void offTheLeadingEndParksWithTheLeadingEdge()
    {
        QFETCH(int, axisValue);
        QFETCH(QString, expectedEdge);
        const auto a = static_cast<ScrollAxis>(axisValue);

        const ParkResult r = resolveTilePlacement(inputsFor(a, roleRect(a, -500, 300)), QString());
        QVERIFY(r.parked);
        QCOMPARE(r.emittedEdge, expectedEdge);
        QCOMPARE(r.rememberedEdge.value_or(QString()), expectedEdge);
    }

    void theWorkAreaNotTheScreenDecidesDeparture_data()
    {
        addAxisTrailingEdgeRows();
    }

    /// Every other fixture here sets workArea == screenRect, which makes the
    /// two interchangeable: the departure test reads the WORK AREA, the park
    /// and the clamps read the SCREEN, and a swap of the two is invisible while
    /// they are equal. This slot insets the work area's trailing end by 100px
    /// and places a tile past it but still inside the screen, so reading the
    /// screen there clamps instead of parking.
    void theWorkAreaNotTheScreenDecidesDeparture()
    {
        QFETCH(int, axisValue);
        QFETCH(QString, expectedEdge);
        const auto a = static_cast<ScrollAxis>(axisValue);
        const StripAxis axis{a};
        const QRect screen = screenFor(a);

        ParkInputs in = inputsFor(a, roleRect(a, 1150, 200));
        in.workArea = axis.makeRect(0, 0, 1100, axis.crossSize(screen));

        const ParkResult r = resolveTilePlacement(in, QString());
        QVERIFY2(r.parked, "past the work area's trailing end is a departure, screen room or not");
        QCOMPARE(r.emittedEdge, expectedEdge);
        QCOMPARE(r.rememberedEdge.value_or(QString()), expectedEdge);
    }

    void crossAxisOverflowParksWithoutAnEdge_data()
    {
        addAxisRows();
    }

    /// THE ROLE PAIR. A park caused by stack overflow carries NO edge, because
    /// the strip made no departure. This slot drives the CROP-mode half of
    /// that, which is the half a main-axis reading of the arm would let
    /// through; the clamp-mode half is the slot below. Under transposition
    /// this arm swaps physical axes with the one above, so keying it on
    /// x-vs-y rather than main-vs-cross fails exactly one row.
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
        addAxisRows();
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
        addAxisRows();
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
        addAxisTrailingEdgeRows();
    }

    /// Below the peek floor the straddler parks rather than committing a
    /// sliver the client's minimum would immediately fight. The edge is
    /// pinned, not merely required non-empty: this tile straddles the TRAILING
    /// end, so an arm that parks it with the leading edge is as wrong as one
    /// that emits nothing, and only the exact value tells them apart.
    void aThinRemainderParksInsteadOfPeeking()
    {
        QFETCH(int, axisValue);
        QFETCH(QString, expectedEdge);
        const auto a = static_cast<ScrollAxis>(axisValue);

        // Only 10px of main extent left on screen, under the 48px floor.
        const ParkResult r = resolveTilePlacement(inputsFor(a, roleRect(a, 1190, 400)), QString());
        QVERIFY(r.parked);
        QCOMPARE(r.emittedEdge, expectedEdge);
        QCOMPARE(r.rememberedEdge.value_or(QString()), expectedEdge);
    }

    void arrivingOnScreenConsumesTheRememberedEdge_data()
    {
        addAxisRows();
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
        addAxisRows();
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

    void aLeadingStraddlerClampsAtTheLeadingEdge_data()
    {
        addAxisRows();
    }

    /// The LEADING half of the main-axis straddle clamp — the trailing half
    /// has rows above, but the leading arm (straddleLow: setMainLow at the
    /// screen's near edge) had none, so deleting it kept the suite green.
    void aLeadingStraddlerClampsAtTheLeadingEdge()
    {
        QFETCH(int, axisValue);
        const auto a = static_cast<ScrollAxis>(axisValue);
        const StripAxis axis{a};

        // Overhangs the leading end by 100px with 300px visible — far over
        // the 48px floor, so it clamps rather than parking.
        const ParkResult r = resolveTilePlacement(inputsFor(a, roleRect(a, -100, 400)), QString());
        QVERIFY2(!r.parked, "a leading straddler with room to spare clamps rather than parking");
        QCOMPARE(axis.mainPos(r.rect), axis.mainLow(screenFor(a)));
        QCOMPARE(axis.mainHigh(r.rect), 299);
        QVERIFY(r.emittedEdge.isEmpty());
    }

    void aCrossStraddlerBelowTheFloorHandsBackAConsumedMainEdge_data()
    {
        addAxisRows();
    }

    /// The STRADDLER twin of aCrossAxisParkHandsBackAConsumedMainEdge: the
    /// fully-beyond arm has a row above, but the below-floor straddler arm
    /// carries its own copy of the remembered-edge hand-back, and nothing
    /// drove it — an arm that emitted the consumed edge (or dropped it)
    /// stayed green.
    void aCrossStraddlerBelowTheFloorHandsBackAConsumedMainEdge()
    {
        QFETCH(int, axisValue);
        const auto a = static_cast<ScrollAxis>(axisValue);
        const StripAxis axis{a};
        const QRect screen = screenFor(a);

        // On screen along the strip, straddling the cross edge with only
        // 10px visible — under the 48px floor, so it parks.
        const QRect tile = axis.makeRect(100, axis.crossSize(screen) - 10, 300, 200);
        ParkInputs in = inputsFor(a, tile);
        in.columnRect = tile;

        const ParkResult r = resolveTilePlacement(in, QStringLiteral("carried"));
        QVERIFY2(r.parked, "10px of cross visibility is under the floor and must park");
        QVERIFY2(r.emittedEdge.isEmpty(), "a cross-axis park emits no edge");
        QCOMPARE(r.rememberedEdge.value_or(QString()), QStringLiteral("carried"));
    }

    void aHiddenTabFollowsItsColumnsSide_data()
    {
        QTest::addColumn<int>("axisValue");
        QTest::addColumn<int>("columnMainPos");
        QTest::addColumn<QString>("expectedEdge");
        QTest::newRow("horizontal leading") << int(ScrollAxis::Horizontal) << -500 << QStringLiteral("left");
        QTest::newRow("horizontal trailing") << int(ScrollAxis::Horizontal) << 1400 << QStringLiteral("right");
        QTest::newRow("vertical leading") << int(ScrollAxis::Vertical) << -500 << QStringLiteral("top");
        QTest::newRow("vertical trailing") << int(ScrollAxis::Vertical) << 1400 << QStringLiteral("bottom");
    }

    /// The HIDDEN arm, never driven before: a tabbed column's non-active tile
    /// parks unconditionally, and its departure side is a fact about the
    /// COLUMN, not the tile. The tile rect here is deliberately ON screen, so
    /// an arm that reads the tile instead of the column answers "no edge" and
    /// fails every row.
    void aHiddenTabFollowsItsColumnsSide()
    {
        QFETCH(int, axisValue);
        QFETCH(int, columnMainPos);
        QFETCH(QString, expectedEdge);
        const auto a = static_cast<ScrollAxis>(axisValue);

        ParkInputs in = inputsFor(a, roleRect(a, 100, 300));
        in.columnRect = roleRect(a, columnMainPos, 300);
        in.hidden = true;

        const ParkResult r = resolveTilePlacement(in, QString());
        QVERIFY2(r.parked, "a hidden tab parks regardless of its own rect");
        QCOMPARE(r.emittedEdge, expectedEdge);
        QCOMPARE(r.rememberedEdge.value_or(QString()), expectedEdge);
    }

    void aHiddenTabOfAnOnScreenColumnParksEdgeless_data()
    {
        addAxisRows();
    }

    /// The other half of the hidden arm: an ON-screen column's hidden tabs
    /// are parked for input reasons, not because the strip scrolled them
    /// away, so no edge is emitted AND a stale remembered departure is
    /// erased — leaving it would slide the next tab switch in from a side
    /// the column has already returned from.
    void aHiddenTabOfAnOnScreenColumnParksEdgeless()
    {
        QFETCH(int, axisValue);
        const auto a = static_cast<ScrollAxis>(axisValue);

        ParkInputs in = inputsFor(a, roleRect(a, 100, 300));
        in.columnRect = roleRect(a, 100, 300);
        in.hidden = true;

        const ParkResult r = resolveTilePlacement(in, QStringLiteral("stale"));
        QVERIFY(r.parked);
        QVERIFY2(r.emittedEdge.isEmpty(), "no departure happened, so no edge may be emitted");
        QVERIFY2(!r.rememberedEdge.has_value(), "the stale memory must be erased, not kept or emitted");
    }

    void theClientMinimumRaisesThePeekFloor_data()
    {
        addAxisTrailingEdgeRows();
    }

    /// Under respectMinimumSize the peek floor rises from the 48px constant to
    /// the client's declared minimum ALONG the strip. The straddler here keeps
    /// 100px visible — comfortably over the constant, as the control leg
    /// proves — and still parks once a 300px minimum is declared, because a
    /// committed 100px extent is one the client would refuse and regrow across
    /// the boundary.
    void theClientMinimumRaisesThePeekFloor()
    {
        QFETCH(int, axisValue);
        QFETCH(QString, expectedEdge);
        const auto a = static_cast<ScrollAxis>(axisValue);
        const StripAxis axis{a};

        const QRect tile = roleRect(a, 1100, 400); // 100px visible at the trailing end

        // Control: with no declared minimum the same straddler clamps.
        const ParkResult clamped = resolveTilePlacement(inputsFor(a, tile), QString());
        QVERIFY2(!clamped.parked, "precondition: 100px visible clears the constant floor");

        ParkInputs in = inputsFor(a, tile);
        in.tileMin = axis.makeRect(0, 0, 300, 0).size();
        const ParkResult r = resolveTilePlacement(in, QString());
        QVERIFY2(r.parked, "a visible span under the client minimum must park, not peek");
        QCOMPARE(r.emittedEdge, expectedEdge);
    }

    void anOversizedMinimumStillPeeksAtFullScreen_data()
    {
        addAxisTrailingEdgeRows();
    }

    /// The cap on that floor: a declared minimum LARGER than the screen would
    /// otherwise park the column forever (no straddle can keep more visible
    /// than the screen holds). Capped at the screen's extent, a straddler
    /// covering the whole screen clamps to it — the regrow hazard is knowingly
    /// re-accepted, because a straddle beats a permanently invisible window —
    /// while one covering any less still parks.
    void anOversizedMinimumStillPeeksAtFullScreen()
    {
        QFETCH(int, axisValue);
        QFETCH(QString, expectedEdge);
        const auto a = static_cast<ScrollAxis>(axisValue);
        const StripAxis axis{a};
        const QRect screen = screenFor(a);

        const QSize oversizedMin = axis.makeRect(0, 0, 2000, 0).size();

        // Covers the full main extent (0..1499 over a 1200 screen): visible
        // equals the capped floor, so it clamps to exactly the screen.
        ParkInputs covering = inputsFor(a, roleRect(a, 0, 1500));
        covering.tileMin = oversizedMin;
        const ParkResult peeked = resolveTilePlacement(covering, QString());
        QVERIFY2(!peeked.parked, "full-screen coverage must peek, or the column can never return");
        QCOMPARE(axis.mainPos(peeked.rect), axis.mainLow(screen));
        QCOMPARE(axis.mainHigh(peeked.rect), axis.mainHigh(screen));

        // 100px short of full coverage: under the capped floor, parks.
        ParkInputs partial = inputsFor(a, roleRect(a, 100, 1500));
        partial.tileMin = oversizedMin;
        const ParkResult parked = resolveTilePlacement(partial, QString());
        QVERIFY(parked.parked);
        QCOMPARE(parked.emittedEdge, expectedEdge);
    }

    /// parkRect is deliberately physical: its safety claim is "below every
    /// output", which is about the desktop, not the strip. It takes no axis
    /// at all, so there is nothing here to run twice.
    ///
    /// The horizontal-span half of its contract is asserted too. Dropping to
    /// parkTop alone would satisfy the vertical claim while leaving a parked
    /// rect hanging off the side of its own screen, which is the case that
    /// puts it under a NEIGHBOUR output instead of under nothing.
    void parkIsPhysicalAndStaysInItsScreensSpan()
    {
        const QRect screen = screenFor(ScrollAxis::Horizontal);

        // Already inside the span: only the vertical move happens.
        QRect inside(100, 100, 300, 200);
        parkRect(inside, screen, 5000);
        QCOMPARE(inside.top(), 5000);
        QCOMPARE(inside.left(), 100);
        QCOMPARE(inside.size(), QSize(300, 200));

        // Hanging off the trailing side: pulled back so its far edge sits on
        // the screen's, size untouched.
        QRect offTrail(1100, 100, 300, 200);
        parkRect(offTrail, screen, 5000);
        QCOMPARE(offTrail.top(), 5000);
        QCOMPARE(offTrail.right(), screen.right());
        QCOMPARE(offTrail.size(), QSize(300, 200));

        // Hanging off the leading side: pushed back to the screen's near edge.
        QRect offLead(-500, 100, 300, 200);
        parkRect(offLead, screen, 5000);
        QCOMPARE(offLead.top(), 5000);
        QCOMPARE(offLead.left(), screen.left());
        QCOMPARE(offLead.size(), QSize(300, 200));

        // Wider than the screen: it cannot fit, so the near edge wins and the
        // rect is never shrunk to make it fit.
        QRect oversized(-100, 100, 1500, 200);
        parkRect(oversized, screen, 5000);
        QCOMPARE(oversized.top(), 5000);
        QCOMPARE(oversized.left(), screen.left());
        QCOMPARE(oversized.size(), QSize(1500, 200));
    }
};

QTEST_APPLESS_MAIN(TestScrollPark)
#include "test_scrollpark.moc"
