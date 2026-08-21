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
#include <QRegularExpression>
#include <QSignalSpy>
#include <QObject>

#include <PhosphorProtocol/Registration.h>
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
    // Baseline: with no ScreenManager injected, windowOpened must not force a
    // dependency on ScreenManager (headless unit tests inject nullptr) and
    // must not QUEUE — the open is dropped outright here because no engine
    // claims the screen, and "did not queue" is all this case pins.
    // -------------------------------------------------------------------------
    void testNoScreenManager_noQueueing()
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

    // -------------------------------------------------------------------------
    // relayTileRequestsJson JSON→struct parse. This pins the JSON key spelling
    // shared with the engine producer (engine_apply.cpp writes "scrollEdge"
    // et al) — a rename on either side would otherwise yield an empty field
    // with no error and no failing test — plus the validator drop for an
    // illegal scrollEdge value and the duplicate-windowId collapse.
    // -------------------------------------------------------------------------
    void testRelayTileRequestsJson_parsesScrollEdgeAndDropsInvalid()
    {
        PhosphorProtocol::registerWireTypes();
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        QSignalSpy spy(&adaptor, &TilingAdaptor::windowsTileRequested);

        // The two validator drops below (c|3 illegal edge, d|4 flag on
        // floating) warn by design at this boundary — expected output, not
        // noise, so keep the ctest log clean.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("dropping entry")));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("dropping entry")));

        const QString json = QStringLiteral(
            "["
            "{\"windowId\":\"a|1\",\"screenId\":\"S1\",\"x\":0,\"y\":0,\"width\":600,\"height\":800,"
            "\"scrollEdge\":\"left\"},"
            "{\"windowId\":\"b|2\",\"screenId\":\"S1\",\"x\":600,\"y\":0,\"width\":600,\"height\":800,"
            "\"windowedFullscreen\":true,\"tabFrom\":\"a|1\"},"
            "{\"windowId\":\"c|3\",\"screenId\":\"S1\",\"x\":0,\"y\":0,\"width\":600,\"height\":800,"
            "\"scrollEdge\":\"up\"},"
            "{\"windowId\":\"a|1\",\"screenId\":\"S1\",\"x\":50,\"y\":0,\"width\":600,\"height\":800,"
            "\"scrollEdge\":\"right\"},"
            "{\"windowId\":\"d|4\",\"screenId\":\"S1\",\"floating\":true,\"windowedFullscreen\":true},"
            "{\"windowId\":\"e|5\",\"screenId\":\"S1\",\"x\":0,\"y\":0,\"width\":600,\"height\":800,"
            "\"visualX\":100},"
            "{\"windowId\":\"f|6\",\"screenId\":\"S1\",\"x\":0,\"y\":0,\"width\":600,\"height\":800,"
            "\"visualX\":4000.5,\"visualY\":10},"
            "{\"windowId\":\"g|7\",\"screenId\":\"S1\",\"x\":0,\"y\":0,\"width\":600,\"height\":800,"
            "\"visualX\":-1200,\"visualY\":40},"
            "{\"windowId\":\"h|8\",\"screenId\":\"S1\",\"floating\":true,\"visualX\":5,\"visualY\":6,"
            "\"tabFrom\":\"a|1\"}"
            "]");
        adaptor.relayTileRequestsJson(json);

        QCOMPARE(spy.count(), 1);
        const auto requests = spy.first().at(0).value<PhosphorProtocol::TileRequestList>();
        // c|3 dropped by the validator (illegal edge), the second a|1 dropped
        // as a duplicate (first entry wins), and d|4 dropped by the
        // windowedFullscreen-on-floating rejection — pinning that the
        // adaptor's parse order (floating first, geometry skipped) still
        // reaches the validator; a reorder that set the flag before parsing
        // floating, or an early continue on the zero geometry, would stop
        // rejecting the pair with no failing test.
        QCOMPARE(requests.size(), 6);
        QCOMPARE(requests.at(0).windowId, QStringLiteral("a|1"));
        QCOMPARE(requests.at(0).scrollEdge, QStringLiteral("left"));
        QCOMPARE(requests.at(0).x, 0);
        QCOMPARE(requests.at(1).windowId, QStringLiteral("b|2"));
        QVERIFY(requests.at(1).scrollEdge.isEmpty());
        // The windowedFullscreen key parses through the same JSON hop (a
        // producer-side rename would otherwise silently read false), and its
        // absence on a|1 reads false.
        QCOMPARE(requests.at(1).windowedFullscreen, true);
        QCOMPARE(requests.at(0).windowedFullscreen, false);
        // tabFrom parses through the same JSON hop on a tiled entry (key
        // spelling pinned against the engine producer), and its absence on
        // a|1 reads empty.
        QCOMPARE(requests.at(1).tabFrom, QStringLiteral("a|1"));
        QVERIFY(requests.at(0).tabFrom.isEmpty());
        // The visual-position unmarshal guard, per arm (previously
        // untested): visualX alone stays unset (the keys are a required
        // PAIR), a fractional value FAILS CLOSED (the floor check is what
        // separates 4000.5 decoding to 0-with-the-flag-latched from a clean
        // reject), a valid integral pair relays (negative x is legal — the
        // park is off-canvas), and a floating entry never carries one.
        QCOMPARE(requests.at(2).windowId, QStringLiteral("e|5"));
        QCOMPARE(requests.at(2).hasVisualPos, false);
        QCOMPARE(requests.at(3).windowId, QStringLiteral("f|6"));
        QCOMPARE(requests.at(3).hasVisualPos, false);
        QCOMPARE(requests.at(4).windowId, QStringLiteral("g|7"));
        QCOMPARE(requests.at(4).hasVisualPos, true);
        QCOMPARE(requests.at(4).visualX, -1200);
        QCOMPARE(requests.at(4).visualY, 40);
        QCOMPARE(requests.at(5).windowId, QStringLiteral("h|8"));
        QCOMPARE(requests.at(5).floating, true);
        QCOMPARE(requests.at(5).hasVisualPos, false);
        // A floating entry never carries a tabFrom: the hint is dropped in
        // the parse (a floating window is not a tab of anything), mirroring
        // the visual-position gate above.
        QVERIFY(requests.at(5).tabFrom.isEmpty());
    }
};

QTEST_MAIN(TestTilingAdaptorPanelGate)
#include "test_tiling_adaptor_panel_gate.moc"
