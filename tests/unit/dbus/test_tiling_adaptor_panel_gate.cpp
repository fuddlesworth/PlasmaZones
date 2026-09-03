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

#include <PhosphorEngine/IPlacementEngine.h>
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
bool emitPanelGeometryReady(PhosphorScreens::ScreenManager& mgr)
{
    // The bool is RETURNED, and every call site QVERIFYs it. Invoking by
    // string name means a rename is a runtime warning rather than a compile
    // error, and one slot here asserts a count that is already zero before the
    // emit — so it would pass against a helper that fired nothing at all.
    return QMetaObject::invokeMethod(&mgr, "panelGeometryReady");
}

/// Records DISPATCH ORDER and the arrival-burst brackets.
///
/// The engines the rest of this file uses answer set membership
/// (isWindowTracked), which cannot tell "a before b" from "b before a" — so
/// reversing the replay loop in flushPendingWindowOpens failed nothing in the
/// repo, even though deferUntilPanelReady's own comment calls replay order
/// load-bearing ("replay order decides strip column order and master
/// assignment"). Same for the beginArrivalBurst/endArrivalBurst pair: deleting
/// either bracket was invisible, because nothing observed them.
///
/// A recorder rather than a real engine on purpose. The property under test
/// belongs to the ADAPTOR (what it dispatches, in what order, inside which
/// brackets), and routing it through a placement engine would make the
/// assertion depend on that engine's own ordering rules as well.
class RecordingEngine : public PhosphorEngine::IPlacementEngine
{
public:
    QStringList dispatched;
    int burstDepth = 0;
    int maxBurstDepth = 0;
    int burstsOpened = 0;
    /// Windows dispatched while NO burst bracket was open. The brackets exist
    /// to suppress partial intermediates, so a dispatch outside them is
    /// exactly the visual glitch they were added to prevent.
    QStringList dispatchedOutsideBurst;

    bool isActiveOnScreen(const QString&) const override
    {
        return true;
    }
    /// Windows the adaptor OFFERED to the cross-screen session reclaim, in
    /// order. Recorded rather than acted on: the property under test is
    /// whether the adaptor runs the claim round at all, and answering the
    /// claim would additionally change which dispatch branch is taken.
    QStringList reclaimOffers;

    void windowOpened(const QString& windowId, const QString&, int, int) override
    {
        dispatched.append(windowId);
        if (burstDepth == 0) {
            dispatchedOutsideBurst.append(windowId);
        }
    }
    bool claimCrossScreenReopen(const QString& windowId, const QString&, int, int) override
    {
        reclaimOffers.append(windowId);
        return false; // decline, so the arrival-screen dispatch still runs
    }
    void beginArrivalBurst() override
    {
        ++burstDepth;
        ++burstsOpened;
        maxBurstDepth = std::max(maxBurstDepth, burstDepth);
    }
    void endArrivalBurst() override
    {
        --burstDepth;
    }
    void windowClosed(const QString&) override
    {
    }
    void windowFocused(const QString&, const QString&) override
    {
    }
    void toggleWindowFloat(const QString&, const QString&) override
    {
    }
    void setWindowFloat(const QString&, bool, const QString&) override
    {
    }
    void focusInDirection(const QString&, const PhosphorEngine::NavigationContext&) override
    {
    }
    void moveFocusedInDirection(const QString&, const PhosphorEngine::NavigationContext&) override
    {
    }
    void swapFocusedInDirection(const QString&, const PhosphorEngine::NavigationContext&) override
    {
    }
    void moveFocusedToPosition(int, const PhosphorEngine::NavigationContext&) override
    {
    }
    void rotateWindows(bool, const PhosphorEngine::NavigationContext&) override
    {
    }
    void reapplyLayout(const PhosphorEngine::NavigationContext&) override
    {
    }
    void snapAllWindows(const PhosphorEngine::NavigationContext&) override
    {
    }
    void cycleFocus(bool, const PhosphorEngine::NavigationContext&) override
    {
    }
    void pushToEmptyZone(const PhosphorEngine::NavigationContext&) override
    {
    }
    void restoreFocusedWindow(const PhosphorEngine::NavigationContext&) override
    {
    }
    void toggleFocusedFloat(const PhosphorEngine::NavigationContext&) override
    {
    }
    void saveState() override
    {
    }
    void loadState() override
    {
    }
    PhosphorEngine::IPlacementState* stateForScreen(const QString&) override
    {
        return nullptr;
    }
    const PhosphorEngine::IPlacementState* stateForScreen(const QString&) const override
    {
        return nullptr;
    }
};
} // namespace

class TestTilingAdaptorPanelGate : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // -------------------------------------------------------------------------
    // Replay ORDER out of the deferral queue, and the burst brackets around it.
    //
    // deferUntilPanelReady's comment calls replay order load-bearing: it
    // "decides strip column order and master assignment". Nothing pinned it.
    // Reversing the loop over toFlush passed the entire repo, because every
    // other slot here observes set membership, which is order-blind.
    //
    // The brackets are pinned in the same slot rather than a separate one
    // because they are a property OF this dispatch: every deferred window must
    // land inside one bracket pair, not merely somewhere near them. Asserting
    // "begin was called" would survive deleting the loop that calls it per
    // engine, and asserting a count would survive the pair being emitted
    // after the dispatches.
    // -------------------------------------------------------------------------
    void testDeferredOpensReplayInArrivalOrderInsideOneBurst()
    {
        PhosphorScreens::ScreenManager mgr;
        RecordingEngine engine;
        QObject adaptorParent;
        TilingAdaptor adaptor(&mgr, &adaptorParent);
        adaptor.setLifecycleEngines({&engine});
        QVERIFY2(!mgr.isPanelGeometryReady(), "the gate must start closed, or nothing defers");

        // Deliberately NOT alphabetical: sorted ids would pass against an
        // implementation that sorted the queue, and reverse-alphabetical would
        // pass against one that reversed it. This order is neither.
        const QStringList arrival{QStringLiteral("mid|2"), QStringLiteral("first|1"), QStringLiteral("zed|3"),
                                  QStringLiteral("alpha|4")};
        for (const QString& id : arrival) {
            adaptor.windowOpened(id, QStringLiteral("HDMI-1"), 0, 0);
        }
        QCOMPARE(adaptor.pendingWindowOpensCount(), arrival.size());
        QVERIFY2(engine.dispatched.isEmpty(), "nothing may dispatch while the gate is closed");

        QVERIFY(emitPanelGeometryReady(mgr));
        QCoreApplication::processEvents();

        QCOMPARE(adaptor.pendingWindowOpensCount(), 0);
        QCOMPARE(engine.dispatched, arrival);
        // Every dispatch sat inside a bracket, and the brackets did not nest.
        QVERIFY2(engine.dispatchedOutsideBurst.isEmpty(),
                 "a deferred open dispatched outside the arrival burst defeats the bracket");
        QCOMPARE(engine.burstsOpened, 1);
        QCOMPARE(engine.maxBurstDepth, 1);
        QCOMPARE(engine.burstDepth, 0);
    }

    // -------------------------------------------------------------------------
    // The move-release one-shot (m_moveReleasedInstances).
    //
    // releaseWindowTracking drops a LIVE window from engine tracking WITHOUT
    // capturing a placement, and the effect then re-announces it on the screen
    // the user moved it to. That announce looks like a first observation to
    // claimCrossScreenReopen, whose same-instance branch matches the window's
    // own stale record — still tiled on the OLD screen — and yanks it back,
    // silently undoing the move. The one-shot suppresses exactly that claim
    // round and nothing else.
    //
    // The whole feature shipped untested, and its consumption sat behind a
    // short-circuited `allowCrossScreenClaim &&`, so a rule-routed re-announce
    // left the entry armed to spend itself on an unrelated later announce.
    // Asserting only "the re-announce was not reclaimed" would pass against
    // that; the third announce below is what pins the ONE in one-shot.
    // -------------------------------------------------------------------------
    void testMoveReleaseSuppressesExactlyOneCrossScreenReclaim()
    {
        RecordingEngine engine;
        QObject adaptorParent;
        // Null screen manager: the panel gate never engages, so every open
        // dispatches synchronously and the assertions read dispatch order
        // directly rather than through a flush.
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        adaptor.setLifecycleEngines({&engine});

        // Baseline: an ordinary open IS offered to the session reclaim.
        adaptor.windowOpened(QStringLiteral("kate|a"), QStringLiteral("HDMI-1"), 0, 0);
        QCOMPARE(engine.reclaimOffers, QStringList{QStringLiteral("kate|a")});
        QCOMPARE(engine.dispatched.size(), 1);

        // A live move release arms the one-shot; the re-announce skips the
        // claim round and is adopted by the arrival screen's engine instead.
        adaptor.releaseWindowTracking(QStringLiteral("kate|a"));
        adaptor.windowOpened(QStringLiteral("kate|a"), QStringLiteral("HDMI-2"), 0, 0);
        QCOMPARE(engine.reclaimOffers.size(), 1); // unchanged — suppressed
        QCOMPARE(engine.dispatched.size(), 2); // but still dispatched

        // ONE shot. The next announce for the same live window is offered
        // again, or a single move would disarm the session reclaim for that
        // window permanently.
        adaptor.windowOpened(QStringLiteral("kate|a"), QStringLiteral("HDMI-2"), 0, 0);
        QCOMPARE(engine.reclaimOffers.size(), 2);
        QCOMPARE(engine.reclaimOffers.last(), QStringLiteral("kate|a"));
    }

    // -------------------------------------------------------------------------
    // The queue's OVERFLOW valve. The panel query is a D-Bus round trip that
    // may never answer, so the queue is capped; past the cap an open is
    // processed against the unreserved screen rect rather than dropped.
    //
    // The valve flushes the backlog BEFORE returning false, and that ordering
    // is the whole point: returning false without draining would put the
    // newcomer in front of every window that arrived before it, which then
    // replays behind it on the next flush. A regression that dropped the
    // flush call would reorder the entire session's startup silently, and
    // nothing observed it.
    //
    // kMaxPendingOpens is a static constexpr with no injection seam, so the
    // slot queues the real 512 and trips the cap with the next one. The
    // entries are cheap POD.
    // -------------------------------------------------------------------------
    void testPendingOpenOverflowFlushesTheBacklogBeforeTheNewcomer()
    {
        PhosphorScreens::ScreenManager mgr;
        RecordingEngine engine;
        QObject adaptorParent;
        TilingAdaptor adaptor(&mgr, &adaptorParent);
        adaptor.setLifecycleEngines({&engine});
        QVERIFY2(!mgr.isPanelGeometryReady(), "the gate must start closed, or nothing defers");

        const int cap = adaptor.pendingWindowOpensCapacity();
        QVERIFY2(cap > 0, "capacity accessor must report the real cap");
        for (int i = 0; i < cap; ++i) {
            adaptor.windowOpened(QStringLiteral("q|%1").arg(i), QStringLiteral("HDMI-1"), 0, 0);
        }
        QCOMPARE(adaptor.pendingWindowOpensCount(), cap);
        QVERIFY2(engine.dispatched.isEmpty(), "the queue must fill without dispatching");

        // One more trips the valve.
        adaptor.windowOpened(QStringLiteral("overflow|new"), QStringLiteral("HDMI-1"), 0, 0);

        // The backlog drained, and the newcomer landed AFTER all of it.
        QCOMPARE(adaptor.pendingWindowOpensCount(), 0);
        QCOMPARE(engine.dispatched.size(), cap + 1);
        QCOMPARE(engine.dispatched.first(), QStringLiteral("q|0"));
        QCOMPARE(engine.dispatched.at(cap - 1), QStringLiteral("q|%1").arg(cap - 1));
        QCOMPARE(engine.dispatched.last(), QStringLiteral("overflow|new"));
    }

    // -------------------------------------------------------------------------
    // The deferral queue's DROP path. A window that opens behind the panel gate
    // and closes before the gate lifts must never reach an engine.
    //
    // Untested until now, and the sibling close-sweep case covers the OTHER
    // queue (the mid-flip park), so deleting removePendingOpen's call site left
    // the whole suite green. What it guards is a phantom tile for the session:
    // the flush would dispatch a dead window into the engine, and there is no
    // later windowClosed to shed it, because the close already happened before
    // the dispatch. A splash screen or a session-restore dialog that opens and
    // closes inside the startup window is exactly this shape.
    //
    // The assertion is on ENGINE TRACKING, not on the queue count: every other
    // deferral case here pins only that the queue drained, which cannot tell a
    // dispatch from a drop.
    // -------------------------------------------------------------------------
    void testDeferredOpenClosedBeforeReady_neverReachesTheEngine()
    {
        PhosphorScreens::ScreenManager mgr;
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QStringLiteral("HDMI-1")});
        QObject adaptorParent;
        TilingAdaptor adaptor(&mgr, &adaptorParent);
        adaptor.setLifecycleEngines({&engine});
        QVERIFY2(!mgr.isPanelGeometryReady(), "the gate must start closed, or nothing defers");

        adaptor.windowOpened(QStringLiteral("splash|1"), QStringLiteral("HDMI-1"), 0, 0);
        adaptor.windowOpened(QStringLiteral("kept|2"), QStringLiteral("HDMI-1"), 0, 0);
        QCOMPARE(adaptor.pendingWindowOpensCount(), 2);

        adaptor.windowClosed(QStringLiteral("splash|1"));
        QCOMPARE(adaptor.pendingWindowOpensCount(), 1);

        QVERIFY(emitPanelGeometryReady(mgr));
        QCoreApplication::processEvents();
        QCOMPARE(adaptor.pendingWindowOpensCount(), 0);
        QVERIFY2(!engine.isWindowTracked(QStringLiteral("splash|1")),
                 "a window closed before the gate lifted must not be dispatched");
        QVERIFY2(engine.isWindowTracked(QStringLiteral("kept|2")),
                 "the surviving open must still dispatch, or the drop proved nothing");
    }

    // -------------------------------------------------------------------------
    // The NO-GATE fast path: an open reaches its engine immediately rather than
    // queueing. Every other deferral case here relies on the gate being CLOSED,
    // so nothing pinned that an ungated open dispatches at all — and the
    // baseline case above, which does use a null manager, has no engine
    // claiming the screen, so it can only assert that nothing queued.
    //
    // This drives the `!m_screenManager` half of the bail. The other half,
    // isPanelGeometryReady() answering TRUE, is NOT reachable from this
    // harness: the flag is set inside ScreenManager's own Plasma D-Bus
    // callback, and the test helper can only emit the notification signal, not
    // flip the state behind it. Stating that rather than leaving a
    // half-covered predicate looking fully covered.
    // -------------------------------------------------------------------------
    void testNoPanelGate_dispatchesImmediatelyWithoutQueueing()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QStringLiteral("HDMI-1")});
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        adaptor.setLifecycleEngines({&engine});

        adaptor.windowOpened(QStringLiteral("app|now"), QStringLiteral("HDMI-1"), 0, 0);
        QCOMPARE(adaptor.pendingWindowOpensCount(), 0);
        QVERIFY2(engine.isWindowTracked(QStringLiteral("app|now")),
                 "with no panel gate an open must dispatch immediately rather than queue");
    }

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
        QVERIFY(emitPanelGeometryReady(mgr));

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

        QVERIFY(emitPanelGeometryReady(mgr));
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

        QVERIFY(emitPanelGeometryReady(mgr));
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
    // closing QCOMPARE would pass with the empty check deleted outright.
    //
    // The branch cannot be reached with a non-empty queue, but the ordering
    // argument alone did not establish that. windowOpened calling
    // ensurePipeline before deferUntilPanelReady stops anything enqueueing
    // once the list is already empty; it says nothing about entries queued
    // BEFORE the list was emptied. That hole was real until
    // setLifecycleEngines({}) started clearing the deferral queue as well as
    // the parked one — both are equally un-retryable without a pipeline. The
    // reachable contract this slot pins is still the drain plus crash-freedom.
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
        QVERIFY(emitPanelGeometryReady(mgr));
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
            "\"windowedFullscreen\":true,\"maximizedToEdges\":true,\"tabFrom\":\"a|1\"},"
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
        // maximizedToEdges rides the same JSON hop, and needs pinning for the
        // same reason the two above do: a typo or a dropped parse line yields
        // false with no error.
        //
        // What this pins is the CONSUMER half only. The fixture below hand-
        // writes its own JSON, so it is a third literal rather than the
        // engine's — a rename in engine_apply.cpp's producer would leave this
        // green. That side is covered by the engine suite, which reads the key
        // out of the engine's real emitted JSON. Neither compares the two
        // literals, so a coordinated rename passes both; sharing one constant
        // between producer and consumer is what would close that.
        // Paired with windowedFullscreen on b|2 deliberately: that combination
        // is explicitly legal (they drive different compositor state, and a
        // maximized column can hold a windowed-fullscreen tile), so this also
        // pins that validationError does not reject the pair.
        QCOMPARE(requests.at(1).maximizedToEdges, true);
        QCOMPARE(requests.at(0).maximizedToEdges, false);
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

    // -------------------------------------------------------------------------
    // Ordering contract between the coalesced screens announce and the tile
    // batch relay. The effect answers a desktop-switch announce by voiding
    // every in-flight staggered apply, so a batch that reaches it BEFORE the
    // announce it was resolved after loses every entry past the first
    // (three columns, focus the middle one from another desktop: the
    // neighbour moved, the focused column stayed at its old rect). A batch
    // relayed while the announce is queued must therefore be held and go
    // out right behind it, in arrival order; with no announce pending the
    // relay stays synchronous; clearEngine sweeps the held batches with the
    // announce they were waiting on.
    // -------------------------------------------------------------------------
    void testTileBatchHeldBehindPendingScreensAnnounce()
    {
        PhosphorProtocol::registerWireTypes();
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setAutotileScreens({QStringLiteral("HDMI-1")});
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        adaptor.setLifecycleEngines({&engine});

        // One recorder for BOTH signals: a pair of spies can count but not
        // order, and order is the whole property.
        QStringList wire;
        connect(&adaptor, &TilingAdaptor::managedScreensChanged, &adaptorParent,
                [&wire](const QStringList&, bool, const QVariantMap&) {
                    wire.append(QStringLiteral("announce"));
                });
        connect(&adaptor, &TilingAdaptor::windowsTileRequested, &adaptorParent,
                [&wire](const PhosphorProtocol::TileRequestList& requests) {
                    wire.append(QStringLiteral("batch:") + requests.first().windowId);
                });

        const auto batchFor = [](const QString& windowId) {
            return QStringLiteral(
                       "[{\"windowId\":\"%1\",\"screenId\":\"HDMI-1\",\"x\":0,\"y\":0,\"width\":600,"
                       "\"height\":800}]")
                .arg(windowId);
        };

        // No announce pending: the relay is synchronous, nothing is held.
        adaptor.relayTileRequestsJson(batchFor(QStringLiteral("a|1")));
        QCOMPARE(wire, (QStringList{QStringLiteral("batch:a|1")}));
        QCOMPARE(adaptor.pendingHeldTileBatchCount(), 0);
        wire.clear();

        // Announce queued, then two batches: both hold, and the wire stays
        // silent until the announce fires, which then trails them in
        // arrival order.
        adaptor.notifyEngineScreensChanged(true);
        adaptor.relayTileRequestsJson(batchFor(QStringLiteral("b|2")));
        adaptor.relayTileRequestsJson(batchFor(QStringLiteral("c|3")));
        QVERIFY(wire.isEmpty());
        QCOMPARE(adaptor.pendingHeldTileBatchCount(), 2);
        QCoreApplication::processEvents();
        QCOMPARE(wire,
                 (QStringList{QStringLiteral("announce"), QStringLiteral("batch:b|2"), QStringLiteral("batch:c|3")}));
        QCOMPARE(adaptor.pendingHeldTileBatchCount(), 0);
        wire.clear();

        // After the flush the relay is synchronous again.
        adaptor.relayTileRequestsJson(batchFor(QStringLiteral("d|4")));
        QCOMPARE(wire, (QStringList{QStringLiteral("batch:d|4")}));
        wire.clear();

        // clearEngine sweeps a held batch with the announce it waited on:
        // neither reaches the wire afterwards.
        adaptor.notifyEngineScreensChanged(true);
        adaptor.relayTileRequestsJson(batchFor(QStringLiteral("e|5")));
        QCOMPARE(adaptor.pendingHeldTileBatchCount(), 1);
        adaptor.clearEngine();
        QCOMPARE(adaptor.pendingHeldTileBatchCount(), 0);
        QCoreApplication::processEvents();
        QVERIFY(wire.isEmpty());
    }
};

QTEST_MAIN(TestTilingAdaptorPanelGate)
#include "test_tiling_adaptor_panel_gate.moc"
