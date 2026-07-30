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
#include <QSignalSpy>
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
        // No screens announce pending, engine claims nothing → the open is
        // DROPPED, never parked (parking is a mid-flip mechanism only).
        QCOMPARE(adaptor.pendingUnclaimedOpensCount(), 0);
    }

    // -------------------------------------------------------------------------
    // m_unclaimedOpens contract: an open landing while a screens announce is
    // pending (mid-flip) parks; the coalesced announce retries it exactly
    // once, in arrival order, into whichever engine claims the screen by
    // then; a still-unclaimed retry drops; windowClosed/clearEngine sweep.
    // -------------------------------------------------------------------------
    void testUnclaimedOpens_parkRetryAndSweep()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        adaptor.setLifecycleEngines({&engine});

        // Arm the coalesced announce (the flip marker), then deliver opens
        // for a screen no engine claims yet: they must park in order.
        adaptor.notifyEngineScreensChanged(false);
        adaptor.windowOpened(QStringLiteral("app|one"), QStringLiteral("HDMI-1"), 0, 0);
        adaptor.windowOpened(QStringLiteral("app|two"), QStringLiteral("HDMI-1"), 0, 0);
        QCOMPARE(adaptor.pendingUnclaimedOpensCount(), 2);

        // A close while parked sweeps that entry.
        adaptor.windowClosed(QStringLiteral("app|two"));
        QCOMPARE(adaptor.pendingUnclaimedOpensCount(), 1);
        adaptor.windowOpened(QStringLiteral("app|two"), QStringLiteral("HDMI-1"), 0, 0);
        QCOMPARE(adaptor.pendingUnclaimedOpensCount(), 2);

        // The flip settles: the engine now claims the screen, and the
        // queued announce retries the parked entries in arrival order.
        engine.setAutotileScreens({QStringLiteral("HDMI-1")});
        QCoreApplication::processEvents();
        QCOMPARE(adaptor.pendingUnclaimedOpensCount(), 0);
        QVERIFY(engine.isWindowTracked(QStringLiteral("app|one")));
        QVERIFY(engine.isWindowTracked(QStringLiteral("app|two")));
        const QStringList order = engine.managedWindowOrder(QStringLiteral("HDMI-1"));
        QCOMPARE(order, (QStringList{QStringLiteral("app|one"), QStringLiteral("app|two")}));
    }

    void testAnnounceCoalescingAndPayload()
    {
        // The coalesced announce folds repeat calls into ONE emission whose
        // isDesktopSwitch flag ORs across the batch, and the payload is the
        // documented SORTED union (wire consumers compare successive
        // payloads).
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QStringLiteral("HDMI-2"), QStringLiteral("HDMI-1")});
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        adaptor.setLifecycleEngines({&engine});

        QSignalSpy spy(&adaptor, &TilingAdaptor::managedScreensChanged);
        adaptor.notifyEngineScreensChanged(false);
        adaptor.notifyEngineScreensChanged(true);
        adaptor.notifyEngineScreensChanged(false);
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toStringList(), (QStringList{QStringLiteral("HDMI-1"), QStringLiteral("HDMI-2")}));
        QCOMPARE(spy.first().at(1).toBool(), true);
    }

    void testUnclaimedOpens_retryWithoutClaimDrops()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        adaptor.setLifecycleEngines({&engine});

        adaptor.notifyEngineScreensChanged(false);
        adaptor.windowOpened(QStringLiteral("app|gone"), QStringLiteral("HDMI-9"), 0, 0);
        QCOMPARE(adaptor.pendingUnclaimedOpensCount(), 1);
        // Announce fires with the screen still unclaimed: exactly one
        // retry, then the entry is dropped — never re-parked.
        QCoreApplication::processEvents();
        QCOMPARE(adaptor.pendingUnclaimedOpensCount(), 0);
        QVERIFY(!engine.isWindowTracked(QStringLiteral("app|gone")));

        // clearEngine sweeps a parked queue outright.
        adaptor.notifyEngineScreensChanged(false);
        adaptor.windowOpened(QStringLiteral("app|swept"), QStringLiteral("HDMI-9"), 0, 0);
        QCOMPARE(adaptor.pendingUnclaimedOpensCount(), 1);
        // The announce queued BEFORE clearEngine must NOT fire after it:
        // the generation void (and the empty-union bail behind it) exists
        // so a daemon-restart teardown never broadcasts an empty screen
        // set the effect would treat as a genuine disable and answer with
        // its destructive per-window teardown. Spy attached BEFORE the
        // clear so even a synchronous flush inside clearEngine would be
        // caught.
        QSignalSpy postClearSpy(&adaptor, &TilingAdaptor::managedScreensChanged);
        adaptor.clearEngine();
        QCOMPARE(adaptor.pendingUnclaimedOpensCount(), 0);
        QCoreApplication::processEvents();
        QCOMPARE(postClearSpy.count(), 0);
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
    // Empty-pipeline safety: Daemon::stop() calls clearEngine(), which empties
    // the lifecycle-engine list. A flush firing afterward must not dereference
    // anything.
    //
    // What this pins, precisely: clearEngine() DRAINS the pending-open queue
    // itself, and a flush arriving afterwards is harmless. It does NOT pin
    // ensurePipeline()'s empty check, even though the comment here used to claim
    // it did — that claim was false in both directions. The count is already 0
    // before emitPanelGeometryReady runs (clearEngine cleared it), so the
    // closing QCOMPARE would pass with the empty check deleted outright; and the
    // check cannot be reached with a non-empty queue at all, because
    // windowOpened calls ensurePipeline BEFORE deferUntilPanelReady, so nothing
    // can enqueue once the engine list is empty. ensurePipeline's empty branch
    // on the flush path is therefore unreachable-by-construction defence, and
    // the reachable contract is the drain plus crash-freedom.
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

        // The drain is the load-bearing half: without it the queue would still
        // hold an entry naming an engine that no longer exists.
        adaptor.clearEngine();
        QCOMPARE(adaptor.pendingWindowOpensCount(), 0);

        // And a late flush against the cleared adaptor must not crash.
        emitPanelGeometryReady(mgr);
        QCOMPARE(adaptor.pendingWindowOpensCount(), 0);
    }

    // -------------------------------------------------------------------------
    // TWO engines, which is the shape this adaptor exists for. Every other test
    // here wires a one-element list, and against a single engine the strict
    // claim loop is indistinguishable from an unconditional
    // m_lifecycleEngines.first() — the exact bug the loop's own comment warns
    // about. With two engines owning different screens, an open must reach the
    // engine that CLAIMS its screen and no other, and the announced screen set
    // must be the union of both.
    // -------------------------------------------------------------------------
    void testTwoEngines_openGoesToTheClaimingEngineOnly()
    {
        AutotileEngine first(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        AutotileEngine second(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        first.setAutotileScreens({QStringLiteral("HDMI-1")});
        second.setAutotileScreens({QStringLiteral("HDMI-2")});

        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        adaptor.setLifecycleEngines({&first, &second});

        // A screen only the SECOND engine claims. Dispatching to the first
        // (the fallback shape) would track the window there instead.
        adaptor.windowOpened(QStringLiteral("kitty|two"), QStringLiteral("HDMI-2"), 0, 0);
        QVERIFY(second.isWindowTracked(QStringLiteral("kitty|two")));
        QVERIFY(!first.isWindowTracked(QStringLiteral("kitty|two")));

        // And the mirror, so neither engine is simply adopting everything.
        adaptor.windowOpened(QStringLiteral("kitty|one"), QStringLiteral("HDMI-1"), 0, 0);
        QVERIFY(first.isWindowTracked(QStringLiteral("kitty|one")));
        QVERIFY(!second.isWindowTracked(QStringLiteral("kitty|one")));

        // A screen NEITHER claims is dropped, not handed to the first engine.
        adaptor.windowOpened(QStringLiteral("kitty|none"), QStringLiteral("DP-9"), 0, 0);
        QVERIFY(!first.isWindowTracked(QStringLiteral("kitty|none")));
        QVERIFY(!second.isWindowTracked(QStringLiteral("kitty|none")));
        QCOMPARE(adaptor.pendingUnclaimedOpensCount(), 0);

        // managedScreens is the UNION, sorted — a single-engine list could
        // never show the difference between a union and a passthrough.
        QSignalSpy spy(&adaptor, &TilingAdaptor::managedScreensChanged);
        adaptor.notifyEngineScreensChanged(false);
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toStringList(), (QStringList{QStringLiteral("HDMI-1"), QStringLiteral("HDMI-2")}));
    }
};

QTEST_MAIN(TestTilingAdaptorPanelGate)
#include "test_tiling_adaptor_panel_gate.moc"
