// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layout_core.cpp
 * @brief Unit tests for PhosphorZones::Layout dirty tracking, batch modify, per-side gaps, copy constructor
 */

#include <QTest>
#include <QFile>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

#include <QTemporaryDir>

#include "core/constants.h"
#include <PhosphorLayoutApi/AspectRatioClass.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutSettingsStore.h>
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

        // The remaining per-layout settings (now relocated to the sidecar on
        // save) must each mark the layout dirty, so an edit reaches saveLayout
        // and triggers the structural/settings split.
        layout.clearDirty();
        layout.setUsePerSideOuterGap(true);
        QVERIFY(layout.isDirty());

        layout.clearDirty();
        layout.setOuterGapTop(10);
        QVERIFY(layout.isDirty());

        layout.clearDirty();
        layout.setOuterGapBottom(11);
        QVERIFY(layout.isDirty());

        layout.clearDirty();
        layout.setOuterGapLeft(12);
        QVERIFY(layout.isDirty());

        layout.clearDirty();
        layout.setOuterGapRight(13);
        QVERIFY(layout.isDirty());

        layout.clearDirty();
        layout.setShowZoneNumbers(false);
        QVERIFY(layout.isDirty());

        layout.clearDirty();
        layout.setOverlayDisplayMode(1);
        QVERIFY(layout.isDirty());

        layout.clearDirty();
        layout.setAutoAssign(true);
        QVERIFY(layout.isDirty());

        layout.clearDirty();
        layout.setUseFullScreenGeometry(true);
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

    // ═══════════════════════════════════════════════════════════════════════════
    // Fixed-zone reference geometry (editor canvas reference — discussion #593)
    // ═══════════════════════════════════════════════════════════════════════════

    // The editor canvas scales fixed zones by its reference size, so the
    // reference must match the live screen unless the fixed zones overflow it.
    // fixedZoneReferenceGeometry() encodes that rule: keep the recalc
    // (screen) geometry when the fixed-zone bounding box fits inside it.
    void testLayout_fixedZoneReference_partialZonesKeepScreen()
    {
        // A 16:9 layout whose two fixed zones only reach 2560px wide on a
        // 3840x2160 screen (the discussion #593 "4K" layout). The raw bbox is
        // 2560x2160 (near-square); pinning to it would stretch the canvas 1.5x
        // horizontally. The reference must stay the full 3840x2160 screen.
        PhosphorZones::Layout layout(QStringLiteral("4K"));
        auto* z1 = new PhosphorZones::Zone();
        z1->setGeometryMode(PhosphorZones::ZoneGeometryMode::Fixed);
        z1->setFixedGeometry(QRectF(0, 1080, 1920, 1080));
        layout.addZone(z1);
        auto* z2 = new PhosphorZones::Zone();
        z2->setGeometryMode(PhosphorZones::ZoneGeometryMode::Fixed);
        z2->setFixedGeometry(QRectF(0, 720, 2560, 1440));
        layout.addZone(z2);

        QCOMPARE(layout.fixedZoneBoundingBox(), QRectF(0, 0, 2560, 2160));

        layout.recalculateZoneGeometries(QRectF(0, 0, 3840, 2160));
        QCOMPARE(layout.fixedZoneReferenceGeometry(), QRectF(0, 0, 3840, 2160));
    }

    // When the fixed zones genuinely exceed the live screen (a 3840x2160
    // layout on a 3840x2126 panel-reduced screen), the reference keeps the
    // authored bounding box so fixed pixels stay accurate.
    void testLayout_fixedZoneReference_overflowKeepsBbox()
    {
        PhosphorZones::Layout layout(QStringLiteral("FullScreen"));
        auto* z = new PhosphorZones::Zone();
        z->setGeometryMode(PhosphorZones::ZoneGeometryMode::Fixed);
        z->setFixedGeometry(QRectF(0, 0, 3840, 2160));
        layout.addZone(z);

        layout.recalculateZoneGeometries(QRectF(0, 0, 3840, 2126));
        QCOMPARE(layout.fixedZoneReferenceGeometry(), QRectF(0, 0, 3840, 2160));
    }

    // Relative-only layouts have no fixed-zone bounding box, so the editor
    // falls back to the live screen (empty reference → no canvas override).
    void testLayout_fixedZoneReference_relativeOnlyIsEmpty()
    {
        PhosphorZones::Layout layout(QStringLiteral("Relative"));
        auto* z = new PhosphorZones::Zone();
        z->setRelativeGeometry(QRectF(0, 0, 0.5, 1.0));
        layout.addZone(z);

        QVERIFY(layout.fixedZoneBoundingBox().isEmpty());
        layout.recalculateZoneGeometries(QRectF(0, 0, 3840, 2160));
        QVERIFY(layout.fixedZoneReferenceGeometry().isEmpty());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // LayoutSettingsStore: per-layout settings split out of the layout file
    // ═══════════════════════════════════════════════════════════════════════════

    // Build a full layout JSON (as Layout::toJson would) carrying both structural
    // data and per-layout settings, including one zone with a custom appearance.
    QJsonObject makeFullLayoutWithSettings() const
    {
        QJsonObject relGeo{{QStringLiteral("x"), 0.0},
                           {QStringLiteral("y"), 0.0},
                           {QStringLiteral("width"), 1.0},
                           {QStringLiteral("height"), 1.0}};
        QJsonObject appearance{{QStringLiteral("useCustomColors"), true},
                               {QStringLiteral("highlightColor"), QStringLiteral("#ff112233")},
                               {QStringLiteral("borderWidth"), 5}};
        QJsonObject zone{{QStringLiteral("id"), QStringLiteral("{11111111-0000-0000-0000-000000000001}")},
                         {QStringLiteral("name"), QStringLiteral("Z1")},
                         {QStringLiteral("zoneNumber"), 1},
                         {QStringLiteral("relativeGeometry"), relGeo},
                         {QStringLiteral("appearance"), appearance}};
        // shaderParams is the only object-valued setting — the highest-risk one
        // for a strip/merge bug — so it's covered here alongside the sentinel
        // (overlayDisplayMode) and per-side gap keys.
        const QJsonObject shaderParams{{QStringLiteral("intensity"), 0.75}, {QStringLiteral("seed"), 42}};
        return QJsonObject{
            {QStringLiteral("id"), QStringLiteral("{abcd0000-0000-0000-0000-000000000000}")},
            {QStringLiteral("name"), QStringLiteral("Settings Layout")},
            {QStringLiteral("showZoneNumbers"), false},
            {QStringLiteral("zonePadding"), 8},
            {QStringLiteral("outerGap"), 12},
            {QStringLiteral("usePerSideOuterGap"), true},
            {QStringLiteral("outerGapTop"), 1},
            {QStringLiteral("outerGapBottom"), 2},
            {QStringLiteral("outerGapLeft"), 3},
            {QStringLiteral("outerGapRight"), 4},
            {QStringLiteral("overlayDisplayMode"), 1},
            {QStringLiteral("autoAssign"), true},
            {QStringLiteral("useFullScreenGeometry"), true},
            {QStringLiteral("shaderId"), QStringLiteral("dissolve")},
            {QStringLiteral("shaderParams"), shaderParams},
            {QStringLiteral("zones"), QJsonArray{zone}},
        };
    }

    void testLayoutSettings_splitProducesStructuralAndSettings()
    {
        using PhosphorZones::LayoutSettingsStore;
        const QJsonObject full = makeFullLayoutWithSettings();

        const QJsonObject structural = LayoutSettingsStore::stripSettings(full);
        // Structural keeps identity + zone geometry, drops every setting.
        QVERIFY(structural.contains(QStringLiteral("id")));
        QVERIFY(structural.contains(QStringLiteral("zones")));
        QVERIFY(!structural.contains(QStringLiteral("showZoneNumbers")));
        QVERIFY(!structural.contains(QStringLiteral("zonePadding")));
        QVERIFY(!structural.contains(QStringLiteral("autoAssign")));
        QVERIFY(!structural.contains(QStringLiteral("useFullScreenGeometry")));
        QVERIFY(!structural.contains(QStringLiteral("shaderId")));
        QVERIFY(!structural.contains(QStringLiteral("shaderParams")));
        QVERIFY(!structural.contains(QStringLiteral("overlayDisplayMode")));
        QVERIFY(!structural.contains(QStringLiteral("usePerSideOuterGap")));
        QVERIFY(!structural.contains(QStringLiteral("outerGapTop")));
        const QJsonObject sZone = structural.value(QStringLiteral("zones")).toArray().at(0).toObject();
        QVERIFY(sZone.contains(QStringLiteral("relativeGeometry")));
        QVERIFY(!sZone.contains(QStringLiteral("appearance")));

        const QJsonObject settings = LayoutSettingsStore::extractSettings(full);
        QCOMPARE(settings.value(QStringLiteral("zonePadding")).toInt(), 8);
        QCOMPARE(settings.value(QStringLiteral("autoAssign")).toBool(), true);
        QCOMPARE(settings.value(QStringLiteral("useFullScreenGeometry")).toBool(), true);
        QCOMPARE(settings.value(QStringLiteral("overlayDisplayMode")).toInt(), 1);
        QCOMPARE(settings.value(QStringLiteral("outerGapLeft")).toInt(), 3);
        QCOMPARE(settings.value(QStringLiteral("shaderId")).toString(), QStringLiteral("dissolve"));
        // The object-valued setting must survive as a nested object, not be flattened.
        const QJsonObject sp = settings.value(QStringLiteral("shaderParams")).toObject();
        QCOMPARE(sp.value(QStringLiteral("intensity")).toDouble(), 0.75);
        QCOMPARE(sp.value(QStringLiteral("seed")).toInt(), 42);
        const QJsonObject zoneAppearance = settings.value(QStringLiteral("zoneAppearance")).toObject();
        QVERIFY(zoneAppearance.contains(QStringLiteral("{11111111-0000-0000-0000-000000000001}")));
    }

    void testLayoutSettings_mergeRestoresFullLayout()
    {
        using PhosphorZones::LayoutSettingsStore;
        const QJsonObject full = makeFullLayoutWithSettings();
        const QJsonObject structural = LayoutSettingsStore::stripSettings(full);
        const QJsonObject settings = LayoutSettingsStore::extractSettings(full);

        const QJsonObject merged = LayoutSettingsStore::mergeSettings(structural, settings);
        QCOMPARE(merged, full);
    }

    void testLayoutSettings_mergeEmptyKeepsStructuralAsIs()
    {
        using PhosphorZones::LayoutSettingsStore;
        // A not-yet-split (full-format) layout with no sidecar entry must round-
        // trip unchanged through merge — settings already embedded survive.
        const QJsonObject full = makeFullLayoutWithSettings();
        const QJsonObject merged = LayoutSettingsStore::mergeSettings(full, QJsonObject{});
        QCOMPARE(merged, full);
    }

    void testLayoutSettings_storeSaveLoadRoundTrip()
    {
        using PhosphorZones::LayoutSettingsStore;
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.filePath(QStringLiteral("layout-settings.json"));
        const QString layoutId = QStringLiteral("{abcd0000-0000-0000-0000-000000000000}");

        LayoutSettingsStore store;
        store.setSettingsFor(layoutId, LayoutSettingsStore::extractSettings(makeFullLayoutWithSettings()));
        QVERIFY(store.saveToFile(path));

        LayoutSettingsStore reloaded;
        QVERIFY(reloaded.loadFromFile(path));
        QCOMPARE(reloaded.settingsFor(layoutId).value(QStringLiteral("zonePadding")).toInt(), 8);

        // Empty settings are dropped, not persisted as an empty object — and the
        // save-time filter keeps an emptied layout out of the file entirely, so a
        // reload sees no entry for it.
        store.setSettingsFor(layoutId, QJsonObject{});
        QVERIFY(store.isEmpty());
        QVERIFY(store.saveToFile(path));
        LayoutSettingsStore reloadedEmpty;
        QVERIFY(reloadedEmpty.loadFromFile(path));
        QVERIFY(reloadedEmpty.settingsFor(layoutId).isEmpty());
        QVERIFY(reloadedEmpty.isEmpty());
    }

    void testLayoutSettings_stripIsIdentityOnSettingsFreeInput()
    {
        using PhosphorZones::LayoutSettingsStore;
        // No settings keys and no zones → stripSettings must not synthesise an
        // empty "zones" array; it is the identity.
        const QJsonObject bare{{QStringLiteral("id"), QStringLiteral("{x}")},
                               {QStringLiteral("name"), QStringLiteral("Bare")}};
        QCOMPARE(LayoutSettingsStore::stripSettings(bare), bare);
    }

    void testLayoutSettings_idlessZoneKeepsInlineAppearance()
    {
        using PhosphorZones::LayoutSettingsStore;
        // An id-less zone has no sidecar key, so extractSettings skips its
        // appearance and stripSettings must leave it inline (otherwise it would
        // be lost). Strip→merge round-trips the id-less zone unchanged.
        QJsonObject idlessZone{{QStringLiteral("id"), QString()},
                               {QStringLiteral("zoneNumber"), 1},
                               {QStringLiteral("appearance"), QJsonObject{{QStringLiteral("borderWidth"), 3}}}};
        const QJsonObject full{{QStringLiteral("id"), QStringLiteral("{abcd0000-0000-0000-0000-000000000000}")},
                               {QStringLiteral("zonePadding"), 8},
                               {QStringLiteral("zones"), QJsonArray{idlessZone}}};

        const QJsonObject settings = LayoutSettingsStore::extractSettings(full);
        QVERIFY(!settings.contains(QStringLiteral("zoneAppearance"))); // id-less zone not mapped

        const QJsonObject structural = LayoutSettingsStore::stripSettings(full);
        const QJsonObject sZone = structural.value(QStringLiteral("zones")).toArray().at(0).toObject();
        QVERIFY(sZone.contains(QStringLiteral("appearance"))); // kept inline, not lost

        QCOMPARE(LayoutSettingsStore::mergeSettings(structural, settings), full);
    }

    void testLayoutSettings_removeLayoutDropsOnlyThatEntry()
    {
        using PhosphorZones::LayoutSettingsStore;
        const QString idA = QStringLiteral("{aaaa0000-0000-0000-0000-000000000000}");
        const QString idB = QStringLiteral("{bbbb0000-0000-0000-0000-000000000000}");

        LayoutSettingsStore store;
        store.setSettingsFor(idA, QJsonObject{{QStringLiteral("zonePadding"), 4}});
        store.setSettingsFor(idB, QJsonObject{{QStringLiteral("zonePadding"), 9}});

        store.removeLayout(idA);
        QVERIFY(store.settingsFor(idA).isEmpty());
        QCOMPARE(store.settingsFor(idB).value(QStringLiteral("zonePadding")).toInt(), 9);
        QVERIFY(!store.isEmpty());
    }

    void testLayoutSettings_loadMissingFileIsEmptySuccess()
    {
        using PhosphorZones::LayoutSettingsStore;
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        LayoutSettingsStore store;
        // A missing sidecar is an empty store, not an error.
        QVERIFY(store.loadFromFile(tmp.filePath(QStringLiteral("does-not-exist.json"))));
        QVERIFY(store.isEmpty());
    }

    void testLayoutSettings_loadCorruptFileFails()
    {
        using PhosphorZones::LayoutSettingsStore;
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.filePath(QStringLiteral("corrupt.json"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArrayLiteral("{ not valid json"));
        f.close();

        LayoutSettingsStore store;
        QVERIFY(!store.loadFromFile(path));
        QVERIFY(store.isEmpty());
    }
};

QTEST_MAIN(TestLayoutCore)
#include "test_layout_core.moc"
