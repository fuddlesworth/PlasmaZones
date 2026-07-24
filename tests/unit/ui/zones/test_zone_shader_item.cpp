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
#include "daemon/rendering/zonelabeltexturebuilder.h"
#include <PhosphorRendering/ZoneShaderCommon.h>
#include <PhosphorRendering/ZoneLabelTexture.h>
#include "config/configdefaults.h"
#include "core/types/constants.h"
#include "helpers/TestHelpers.h"

#include <QImage>

using namespace PlasmaZones;

/**
 * @brief Unit tests for ZoneShaderItem
 *
 * Tests cover:
 * - PhosphorZones::Zone data parsing and snapshot consistency (setZones / getZoneDataSnapshot)
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

        PhosphorRendering::ZoneDataSnapshot snapshot = item.getZoneDataSnapshot();

        // PhosphorZones::Zone count must match input
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

        PhosphorRendering::ZoneDataSnapshot before = item.getZoneDataSnapshot();
        const int versionBefore = before.version;

        // Changing hoveredZoneIndex should update highlight flags but NOT reparse
        // all zone data from scratch. We verify by checking that the rect geometry
        // is identical and version incremented only by 1 (lightweight update).
        item.setHoveredZoneIndex(2);

        PhosphorRendering::ZoneDataSnapshot after = item.getZoneDataSnapshot();

        // Geometry must be unchanged (no reparse)
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(after.rects[i].x, before.rects[i].x);
            QCOMPARE(after.rects[i].y, before.rects[i].y);
            QCOMPARE(after.rects[i].width, before.rects[i].width);
            QCOMPARE(after.rects[i].height, before.rects[i].height);
        }

        // PhosphorZones::Zone 2 (index 2) should now be highlighted
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

        PhosphorRendering::ZoneDataSnapshot snapshot = item.getZoneDataSnapshot();

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

        PhosphorRendering::ZoneDataSnapshot snapshot = item.getZoneDataSnapshot();
        QCOMPARE(snapshot.rects.size(), 1);

        const PhosphorRendering::ZoneRect& rect = snapshot.rects[0];

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

        PhosphorRendering::ZoneDataSnapshot snapshot = item.getZoneDataSnapshot();
        QCOMPARE(snapshot.highlightedCount, 2);
    }

    void testZoneShaderItem_setShaderParamsAppliesCustomColors()
    {
        ZoneShaderItem item;

        // Verify default color 1 matches ConfigDefaults::highlightColor()
        constexpr float kEpsilon = 0.002f;
        QColor defaultColor = item.customColor1();
        QColor expectedColor = PlasmaZones::ConfigDefaults::highlightColor();
        QVERIFY(qAbs(static_cast<float>(defaultColor.redF()) - static_cast<float>(expectedColor.redF())) < kEpsilon);
        QVERIFY(qAbs(static_cast<float>(defaultColor.greenF()) - static_cast<float>(expectedColor.greenF()))
                < kEpsilon);
        QVERIFY(qAbs(static_cast<float>(defaultColor.blueF()) - static_cast<float>(expectedColor.blueF())) < kEpsilon);
        QVERIFY(qAbs(static_cast<float>(defaultColor.alphaF()) - static_cast<float>(expectedColor.alphaF()))
                < kEpsilon);

        // Apply custom color via shaderParams
        QVariantMap params;
        params.insert(QStringLiteral("customColor1"), QColor(Qt::blue));
        item.setShaderParams(params);

        QColor newColor = item.customColor1();
        // Blue: (0, 0, 1, 1)
        QVERIFY(qAbs(static_cast<float>(newColor.redF())) < kEpsilon);
        QVERIFY(qAbs(static_cast<float>(newColor.greenF())) < kEpsilon);
        QVERIFY(qAbs(static_cast<float>(newColor.blueF()) - 1.0f) < kEpsilon);
        QVERIFY(qAbs(static_cast<float>(newColor.alphaF()) - 1.0f) < kEpsilon);
    }

    void testZoneShaderItem_setAudioSpectrumPreservesType()
    {
        ZoneShaderItem item;

        QSignalSpy spectrumSpy(&item, &ZoneShaderItem::audioSpectrumChanged);

        // Use the raw QVector<float> fast path
        QVector<float> spectrum = {0.1f, 0.5f, 0.8f, 1.0f};
        item.setAudioSpectrum(spectrum);

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

        PhosphorRendering::ZoneDataSnapshot snapshot = item.getZoneDataSnapshot();
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
        PhosphorRendering::ZoneDataSnapshot snapshot = item.getZoneDataSnapshot();
        QCOMPARE(snapshot.zoneCount, 1);

        // With fallback divisor of 1.0, pixel coords pass through as-is
        QVERIFY(qFuzzyCompare(snapshot.rects[0].x, 100.0f));
        QVERIFY(qFuzzyCompare(snapshot.rects[0].y, 200.0f));
        QVERIFY(qFuzzyCompare(snapshot.rects[0].width, 300.0f));
        QVERIFY(qFuzzyCompare(snapshot.rects[0].height, 400.0f));
    }

    void testZoneShaderItem_shaderSourceTransitionsToLoadingInHeadlessMode()
    {
        // Renamed from the old misleading "SetsErrorStatus" — in headless
        // tests there is no scene graph, so updatePaintNode never runs and
        // setShaderSource's Loading state never advances to Error or Null.
        // That's all this test can assert without a QQuickView on a live
        // compositor.
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

    void testZoneShaderItem_unsupportedUrlSchemeSetsError()
    {
        // http:// / ftp:// / etc. can't be loaded by the RHI pipeline; the
        // library now rejects them at setShaderSource() (input-validation
        // boundary) with an Error status + log, rather than silently
        // deferring to the render thread where it would fail with a
        // generic "Shader loading failed" message.
        ZoneShaderItem item;
        item.setShaderSource(QUrl(QStringLiteral("http://example.com/shader.frag")));
        QCOMPARE(item.status(), ZoneShaderItem::Status::Error);
        QVERIFY(!item.errorLog().isEmpty());
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

    void testZoneShaderItem_hoveredZoneIndexReDerivedOnZoneListChange()
    {
        // The two QML bindings (zones, hoveredZoneIndex) evaluate in an
        // unspecified order, so setZones has to re-derive the effective hover
        // from the last REQUESTED index in both directions.
        ZoneShaderItem item;
        item.setIResolution(QSizeF(1920, 1080));
        item.setZones(makeFourZoneLayout());
        item.setHoveredZoneIndex(3);
        QCOMPARE(item.hoveredZoneIndex(), 3);

        QSignalSpy hoverSpy(&item, &ZoneShaderItem::hoveredZoneIndexChanged);

        // Shrink past the hovered index: it must drop, not stay stale.
        QVariantList two;
        two.append(makeZone(0, 0, 960, 1080, 1, false));
        two.append(makeZone(960, 0, 960, 1080, 2, false));
        item.setZones(two);
        QCOMPARE(item.hoveredZoneIndex(), -1);
        QCOMPARE(hoverSpy.count(), 1);
        // Nothing is highlighted by hover any more, and neither test zone
        // carries its own highlight flag.
        QCOMPARE(item.highlightedCount(), 0);

        // Grow again: the request that arrived while the list was short has to
        // come back, or the highlight is lost until the cursor moves.
        item.setZones(makeFourZoneLayout());
        QCOMPARE(item.hoveredZoneIndex(), 3);
        QCOMPARE(hoverSpy.count(), 2);
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

    // ═══════════════════════════════════════════════════════════════════════════
    // Zone-label sparse payload (memory: full-screen image → sparse glyph tiles)
    // ═══════════════════════════════════════════════════════════════════════════

    void testZoneLabelBuilder_producesSparseTilesNotFullScreen()
    {
        const QSize screen(1920, 1080);
        const PhosphorRendering::ZoneLabelTexture labels =
            ZoneLabelTextureBuilder::build(makeFourZoneLayout(), screen, Qt::white, /*showNumbers=*/true);

        QVERIFY(!labels.isEmpty());
        // The payload is screen-addressed but carries one tile per numbered zone.
        QCOMPARE(labels.size, screen);
        QCOMPARE(labels.tiles.size(), 4);

        const qint64 fullScreenBytes = qint64(screen.width()) * screen.height() * 4;
        qint64 tileBytes = 0;
        for (const PhosphorRendering::ZoneLabelTile& tile : labels.tiles) {
            QVERIFY(!tile.image.isNull());
            // Each tile is a small glyph region, far smaller than the screen.
            QVERIFY(tile.image.width() < screen.width() / 2);
            QVERIFY(tile.image.height() < screen.height() / 2);
            // And it sits fully inside the texture.
            const QRect r(tile.dest, tile.image.size());
            QVERIFY(QRect(QPoint(0, 0), screen).contains(r));
            tileBytes += qint64(tile.image.width()) * tile.image.height() * 4;
        }
        // The whole point: glyph tiles cost a tiny fraction of a full-screen image.
        QVERIFY(tileBytes < fullScreenBytes / 10);
    }

    void testZoneLabelBuilder_emptyWhenNumbersOff()
    {
        const PhosphorRendering::ZoneLabelTexture labels =
            ZoneLabelTextureBuilder::build(makeFourZoneLayout(), QSize(1920, 1080), Qt::white, /*showNumbers=*/false);
        QVERIFY(labels.isEmpty());
        QVERIFY(labels.toImage().isNull());
    }

    void testZoneLabelTexture_toImageCompositesToFullSize()
    {
        const QSize screen(1920, 1080);
        const PhosphorRendering::ZoneLabelTexture labels =
            ZoneLabelTextureBuilder::build(makeFourZoneLayout(), screen, Qt::white, /*showNumbers=*/true);

        const QImage composited = labels.toImage();
        QCOMPARE(composited.size(), screen);
        QVERIFY(!composited.isNull());

        // Some pixels must be non-transparent (the glyphs were painted in).
        bool anyOpaque = false;
        for (const PhosphorRendering::ZoneLabelTile& tile : labels.tiles) {
            // Sample the centre of each tile's destination region.
            const QPoint c = tile.dest + QPoint(tile.image.width() / 2, tile.image.height() / 2);
            if (qAlpha(composited.pixel(c)) > 0) {
                anyOpaque = true;
                break;
            }
        }
        QVERIFY(anyOpaque);
    }

    void testZoneShaderItem_labelsTexturePayloadRoundTrips()
    {
        ZoneShaderItem item;
        const PhosphorRendering::ZoneLabelTexture labels =
            ZoneLabelTextureBuilder::build(makeFourZoneLayout(), QSize(1920, 1080), Qt::white, /*showNumbers=*/true);

        QSignalSpy spy(&item, &ZoneShaderItem::labelsTextureChanged);
        item.setLabelsTexture(labels);
        QCOMPARE(spy.count(), 1);

        const PhosphorRendering::ZoneLabelTexture got = item.labelsTexture();
        QCOMPARE(got.size, labels.size);
        QCOMPARE(got.tiles.size(), labels.tiles.size());
    }

    void testZoneShaderItem_qImageConverterWrapsAsSingleTile()
    {
        // The editor/settings shader previews still hand the labelsTexture
        // property a full QImage; the ctor-registered converter must wrap it as
        // a single full-size tile so those paths keep working unchanged.
        ZoneShaderItem item; // ctor registers the QImage→ZoneLabelTexture converter

        QImage img(64, 48, QImage::Format_ARGB32);
        img.fill(Qt::red);

        QVERIFY(item.setProperty("labelsTexture", QVariant::fromValue(img)));
        const PhosphorRendering::ZoneLabelTexture got = item.labelsTexture();
        QCOMPARE(got.size, img.size());
        QCOMPARE(got.tiles.size(), 1);
        QCOMPARE(got.tiles.first().dest, QPoint(0, 0));
        QCOMPARE(got.tiles.first().image.size(), img.size());
    }

    void testZoneShaderItem_labelsTextureSamePayloadSuppressesSignal()
    {
        ZoneShaderItem item;
        const PhosphorRendering::ZoneLabelTexture labels =
            ZoneLabelTextureBuilder::build(makeFourZoneLayout(), QSize(1920, 1080), Qt::white, /*showNumbers=*/true);

        item.setLabelsTexture(labels); // first set: genuine change from the empty default

        QSignalSpy spy(&item, &ZoneShaderItem::labelsTextureChanged);
        // Re-setting an equal payload must be suppressed by the operator== guard
        // (the "emit only on change" rule). Without the guard this would fire.
        item.setLabelsTexture(labels);
        QCOMPARE(spy.count(), 0);

        // A genuinely different payload (same geometry, different glyph color →
        // different tile pixels) still emits.
        const PhosphorRendering::ZoneLabelTexture other =
            ZoneLabelTextureBuilder::build(makeFourZoneLayout(), QSize(1920, 1080), Qt::red, /*showNumbers=*/true);
        item.setLabelsTexture(other);
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(TestZoneShaderItem)
#include "test_zone_shader_item.moc"
