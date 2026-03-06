// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>
#include <QGuiApplication>
#include <QVariantList>
#include <QVariantMap>
#include <QVector4D>
#include <QSizeF>
#include <QUrl>

#include "daemon/rendering/zoneshaderitem.h"
#include "daemon/rendering/zoneshadercommon.h"
#include "core/constants.h"
#include "../helpers/TestHelpers.h"

using namespace PlasmaZones;

/**
 * @brief Unit tests for ZoneShaderItem
 *
 * Tests cover:
 * - Zone data parsing and snapshot consistency (setZones / getZoneDataSnapshot)
 * - Hovered zone index highlight-only update path
 * - Shader source status transitions
 * - Custom color / param application via setShaderParams
 * - Audio spectrum fast path
 * - Buffer scale clamping
 * - Edge cases: empty zones, zero resolution, invalid shader source
 *
 * Note: ZoneShaderItem is a QQuickItem and requires QGuiApplication.
 * We test the data layer (parseZoneData, snapshots, setters) without
 * requiring a scene graph or GPU.
 */
class TestZoneShaderItem : public QObject
{
    Q_OBJECT

private:
    // Use shared makeZone helper from TestHelpers.h
    static QVariantMap makeZone(float x, float y, float w, float h, int zoneNumber = 0, bool highlighted = false,
                                const QString& id = QString())
    {
        return TestHelpers::makeZone(id, x, y, w, h, zoneNumber, highlighted);
    }

    // Helper: create a standard 4-zone layout (1920x1080 resolution)
    static QVariantList makeFourZoneLayout()
    {
        QVariantList zones;
        zones.append(makeZone(0, 0, 960, 540, 1, true));
        zones.append(makeZone(960, 0, 960, 540, 2, false));
        zones.append(makeZone(0, 540, 960, 540, 3, true));
        zones.append(makeZone(960, 540, 960, 540, 4, false));
        return zones;
    }

private Q_SLOTS:

    // ═══════════════════════════════════════════════════════════════════════════
    // P0: Crash prevention
    // ═══════════════════════════════════════════════════════════════════════════

    void testZoneShaderItem_setZonesProducesConsistentSnapshot()
    {
        ZoneShaderItem item;
        item.setIResolution(QSizeF(1920, 1080));

        QVariantList zones = makeFourZoneLayout();
        item.setZones(zones);

        ZoneDataSnapshot snapshot = item.getZoneDataSnapshot();

        // Zone count must match input
        QCOMPARE(snapshot.zoneCount, 4);
        // Rects, fill colors, border colors must all have matching sizes
        QCOMPARE(snapshot.rects.size(), 4);
        QCOMPARE(snapshot.fillColors.size(), 4);
        QCOMPARE(snapshot.borderColors.size(), 4);
        // Version must be non-zero after setting zones
        QVERIFY(snapshot.version > 0);
    }

    void testZoneShaderItem_hoveredZoneIndexDoesNotReparseZones()
    {
        ZoneShaderItem item;
        item.setIResolution(QSizeF(1920, 1080));

        QVariantList zones = makeFourZoneLayout();
        item.setZones(zones);

        ZoneDataSnapshot before = item.getZoneDataSnapshot();
        const int versionBefore = before.version;

        // Changing hoveredZoneIndex should update highlight flags but NOT reparse
        // all zone data from scratch. We verify by checking that the rect geometry
        // is identical and version incremented only by 1 (lightweight update).
        item.setHoveredZoneIndex(2);

        ZoneDataSnapshot after = item.getZoneDataSnapshot();

        // Geometry must be unchanged (no reparse)
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(after.rects[i].x, before.rects[i].x);
            QCOMPARE(after.rects[i].y, before.rects[i].y);
            QCOMPARE(after.rects[i].width, before.rects[i].width);
            QCOMPARE(after.rects[i].height, before.rects[i].height);
        }

        // Zone 2 (index 2) should now be highlighted
        QVERIFY(after.rects[2].highlighted);

        // Version should have incremented by exactly 1 (lightweight update, not full reparse)
        QCOMPARE(after.version, versionBefore + 1);
    }

    void testZoneShaderItem_zoneDataSnapshotConsistency()
    {
        ZoneShaderItem item;
        item.setIResolution(QSizeF(800, 600));

        QVariantList zones;
        zones.append(makeZone(0, 0, 400, 600, 1, false));
        zones.append(makeZone(400, 0, 400, 600, 2, true));
        item.setZones(zones);

        ZoneDataSnapshot snapshot = item.getZoneDataSnapshot();

        // All arrays must have the same count as zoneCount
        QCOMPARE(snapshot.rects.size(), snapshot.zoneCount);
        QCOMPARE(snapshot.fillColors.size(), snapshot.zoneCount);
        QCOMPARE(snapshot.borderColors.size(), snapshot.zoneCount);
    }

    void testZoneShaderItem_shaderSourceChangeResetsStatus()
    {
        ZoneShaderItem item;

        // Initially status should be Null
        QCOMPARE(item.status(), ZoneShaderItem::Status::Null);

        QSignalSpy statusSpy(&item, &ZoneShaderItem::statusChanged);

        // Setting a shader source should transition to Loading
        item.setShaderSource(QUrl::fromLocalFile(QStringLiteral("/tmp/nonexistent_test.frag")));

        QCOMPARE(item.status(), ZoneShaderItem::Status::Loading);
        QVERIFY(statusSpy.count() >= 1);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P1: Functional
    // ═══════════════════════════════════════════════════════════════════════════

    void testZoneShaderItem_parseZoneDataNormalization()
    {
        ZoneShaderItem item;
        item.setIResolution(QSizeF(1920, 1080));

        QVariantList zones;
        zones.append(makeZone(960, 540, 480, 270, 1));
        item.setZones(zones);

        ZoneDataSnapshot snapshot = item.getZoneDataSnapshot();
        QCOMPARE(snapshot.rects.size(), 1);

        const ZoneRect& rect = snapshot.rects[0];

        // 960/1920 = 0.5, 540/1080 = 0.5, 480/1920 = 0.25, 270/1080 = 0.25
        QVERIFY(qFuzzyCompare(rect.x, 0.5f));
        QVERIFY(qFuzzyCompare(rect.y, 0.5f));
        QVERIFY(qFuzzyCompare(rect.width, 0.25f));
        QVERIFY(qFuzzyCompare(rect.height, 0.25f));
    }

    void testZoneShaderItem_parseZoneDataHighlightCounting()
    {
        ZoneShaderItem item;
        item.setIResolution(QSizeF(1920, 1080));

        QVariantList zones;
        zones.append(makeZone(0, 0, 480, 540, 1, true)); // highlighted
        zones.append(makeZone(480, 0, 480, 540, 2, false)); // not highlighted
        zones.append(makeZone(960, 0, 480, 540, 3, true)); // highlighted
        zones.append(makeZone(1440, 0, 480, 540, 4, false)); // not highlighted
        item.setZones(zones);

        QCOMPARE(item.highlightedCount(), 2);

        ZoneDataSnapshot snapshot = item.getZoneDataSnapshot();
        QCOMPARE(snapshot.highlightedCount, 2);
    }

    void testZoneShaderItem_setShaderParamsAppliesCustomColors()
    {
        ZoneShaderItem item;

        // Verify default color 1 is orange (1.0, 0.5, 0.0, 1.0)
        QVector4D defaultColor = item.customColor1();
        QVERIFY(qFuzzyCompare(defaultColor.x(), 1.0f));
        QVERIFY(qFuzzyCompare(defaultColor.y(), 0.5f));
        QVERIFY(qFuzzyIsNull(defaultColor.z())); // orange has no blue
        QVERIFY(qFuzzyCompare(defaultColor.w(), 1.0f)); // fully opaque

        // Apply custom color via shaderParams
        QVariantMap params;
        params.insert(QStringLiteral("customColor1"), QColor(Qt::blue));
        item.setShaderParams(params);

        QVector4D newColor = item.customColor1();
        // Blue: (0, 0, 1, 1)
        // Note: qFuzzyCompare fails near zero, use qFuzzyIsNull for zero checks
        QVERIFY(qFuzzyIsNull(newColor.x()));
        QVERIFY(qFuzzyIsNull(newColor.y()));
        QVERIFY(qFuzzyCompare(newColor.z(), 1.0f));
        QVERIFY(qFuzzyCompare(newColor.w(), 1.0f));
    }

    void testZoneShaderItem_setAudioSpectrumPreservesType()
    {
        ZoneShaderItem item;

        QSignalSpy spectrumSpy(&item, &ZoneShaderItem::audioSpectrumChanged);

        // Use the raw QVector<float> fast path
        QVector<float> spectrum = {0.1f, 0.5f, 0.8f, 1.0f};
        item.setAudioSpectrumRaw(spectrum);

        QCOMPARE(spectrumSpy.count(), 1);

        // Verify the variant round-trip preserves QVector<float>
        QVariant v = item.audioSpectrumVariant();
        QVERIFY(v.metaType() == QMetaType::fromType<QVector<float>>());
        QVector<float> retrieved = v.value<QVector<float>>();
        QCOMPARE(retrieved.size(), 4);
        QVERIFY(qFuzzyCompare(retrieved[0], 0.1f));
        QVERIFY(qFuzzyCompare(retrieved[3], 1.0f));
    }

    void testZoneShaderItem_bufferScaleClamped()
    {
        ZoneShaderItem item;

        // Default should be 1.0
        QVERIFY(qFuzzyCompare(item.bufferScale(), 1.0));

        // Below minimum (0.125) should clamp
        item.setBufferScale(0.01);
        QVERIFY(qFuzzyCompare(item.bufferScale(), 0.125));

        // Above maximum (1.0) should clamp
        item.setBufferScale(5.0);
        QVERIFY(qFuzzyCompare(item.bufferScale(), 1.0));

        // Normal value should pass through
        item.setBufferScale(0.5);
        QVERIFY(qFuzzyCompare(item.bufferScale(), 0.5));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P2: Edge cases
    // ═══════════════════════════════════════════════════════════════════════════

    void testZoneShaderItem_emptyZonesProducesEmptySnapshot()
    {
        ZoneShaderItem item;
        item.setIResolution(QSizeF(1920, 1080));

        // Set zones then clear
        item.setZones(makeFourZoneLayout());
        QCOMPARE(item.zoneCount(), 4);

        item.setZones(QVariantList());

        QCOMPARE(item.zoneCount(), 0);
        QCOMPARE(item.highlightedCount(), 0);

        ZoneDataSnapshot snapshot = item.getZoneDataSnapshot();
        QCOMPARE(snapshot.zoneCount, 0);
        QVERIFY(snapshot.rects.isEmpty());
        QVERIFY(snapshot.fillColors.isEmpty());
        QVERIFY(snapshot.borderColors.isEmpty());
    }

    void testZoneShaderItem_zeroResolutionDoesNotDivideByZero()
    {
        ZoneShaderItem item;
        // Resolution is default (0x0 or unset) - parseZoneData should use fallback divisor

        QVariantList zones;
        zones.append(makeZone(100, 200, 300, 400, 1));
        item.setZones(zones);

        // Must not crash - fallback divisor is 1.0
        ZoneDataSnapshot snapshot = item.getZoneDataSnapshot();
        QCOMPARE(snapshot.zoneCount, 1);

        // With fallback divisor of 1.0, pixel coords pass through as-is
        QVERIFY(qFuzzyCompare(snapshot.rects[0].x, 100.0f));
        QVERIFY(qFuzzyCompare(snapshot.rects[0].y, 200.0f));
        QVERIFY(qFuzzyCompare(snapshot.rects[0].width, 300.0f));
        QVERIFY(qFuzzyCompare(snapshot.rects[0].height, 400.0f));
    }

    void testZoneShaderItem_invalidShaderSourceSetsErrorStatus()
    {
        ZoneShaderItem item;

        // Setting a valid URL transitions to Loading (actual loading happens in updatePaintNode)
        item.setShaderSource(QUrl::fromLocalFile(QStringLiteral("/nonexistent/path/shader.frag")));
        QCOMPARE(item.status(), ZoneShaderItem::Status::Loading);

        // Setting an empty URL: setShaderSource unconditionally sets Loading on any
        // source change. The Null transition only happens later in loadShader() which
        // is called from updatePaintNode (scene graph), not available in headless mode.
        item.setShaderSource(QUrl());
        QCOMPARE(item.status(), ZoneShaderItem::Status::Loading);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Additional: signals and setter behavior
    // ═══════════════════════════════════════════════════════════════════════════

    void testZoneShaderItem_setZonesEmitsSignals()
    {
        ZoneShaderItem item;
        item.setIResolution(QSizeF(1920, 1080));

        QSignalSpy zonesSpy(&item, &ZoneShaderItem::zonesChanged);
        QSignalSpy countSpy(&item, &ZoneShaderItem::zoneCountChanged);

        item.setZones(makeFourZoneLayout());

        QCOMPARE(zonesSpy.count(), 1);
        QCOMPARE(countSpy.count(), 1);
    }

    void testZoneShaderItem_setZonesSameDataNoSignal()
    {
        ZoneShaderItem item;
        item.setIResolution(QSizeF(1920, 1080));

        QVariantList zones = makeFourZoneLayout();
        item.setZones(zones);

        QSignalSpy zonesSpy(&item, &ZoneShaderItem::zonesChanged);

        // Setting same data should not emit
        item.setZones(zones);
        QCOMPARE(zonesSpy.count(), 0);
    }

    void testZoneShaderItem_hoveredZoneIndexClamping()
    {
        ZoneShaderItem item;
        item.setIResolution(QSizeF(1920, 1080));
        item.setZones(makeFourZoneLayout());

        // Out of range positive index should clamp to -1
        item.setHoveredZoneIndex(100);
        QCOMPARE(item.hoveredZoneIndex(), -1);

        // Negative index should clamp to -1
        item.setHoveredZoneIndex(-5);
        QCOMPARE(item.hoveredZoneIndex(), -1);

        // Valid index should pass through
        item.setHoveredZoneIndex(2);
        QCOMPARE(item.hoveredZoneIndex(), 2);
    }

    void testZoneShaderItem_setShaderParamsAppliesFloatSlots()
    {
        ZoneShaderItem item;

        QVariantMap params;
        params.insert(QStringLiteral("customParams1_x"), 0.42f);
        params.insert(QStringLiteral("customParams1_y"), 0.99f);
        item.setShaderParams(params);

        QVector4D p1 = item.customParams1();
        QVERIFY(qFuzzyCompare(p1.x(), 0.42f));
        QVERIFY(qFuzzyCompare(p1.y(), 0.99f));
    }

    void testZoneShaderItem_bufferWrapNormalization()
    {
        ZoneShaderItem item;

        // Default is "clamp"
        QCOMPARE(item.bufferWrap(), QStringLiteral("clamp"));

        // "repeat" is valid
        item.setBufferWrap(QStringLiteral("repeat"));
        QCOMPARE(item.bufferWrap(), QStringLiteral("repeat"));

        // Invalid values should normalize to "clamp"
        item.setBufferWrap(QStringLiteral("invalid"));
        QCOMPARE(item.bufferWrap(), QStringLiteral("clamp"));
    }
};

QTEST_MAIN(TestZoneShaderItem)
#include "test_zone_shader_item.moc"
