// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QColor>
#include <QImage>
#include <QRegularExpression>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <qqml.h>
#include <QGuiApplication>
#include <QPointF>
#include <QSignalSpy>
#include <QSizeF>
#include <QTest>
#include <QUrl>
#include <QVariantMap>
#include <QVector4D>

#include "daemon/rendering/surfaceshaderitem.h"
#include "daemon/rendering/zoneshaderitem.h"
#include <PhosphorRendering/ZoneLabelTexture.h>
#include "daemon/rendering/zoneshaderitem.h"
#include <PhosphorRendering/ZoneLabelTexture.h>

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

    /// A QML BINDING must actually deliver the backdrop image to the item.
    ///
    /// Regression guard, and it has to go through the QML engine rather than
    /// setProperty(): the property used to be QImage-typed, and a
    /// `Binding on wallpaperTexture { value: <var holding a QImage> }` wrote
    /// NOTHING through it — silently, with no engine warning, while a bool
    /// binding beside it applied. Every decoration preview in the settings app
    /// therefore ran with uHasBackdrop = 0 and drew its no-backdrop fallback:
    /// the blur family showed a flat gradient slab where a blurred desktop
    /// belonged. A C++ setProperty() with an exact-typed QVariant never
    /// reproduced it, so only a real binding pins the fix.
    void testSurfaceShaderItem_qmlBindingDeliversTheWallpaperImage()
    {
        qmlRegisterType<PlasmaZones::SurfaceShaderItem>("PlasmaZonesTest", 1, 0, "SurfaceShaderItem");

        QImage backdrop(8, 4, QImage::Format_RGBA8888);
        backdrop.fill(Qt::red);

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("testBackdrop"), QVariant::fromValue(backdrop));

        // A DIRECT binding, which is the only shape that works and therefore
        // the only shape the shipping QML is allowed to use — see the sweep in
        // the slot below.
        QQmlComponent component(&engine);
        component.setData(R"QML(
import QtQuick
import PlasmaZonesTest 1.0
Item {
    property alias direct: a
    SurfaceShaderItem { id: a; wallpaperTexture: testBackdrop }
}
)QML",
                          QUrl(QStringLiteral("qrc:/test_wallpaper_binding.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY(root);

        auto* item = root->property("direct").value<PlasmaZones::SurfaceShaderItem*>();
        QVERIFY(item);
        const QImage delivered = item->wallpaperTexture();
        QVERIFY(!delivered.isNull());
        QCOMPARE(delivered.size(), backdrop.size());
    }

    /// No shipping QML may drive `wallpaperTexture` through a Binding ELEMENT.
    ///
    /// `Binding { property: "wallpaperTexture"; value: <a QImage> }` hands the
    /// setter an INVALID QVariant — the image does not survive Binding's own
    /// `value` property, and nothing is logged, so the surface silently draws
    /// its no-backdrop appearance. (`Binding on wallpaperTexture { ... }` is
    /// worse still: it never writes at all.) A direct binding on the item
    /// delivers the image intact, so this pins the two hosts to that shape.
    /// labelsTexture is swept for the same reason even though its payload
    /// type does survive a Binding: the settings and editor previews hand that
    /// property a QImage and rely on the registered converter, and a QImage
    /// does not survive. audioSpectrum next to it is safe and not swept, since
    /// a QVariantList comes through intact.
    void testSurfaceShaderItem_noQmlDrivesTheWallpaperThroughABindingElement()
    {
        const QStringList hosts{QStringLiteral(P_SOURCE_DIR "/src/shared/SurfaceDecoration.qml"),
                                QStringLiteral(P_SOURCE_DIR "/src/shared/ZoneShaderRenderer.qml")};
        for (const QString& path : hosts) {
            QFile f(path);
            QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(path));
            QString src = QString::fromUtf8(f.readAll());
            // Comments first: both files DOCUMENT the broken shapes at length,
            // and a guard that matched the warning would fail on the warning.
            static const QRegularExpression blockComment(QStringLiteral("/\\*.*?\\*/"),
                                                         QRegularExpression::DotMatchesEverythingOption);
            static const QRegularExpression lineComment(QStringLiteral("(?<![:\"'])//[^\n]*"));
            src.remove(blockComment);
            src.remove(lineComment);

            for (const QString& prop : {QStringLiteral("wallpaperTexture"), QStringLiteral("labelsTexture")}) {
                const QString where = QStringLiteral("%1 (%2)").arg(path, prop);
                QVERIFY2(!src.contains(QStringLiteral("Binding on ") + prop), qPrintable(where));
                const QRegularExpression bindingElement(
                    QStringLiteral("Binding\\s*\\{[^}]*property:\\s*[\"']%1[\"'][^}]*\\}").arg(prop),
                    QRegularExpression::DotMatchesEverythingOption);
                QVERIFY2(!bindingElement.match(src).hasMatch(), qPrintable(where));
            }
        }
    }

    /// The zone labels must survive the trip through QML too, from BOTH shapes
    /// the hosts produce: the daemon passes a ZoneLabelTexture payload, while
    /// the settings and editor shader previews pass a full QImage and rely on
    /// the QImage→ZoneLabelTexture converter registered in the item.
    ///
    /// The payload happens to survive a Binding element; a QImage does not, so
    /// the previews drew their zones with no numbers on them. Only the QImage
    /// case ever failed, so both are pinned here rather than the interesting
    /// one alone.
    void testZoneShaderItem_qmlDeliversLabelsAsAPayloadAndAsAnImage()
    {
        qmlRegisterType<PlasmaZones::ZoneShaderItem>("PlasmaZonesTest", 1, 0, "ZoneShaderItem");

        QImage glyphs(16, 16, QImage::Format_ARGB32_Premultiplied);
        glyphs.fill(Qt::white);
        const PhosphorRendering::ZoneLabelTexture payload = PhosphorRendering::ZoneLabelTexture::fromImage(glyphs);
        QVERIFY(!payload.isEmpty());

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("testPayload"), QVariant::fromValue(payload));
        engine.rootContext()->setContextProperty(QStringLiteral("testImage"), QVariant::fromValue(glyphs));

        QQmlComponent component(&engine);
        component.setData(R"QML(
import QtQuick
import PlasmaZonesTest 1.0
Item {
    property alias fromPayload: a
    property alias fromImage: b
    ZoneShaderItem { id: a; labelsTexture: testPayload }
    ZoneShaderItem { id: b; labelsTexture: testImage }
}
)QML",
                          QUrl(QStringLiteral("qrc:/test_labels_binding.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY(root);

        for (const char* which : {"fromPayload", "fromImage"}) {
            auto* item = root->property(which).value<PlasmaZones::ZoneShaderItem*>();
            QVERIFY2(item, which);
            QVERIFY2(!item->labelsTexture().isEmpty(), which);
            QCOMPARE(item->labelsTexture().size, glyphs.size());
        }
    }

    /// A host with no labels passes null, which means "no numbers" and must
    /// clear the payload rather than warn or leave the previous one up.
    void testZoneShaderItem_aNullValueClearsTheLabels()
    {
        QImage glyphs(4, 4, QImage::Format_ARGB32_Premultiplied);
        glyphs.fill(Qt::white);

        PlasmaZones::ZoneShaderItem item;
        item.setProperty("labelsTexture", QVariant::fromValue(glyphs));
        QVERIFY(!item.labelsTexture().isEmpty());

        item.setProperty("labelsTexture", QVariant());
        QVERIFY(item.labelsTexture().isEmpty());
    }

    /// The no-backdrop state is ordinary, not an error: a host with nothing
    /// behind its surface passes null, and that must resolve to a null image
    /// rather than warning or leaving a stale one in place.
    void testSurfaceShaderItem_aNullValueClearsTheWallpaperImage()
    {
        QImage backdrop(4, 4, QImage::Format_RGBA8888);
        backdrop.fill(Qt::blue);

        PlasmaZones::SurfaceShaderItem item;
        item.setProperty("wallpaperTexture", QVariant::fromValue(backdrop));
        QVERIFY(!item.wallpaperTexture().isNull());

        item.setProperty("wallpaperTexture", QVariant());
        QVERIFY(item.wallpaperTexture().isNull());
    }

    void testSurfaceShaderItem_unsupportedUrlSchemeIsFullyRefused()
    {
        // http:// / ftp:// can't be loaded by the RHI pipeline; the base
        // rejects them at setShaderSource() as a FULL refusal — no state
        // changes, only a warning — so status, the property value, and the
        // rendered output always agree (see the zone item's sibling test
        // for the full rationale).
        SurfaceShaderItem item;
        item.setShaderSource(QUrl(QStringLiteral("http://example.com/effect.frag")));
        QCOMPARE(item.status(), SurfaceShaderItem::Status::Null);
        QVERIFY(item.errorLog().isEmpty());
        QVERIFY(item.shaderSource().isEmpty());
    }
};

QTEST_MAIN(TestSurfaceShaderItem)
#include "test_surface_shader_item.moc"
