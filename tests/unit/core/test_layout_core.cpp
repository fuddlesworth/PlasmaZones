// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layout_core.cpp
 * @brief Unit tests for PhosphorZones::Layout dirty tracking, batch modify, per-side gaps, copy constructor
 */

#include <QTest>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

#include "core/constants.h"
#include <PhosphorLayoutApi/AspectRatioClass.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>

using namespace PlasmaZones;
using PhosphorLayout::AspectRatioClass;
namespace ScreenClassification = PhosphorLayout::ScreenClassification;

class TestLayoutCore : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ═══════════════════════════════════════════════════════════════════════════
    // P0: Dirty tracking
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayout_dirtyTracking_newLayoutIsClean()
    {
        // A freshly constructed layout is NOT dirty -- it has never been modified.
        // PhosphorZones::LayoutRegistry::addLayout() calls markDirty() explicitly when adding.
        PhosphorZones::Layout layout(QStringLiteral("Fresh"));
        QVERIFY(!layout.isDirty());
    }

    void testLayout_dirtyTracking_clearDirtyAfterSave()
    {
        PhosphorZones::Layout layout(QStringLiteral("Test"));
        layout.setName(QStringLiteral("Changed"));
        QVERIFY(layout.isDirty());

        layout.clearDirty();
        QVERIFY(!layout.isDirty());
    }

    void testLayout_dirtyTracking_setterMarksDirty()
    {
        PhosphorZones::Layout layout(QStringLiteral("Test"));
        layout.clearDirty();
        QVERIFY(!layout.isDirty());

        layout.setName(QStringLiteral("NewName"));
        QVERIFY(layout.isDirty());

        layout.clearDirty();
        layout.setDescription(QStringLiteral("desc"));
        QVERIFY(layout.isDirty());

        layout.clearDirty();
        layout.setZonePadding(10);
        QVERIFY(layout.isDirty());

        layout.clearDirty();
        layout.setOuterGap(5);
        QVERIFY(layout.isDirty());

        layout.clearDirty();
        layout.setHiddenFromSelector(true);
        QVERIFY(layout.isDirty());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P0: Batch modify
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayout_batchModify_defersLayoutModifiedSignal()
    {
        PhosphorZones::Layout layout(QStringLiteral("Test"));
        QSignalSpy modifiedSpy(&layout, &PhosphorZones::Layout::layoutModified);

        layout.beginBatchModify();

        layout.setName(QStringLiteral("A"));
        layout.setDescription(QStringLiteral("B"));
        layout.setZonePadding(10);

        QCOMPARE(modifiedSpy.count(), 0);

        layout.endBatchModify();

        QCOMPARE(modifiedSpy.count(), 1);
    }

    void testLayout_batchModify_nestedDepth()
    {
        PhosphorZones::Layout layout(QStringLiteral("Test"));
        QSignalSpy modifiedSpy(&layout, &PhosphorZones::Layout::layoutModified);

        layout.beginBatchModify();
        layout.beginBatchModify();

        layout.setName(QStringLiteral("Nested"));
        QCOMPARE(modifiedSpy.count(), 0);

        layout.endBatchModify();
        QCOMPARE(modifiedSpy.count(), 0);

        layout.endBatchModify();
        QCOMPARE(modifiedSpy.count(), 1);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P1: Per-side outer gap overrides
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayout_perSideOuterGap_sentinel_minus1_isDefault()
    {
        PhosphorZones::Layout layout;
        QCOMPARE(layout.outerGapTop(), -1);
        QCOMPARE(layout.outerGapBottom(), -1);
        QCOMPARE(layout.outerGapLeft(), -1);
        QCOMPARE(layout.outerGapRight(), -1);
        QVERIFY(!layout.usePerSideOuterGap());
    }

    void testLayout_perSideOuterGap_setter_clampsBelow_minus1()
    {
        PhosphorZones::Layout layout;
        layout.setOuterGapTop(-5);
        QCOMPARE(layout.outerGapTop(), -1);

        layout.setOuterGapBottom(-100);
        QCOMPARE(layout.outerGapBottom(), -1);

        layout.setOuterGapLeft(-2);
        QCOMPARE(layout.outerGapLeft(), -1);

        layout.setOuterGapRight(-999);
        QCOMPARE(layout.outerGapRight(), -1);

        layout.setZonePadding(-3);
        QCOMPARE(layout.zonePadding(), -1);

        layout.setOuterGap(-50);
        QCOMPARE(layout.outerGap(), -1);
    }

    void testLayout_hasPerSideOuterGapOverride_allNegativeOneMeansFalse()
    {
        PhosphorZones::Layout layout;
        layout.setUsePerSideOuterGap(true);
        QVERIFY(!layout.hasPerSideOuterGapOverride());

        layout.setOuterGapTop(10);
        QVERIFY(layout.hasPerSideOuterGapOverride());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P1: Copy constructor
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayout_copyConstructor_deepCopiesZones()
    {
        PhosphorZones::Layout original(QStringLiteral("Original"));
        auto* zone = new PhosphorZones::Zone();
        zone->setRelativeGeometry(QRectF(0, 0, 0.5, 1));
        zone->setName(QStringLiteral("Zone1"));
        original.addZone(zone);

        PhosphorZones::Layout copy(original);
        QCOMPARE(copy.zoneCount(), 1);
        QCOMPARE(copy.zone(0)->name(), QStringLiteral("Zone1"));

        copy.zone(0)->setName(QStringLiteral("Modified"));
        QCOMPARE(original.zone(0)->name(), QStringLiteral("Zone1"));
        QCOMPARE(copy.zone(0)->name(), QStringLiteral("Modified"));

        QVERIFY(original.zone(0) != copy.zone(0));
    }

    void testLayout_copyConstructor_newId()
    {
        PhosphorZones::Layout original(QStringLiteral("Original"));
        PhosphorZones::Layout copy(original);

        QVERIFY(copy.id() != original.id());
        QVERIFY(!copy.id().isNull());
    }

    void testLayout_copyConstructor_noSourcePath()
    {
        PhosphorZones::Layout original(QStringLiteral("Original"));
        original.setSourcePath(QStringLiteral("/usr/share/plasmazones/layouts/test.json"));

        PhosphorZones::Layout copy(original);

        QVERIFY(copy.sourcePath().isEmpty());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P1: Aspect ratio classification
    // ═══════════════════════════════════════════════════════════════════════════

    void testScreenClassification_standardMonitor()
    {
        // 16:9 = 1.778
        QCOMPARE(ScreenClassification::classify(1920, 1080), AspectRatioClass::Standard);
        // 16:10 = 1.6
        QCOMPARE(ScreenClassification::classify(1920, 1200), AspectRatioClass::Standard);
    }

    void testScreenClassification_ultrawideMonitor()
    {
        // 21:9 = 2.333
        QCOMPARE(ScreenClassification::classify(3440, 1440), AspectRatioClass::Ultrawide);
        // 2560x1080 = 2.37
        QCOMPARE(ScreenClassification::classify(2560, 1080), AspectRatioClass::Ultrawide);
    }

    void testScreenClassification_superUltrawideMonitor()
    {
        // 32:9 = 3.556
        QCOMPARE(ScreenClassification::classify(5120, 1440), AspectRatioClass::SuperUltrawide);
    }

    void testScreenClassification_portraitMonitor()
    {
        // 9:16 = 0.5625
        QCOMPARE(ScreenClassification::classify(1080, 1920), AspectRatioClass::Portrait);
        // 10:16 = 0.625
        QCOMPARE(ScreenClassification::classify(1200, 1920), AspectRatioClass::Portrait);
    }

    void testScreenClassification_toString_roundtrip()
    {
        QCOMPARE(ScreenClassification::fromString(ScreenClassification::toString(AspectRatioClass::Any)),
                 AspectRatioClass::Any);
        QCOMPARE(ScreenClassification::fromString(ScreenClassification::toString(AspectRatioClass::Standard)),
                 AspectRatioClass::Standard);
        QCOMPARE(ScreenClassification::fromString(ScreenClassification::toString(AspectRatioClass::Ultrawide)),
                 AspectRatioClass::Ultrawide);
        QCOMPARE(ScreenClassification::fromString(ScreenClassification::toString(AspectRatioClass::SuperUltrawide)),
                 AspectRatioClass::SuperUltrawide);
        QCOMPARE(ScreenClassification::fromString(ScreenClassification::toString(AspectRatioClass::Portrait)),
                 AspectRatioClass::Portrait);
    }

    void testLayout_aspectRatioClass_defaultIsAny()
    {
        PhosphorZones::Layout layout;
        QCOMPARE(layout.aspectRatioClass(), AspectRatioClass::Any);
        QCOMPARE(layout.minAspectRatio(), 0.0);
        QCOMPARE(layout.maxAspectRatio(), 0.0);
    }

    void testLayout_matchesAspectRatio_anyMatchesAll()
    {
        PhosphorZones::Layout layout;
        // Any matches everything
        QVERIFY(layout.matchesAspectRatio(0.5)); // portrait
        QVERIFY(layout.matchesAspectRatio(1.78)); // standard
        QVERIFY(layout.matchesAspectRatio(2.33)); // ultrawide
        QVERIFY(layout.matchesAspectRatio(3.56)); // super-ultrawide
    }

    void testLayout_matchesAspectRatio_classMatching()
    {
        PhosphorZones::Layout layout;
        layout.setAspectRatioClass(AspectRatioClass::Ultrawide);

        QVERIFY(!layout.matchesAspectRatio(1.78)); // standard — no match
        QVERIFY(layout.matchesAspectRatio(2.33)); // ultrawide — match
        QVERIFY(!layout.matchesAspectRatio(3.56)); // super-ultrawide — no match
        QVERIFY(!layout.matchesAspectRatio(0.56)); // portrait — no match
    }

    void testLayout_matchesAspectRatio_explicitBoundsOverrideClass()
    {
        PhosphorZones::Layout layout;
        layout.setAspectRatioClass(AspectRatioClass::Ultrawide);
        layout.setMinAspectRatio(2.0);
        layout.setMaxAspectRatio(3.0);

        // Explicit bounds: 2.0 - 3.0
        QVERIFY(!layout.matchesAspectRatio(1.78)); // below min
        QVERIFY(layout.matchesAspectRatio(2.33)); // in range
        QVERIFY(layout.matchesAspectRatio(2.0)); // at min
        QVERIFY(layout.matchesAspectRatio(3.0)); // at max
        QVERIFY(!layout.matchesAspectRatio(3.56)); // above max
    }

    void testLayout_aspectRatio_serialization_roundtrip()
    {
        PhosphorZones::Layout original(QStringLiteral("Test"));
        original.setAspectRatioClass(AspectRatioClass::Ultrawide);
        original.setMinAspectRatio(1.9);
        original.setMaxAspectRatio(2.8);

        QJsonObject json = original.toJson();

        QCOMPARE(json[QStringLiteral("aspectRatioClass")].toString(), QStringLiteral("ultrawide"));
        QCOMPARE(json[QStringLiteral("minAspectRatio")].toDouble(), 1.9);
        QCOMPARE(json[QStringLiteral("maxAspectRatio")].toDouble(), 2.8);

        auto* restored = PhosphorZones::Layout::fromJson(json);
        QCOMPARE(restored->aspectRatioClass(), AspectRatioClass::Ultrawide);
        QCOMPARE(restored->minAspectRatio(), 1.9);
        QCOMPARE(restored->maxAspectRatio(), 2.8);
        QVERIFY(restored->matchesAspectRatio(2.33)); // ultrawide
        QVERIFY(!restored->matchesAspectRatio(1.78)); // standard

        delete restored;
    }

    void testLayout_aspectRatio_omittedInJson_meansAny()
    {
        QJsonObject json;
        json[QStringLiteral("id")] = QStringLiteral("{00000000-0000-0000-0000-000000000001}");
        json[QStringLiteral("name")] = QStringLiteral("NoAR");
        json[QStringLiteral("showZoneNumbers")] = true;
        json[QStringLiteral("zones")] = QJsonArray();

        auto* layout = PhosphorZones::Layout::fromJson(json);
        QCOMPARE(layout->aspectRatioClass(), AspectRatioClass::Any);
        QCOMPARE(layout->minAspectRatio(), 0.0);
        QCOMPARE(layout->maxAspectRatio(), 0.0);
        QVERIFY(layout->matchesAspectRatio(2.33)); // matches everything

        delete layout;
    }

    void testScreenClassification_boundaryValues()
    {
        // Exactly 1.0 (square) → Standard
        QCOMPARE(ScreenClassification::classify(1.0), AspectRatioClass::Standard);
        // Just below 1.0 → Portrait
        QCOMPARE(ScreenClassification::classify(0.999), AspectRatioClass::Portrait);

        // Exactly 1.9 → Ultrawide (>= UltrawideMin)
        QCOMPARE(ScreenClassification::classify(1.9), AspectRatioClass::Ultrawide);
        // Just below 1.9 → Standard
        QCOMPARE(ScreenClassification::classify(1.899), AspectRatioClass::Standard);

        // Exactly 2.8 → SuperUltrawide (>= SuperUltrawideMin)
        QCOMPARE(ScreenClassification::classify(2.8), AspectRatioClass::SuperUltrawide);
        // Just below 2.8 → Ultrawide
        QCOMPARE(ScreenClassification::classify(2.799), AspectRatioClass::Ultrawide);
    }

    void testScreenClassification_degenerateInputs()
    {
        // Zero height → Any (not Standard)
        QCOMPARE(ScreenClassification::classify(1920, 0), AspectRatioClass::Any);
        // Zero width → Any
        QCOMPARE(ScreenClassification::classify(0, 1080), AspectRatioClass::Any);
        // Negative height → Any
        QCOMPARE(ScreenClassification::classify(1920, -1), AspectRatioClass::Any);
    }

    void testLayout_matchesAspectRatio_onlyMinBoundSet()
    {
        PhosphorZones::Layout layout;
        layout.setMinAspectRatio(2.0);
        // No max set — should match anything >= 2.0
        QVERIFY(!layout.matchesAspectRatio(1.78)); // below min
        QVERIFY(layout.matchesAspectRatio(2.0)); // at min
        QVERIFY(layout.matchesAspectRatio(2.33)); // above min
        QVERIFY(layout.matchesAspectRatio(5.0)); // way above min — still matches
    }

    void testLayout_matchesAspectRatio_onlyMaxBoundSet()
    {
        PhosphorZones::Layout layout;
        layout.setMaxAspectRatio(1.0);
        // No min set — should match anything <= 1.0
        QVERIFY(layout.matchesAspectRatio(0.56)); // below max
        QVERIFY(layout.matchesAspectRatio(1.0)); // at max
        QVERIFY(!layout.matchesAspectRatio(1.78)); // above max
    }

    void testLayout_matchesAspectRatio_epsilonBoundaries()
    {
        PhosphorZones::Layout layout;
        layout.setMinAspectRatio(2.0);
        layout.setMaxAspectRatio(3.0);

        QVERIFY(!layout.matchesAspectRatio(1.999)); // just below min
        QVERIFY(!layout.matchesAspectRatio(3.001)); // just above max
    }

    void testLayout_copyConstructor_copiesAspectRatio()
    {
        PhosphorZones::Layout original(QStringLiteral("Original"));
        original.setAspectRatioClass(AspectRatioClass::Portrait);
        original.setMinAspectRatio(0.3);
        original.setMaxAspectRatio(0.9);

        PhosphorZones::Layout copy(original);
        QCOMPARE(copy.aspectRatioClass(), AspectRatioClass::Portrait);
        QCOMPARE(copy.minAspectRatio(), 0.3);
        QCOMPARE(copy.maxAspectRatio(), 0.9);
    }
};

QTEST_MAIN(TestLayoutCore)
#include "test_layout_core.moc"
