// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QGuiApplication>
#include <QPointF>
#include <QSignalSpy>
#include <QSizeF>
#include <QTest>
#include <QUrl>
#include <QVariantMap>
#include <QVector4D>

#include "daemon/rendering/surfaceshaderitem.h"

using namespace PlasmaZones;

/**
 * @brief Unit tests for SurfaceShaderItem
 *
 * SurfaceShaderItem is a QQuickItem (requires QGuiApplication). As with the
 * ZoneShaderItem tests, we exercise only the data layer — construction, the
 * surface-state property surface, inherited param application, and the shader-
 * source status transition — without a scene graph or GPU, so updatePaintNode
 * (which is where the SurfaceUniformProfile-backed node is actually created) is
 * not driven here. The profile wiring is verified structurally: createShaderNode
 * is the only surface-specific node hook and a node-creation test would need a
 * live QQuickWindow on a compositor.
 */
class TestSurfaceShaderItem : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ═══════════════════════════════════════════════════════════════════════
    // Construction + surface-state defaults
    // ═══════════════════════════════════════════════════════════════════════

    void testSurfaceShaderItem_constructsWithIdentityDefaults()
    {
        SurfaceShaderItem item;

        // Defaults mirror the UboFrameState surface-only field defaults: an
        // identity decoration (full scale, unfocused, zero geometry).
        QVERIFY(qFuzzyCompare(item.surfaceScale(), 1.0));
        QVERIFY(!item.surfaceFocused());
        QCOMPARE(item.surfaceSize(), QSizeF());
        QCOMPARE(item.surfaceFrameTopLeft(), QPointF());
        QCOMPARE(item.surfaceFrameSize(), QSizeF());

        // No shader assigned yet.
        QCOMPARE(item.status(), SurfaceShaderItem::Status::Null);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Surface-state setters + change signals
    // ═══════════════════════════════════════════════════════════════════════

    void testSurfaceShaderItem_surfaceScaleSetterEmitsOnChange()
    {
        SurfaceShaderItem item;
        QSignalSpy spy(&item, &SurfaceShaderItem::surfaceScaleChanged);

        item.setSurfaceScale(2.0);
        QCOMPARE(spy.count(), 1);
        QVERIFY(qFuzzyCompare(item.surfaceScale(), 2.0));

        // Re-setting the same value must not emit (emit-only-on-change rule).
        item.setSurfaceScale(2.0);
        QCOMPARE(spy.count(), 1);
    }

    void testSurfaceShaderItem_surfaceFocusedSetterEmitsOnChange()
    {
        SurfaceShaderItem item;
        QSignalSpy spy(&item, &SurfaceShaderItem::surfaceFocusedChanged);

        item.setSurfaceFocused(true);
        QCOMPARE(spy.count(), 1);
        QVERIFY(item.surfaceFocused());

        item.setSurfaceFocused(true);
        QCOMPARE(spy.count(), 1);
    }

    void testSurfaceShaderItem_surfaceGeometrySettersEmitOnChange()
    {
        SurfaceShaderItem item;
        QSignalSpy sizeSpy(&item, &SurfaceShaderItem::surfaceSizeChanged);
        QSignalSpy frameTlSpy(&item, &SurfaceShaderItem::surfaceFrameTopLeftChanged);
        QSignalSpy frameSizeSpy(&item, &SurfaceShaderItem::surfaceFrameSizeChanged);

        item.setSurfaceSize(QSizeF(800, 600));
        QCOMPARE(sizeSpy.count(), 1);
        QCOMPARE(item.surfaceSize(), QSizeF(800, 600));

        item.setSurfaceFrameTopLeft(QPointF(4, 8));
        QCOMPARE(frameTlSpy.count(), 1);
        QCOMPARE(item.surfaceFrameTopLeft(), QPointF(4, 8));

        item.setSurfaceFrameSize(QSizeF(792, 584));
        QCOMPARE(frameSizeSpy.count(), 1);
        QCOMPARE(item.surfaceFrameSize(), QSizeF(792, 584));

        // Idempotent re-sets suppress signals.
        item.setSurfaceSize(QSizeF(800, 600));
        item.setSurfaceFrameTopLeft(QPointF(4, 8));
        item.setSurfaceFrameSize(QSizeF(792, 584));
        QCOMPARE(sizeSpy.count(), 1);
        QCOMPARE(frameTlSpy.count(), 1);
        QCOMPARE(frameSizeSpy.count(), 1);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Inherited base behaviour (shaderParams / status)
    // ═══════════════════════════════════════════════════════════════════════

    void testSurfaceShaderItem_setShaderParamsAppliesSlots()
    {
        // The base setShaderParams maps `customParamsN_<xyzw>` / `customColorN`
        // (the slot form SurfaceShaderRegistry::translateSurfaceParams emits)
        // onto the UBO — SurfaceShaderItem does not override it.
        SurfaceShaderItem item;

        QVariantMap params;
        params.insert(QStringLiteral("customParams1_x"), 0.42f);
        params.insert(QStringLiteral("customColor1"), QColor(Qt::blue));
        item.setShaderParams(params);

        const QVector4D p1 = item.customParams1();
        QVERIFY(qFuzzyCompare(p1.x(), 0.42f));

        constexpr float kEpsilon = 0.002f;
        const QColor c1 = item.customColor1();
        QVERIFY(qAbs(static_cast<float>(c1.blueF()) - 1.0f) < kEpsilon);
        QVERIFY(qAbs(static_cast<float>(c1.redF())) < kEpsilon);
    }

    void testSurfaceShaderItem_shaderSourceTransitionsToLoading()
    {
        // In headless tests there is no scene graph, so updatePaintNode never
        // runs and setShaderSource's Loading state never advances to Ready /
        // Error / Null. Mirrors the ZoneShaderItem headless status test.
        SurfaceShaderItem item;
        QCOMPARE(item.status(), SurfaceShaderItem::Status::Null);

        QSignalSpy statusSpy(&item, &SurfaceShaderItem::statusChanged);
        item.setShaderSource(QUrl::fromLocalFile(QStringLiteral("/nonexistent/effect.frag")));
        QCOMPARE(item.status(), SurfaceShaderItem::Status::Loading);
        // Exactly one Null -> Loading transition; headless, updatePaintNode never
        // runs so no further Ready/Error/Null change can follow to inflate this.
        QCOMPARE(statusSpy.count(), 1);
    }

    void testSurfaceShaderItem_unsupportedUrlSchemeSetsError()
    {
        // http:// / ftp:// can't be loaded by the RHI pipeline; the base
        // rejects them at setShaderSource() with an Error status + log.
        SurfaceShaderItem item;
        item.setShaderSource(QUrl(QStringLiteral("http://example.com/effect.frag")));
        QCOMPARE(item.status(), SurfaceShaderItem::Status::Error);
        QVERIFY(!item.errorLog().isEmpty());
    }
};

QTEST_MAIN(TestSurfaceShaderItem)
#include "test_surface_shader_item.moc"
