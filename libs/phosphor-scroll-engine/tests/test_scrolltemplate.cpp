// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QtTest>

#include <PhosphorScrollEngine/ScrollTemplate.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

using namespace PhosphorScrollEngine;

/// Pure-function coverage for extractTemplateVocabulary: layout zone rects
/// (normalized 0–1) in, preset column-width / window-height vocabulary out.
/// No engine, no QObject — APPLESS.
class TestScrollTemplate : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void threeEqualColumns();
    void unsortedInputSortsAscending();
    void stackedBandYieldsHeights();
    void soloPartialHeightZoneYieldsHeight();
    void soloFullHeightZoneYieldsNoHeight();
    void misalignedOverlapStartsOwnBand();
    void sliverZonesDropped();
    void duplicateWidthsDeduped();
    void toleranceBoundary();
    void degenerateInputYieldsEmpty();
    void widthsClampedToMinimum();
};

void TestScrollTemplate::threeEqualColumns()
{
    // columns-3.json shape: thirds with float dust from serialization.
    const auto v = extractTemplateVocabulary({
        {0.0, 0.0, 0.333333, 1.0},
        {0.333333, 0.0, 0.333334, 1.0},
        {0.666667, 0.0, 0.333333, 1.0},
    });
    QCOMPARE(v.columnWidths.size(), 1); // three equal widths dedupe to one
    QVERIFY(qAbs(v.columnWidths.first() - 1.0 / 3.0) < 0.01);
    QVERIFY(v.windowHeights.isEmpty()); // full-height solos add no heights
}

void TestScrollTemplate::unsortedInputSortsAscending()
{
    const auto v = extractTemplateVocabulary({
        {0.75, 0.0, 0.25, 1.0},
        {0.0, 0.0, 0.25, 1.0},
        {0.25, 0.0, 0.5, 1.0},
    });
    QCOMPARE(v.columnWidths.size(), 2);
    QVERIFY(v.columnWidths.first() < v.columnWidths.last());
    QVERIFY(qAbs(v.columnWidths.first() - 0.25) < 0.01);
    QVERIFY(qAbs(v.columnWidths.last() - 0.5) < 0.01);
}

void TestScrollTemplate::stackedBandYieldsHeights()
{
    // 25/75 split column on the right: both member heights are presets.
    const auto v = extractTemplateVocabulary({
        {0.0, 0.0, 0.6, 1.0},
        {0.6, 0.0, 0.4, 0.25},
        {0.6, 0.25, 0.4, 0.75},
    });
    QCOMPARE(v.columnWidths.size(), 2);
    QCOMPARE(v.windowHeights.size(), 2);
    QVERIFY(qAbs(v.windowHeights.first() - 0.25) < 0.01);
    QVERIFY(qAbs(v.windowHeights.last() - 0.75) < 0.01);
}

void TestScrollTemplate::soloPartialHeightZoneYieldsHeight()
{
    const auto v = extractTemplateVocabulary({
        {0.0, 0.0, 0.5, 1.0},
        {0.5, 0.0, 0.5, 0.6},
    });
    QCOMPARE(v.windowHeights.size(), 1);
    QVERIFY(qAbs(v.windowHeights.first() - 0.6) < 0.01);
}

void TestScrollTemplate::soloFullHeightZoneYieldsNoHeight()
{
    const auto v = extractTemplateVocabulary({{0.0, 0.0, 1.0, 1.0}});
    QCOMPARE(v.columnWidths.size(), 1);
    QVERIFY(v.windowHeights.isEmpty());
}

void TestScrollTemplate::misalignedOverlapStartsOwnBand()
{
    // Second zone shares neither edge with the first: two width presets,
    // and each is that zone's own extent (no merging of overlap).
    const auto v = extractTemplateVocabulary({
        {0.0, 0.0, 0.5, 1.0},
        {0.3, 0.0, 0.6, 1.0},
    });
    QCOMPARE(v.columnWidths.size(), 2);
    QVERIFY(qAbs(v.columnWidths.first() - 0.5) < 0.01);
    QVERIFY(qAbs(v.columnWidths.last() - 0.6) < 0.01);
}

void TestScrollTemplate::sliverZonesDropped()
{
    const auto v = extractTemplateVocabulary({
        {0.0, 0.0, 0.005, 1.0}, // sliver width
        {0.0, 0.0, 0.5, 0.005}, // sliver height
        {0.5, 0.0, 0.5, 1.0},
    });
    QCOMPARE(v.columnWidths.size(), 1);
    QVERIFY(qAbs(v.columnWidths.first() - 0.5) < 0.01);
}

void TestScrollTemplate::duplicateWidthsDeduped()
{
    const auto v = extractTemplateVocabulary({
        {0.0, 0.0, 0.25, 1.0},
        {0.25, 0.0, 0.5, 1.0},
        {0.75, 0.0, 0.25, 1.0},
    });
    QCOMPARE(v.columnWidths.size(), 2);
}

void TestScrollTemplate::toleranceBoundary()
{
    // 0.30 vs 0.35 column widths differ by more than the 0.01 tolerance and
    // must stay distinct; 0.300 vs 0.304 collapse.
    const auto distinct = extractTemplateVocabulary({
        {0.0, 0.0, 0.30, 1.0},
        {0.30, 0.0, 0.35, 1.0},
        {0.65, 0.0, 0.35, 1.0},
    });
    QCOMPARE(distinct.columnWidths.size(), 2);

    const auto collapsed = extractTemplateVocabulary({
        {0.0, 0.0, 0.300, 1.0},
        {0.300, 0.0, 0.304, 1.0},
        {0.604, 0.0, 0.396, 1.0},
    });
    QCOMPARE(collapsed.columnWidths.size(), 2); // 0.300≈0.304 dedupe, 0.396 distinct
}

void TestScrollTemplate::degenerateInputYieldsEmpty()
{
    QVERIFY(extractTemplateVocabulary({}).columnWidths.isEmpty());
    // Out-of-range rect and pure slivers leave nothing usable.
    const auto v = extractTemplateVocabulary({
        {1.5, 0.0, 0.5, 1.0},
        {0.0, 0.0, 0.001, 1.0},
    });
    QVERIFY(v.columnWidths.isEmpty());
    QVERIFY(v.windowHeights.isEmpty());
}

void TestScrollTemplate::widthsClampedToMinimum()
{
    // A 2% zone survives the sliver filter (width > Eps) but clamps up to
    // the engine's floor so a Preset column can never resolve below it.
    const auto v = extractTemplateVocabulary({
        {0.0, 0.0, 0.02, 1.0},
        {0.02, 0.0, 0.98, 1.0},
    });
    QCOMPARE(v.columnWidths.size(), 2);
    QVERIFY(v.columnWidths.first() >= MinColumnWidthFraction);
}

QTEST_APPLESS_MAIN(TestScrollTemplate)
#include "test_scrolltemplate.moc"
