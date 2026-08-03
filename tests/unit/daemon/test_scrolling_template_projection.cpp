// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_scrolling_template_projection.cpp
 * @brief The daemon's zone-projection half of the template push.
 *
 * scrollingTemplateOverrides is the pure function between "the registry
 * resolved template T for this screen" and "these ScrollPerScreenKeys
 * preset-list overrides reach the engine": reference-rect selection
 * (full vs available geometry per the layout's own flag), zone
 * normalization, vocabulary extraction, and the fail-soft empty map for
 * unusable templates. The extractor's band math is covered exhaustively in
 * libs/phosphor-scroll-engine/tests/test_scrolltemplate.cpp; these cases pin
 * the daemon-side plumbing around it.
 */

#include <QTest>

#include "daemon/daemon/scrollingtemplateprojection.h"

#include <PhosphorScrollEngine/ScrollTypes.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>

using namespace PlasmaZones;

namespace {

/// A layout with one zone per relative rect. Caller owns the layout.
PhosphorZones::Layout* makeLayout(const QVector<QRectF>& zoneRects)
{
    auto* layout = new PhosphorZones::Layout(QStringLiteral("Template"));
    for (const QRectF& rect : zoneRects) {
        auto* zone = new PhosphorZones::Zone();
        zone->setRelativeGeometry(rect);
        layout->addZone(zone);
    }
    return layout;
}

const QRect kFull{0, 0, 2560, 1440};
const QRect kAvailable{64, 0, 2496, 1440}; // a 64px left panel — the two bases
                                           // differ in WIDTH so the fixed-zone
                                           // case below can discriminate them

} // namespace

class TestScrollingTemplateProjection : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testNullLayout_yieldsEmptyMap()
    {
        QVERIFY(scrollingTemplateOverrides(nullptr, kFull, kAvailable).isEmpty());
    }

    void testColumnLayout_yieldsWidthList()
    {
        // 25 / 50 / 25 columns: three distinct widths dedupe to two presets,
        // ascending; full-height columns yield no height list (mixed
        // vocabulary — the engine keeps its settings heights).
        QScopedPointer<PhosphorZones::Layout> layout(
            makeLayout({{0.0, 0.0, 0.25, 1.0}, {0.25, 0.0, 0.5, 1.0}, {0.75, 0.0, 0.25, 1.0}}));
        const QVariantMap overrides = scrollingTemplateOverrides(layout.data(), kFull, kAvailable);

        const QVariantList widths =
            overrides.value(PhosphorScrollEngine::ScrollPerScreenKeys::presetColumnWidths()).toList();
        QCOMPARE(widths.size(), 2);
        QVERIFY(qAbs(widths.at(0).toDouble() - 0.25) < 0.01);
        QVERIFY(qAbs(widths.at(1).toDouble() - 0.5) < 0.01);
        QVERIFY(!overrides.contains(PhosphorScrollEngine::ScrollPerScreenKeys::presetWindowHeights()));
    }

    void testStackedBand_yieldsHeightsToo()
    {
        // A 60/40 stacked band beside a full column: both keys present.
        QScopedPointer<PhosphorZones::Layout> layout(
            makeLayout({{0.0, 0.0, 0.4, 0.6}, {0.0, 0.6, 0.4, 0.4}, {0.4, 0.0, 0.6, 1.0}}));
        const QVariantMap overrides = scrollingTemplateOverrides(layout.data(), kFull, kAvailable);

        QVERIFY(overrides.contains(PhosphorScrollEngine::ScrollPerScreenKeys::presetColumnWidths()));
        const QVariantList heights =
            overrides.value(PhosphorScrollEngine::ScrollPerScreenKeys::presetWindowHeights()).toList();
        QCOMPARE(heights.size(), 2);
        QVERIFY(qAbs(heights.at(0).toDouble() - 0.4) < 0.01);
        QVERIFY(qAbs(heights.at(1).toDouble() - 0.6) < 0.01);
    }

    void testRowOnlyLayout_failsSoftToEmptyMap()
    {
        // Full-width stacked rows extract a [1.0]-only width vocabulary,
        // which the extractor rejects as "no template" — the map stays empty
        // so the engine keeps the settings preset lists.
        QScopedPointer<PhosphorZones::Layout> layout(makeLayout({{0.0, 0.0, 1.0, 0.5}, {0.0, 0.5, 1.0, 0.5}}));
        QVERIFY(scrollingTemplateOverrides(layout.data(), kFull, kAvailable).isEmpty());
    }

    void testReferenceRect_followsLayoutGeometryFlag()
    {
        // A FIXED-geometry zone spanning the left half of the AVAILABLE
        // area. Under the default flag (available basis) it normalizes to
        // 0.5 width; opting the layout into full-screen geometry changes
        // the basis, so the same pixel rect projects a different fraction —
        // the projection must follow the layout's own flag, mirroring how
        // the layout resolves its zones everywhere else.
        // Fixed coords are RELATIVE TO THE REFERENCE ORIGIN
        // (Zone::computeAbsoluteGeometry adds them to it), so the fixture
        // authors them origin-free and only the extents matter per basis.
        auto* layout = new PhosphorZones::Layout(QStringLiteral("Fixed"));
        auto* left = new PhosphorZones::Zone();
        left->setFixedGeometry(QRect(0, 0, kAvailable.width() / 2, kAvailable.height()));
        left->setGeometryMode(PhosphorZones::ZoneGeometryMode::Fixed);
        layout->addZone(left);
        auto* right = new PhosphorZones::Zone();
        right->setFixedGeometry(QRect(kAvailable.width() / 2, 0, kAvailable.width() / 4, kAvailable.height()));
        right->setGeometryMode(PhosphorZones::ZoneGeometryMode::Fixed);
        layout->addZone(right);
        QScopedPointer<PhosphorZones::Layout> owner(layout);

        const QVariantList availableBasis = scrollingTemplateOverrides(layout, kFull, kAvailable)
                                                .value(PhosphorScrollEngine::ScrollPerScreenKeys::presetColumnWidths())
                                                .toList();
        QCOMPARE(availableBasis.size(), 2);
        QVERIFY(qAbs(availableBasis.at(0).toDouble() - 0.25) < 0.01);
        QVERIFY(qAbs(availableBasis.at(1).toDouble() - 0.5) < 0.01);

        layout->setUseFullScreenGeometry(true);
        const QVariantList fullBasis = scrollingTemplateOverrides(layout, kFull, kAvailable)
                                           .value(PhosphorScrollEngine::ScrollPerScreenKeys::presetColumnWidths())
                                           .toList();
        QCOMPARE(fullBasis.size(), 2);
        // Same pixels, wider basis: the fractions shrink accordingly.
        QVERIFY(fullBasis.at(1).toDouble() < availableBasis.at(1).toDouble());
    }
};

QTEST_GUILESS_MAIN(TestScrollingTemplateProjection)
#include "test_scrolling_template_projection.moc"
