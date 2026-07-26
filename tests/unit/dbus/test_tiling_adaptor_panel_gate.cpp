// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_tiling_adaptor_panel_gate.cpp
 *
 * NOTE: the adaptor is parented to a plain QObject rather than a
 * D-Bus-registered object.
 * QDBusAbstractAdaptor walks its parent's meta-object at construction and some
 * code paths in Qt6DBus assume the parent is a D-Bus-registered object; using
 * a vanilla QObject parent sidesteps that entirely.
 */

#include <QTest>
#include <QCoreApplication>
#include <QObject>

#include <PhosphorProtocol/WindowMarshalling.h>

#include <PhosphorTileEngine/AutotileEngine.h>
#include "helpers/AutotileTestHelpers.h"
#include <PhosphorScreens/Manager.h>
#include "dbus/tilingadaptor/tilingadaptor.h"

using namespace PlasmaZones;
using namespace PhosphorTileEngine;

namespace {
/// Fire PhosphorScreens::ScreenManager::panelGeometryReady directly on the instance. The signal
/// is only emitted from within PhosphorScreens::ScreenManager's D-Bus panel callback in
/// production; for unit tests we need a way to simulate that moment without
/// running a real Plasma shell.
void emitPanelGeometryReady(PhosphorScreens::ScreenManager& mgr)
{
    QMetaObject::invokeMethod(&mgr, "panelGeometryReady");
}
} // namespace

class TestTilingAdaptorPanelGate : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // -------------------------------------------------------------------------
    // Baseline: with no ScreenManager injected, windowOpened forwards straight
    // to the engine without queueing. The adaptor must not force a dependency
    // on ScreenManager — headless unit tests inject nullptr.
    // -------------------------------------------------------------------------
    void testNoScreenManager_passThrough()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        adaptor.setLifecycleEngines({&engine});

        adaptor.windowOpened(QStringLiteral("kitty|uuid-1"), QStringLiteral("HDMI-1"), 0, 0);
        QCOMPARE(adaptor.pendingWindowOpensCount(), 0);
    }

    // -------------------------------------------------------------------------
    // Primary scenario: panel geometry NOT ready at daemon startup, windowOpened
    // entries are deferred. After panelGeometryReady fires, the queue drains.
    // -------------------------------------------------------------------------
    void testDefersWhenPanelNotReady_flushesOnSignal()
    {
        PhosphorScreens::ScreenManager mgr;
        QVERIFY(!mgr.isPanelGeometryReady());

        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QObject adaptorParent;
        TilingAdaptor adaptor(&mgr, &adaptorParent);
        adaptor.setLifecycleEngines({&engine});

        // Single-open path: queues.
        adaptor.windowOpened(QStringLiteral("konsole|uuid-a"), QStringLiteral("HDMI-1"), 100, 50);
        QCOMPARE(adaptor.pendingWindowOpensCount(), 1);

        // Batch-open path: queues atomically on top of the existing entry,
        // preserving order so flush replays them as if the batch had arrived
        // after panel ready.
        PhosphorProtocol::WindowOpenedList batch;
        batch.append(
            PhosphorProtocol::WindowOpenedEntry{QStringLiteral("firefox|uuid-b"), QStringLiteral("HDMI-1"), 800, 600});
        batch.append(
            PhosphorProtocol::WindowOpenedEntry{QStringLiteral("vesktop|uuid-c"), QStringLiteral("HDMI-1"), 940, 500});
        adaptor.windowsOpenedBatch(batch);
        QCOMPARE(adaptor.pendingWindowOpensCount(), 3);

        // Fire panelGeometryReady. The adaptor's auto-connection fires the flush
        // slot on the same thread, synchronously.
        emitPanelGeometryReady(mgr);

        QCOMPARE(adaptor.pendingWindowOpensCount(), 0);
    }

    // -------------------------------------------------------------------------
    // Argument validation: single windowOpened with empty windowId or empty
    // screenId must not end up in the pending queue. Flush should also skip
    // any garbage that did get in (though the public API shouldn't enqueue it).
    // -------------------------------------------------------------------------
    void testRejectsInvalidSingleOpens()
    {
        PhosphorScreens::ScreenManager mgr;

        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QObject adaptorParent;
        TilingAdaptor adaptor(&mgr, &adaptorParent);
        adaptor.setLifecycleEngines({&engine});

        adaptor.windowOpened(QString(), QStringLiteral("HDMI-1"), 0, 0);
        QCOMPARE(adaptor.pendingWindowOpensCount(), 0);

        adaptor.windowOpened(QStringLiteral("valid|uuid"), QString(), 0, 0);
        QCOMPARE(adaptor.pendingWindowOpensCount(), 0);

        adaptor.windowOpened(QStringLiteral("ok|uuid"), QStringLiteral("HDMI-1"), 0, 0);
        QCOMPARE(adaptor.pendingWindowOpensCount(), 1);

        emitPanelGeometryReady(mgr);
        QCOMPARE(adaptor.pendingWindowOpensCount(), 0);
    }

    // -------------------------------------------------------------------------
    // Batch path queues atomically and the flush drains it. Only queue
    // COUNTS are observable here (the null-dep engine drops the dispatches),
    // so this deliberately does NOT claim order coverage — arrival order
    // through the engine is pinned by the engine-side order tests.
    // -------------------------------------------------------------------------
    void testBatchQueuesAndDrains()
    {
        PhosphorScreens::ScreenManager mgr;

        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QObject adaptorParent;
        TilingAdaptor adaptor(&mgr, &adaptorParent);
        adaptor.setLifecycleEngines({&engine});

        PhosphorProtocol::WindowOpenedList batch;
        for (int i = 0; i < 5; ++i) {
            batch.append(PhosphorProtocol::WindowOpenedEntry{QStringLiteral("app|uuid-%1").arg(i),
                                                             QStringLiteral("HDMI-1"), 0, 0});
        }
        adaptor.windowsOpenedBatch(batch);
        QCOMPARE(adaptor.pendingWindowOpensCount(), 5);

        emitPanelGeometryReady(mgr);
        QCOMPARE(adaptor.pendingWindowOpensCount(), 0);
        // The contract pinned here is only that the flush drained the queue.
    }

    // -------------------------------------------------------------------------
    // Empty-pipeline safety: Daemon::stop() calls clearEngine(), which
    // empties the lifecycle-engine list. A flush firing afterward must hit
    // ensurePipeline()'s empty check (dropping the queue) instead of
    // dereferencing anything — this test calls clearEngine() directly and
    // pins exactly that.
    // -------------------------------------------------------------------------
    void testFlushWithClearedEngine_noCrash()
    {
        PhosphorScreens::ScreenManager mgr;

        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QObject adaptorParent;
        TilingAdaptor adaptor(&mgr, &adaptorParent);
        adaptor.setLifecycleEngines({&engine});

        adaptor.windowOpened(QStringLiteral("a|1"), QStringLiteral("HDMI-1"), 0, 0);
        QCOMPARE(adaptor.pendingWindowOpensCount(), 1);

        adaptor.clearEngine();

        // Should not crash — ensurePipeline() returns false, queue is cleared.
        emitPanelGeometryReady(mgr);
        QCOMPARE(adaptor.pendingWindowOpensCount(), 0);
    }
};

QTEST_MAIN(TestTilingAdaptorPanelGate)
#include "test_tiling_adaptor_panel_gate.moc"
