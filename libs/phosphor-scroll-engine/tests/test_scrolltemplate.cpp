// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QtTest>

#include <PhosphorScrollEngine/ScrollTemplate.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

using namespace PhosphorScrollEngine;

/// Pure-function coverage for extractTemplateVocabulary: layout zone rects
/// (normalized 0-1) in, preset column-width / window-height vocabulary out.
/// No engine, no QObject — APPLESS.
class TestScrollTemplate : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void threeEqualColumns();
    void unsortedInputSortsAscending();
    void stackedBandYieldsHeights();
    void threeMemberBandYieldsAllHeights();
    void soloPartialHeightZoneYieldsHeight();
    void soloFullHeightZoneYieldsNoHeight();
    void misalignedOverlapStartsOwnBand();
    void sliverZonesDropped();
    void sliverAtExactlyEpsIsDropped();
    void duplicateWidthsDeduped();
    void toleranceBoundary();
    void degenerateInputYieldsEmpty();
    void slimWidthsDroppedNotClamped();
    void slimHeightsDroppedNotClamped();
    void tabStyleFullHeightStackYieldsNoHeights();
    void rowOnlyLayoutYieldsNoVocabulary();
    void vocabularyCappedAtSixteenEntries();
    void nonFiniteRectsDropped();
};

void TestScrollTemplate::threeEqualColumns()
{
    // columns-3.json shape: thirds with float dust from serialization. A
    // single-entry NON-full-width list is a legitimate uniform vocabulary
    // and must be kept (contrast rowOnlyLayoutYieldsNoVocabulary).
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

void TestScrollTemplate::threeMemberBandYieldsAllHeights()
{
    // Pins the whole-band height walk: a two-member test would pass an
    // off-by-one that appended only the first two.
    const auto v = extractTemplateVocabulary({
        {0.0, 0.0, 0.6, 1.0},
        {0.6, 0.0, 0.4, 0.2},
        {0.6, 0.2, 0.4, 0.3},
        {0.6, 0.5, 0.4, 0.5},
    });
    QCOMPARE(v.windowHeights.size(), 3);
    QVERIFY(qAbs(v.windowHeights.at(0) - 0.2) < 0.01);
    QVERIFY(qAbs(v.windowHeights.at(1) - 0.3) < 0.01);
    QVERIFY(qAbs(v.windowHeights.at(2) - 0.5) < 0.01);
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
    const auto v = extractTemplateVocabulary({{0.0, 0.0, 0.6, 1.0}, {0.6, 0.0, 0.4, 1.0}});
    QCOMPARE(v.columnWidths.size(), 2);
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
    // The sliver-HEIGHT zone carries a DISTINCT width (0.4) so its survival
    // would show up as a second width preset — a same-width sliver would
    // dedupe away and leave the filter half unpinned.
    const auto v = extractTemplateVocabulary({
        {0.0, 0.0, 0.005, 1.0}, // sliver width
        {0.0, 0.0, 0.4, 0.005}, // sliver height, distinct width
        {0.5, 0.0, 0.5, 1.0},
    });
    QCOMPARE(v.columnWidths.size(), 1);
    QVERIFY(qAbs(v.columnWidths.first() - 0.5) < 0.01);
    QVERIFY(v.windowHeights.isEmpty());
}

void TestScrollTemplate::sliverAtExactlyEpsIsDropped()
{
    // The filter is `<= Eps`, so exactly-Eps extents are dropped; just-above
    // survives the filter. Observed through the HEIGHT channel because a
    // just-above-Eps WIDTH is below the preset floor and normalizeFractions
    // drops it anyway.
    const auto atEps = extractTemplateVocabulary({
        {0.0, 0.0, 0.01, 0.6}, // width exactly Eps: filtered, no band, no height
        {0.5, 0.0, 0.5, 1.0},
    });
    QVERIFY(atEps.windowHeights.isEmpty());

    const auto aboveEps = extractTemplateVocabulary({
        {0.0, 0.0, 0.011, 0.6}, // just above Eps: forms a band, height kept
        {0.5, 0.0, 0.5, 1.0},
    });
    QCOMPARE(aboveEps.windowHeights.size(), 1);
    QVERIFY(qAbs(aboveEps.windowHeights.first() - 0.6) < 0.01);
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

void TestScrollTemplate::slimWidthsDroppedNotClamped()
{
    // A 2% zone survives the sliver filter (width > Eps) but sits below the
    // engine's preset floor: it is DROPPED, matching the settings parser,
    // never clamped up to a preset the template does not contain.
    const auto v = extractTemplateVocabulary({
        {0.0, 0.0, 0.02, 1.0},
        {0.02, 0.0, 0.98, 1.0},
    });
    QCOMPARE(v.columnWidths.size(), 1);
    QVERIFY(qAbs(v.columnWidths.first() - 0.98) < 0.01);
}

void TestScrollTemplate::slimHeightsDroppedNotClamped()
{
    // Height twin of the width drop: the ONLY coverage of the
    // MinWindowHeightFraction argument. A 2% stacked member drops; its 98%
    // sibling stays.
    const auto v = extractTemplateVocabulary({
        {0.0, 0.0, 0.5, 1.0},
        {0.5, 0.0, 0.5, 0.02},
        {0.5, 0.02, 0.5, 0.98},
    });
    QCOMPARE(v.windowHeights.size(), 1);
    QVERIFY(qAbs(v.windowHeights.first() - 0.98) < 0.01);
}

void TestScrollTemplate::tabStyleFullHeightStackYieldsNoHeights()
{
    // Overlapping tab-style zones sharing one extent at ~full height must
    // not contribute a 1.0 height preset — that would make the whole height
    // vocabulary [1.0] and every Preset height resolve full-column.
    const auto v = extractTemplateVocabulary({
        {0.0, 0.0, 0.5, 1.0},
        {0.5, 0.0, 0.5, 1.0},
        {0.5, 0.0, 0.5, 1.0},
    });
    QCOMPARE(v.columnWidths.size(), 1);
    QVERIFY(v.windowHeights.isEmpty());
}

void TestScrollTemplate::rowOnlyLayoutYieldsNoVocabulary()
{
    // rows-2.json shape: full-width rows define no COLUMN vocabulary at all.
    // A [1.0]-only width list would degenerate the strip to one full-width
    // column per window under a Preset-kind default, so the whole extraction
    // reports "no usable template".
    const auto v = extractTemplateVocabulary({
        {0.0, 0.0, 1.0, 0.5},
        {0.0, 0.5, 1.0, 0.5},
    });
    QVERIFY(v.columnWidths.isEmpty());
    QVERIFY(v.windowHeights.isEmpty());
}

void TestScrollTemplate::vocabularyCappedAtSixteenEntries()
{
    // Overlapping misaligned zones each form their own band, so a
    // pathological layout can yield arbitrarily many widths; the vocabulary
    // caps at the settings validator's mirrored 16.
    QVector<QRectF> zones;
    for (int i = 0; i < 20; ++i) {
        zones.append(QRectF(0.0, 0.0, 0.05 + i * 0.02, 1.0));
    }
    const auto v = extractTemplateVocabulary(zones);
    QCOMPARE(v.columnWidths.size(), 16);
}

void TestScrollTemplate::nonFiniteRectsDropped()
{
    // NaN coordinates must not reach the sort (strict-weak-ordering UB).
    const auto v = extractTemplateVocabulary({
        {qQNaN(), 0.0, 0.5, 1.0},
        {0.0, 0.0, qQNaN(), 1.0},
        {0.5, 0.0, 0.5, 1.0},
    });
    QCOMPARE(v.columnWidths.size(), 1);
    QVERIFY(qAbs(v.columnWidths.first() - 0.5) < 0.01);
}

QTEST_APPLESS_MAIN(TestScrollTemplate)
#include "test_scrolltemplate.moc"
