// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSurfaceQuick/SurfaceShaderItem.h>

#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <QPointF>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QSizeF>
#include <QTest>
#include <QUrl>
#include <QVariantMap>
#include <QVector4D>

#include <memory>

using PhosphorSurfaceQuick::SurfaceShaderItem;

/**
 * @brief Unit tests for SurfaceShaderItem and the org.phosphor.surface module.
 *
 * SurfaceShaderItem is a QQuickItem (requires QGuiApplication). Only the data
 * layer is exercised here: construction, the surface-state property surface,
 * inherited param application, and the shader-source status transition, with
 * no scene graph or GPU, so updatePaintNode (where the SurfaceUniformProfile-
 * backed node is actually created) is not driven. The profile wiring is
 * verified structurally: createShaderNode is the only surface-specific node
 * hook and a node-creation test would need a live QQuickWindow.
 *
 * The module slots pin what the QML side of this library promises: the type
 * and the SurfaceDecoration host both resolve through `import
 * org.phosphor.surface`, and the item's status enum reaches QML.
 */
class TestSurfaceShaderItem : public QObject
{
    Q_OBJECT

private:
    QQmlEngine m_engine;

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

    void testSurfaceShaderItem_backdropRectSettersEmitOnChange()
    {
        SurfaceShaderItem item;
        QSignalSpy screenSpy(&item, &SurfaceShaderItem::backdropScreenRectChanged);
        QSignalSpy surfaceSpy(&item, &SurfaceShaderItem::backdropSurfaceRectChanged);

        // Both empty by default: the whole wallpaper is sampled.
        QVERIFY(!item.backdropScreenRect().isValid());
        QVERIFY(!item.backdropSurfaceRect().isValid());

        item.setBackdropScreenRect(QRectF(0, 0, 1920, 1080));
        QCOMPARE(screenSpy.count(), 1);
        item.setBackdropSurfaceRect(QRectF(100, 50, 400, 300));
        QCOMPARE(surfaceSpy.count(), 1);

        item.setBackdropScreenRect(QRectF(0, 0, 1920, 1080));
        item.setBackdropSurfaceRect(QRectF(100, 50, 400, 300));
        QCOMPARE(screenSpy.count(), 1);
        QCOMPARE(surfaceSpy.count(), 1);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Inherited base behaviour (shaderParams / status)
    // ═══════════════════════════════════════════════════════════════════════

    void testSurfaceShaderItem_setShaderParamsAppliesSlots()
    {
        // The base setShaderParams maps `customParamsN_<xyzw>` / `customColorN`
        // (the slot form SurfaceShaderRegistry::translateSurfaceParams emits)
        // onto the UBO. SurfaceShaderItem does not override it.
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
        // Error / Null.
        SurfaceShaderItem item;
        QCOMPARE(item.status(), SurfaceShaderItem::Status::Null);

        QSignalSpy statusSpy(&item, &SurfaceShaderItem::statusChanged);
        item.setShaderSource(QUrl::fromLocalFile(QStringLiteral("/nonexistent/effect.frag")));
        QCOMPARE(item.status(), SurfaceShaderItem::Status::Loading);
        // Exactly one Null -> Loading transition; headless, updatePaintNode never
        // runs so no further Ready/Error/Null change can follow to inflate this.
        QCOMPARE(statusSpy.count(), 1);
    }

    void testSurfaceShaderItem_unsupportedUrlSchemeIsFullyRefused()
    {
        // http:// / ftp:// can't be loaded by the RHI pipeline; the base
        // rejects them at setShaderSource() as a FULL refusal (no state
        // changes, only a warning) so status, the property value, and the
        // rendered output always agree.
        SurfaceShaderItem item;
        item.setShaderSource(QUrl(QStringLiteral("http://example.com/effect.frag")));
        QCOMPARE(item.status(), SurfaceShaderItem::Status::Null);
        QVERIFY(item.errorLog().isEmpty());
        QVERIFY(item.shaderSource().isEmpty());
    }

    // ═══════════════════════════════════════════════════════════════════════
    // The wallpaper image through QML
    // ═══════════════════════════════════════════════════════════════════════

    /// A QML BINDING must actually deliver the backdrop image to the item.
    ///
    /// Regression guard, and it has to go through the QML engine rather than
    /// setProperty(): the property used to be QImage-typed, and a
    /// `Binding on wallpaperTexture { value: <var holding a QImage> }` wrote
    /// NOTHING through it, silently, with no engine warning, while a bool
    /// binding beside it applied. A C++ setProperty() with an exact-typed
    /// QVariant never reproduced it, so only a real binding pins the fix. The
    /// plasmazones tier sweeps its shipping QML for the broken shapes
    /// (test_shader_item_qml_bindings); this pins the shape that works.
    void testSurfaceShaderItem_qmlBindingDeliversTheWallpaperImage()
    {
        QImage backdrop(8, 4, QImage::Format_RGBA8888);
        backdrop.fill(Qt::red);

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("testBackdrop"), QVariant::fromValue(backdrop));

        QQmlComponent component(&engine);
        component.setData(R"QML(
import QtQuick
import org.phosphor.surface
Item {
    property alias direct: a
    SurfaceShaderItem { id: a; wallpaperTexture: testBackdrop }
}
)QML",
                          QUrl(QStringLiteral("inline://test_wallpaper_binding.qml")));
        // A module import compiles asynchronously; wait for it to settle before
        // reading the status, the same way the module-load slots below do.
        QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 5000);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY(root);

        auto* item = root->property("direct").value<SurfaceShaderItem*>();
        QVERIFY(item);
        const QImage delivered = item->wallpaperTexture();
        QVERIFY(!delivered.isNull());
        QCOMPARE(delivered.size(), backdrop.size());
    }

    /// The no-backdrop state is ordinary, not an error: a host with nothing
    /// behind its surface passes null, and that must resolve to a null image
    /// rather than warning or leaving a stale one in place.
    void testSurfaceShaderItem_aNullValueClearsTheWallpaperImage()
    {
        QImage backdrop(4, 4, QImage::Format_RGBA8888);
        backdrop.fill(Qt::blue);

        SurfaceShaderItem item;
        item.setProperty("wallpaperTexture", QVariant::fromValue(backdrop));
        QVERIFY(!item.wallpaperTexture().isNull());

        item.setProperty("wallpaperTexture", QVariant());
        QVERIFY(item.wallpaperTexture().isNull());
    }

    // ═══════════════════════════════════════════════════════════════════════
    // The org.phosphor.surface module
    // ═══════════════════════════════════════════════════════════════════════

    /// The chain host instantiates through the module. "X is not a type" at
    /// app runtime hides the nested cause (a bad property in the file, a
    /// missing dependent import); pinning creation here fails with the real
    /// error instead. An undecorated instance is the interesting case: every
    /// host starts here and only then writes a chain.
    void moduleLoadsSurfaceDecoration()
    {
        QQmlComponent comp(&m_engine);
        comp.setData(QByteArrayLiteral("import QtQuick\nimport org.phosphor.surface\n"
                                       "SurfaceDecoration { decorationChain: []; decorationOuterPadding: 0 }\n"),
                     QUrl(QStringLiteral("inline://SurfaceDecoration.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(comp.status() != QQmlComponent::Loading, 5000);
        if (comp.status() != QQmlComponent::Ready) {
            qWarning() << "SurfaceDecoration status:" << comp.status() << "errors:" << comp.errorString();
        }
        QVERIFY2(!comp.isError(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> obj(comp.create());
        if (!obj) {
            qWarning() << "SurfaceDecoration creation errors:" << comp.errorString();
        }
        QVERIFY(obj != nullptr);
    }

    /// SurfaceDecoration's readiness gate compares a stage against
    /// `SurfaceShaderItem.Ready` and `SurfaceShaderItem.Error`, enums it
    /// reaches through the registered type rather than declaring itself.
    ///
    /// Worth pinning because the failure is silent and inverted: an
    /// unresolvable enum yields `undefined`, every `status === undefined`
    /// comparison is false, so `chainReady` never goes true and a host that
    /// covers its preview until then covers it forever.
    void surfaceShaderItemStatusEnumResolves()
    {
        QQmlComponent comp(&m_engine);
        comp.setData(QByteArrayLiteral("import QtQuick\nimport org.phosphor.surface\n"
                                       "Item {\n"
                                       "    property int ready: SurfaceShaderItem.Ready\n"
                                       "    property int loading: SurfaceShaderItem.Loading\n"
                                       "    property int failed: SurfaceShaderItem.Error\n"
                                       "}\n"),
                     QUrl(QStringLiteral("inline://surfacestatus.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(comp.status() != QQmlComponent::Loading, 5000);
        if (comp.status() != QQmlComponent::Ready) {
            qWarning() << "status enum:" << comp.status() << "errors:" << comp.errorString();
        }
        QVERIFY(!comp.isError());
        std::unique_ptr<QObject> obj(comp.create());
        QVERIFY(obj != nullptr);
        // Distinct and non-negative: proves they resolved to the real
        // enumerators rather than both collapsing to a default-constructed int.
        QVERIFY(obj->property("ready").toInt() >= 0);
        QVERIFY(obj->property("ready").toInt() != obj->property("loading").toInt());
        // Error too: it is what settles a chain whose shader will not compile,
        // so an unresolvable one leaves that chain permanently unsettled, the
        // same silent inversion reached by a different route.
        QVERIFY(obj->property("failed").toInt() >= 0);
        QVERIFY(obj->property("failed").toInt() != obj->property("ready").toInt());
        QVERIFY(obj->property("failed").toInt() != obj->property("loading").toInt());
    }
};

QTEST_MAIN(TestSurfaceShaderItem)
#include "test_surfaceshaderitem.moc"
