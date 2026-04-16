// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/layersurface.h"

// Full definition needed for tests that directly construct LayerShellIntegration.
// Include the PhosphorShell private header (full definition) then the
// PlasmaZones compatibility wrapper (using alias into PlasmaZones namespace).
#include "../../../phosphorshell/src/qpa/layershellintegration.h"
#include "core/qpa/layershellintegration.h"

#include <QTest>
#include <QSignalSpy>
#include <QGuiApplication>
#include <QPointer>
#include <QWindow>

using namespace PlasmaZones;

class TestLayerSurface : public QObject
{
    Q_OBJECT

private:
    /// RAII guard that deletes the LayerSurface before the window goes out of
    /// scope, ensuring the internal registry is cleaned while QPointer is alive.
    struct SurfaceGuard
    {
        QWindow& window;
        explicit SurfaceGuard(QWindow& w)
            : window(w)
        {
        }
        ~SurfaceGuard()
        {
            auto* surface = window.findChild<LayerSurface*>();
            delete surface;
        }
        SurfaceGuard(const SurfaceGuard&) = delete;
        SurfaceGuard& operator=(const SurfaceGuard&) = delete;
    };

private Q_SLOTS:

    // ═══════════════════════════════════════════════════════════════════════════
    // LayerSurface::get — creation and retrieval
    // ═══════════════════════════════════════════════════════════════════════════

    void testGet_createsForNewWindow()
    {
        QWindow window;
        SurfaceGuard guard(window);
        auto* surface = LayerSurface::get(&window);
        QVERIFY(surface != nullptr);
        QCOMPARE(surface->parent(), &window);
    }

    void testGet_returnsSameForSameWindow()
    {
        QWindow window;
        SurfaceGuard guard(window);
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
        // A window that has never had get() called, then is shown.
        // Heap-allocate to avoid stack-address reuse collisions with the
        // LayerSurface registry from earlier tests.
        auto* window = new QWindow;
        window->show();
        if (!window->isVisible()) {
            delete window;
            QSKIP("Platform does not make windows visible on show()");
        }
        auto* surface = LayerSurface::get(window);
        // get() on a visible window with no prior LayerSurface returns nullptr
        QCOMPARE(surface, nullptr);
        window->close();
        delete window;
    }

    void testGet_afterShow_returnsExistingIfCreatedBefore()
    {
        QWindow window;
        SurfaceGuard guard(window);
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
        SurfaceGuard guard(window);
        auto* surface = LayerSurface::get(&window);
        QCOMPARE(surface->layer(), LayerSurface::LayerTop);
        QCOMPARE(surface->anchors(), LayerSurface::Anchors());
        QCOMPARE(surface->exclusiveZone(), int32_t(-1));
        QCOMPARE(surface->keyboardInteractivity(), LayerSurface::KeyboardInteractivityNone);
        QCOMPARE(surface->scope(), QStringLiteral("phosphorshell"));
        QCOMPARE(surface->screen(), nullptr);
        QCOMPARE(surface->margins(), QMargins());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Setters — change guard (only emit when value actually changes)
    // ═══════════════════════════════════════════════════════════════════════════

    void testSetLayer_emitsOnChange()
    {
        QWindow window;
        SurfaceGuard guard(window);
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
        SurfaceGuard guard(window);
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
        SurfaceGuard guard(window);
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
        SurfaceGuard guard(window);
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
        SurfaceGuard guard(window);
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
        SurfaceGuard guard(window);
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
        SurfaceGuard guard(window);
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
        SurfaceGuard guard(window);
        auto* surface = LayerSurface::get(&window);
        QSignalSpy spy(surface, &LayerSurface::scopeChanged);

        window.show();
        surface->setScope(QStringLiteral("new-scope"));
        // setScope after show returns early without emitting (scope is immutable)
        QCOMPARE(spy.count(), 0);
        QCOMPARE(surface->scope(), QStringLiteral("phosphorshell")); // original default unchanged
        window.close();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // propertiesChanged — emitted after each setter
    // ═══════════════════════════════════════════════════════════════════════════

    void testPropertiesChanged_emittedPerSetter()
    {
        QWindow window;
        SurfaceGuard guard(window);
        auto* surface = LayerSurface::get(&window);
        QSignalSpy spy(surface, &LayerSurface::propertiesChanged);

        surface->setLayer(LayerSurface::LayerOverlay);
        QCOMPARE(spy.count(), 1);
        surface->setAnchors(LayerSurface::AnchorTop);
        QCOMPARE(spy.count(), 2);
        surface->setExclusiveZone(0);
        QCOMPARE(spy.count(), 3);
    }

    void testPropertiesChanged_notEmittedForImmutableProps()
    {
        // Scope and screen are baked into zwlr_layer_shell_v1_get_layer_surface
        // at creation time — they must NOT emit propertiesChanged because the
        // QPA plugin cannot push these to the compositor after surface creation.
        QWindow window;
        SurfaceGuard guard(window);
        auto* surface = LayerSurface::get(&window);
        QSignalSpy spy(surface, &LayerSurface::propertiesChanged);

        surface->setScope(QStringLiteral("immutable-test"));
        QCOMPARE(spy.count(), 0);

        surface->setScreen(QGuiApplication::primaryScreen());
        QCOMPARE(spy.count(), 0);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // BatchGuard — coalesces propertiesChanged emissions
    // ═══════════════════════════════════════════════════════════════════════════

    void testBatchGuard_coalesces()
    {
        QWindow window;
        SurfaceGuard surfaceGuard(window);
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
        SurfaceGuard surfaceGuard(window);
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
        SurfaceGuard surfaceGuard(window);
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

    void testBatchGuard_moveSemantics()
    {
        QWindow window;
        SurfaceGuard surfaceGuard(window);
        auto* surface = LayerSurface::get(&window);
        QSignalSpy spy(surface, &LayerSurface::propertiesChanged);

        {
            LayerSurface::BatchGuard outer(surface);
            surface->setLayer(LayerSurface::LayerOverlay);

            // Move the guard — source should be disarmed
            LayerSurface::BatchGuard moved(std::move(outer));
            // outer destructs here (disarmed, no decrement)
        }
        // moved destructs here — single emission
        QCOMPARE(spy.count(), 1);
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

    void testDefaultsPropagateToWindow()
    {
        // Default values must be set on the QWindow at construction time so the
        // QPA plugin reads correct initial state. Without this, setters called with
        // the default value (e.g. setLayer(LayerTop) when m_layer is already LayerTop)
        // hit the change-guard early return and never set the QWindow property.
        QWindow window;
        SurfaceGuard guard(window);
        LayerSurface::get(&window);

        QCOMPARE(window.property(LayerSurfaceProps::Layer).toInt(), 2); // LayerTop
        QCOMPARE(window.property(LayerSurfaceProps::Anchors).toInt(), 0); // AnchorNone
        QCOMPARE(window.property(LayerSurfaceProps::ExclusiveZone).toInt(), -1);
        QCOMPARE(window.property(LayerSurfaceProps::Keyboard).toInt(), 0); // None
        QCOMPARE(window.property(LayerSurfaceProps::Scope).toString(), QStringLiteral("phosphorshell"));
        QCOMPARE(window.property(LayerSurfaceProps::MarginsLeft).toInt(), 0);
        QCOMPARE(window.property(LayerSurfaceProps::MarginsTop).toInt(), 0);
        QCOMPARE(window.property(LayerSurfaceProps::MarginsRight).toInt(), 0);
        QCOMPARE(window.property(LayerSurfaceProps::MarginsBottom).toInt(), 0);
    }

    void testPropertiesPropagateToWindow()
    {
        QWindow window;
        SurfaceGuard guard(window);
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
        SurfaceGuard guard(window);
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
        auto [w, h] = LayerSurface::computeLayerSize(LayerSurface::Anchors(), QSize(400, 200));
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
        auto [w, h] = LayerSurface::computeLayerSize(LayerSurface::Anchors(), QSize(-10, -5));
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
            SurfaceGuard guard(window);
            auto* surface = LayerSurface::get(&window);
            QVERIFY(surface != nullptr);
            weak = surface;
            QVERIFY(!weak.isNull());
        }
        // QWindow destroyed — child LayerSurface already deleted by SurfaceGuard
        QVERIFY(weak.isNull());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // setScreen — success path before show()
    // ═══════════════════════════════════════════════════════════════════════════

    void testSetScreen_beforeShow_emitsAndUpdates()
    {
        QScreen* primary = QGuiApplication::primaryScreen();
        if (!primary) {
            QSKIP("No primary screen available on this platform");
        }

        QWindow window;
        SurfaceGuard guard(window);
        auto* surface = LayerSurface::get(&window);
        QSignalSpy spy(surface, &LayerSurface::screenChanged);

        surface->setScreen(primary);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(surface->screen(), primary);

        // Same value — no emission
        surface->setScreen(primary);
        QCOMPARE(spy.count(), 1);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Registry cleanup after window address reuse
    // ═══════════════════════════════════════════════════════════════════════════

    void testGet_afterWindowDestroy_returnsFreshSurface()
    {
        QPointer<LayerSurface> weakFirst;
        {
            QWindow window;
            SurfaceGuard guard(window);
            auto* firstSurface = LayerSurface::get(&window);
            QVERIFY(firstSurface != nullptr);
            weakFirst = firstSurface;
            // window destroyed here; registry entry already removed by SurfaceGuard
        }
        QVERIFY(weakFirst.isNull());

        // Create a new window — must get a fresh LayerSurface, not a stale one
        QWindow newWindow;
        SurfaceGuard newGuard(newWindow);
        auto* freshSurface = LayerSurface::get(&newWindow);
        QVERIFY(freshSurface != nullptr);
        QCOMPARE(freshSurface->parent(), &newWindow);
    }
    // ═══════════════════════════════════════════════════════════════════════════
    // LayerShellIntegration — global removed callback lifecycle
    // ═══════════════════════════════════════════════════════════════════════════

    void testGlobalRemovedCallback_firesAndIsCleared()
    {
        // Test the callback registration/deregistration/fire lifecycle
        // without needing a real Wayland compositor. We test the
        // LayerShellIntegration's callback bookkeeping directly.
        LayerShellIntegration integration;

        bool called1 = false;
        bool called2 = false;
        auto id1 = integration.addGlobalRemovedCallback([&called1]() {
            called1 = true;
        });
        Q_UNUSED(integration.addGlobalRemovedCallback([&called2]() {
            called2 = true;
        }))

        // Remove one callback before firing
        integration.removeGlobalRemovedCallback(id1);

        // Simulate global removal by calling the static handler with a fake
        // registry id=0 — since m_layerShellId is 0 (never bound), this triggers
        // the removal path.
        LayerShellIntegration::registryRemoveHandler(&integration, nullptr, 0);

        QVERIFY(!called1); // Was removed before firing
        QVERIFY(called2); // Was still registered

        // After firing, the callback vector should be cleared (std::exchange)
        // Adding a new callback after removal should work but never fire
        // (since the global is already gone — documented in the header)
        bool called3 = false;
        integration.addGlobalRemovedCallback([&called3]() {
            called3 = true;
        });
        // No second removal event, so called3 stays false
        QVERIFY(!called3);
    }

    void testGlobalRemovedCallback_deregisterPreventsUAF()
    {
        LayerShellIntegration integration;

        // Simulate a callback that would UAF if not deregistered
        bool called = false;
        auto id = integration.addGlobalRemovedCallback([&called]() {
            called = true;
        });

        // Deregister (simulates LayerShellWindow destructor)
        integration.removeGlobalRemovedCallback(id);

        // Trigger removal — callback must NOT fire
        LayerShellIntegration::registryRemoveHandler(&integration, nullptr, 0);
        QVERIFY(!called);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // isSupported — offscreen platform returns false
    // ═══════════════════════════════════════════════════════════════════════════

    void testIsSupported_offscreen_returnsFalse()
    {
        QVERIFY(!LayerSurface::isSupported());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Window destruction cascade-deletes surface
    // ═══════════════════════════════════════════════════════════════════════════

    void testWindowDestruction_cascadeDeletesSurface()
    {
        auto* window = new QWindow;
        auto* surface = LayerSurface::get(window);
        QVERIFY(surface);
        QPointer<LayerSurface> weak = surface;
        delete window;
        QVERIFY(weak.isNull());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // setScreen(nullptr) falls back to primary
    // ═══════════════════════════════════════════════════════════════════════════

    void testSetScreen_null_fallsToPrimary()
    {
        QWindow window;
        SurfaceGuard guard(window);
        auto* surface = LayerSurface::get(&window);
        QVERIFY(surface);
        auto* primary = QGuiApplication::primaryScreen();
        if (!primary) {
            QSKIP("No primary screen available");
        }
        surface->setScreen(primary);
        QCOMPARE(surface->screen(), primary);
        surface->setScreen(nullptr);
        // After setting nullptr, should fall back to primary
        QCOMPARE(surface->screen(), primary);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // find() — read-only lookup (never creates)
    // ═══════════════════════════════════════════════════════════════════════════

    void testFind_null_returnsNull()
    {
        QCOMPARE(LayerSurface::find(nullptr), nullptr);
    }

    void testFind_noSurface_returnsNull()
    {
        QWindow window;
        QCOMPARE(LayerSurface::find(&window), nullptr);
    }

    void testFind_existingSurface_returnsIt()
    {
        QWindow window;
        SurfaceGuard guard(window);
        auto* surface = LayerSurface::get(&window);
        QVERIFY(surface != nullptr);
        QCOMPARE(LayerSurface::find(&window), surface);
    }

    void testFind_afterDelete_returnsNull()
    {
        auto* window = new QWindow;
        auto* surface = LayerSurface::get(window);
        QVERIFY(surface != nullptr);
        QCOMPARE(LayerSurface::find(window), surface);
        // Explicitly delete the surface (removes from registry)
        delete surface;
        QCOMPARE(LayerSurface::find(window), nullptr);
        delete window;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Input validation — out-of-range enum values
    // ═══════════════════════════════════════════════════════════════════════════

    void testSetLayer_outOfRange_ignored()
    {
        QWindow window;
        SurfaceGuard guard(window);
        auto* surface = LayerSurface::get(&window);
        QSignalSpy spy(surface, &LayerSurface::layerChanged);

        const auto originalLayer = surface->layer();
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("setLayer\\(\\) called with out-of-range value")));
        surface->setLayer(static_cast<LayerSurface::Layer>(99));
        QCOMPARE(spy.count(), 0);
        QCOMPARE(surface->layer(), originalLayer);
    }

    void testSetKeyboardInteractivity_outOfRange_ignored()
    {
        QWindow window;
        SurfaceGuard guard(window);
        auto* surface = LayerSurface::get(&window);
        QSignalSpy spy(surface, &LayerSurface::keyboardInteractivityChanged);

        const auto original = surface->keyboardInteractivity();
        QTest::ignoreMessage(
            QtWarningMsg,
            QRegularExpression(QStringLiteral("setKeyboardInteractivity\\(\\) called with out-of-range value")));
        surface->setKeyboardInteractivity(static_cast<LayerSurface::KeyboardInteractivity>(5));
        QCOMPARE(spy.count(), 0);
        QCOMPARE(surface->keyboardInteractivity(), original);
    }

    void testSetAnchors_undefinedBits_masked()
    {
        QWindow window;
        SurfaceGuard guard(window);
        auto* surface = LayerSurface::get(&window);
        QSignalSpy spy(surface, &LayerSurface::anchorsChanged);

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("setAnchors\\(\\) called with undefined bits set")));
        surface->setAnchors(LayerSurface::Anchors(0xFF));
        // 0xFF masked to 0x0F = AnchorAll
        QCOMPARE(spy.count(), 1);
        QCOMPARE(surface->anchors(), LayerSurface::AnchorAll);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // get() — refuses to recreate after explicit delete
    // ═══════════════════════════════════════════════════════════════════════════

    void testGet_afterExplicitDelete_returnsNull()
    {
        auto* window = new QWindow;
        auto* surface = LayerSurface::get(window);
        QVERIFY(surface != nullptr);

        // Explicitly delete the LayerSurface — removes from registry but the
        // QWindow still carries the _pz_layer_shell property.
        delete surface;

        // get() should detect the stale marker property and refuse to create.
        QTest::ignoreMessage(
            QtWarningMsg,
            QRegularExpression(QStringLiteral("LayerSurface::get\\(\\) called on a window that already had a")));
        auto* again = LayerSurface::get(window);
        QCOMPARE(again, nullptr);

        delete window;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // setScreen(nullptr) — no emission when already on primary
    // ═══════════════════════════════════════════════════════════════════════════

    void testSetScreen_null_noEmitWhenAlreadyPrimary()
    {
        QScreen* primary = QGuiApplication::primaryScreen();
        if (!primary) {
            QSKIP("No primary screen available on this platform");
        }

        QWindow window;
        SurfaceGuard guard(window);
        auto* surface = LayerSurface::get(&window);

        // Set to primary first
        surface->setScreen(primary);
        QCOMPARE(surface->screen(), primary);

        QSignalSpy spy(surface, &LayerSurface::screenChanged);
        // nullptr resolves to primary, which is already set — no change, no emission
        surface->setScreen(nullptr);
        QCOMPARE(spy.count(), 0);
        QCOMPARE(surface->screen(), primary);
    }
};

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    TestLayerSurface tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_layersurface.moc"
