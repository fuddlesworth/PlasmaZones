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

/// Bar style with the shipped spacing defaults and a theme that resolves to
/// opaque colours, so a rasterised segment is unambiguously non-transparent.
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
    QTest::addColumn<qreal>("dpr");
    QTest::newRow("dpr 1.0") << qreal(1.0);
    QTest::newRow("dpr 2.0") << qreal(2.0);
}

void TestScrollTabLayout::rasteriseSmoke()
{
    QFETCH(qreal, dpr);

    const QRect rect(100, 50, 300, 20);
    const ScrollTabIndicator indicator = makeIndicator(rect, 2, 2);
    const ScrollTabIndicatorStyle style = makeStyle(1, 0);

    const QImage image = ScrollTabRaster::rasterise({indicator}, style, rect, dpr, QString());

    QVERIFY(!image.isNull());
    QCOMPARE(image.format(), QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(image.width(), int(std::ceil(rect.width() * dpr)));
    QCOMPARE(image.height(), int(std::ceil(rect.height() * dpr)));

    // The active segment is the first one, filled with the theme highlight.
    // Its centre, converted from absolute logical to device pixels, must carry
    // paint rather than the transparent fill.
    const auto hits = ScrollTabRaster::layoutPills(indicator, style);
    QCOMPARE(hits.size(), 2);
    const QPoint centre = hits.at(0).rect.center() - rect.topLeft();
    const int x = int(centre.x() * dpr);
    const int y = int(centre.y() * dpr);
    QVERIFY(x >= 0 && x < image.width());
    QVERIFY(y >= 0 && y < image.height());
    QVERIFY(image.pixelColor(x, y).alpha() > 0);
}

QTEST_MAIN(TestScrollTabLayout)
#include "test_scroll_tab_layout.moc"
