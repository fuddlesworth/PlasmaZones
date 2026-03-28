// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>
#include <QGuiApplication>
#include <QPointer>
#include <QWindow>

#include "core/layersurface.h"

using namespace PlasmaZones;

class TestLayerSurface : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ═══════════════════════════════════════════════════════════════════════════
    // LayerSurface::get — creation and retrieval
    // ═══════════════════════════════════════════════════════════════════════════

    void testGet_createsForNewWindow()
    {
        QWindow window;
        auto* surface = LayerSurface::get(&window);
        QVERIFY(surface != nullptr);
        QCOMPARE(surface->parent(), &window);
    }

    void testGet_returnsSameForSameWindow()
    {
        QWindow window;
        auto* first = LayerSurface::get(&window);
        auto* second = LayerSurface::get(&window);
        QCOMPARE(first, second);
    }

    void testGet_nullWindow_returnsNull()
    {
        QCOMPARE(LayerSurface::get(nullptr), nullptr);
    }

    void testGet_afterShow_returnsNull()
    {
        // A window that has never had get() called, then is shown
        QWindow window;
        window.show();
        auto* surface = LayerSurface::get(&window);
        // get() on a visible window with no prior LayerSurface returns nullptr
        QCOMPARE(surface, nullptr);
        window.close();
    }

    void testGet_afterShow_returnsExistingIfCreatedBefore()
    {
        QWindow window;
        auto* surface = LayerSurface::get(&window);
        QVERIFY(surface != nullptr);
        window.show();
        // get() on a visible window WITH a prior LayerSurface returns it
        auto* again = LayerSurface::get(&window);
        QCOMPARE(again, surface);
        window.close();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Default values
    // ═══════════════════════════════════════════════════════════════════════════

    void testDefaults()
    {
        QWindow window;
        auto* surface = LayerSurface::get(&window);
        QCOMPARE(surface->layer(), LayerSurface::LayerTop);
        QCOMPARE(surface->anchors(), LayerSurface::Anchors());
        QCOMPARE(surface->exclusiveZone(), int32_t(-1));
        QCOMPARE(surface->keyboardInteractivity(), LayerSurface::KeyboardInteractivityNone);
        QCOMPARE(surface->scope(), QStringLiteral("plasmazones"));
        QCOMPARE(surface->screen(), nullptr);
        QCOMPARE(surface->margins(), QMargins());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Setters — change guard (only emit when value actually changes)
    // ═══════════════════════════════════════════════════════════════════════════

    void testSetLayer_emitsOnChange()
    {
        QWindow window;
        auto* surface = LayerSurface::get(&window);
        QSignalSpy spy(surface, &LayerSurface::layerChanged);

        surface->setLayer(LayerSurface::LayerOverlay);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(surface->layer(), LayerSurface::LayerOverlay);

        // Same value — no emission
        surface->setLayer(LayerSurface::LayerOverlay);
        QCOMPARE(spy.count(), 1);
    }

    void testSetAnchors_emitsOnChange()
    {
        QWindow window;
        auto* surface = LayerSurface::get(&window);
        QSignalSpy spy(surface, &LayerSurface::anchorsChanged);

        surface->setAnchors(LayerSurface::AnchorAll);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(surface->anchors(), LayerSurface::AnchorAll);

        surface->setAnchors(LayerSurface::AnchorAll);
        QCOMPARE(spy.count(), 1);
    }

    void testSetExclusiveZone_emitsOnChange()
    {
        QWindow window;
        auto* surface = LayerSurface::get(&window);
        QSignalSpy spy(surface, &LayerSurface::exclusiveZoneChanged);

        surface->setExclusiveZone(0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(surface->exclusiveZone(), int32_t(0));

        surface->setExclusiveZone(0);
        QCOMPARE(spy.count(), 1);
    }

    void testSetKeyboardInteractivity_emitsOnChange()
    {
        QWindow window;
        auto* surface = LayerSurface::get(&window);
        QSignalSpy spy(surface, &LayerSurface::keyboardInteractivityChanged);

        surface->setKeyboardInteractivity(LayerSurface::KeyboardInteractivityExclusive);
        QCOMPARE(spy.count(), 1);

        surface->setKeyboardInteractivity(LayerSurface::KeyboardInteractivityExclusive);
        QCOMPARE(spy.count(), 1);
    }

    void testSetScope_emitsOnChange()
    {
        QWindow window;
        auto* surface = LayerSurface::get(&window);
        QSignalSpy spy(surface, &LayerSurface::scopeChanged);

        surface->setScope(QStringLiteral("test-scope"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(surface->scope(), QStringLiteral("test-scope"));

        surface->setScope(QStringLiteral("test-scope"));
        QCOMPARE(spy.count(), 1);
    }

    void testSetMargins_emitsOnChange()
    {
        QWindow window;
        auto* surface = LayerSurface::get(&window);
        QSignalSpy spy(surface, &LayerSurface::marginsChanged);

        const QMargins m(10, 20, 30, 40);
        surface->setMargins(m);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(surface->margins(), m);

        surface->setMargins(m);
        QCOMPARE(spy.count(), 1);
    }

    void testSetScreen_warnsAfterShow()
    {
        QWindow window;
        auto* surface = LayerSurface::get(&window);
        QSignalSpy spy(surface, &LayerSurface::screenChanged);

        window.show();
        surface->setScreen(QGuiApplication::primaryScreen());
        // setScreen after show returns early without emitting
        QCOMPARE(spy.count(), 0);
        window.close();
    }

    void testSetScope_warnsAfterShow()
    {
        QWindow window;
        auto* surface = LayerSurface::get(&window);
        QSignalSpy spy(surface, &LayerSurface::scopeChanged);

        window.show();
        surface->setScope(QStringLiteral("new-scope"));
        // setScope after show returns early without emitting (scope is immutable)
        QCOMPARE(spy.count(), 0);
        QCOMPARE(surface->scope(), QStringLiteral("plasmazones")); // original default unchanged
        window.close();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // propertiesChanged — emitted after each setter
    // ═══════════════════════════════════════════════════════════════════════════

    void testPropertiesChanged_emittedPerSetter()
    {
        QWindow window;
        auto* surface = LayerSurface::get(&window);
        QSignalSpy spy(surface, &LayerSurface::propertiesChanged);

        surface->setLayer(LayerSurface::LayerOverlay);
        QCOMPARE(spy.count(), 1);
        surface->setAnchors(LayerSurface::AnchorTop);
        QCOMPARE(spy.count(), 2);
        surface->setExclusiveZone(0);
        QCOMPARE(spy.count(), 3);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // BatchGuard — coalesces propertiesChanged emissions
    // ═══════════════════════════════════════════════════════════════════════════

    void testBatchGuard_coalesces()
    {
        QWindow window;
        auto* surface = LayerSurface::get(&window);
        QSignalSpy spy(surface, &LayerSurface::propertiesChanged);

        {
            LayerSurface::BatchGuard guard(surface);
            surface->setLayer(LayerSurface::LayerOverlay);
            surface->setAnchors(LayerSurface::AnchorAll);
            surface->setExclusiveZone(0);
            surface->setScope(QStringLiteral("batched"));
            // No emission yet while guard is alive
            QCOMPARE(spy.count(), 0);
        }
        // Single emission after guard destroyed
        QCOMPARE(spy.count(), 1);
    }

    void testBatchGuard_noEmitIfNothingChanged()
    {
        QWindow window;
        auto* surface = LayerSurface::get(&window);
        QSignalSpy spy(surface, &LayerSurface::propertiesChanged);

        {
            LayerSurface::BatchGuard guard(surface);
            // Set same values — no change, no dirty flag
            surface->setLayer(LayerSurface::LayerTop); // default
            surface->setExclusiveZone(-1); // default
        }
        QCOMPARE(spy.count(), 0);
    }

    void testBatchGuard_nested()
    {
        QWindow window;
        auto* surface = LayerSurface::get(&window);
        QSignalSpy spy(surface, &LayerSurface::propertiesChanged);

        {
            LayerSurface::BatchGuard outer(surface);
            surface->setLayer(LayerSurface::LayerOverlay);
            {
                LayerSurface::BatchGuard inner(surface);
                surface->setAnchors(LayerSurface::AnchorAll);
            }
            // Inner guard destroyed, but outer still alive — no emission
            QCOMPARE(spy.count(), 0);
        }
        // Outer destroyed — single emission
        QCOMPARE(spy.count(), 1);
    }

    void testBatchGuard_nullSurface()
    {
        // Must not crash with nullptr
        LayerSurface::BatchGuard guard(nullptr);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // AnchorAll constant
    // ═══════════════════════════════════════════════════════════════════════════

    void testAnchorAll()
    {
        QVERIFY(LayerSurface::AnchorAll.testFlag(LayerSurface::AnchorTop));
        QVERIFY(LayerSurface::AnchorAll.testFlag(LayerSurface::AnchorBottom));
        QVERIFY(LayerSurface::AnchorAll.testFlag(LayerSurface::AnchorLeft));
        QVERIFY(LayerSurface::AnchorAll.testFlag(LayerSurface::AnchorRight));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // QWindow property propagation
    // ═══════════════════════════════════════════════════════════════════════════

    void testPropertiesPropagateToWindow()
    {
        QWindow window;
        auto* surface = LayerSurface::get(&window);

        surface->setLayer(LayerSurface::LayerOverlay);
        QCOMPARE(window.property(LayerSurfaceProps::Layer).toInt(), 3);

        surface->setAnchors(LayerSurface::AnchorTop | LayerSurface::AnchorBottom);
        QCOMPARE(window.property(LayerSurfaceProps::Anchors).toInt(), 3); // 1|2

        surface->setExclusiveZone(42);
        QCOMPARE(window.property(LayerSurfaceProps::ExclusiveZone).toInt(), 42);

        surface->setKeyboardInteractivity(LayerSurface::KeyboardInteractivityOnDemand);
        QCOMPARE(window.property(LayerSurfaceProps::Keyboard).toInt(), 2);

        surface->setScope(QStringLiteral("test"));
        QCOMPARE(window.property(LayerSurfaceProps::Scope).toString(), QStringLiteral("test"));

        surface->setMargins(QMargins(1, 2, 3, 4));
        QCOMPARE(window.property(LayerSurfaceProps::MarginsLeft).toInt(), 1);
        QCOMPARE(window.property(LayerSurfaceProps::MarginsTop).toInt(), 2);
        QCOMPARE(window.property(LayerSurfaceProps::MarginsRight).toInt(), 3);
        QCOMPARE(window.property(LayerSurfaceProps::MarginsBottom).toInt(), 4);
    }

    void testIsLayerShellProperty()
    {
        QWindow window;
        QVERIFY(!window.property(LayerSurfaceProps::IsLayerShell).toBool());

        LayerSurface::get(&window);
        QVERIFY(window.property(LayerSurfaceProps::IsLayerShell).toBool());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // computeLayerSize — pure static helper
    // ═══════════════════════════════════════════════════════════════════════════

    void testComputeLayerSize_allAnchors_zeroSize()
    {
        // Anchored to all edges: compositor controls size, we send 0x0
        auto [w, h] = LayerSurface::computeLayerSize(LayerSurface::AnchorTop | LayerSurface::AnchorBottom
                                                         | LayerSurface::AnchorLeft | LayerSurface::AnchorRight,
                                                     QSize(800, 600));
        QCOMPARE(w, uint32_t(0));
        QCOMPARE(h, uint32_t(0));
    }

    void testComputeLayerSize_horizontalAnchors_zeroWidth()
    {
        // Anchored left+right: width is compositor-controlled, height is explicit
        auto [w, h] =
            LayerSurface::computeLayerSize(LayerSurface::AnchorLeft | LayerSurface::AnchorRight, QSize(800, 100));
        QCOMPARE(w, uint32_t(0));
        QCOMPARE(h, uint32_t(100));
    }

    void testComputeLayerSize_verticalAnchors_zeroHeight()
    {
        // Anchored top+bottom: height is compositor-controlled, width is explicit
        auto [w, h] =
            LayerSurface::computeLayerSize(LayerSurface::AnchorTop | LayerSurface::AnchorBottom, QSize(300, 600));
        QCOMPARE(w, uint32_t(300));
        QCOMPARE(h, uint32_t(0));
    }

    void testComputeLayerSize_noAnchors_explicitSize()
    {
        // No anchors: full explicit size
        auto [w, h] = LayerSurface::computeLayerSize(0, QSize(400, 200));
        QCOMPARE(w, uint32_t(400));
        QCOMPARE(h, uint32_t(200));
    }

    void testComputeLayerSize_singleAnchor_explicitSize()
    {
        // Single anchor (e.g. top-left): explicit size
        auto [w, h] =
            LayerSurface::computeLayerSize(LayerSurface::AnchorTop | LayerSurface::AnchorLeft, QSize(250, 150));
        QCOMPARE(w, uint32_t(250));
        QCOMPARE(h, uint32_t(150));
    }

    void testComputeLayerSize_negativeSize_clampedToZero()
    {
        // Negative dimensions are clamped to 0 before uint32_t cast
        auto [w, h] = LayerSurface::computeLayerSize(0, QSize(-10, -5));
        QCOMPARE(w, uint32_t(0));
        QCOMPARE(h, uint32_t(0));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Cleanup: LayerSurface removed from registry when window destroyed
    // ═══════════════════════════════════════════════════════════════════════════

    void testDestroyedWithWindow()
    {
        QPointer<LayerSurface> weak;
        {
            QWindow window;
            auto* surface = LayerSurface::get(&window);
            QVERIFY(surface != nullptr);
            weak = surface;
            QVERIFY(!weak.isNull());
        }
        // QWindow destroyed — child LayerSurface destroyed with it
        QVERIFY(weak.isNull());
    }
};

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    TestLayerSurface tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_layersurface.moc"
