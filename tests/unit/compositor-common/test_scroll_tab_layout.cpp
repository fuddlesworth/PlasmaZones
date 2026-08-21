// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Layout + raster contract of the scrolling tab indicators.
//
// ScrollTabRaster::layoutPills() is the single source of the pills' geometry:
// rasterise() iterates the same rects, and the effect hit-tests them, so a
// drift here shows up as pills that are drawn in one place and clickable in
// another. The cases below pin the arithmetic the QML port fixed in place —
// contiguous segments at zero gap, the last tab absorbing the division
// remainder, the chip inset derived from smallSpacing, and every hit rect
// clipped to the indicator it belongs to.

#include "compositor/scrolltabindicatorpainter.h"

#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QTest>
#include <QVector>

#include <cmath>

using namespace PlasmaZones;

namespace {

ScrollTabPill makePill(const QString& id, bool active = false)
{
    ScrollTabPill pill;
    pill.windowId = id;
    pill.title = id;
    pill.active = active;
    return pill;
}

ScrollTabIndicator makeIndicator(const QRect& rect, int position, int tabCount)
{
    ScrollTabIndicator indicator;
    indicator.rect = rect;
    indicator.position = position;
    for (int i = 0; i < tabCount; ++i) {
        indicator.tabs.append(makePill(QStringLiteral("w%1").arg(i), i == 0));
    }
    return indicator;
}

/// One indicator style with the shipped spacing defaults and a theme that
/// resolves to opaque colours, so a rasterised segment is unambiguously
/// non-transparent. @p style is the raw setting value: 0 is the title chips,
/// 1 is the segment bar. Several tests below pass 0, so this is NOT a
/// bar-only helper.
ScrollTabIndicatorStyle makeStyle(int style, int gaps)
{
    ScrollTabIndicatorStyle s;
    s.style = style;
    s.gapsBetweenTabs = gaps;
    s.cornerRadius = 0;
    s.smallSpacing = 4;
    s.largeSpacing = 8;
    s.themeHighlight = QColor(0, 120, 215);
    s.themeHighlightedText = QColor(255, 255, 255);
    s.themeText = QColor(20, 20, 20);
    s.themeBackground = QColor(240, 240, 240);
    s.themeNegativeText = QColor(200, 40, 40);
    s.font = QFont(QStringLiteral("sans"), 10);
    return s;
}

} // namespace

class TestScrollTabLayout : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void barSingleTabFillsRect();
    void barSegmentsAreContiguousAtZeroGap();
    void barSegmentsStepByBasePlusGap();
    void chipInsetAndThickness();
    void chipLastAbsorbsRemainder();
    void verticalPositionsStackAlongY();
    void horizontalPositionsStackAlongX();
    void emptyModelsProduceNoHits();
    void overflowingRunStaysInsideRect();
    void extremeOverflowNeverEscapesRect();
    void rasteriseSmoke_data();
    void rasteriseSmoke();
    void chipLabelFitsInsideTheChip();
};

void TestScrollTabLayout::barSingleTabFillsRect()
{
    const QRect rect(100, 50, 300, 20);
    const auto hits = ScrollTabRaster::layoutPills(makeIndicator(rect, 2, 1), makeStyle(1, 0));

    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.at(0).rect, rect);
    QCOMPARE(hits.at(0).windowId, QStringLiteral("w0"));
}

void TestScrollTabLayout::barSegmentsAreContiguousAtZeroGap()
{
    const QRect rect(100, 50, 300, 20);
    const auto hits = ScrollTabRaster::layoutPills(makeIndicator(rect, 2, 3), makeStyle(1, 0));

    QCOMPARE(hits.size(), 3);
    int total = 0;
    for (int i = 0; i < hits.size(); ++i) {
        total += hits.at(i).rect.width();
        QCOMPARE(hits.at(i).rect.height(), rect.height());
        QCOMPARE(hits.at(i).rect.y(), rect.y());
        if (i > 0) {
            // Contiguous: each segment starts exactly where the previous ended.
            QCOMPARE(hits.at(i).rect.left(), hits.at(i - 1).rect.right() + 1);
        }
    }
    // The last segment absorbs the division remainder, so the run fills the
    // long extent exactly rather than leaving up to tabCount-1 stray pixels.
    QCOMPARE(total, rect.width());
    QCOMPARE(hits.last().rect.right(), rect.right());
}

void TestScrollTabLayout::barSegmentsStepByBasePlusGap()
{
    const QRect rect(100, 50, 300, 20);
    const int gaps = 4;
    const auto hits = ScrollTabRaster::layoutPills(makeIndicator(rect, 2, 3), makeStyle(1, gaps));

    QCOMPARE(hits.size(), 3);
    // base = (300 - 4*2) / 3 = 97; offsets step by base + gap.
    const int base = 97;
    for (int i = 0; i < hits.size(); ++i) {
        QCOMPARE(hits.at(i).rect.x(), rect.x() + i * (base + gaps));
    }
    QCOMPARE(hits.at(0).rect.width(), base);
    QCOMPARE(hits.at(1).rect.width(), base);
    // Last absorbs the remainder against the rect's end, gaps included.
    QCOMPARE(hits.last().rect.right(), rect.right());
}

void TestScrollTabLayout::chipInsetAndThickness()
{
    // inset = max(1, round(smallSpacing / 2)) = 2 at the default spacing;
    // thickness = shortExtent - 2 * inset.
    const QRect rect(0, 0, 200, 30);
    const auto hits = ScrollTabRaster::layoutPills(makeIndicator(rect, 3, 2), makeStyle(0, 0));

    QCOMPARE(hits.size(), 2);
    const int inset = 2;
    for (const ScrollTabHitRect& hit : hits) {
        QCOMPARE(hit.rect.y(), rect.y() + inset);
        QCOMPARE(hit.rect.height(), rect.height() - 2 * inset);
    }
    QCOMPARE(hits.at(0).rect.x(), rect.x() + inset);
}

void TestScrollTabLayout::chipLastAbsorbsRemainder()
{
    const QRect rect(0, 0, 200, 30);
    const auto hits = ScrollTabRaster::layoutPills(makeIndicator(rect, 3, 2), makeStyle(0, 0));

    QCOMPARE(hits.size(), 2);
    // The run ends flush with the pill's inner edge, one inset in from the
    // indicator's own edge.
    QCOMPARE(hits.last().rect.right(), rect.right() - 2);
}

void TestScrollTabLayout::verticalPositionsStackAlongY()
{
    const QRect rect(10, 20, 40, 300);
    for (int position : {0, 1}) {
        const auto hits = ScrollTabRaster::layoutPills(makeIndicator(rect, position, 3), makeStyle(1, 0));
        QCOMPARE(hits.size(), 3);
        for (int i = 0; i < hits.size(); ++i) {
            // Left/Right run the tabs DOWN the column: x and width are shared.
            QCOMPARE(hits.at(i).rect.x(), rect.x());
            QCOMPARE(hits.at(i).rect.width(), rect.width());
            if (i > 0) {
                QVERIFY(hits.at(i).rect.y() > hits.at(i - 1).rect.y());
            }
        }
        QCOMPARE(hits.last().rect.bottom(), rect.bottom());
    }
}

void TestScrollTabLayout::horizontalPositionsStackAlongX()
{
    const QRect rect(10, 20, 300, 40);
    for (int position : {2, 3}) {
        const auto hits = ScrollTabRaster::layoutPills(makeIndicator(rect, position, 3), makeStyle(1, 0));
        QCOMPARE(hits.size(), 3);
        for (int i = 0; i < hits.size(); ++i) {
            QCOMPARE(hits.at(i).rect.y(), rect.y());
            QCOMPARE(hits.at(i).rect.height(), rect.height());
            if (i > 0) {
                QVERIFY(hits.at(i).rect.x() > hits.at(i - 1).rect.x());
            }
        }
        QCOMPARE(hits.last().rect.right(), rect.right());
    }
}

void TestScrollTabLayout::emptyModelsProduceNoHits()
{
    for (int style : {0, 1}) {
        // No tabs at all.
        QVERIFY(ScrollTabRaster::layoutPills(makeIndicator(QRect(0, 0, 200, 30), 2, 0), makeStyle(style, 0)).isEmpty());
        // A resolved rect with no area.
        QVERIFY(ScrollTabRaster::layoutPills(makeIndicator(QRect(), 2, 3), makeStyle(style, 0)).isEmpty());
        QVERIFY(ScrollTabRaster::layoutPills(makeIndicator(QRect(5, 5, 0, 30), 2, 3), makeStyle(style, 0)).isEmpty());
    }
}

void TestScrollTabLayout::overflowingRunStaysInsideRect()
{
    // 30 logical px for 20 tabs: every share floors at 1, and the last one
    // still lands inside the rect. Nothing may be dropped or escape.
    const QRect rect(0, 0, 30, 20);
    for (int style : {0, 1}) {
        const auto hits = ScrollTabRaster::layoutPills(makeIndicator(rect, 2, 20), makeStyle(style, 0));
        QCOMPARE(hits.size(), 20);
        for (const ScrollTabHitRect& hit : hits) {
            QVERIFY(!hit.rect.isEmpty());
            QVERIFY(rect.contains(hit.rect));
        }
    }
}

void TestScrollTabLayout::extremeOverflowNeverEscapesRect()
{
    // Short enough that the tail of the run is clipped away entirely. Clipped
    // tabs come back as empty rects (neither drawn nor clickable), which is
    // what the QML delegate's clip did; what must never happen is a hit rect
    // reaching past the indicator onto the neighbouring window.
    const QRect rect(40, 40, 10, 20);
    for (int style : {0, 1}) {
        const auto hits = ScrollTabRaster::layoutPills(makeIndicator(rect, 2, 20), makeStyle(style, 0));
        QCOMPARE(hits.size(), 20);
        for (const ScrollTabHitRect& hit : hits) {
            QVERIFY(hit.rect.isEmpty() || rect.contains(hit.rect));
        }
        QVERIFY(!hits.at(0).rect.isEmpty());
    }
}

void TestScrollTabLayout::rasteriseSmoke_data()
{
    // The style column is the raw setting int, which is what makeStyle()
    // already takes — an int needs no Q_DECLARE_METATYPE, a
    // ScrollTabIndicatorStyle column would.
    QTest::addColumn<int>("style");
    QTest::addColumn<qreal>("dpr");
    // Both styles at both scales. The chips rows are the only ones that reach
    // the pill fill, the chip fill and the label fit at all: the segment bar
    // returns before every one of them.
    QTest::newRow("bar dpr 1.0") << 1 << qreal(1.0);
    QTest::newRow("bar dpr 2.0") << 1 << qreal(2.0);
    QTest::newRow("chips dpr 1.0") << 0 << qreal(1.0);
    QTest::newRow("chips dpr 2.0") << 0 << qreal(2.0);
}

void TestScrollTabLayout::rasteriseSmoke()
{
    QFETCH(int, style);
    QFETCH(qreal, dpr);

    const QRect rect(100, 50, 300, 20);
    const ScrollTabIndicator indicator = makeIndicator(rect, 2, 2);
    const ScrollTabIndicatorStyle tabStyle = makeStyle(style, 0);

    const QImage image = ScrollTabRaster::rasterise({indicator}, tabStyle, rect, dpr, QString());

    QVERIFY(!image.isNull());
    QCOMPARE(image.format(), QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(image.width(), int(std::ceil(rect.width() * dpr)));
    QCOMPARE(image.height(), int(std::ceil(rect.height() * dpr)));

    // The active segment is the first one, filled with the theme highlight.
    // Its centre, converted from absolute logical to device pixels, must carry
    // paint rather than the transparent fill.
    const auto hits = ScrollTabRaster::layoutPills(indicator, tabStyle);
    QCOMPARE(hits.size(), 2);
    const QPoint centre = hits.at(0).rect.center() - rect.topLeft();
    const int x = int(centre.x() * dpr);
    const int y = int(centre.y() * dpr);
    QVERIFY(x >= 0 && x < image.width());
    QVERIFY(y >= 0 && y < image.height());
    QVERIFY(image.pixelColor(x, y).alpha() > 0);

    if (style != 0) {
        return;
    }
    // For CHIPS that alpha assertion passes VACUOUSLY: the pill behind the
    // chips is filled across the whole indicator at 0.85, so every pixel of
    // the image is non-transparent whether a chip was drawn or not.
    // Discriminate by opacity instead. The active chip is an OPAQUE fill over
    // that pill, while the inset band between the pill's edge and the chips
    // can only ever carry the translucent pill.
    QCOMPARE(image.pixelColor(x, y).alpha(), 255);
    // One and a half logical px inside the indicator's top edge: inside the
    // two-px chip inset and clear of the half-covered outermost row, so it is
    // pill and never chip. Sampled at the centre of the logical pixel so the
    // reading does not depend on the scale.
    const int bandX = int((centre.x() + 0.5) * dpr);
    const int bandY = int(1.5 * dpr);
    QVERIFY(bandY < image.height());
    const int bandAlpha = image.pixelColor(bandX, bandY).alpha();
    QVERIFY2(bandAlpha > 0 && bandAlpha < 255,
             qPrintable(
                 QStringLiteral("inset band alpha %1: expected the translucent pill, not a chip fill").arg(bandAlpha)));
}

void TestScrollTabLayout::chipLabelFitsInsideTheChip()
{
    // The chip style fits the label to the chip's THICKNESS — there is no font
    // size setting, so Width gives the pill its thickness and the painter walks
    // the pixel size down until the line box fits. The property that matters is
    // that the fitted glyphs stay INSIDE the chip, because the indicator clips
    // and an overflowing label is silently cut rather than shrunk.
    //
    // Read off the INACTIVE chip. Its fill is fully transparent, so inside its
    // rect the only two things on the image are the pill backdrop
    // (themeBackground at 0.85, a light grey) and the label drawn in themeText
    // (near-black). That turns "is this pixel label ink?" into a plain darkness
    // test with no dependence on which glyphs a given system renders.
    // This is the one test in the file that needs a real typeface: it reads
    // glyph ink off the raster. An image with no fonts installed renders an
    // empty glyph run, which would make the two "no ink at the edge"
    // assertions pass while proving nothing. Skip loudly there rather than
    // either failing a legitimate environment or passing a useless one.
    if (QFontDatabase::families().isEmpty()) {
        QSKIP("no font families installed, so there is no label ink to measure");
    }

    const QRect rect(0, 0, 400, 40);
    const ScrollTabIndicator indicator = makeIndicator(rect, 2, 2);
    const ScrollTabIndicatorStyle style = makeStyle(0, 0);

    // dpr 1.0 with the indicator anchored at the origin, so image coordinates
    // and absolute logical coordinates are the same thing throughout.
    const QImage image = ScrollTabRaster::rasterise({indicator}, style, rect, 1.0, QString());
    QVERIFY(!image.isNull());

    const auto hits = ScrollTabRaster::layoutPills(indicator, style);
    QCOMPARE(hits.size(), 2);
    // The second tab, which makeIndicator leaves inactive.
    const QRect chip = hits.at(1).rect;
    QVERIFY(!chip.isEmpty());
    QVERIFY(chip.height() >= 8); // enough rows for a top, a bottom and a middle

    const auto inkInRow = [&image, &chip](int y) {
        int count = 0;
        for (int x = chip.left(); x <= chip.right(); ++x) {
            // The pill backdrop reads back light; the label near-black. Half
            // way between, so an antialiased glyph edge lands on whichever
            // side it is nearer.
            if (image.pixelColor(x, y).red() < 128) {
                ++count;
            }
        }
        return count;
    };

    // The honesty guard, and it MUST come first. On an image with no usable
    // fonts the glyph run is empty, every row reads zero ink, and the two
    // "no ink" assertions below would pass while proving nothing. A zero here
    // is a broken environment, not a passing fit.
    int middleInk = 0;
    const int quarter = chip.height() / 4;
    for (int y = chip.top() + quarter; y <= chip.bottom() - quarter; ++y) {
        middleInk += inkInRow(y);
    }
    QVERIFY2(middleInk > 0,
             "no label ink in the middle of the chip: the font produced no glyphs, so the fit is unverified");

    // The fit's actual claim. The label's line box is centred in the chip and
    // is at most the chip's thickness less the two-px margin, so the chip's own
    // first and last rows carry pill backdrop and never a glyph.
    QCOMPARE(inkInRow(chip.top()), 0);
    QCOMPARE(inkInRow(chip.bottom()), 0);
}

QTEST_MAIN(TestScrollTabLayout)
#include "test_scroll_tab_layout.moc"
