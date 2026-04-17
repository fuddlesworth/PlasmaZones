// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/PhosphorLayer.h>

#include "mocks/mockscreenprovider.h"
#include "mocks/mocktransport.h"

#include <QSignalSpy>
#include <QTest>

using namespace PhosphorLayer;
using PhosphorLayer::Testing::MockScreenProvider;
using PhosphorLayer::Testing::MockTransport;

class TestTopology : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void providerChangesDriveDebouncedSync()
    {
        MockTransport t;
        MockScreenProvider s;
        TopologyCoordinator coord(&s, &t, {/*debounceMs*/ 50, /*debug*/ false});

        QSignalSpy changingSpy(&coord, &TopologyCoordinator::screensChanging);
        QSignalSpy changedSpy(&coord, &TopologyCoordinator::screensChanged);

        int callbackRuns = 0;
        coord.attachSyncCallback([&] {
            ++callbackRuns;
        });

        // Three bursty provider pings within the debounce window should
        // coalesce into exactly one screensChanged emission.
        s.emitScreensChanged();
        s.emitScreensChanged();
        s.emitScreensChanged();

        QCOMPARE(changingSpy.count(), 1); // fired once on first burst
        QCOMPARE(changedSpy.count(), 0); // debounced — not yet

        QTRY_COMPARE_WITH_TIMEOUT(changedSpy.count(), 1, 500);
        QCOMPARE(callbackRuns, 1);
    }

    void detachedCallbackIsNotInvoked()
    {
        MockTransport t;
        MockScreenProvider s;
        TopologyCoordinator coord(&s, &t, {20, false});

        int runs = 0;
        const auto id = coord.attachSyncCallback([&] {
            ++runs;
        });
        coord.detachSyncCallback(id);

        s.emitScreensChanged();
        QSignalSpy done(&coord, &TopologyCoordinator::screensChanged);
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 500);
        QCOMPARE(runs, 0);
    }

    void compositorLostIsForwarded()
    {
        MockTransport t;
        MockScreenProvider s;
        TopologyCoordinator coord(&s, &t, {20, false});

        QSignalSpy spy(&coord, &TopologyCoordinator::compositorRestarted);
        t.simulateCompositorLost();
        // Emission is Queued (cross-thread pattern) — needs event loop
        // turnaround even when sender and receiver are on the same thread.
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 500);
    }

    void multipleCallbacksFireInRegistrationOrder()
    {
        MockTransport t;
        MockScreenProvider s;
        TopologyCoordinator coord(&s, &t, {20, false});

        QList<int> order;
        coord.attachSyncCallback([&] {
            order.append(1);
        });
        coord.attachSyncCallback([&] {
            order.append(2);
        });
        coord.attachSyncCallback([&] {
            order.append(3);
        });

        s.emitScreensChanged();
        QSignalSpy done(&coord, &TopologyCoordinator::screensChanged);
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 500);
        QCOMPARE(order, QList<int>({1, 2, 3}));
    }

    void reentrantDetachDuringCallbackIsSafe()
    {
        // topologycoordinator.cpp:79-84 specifically handles a callback that
        // detaches another callback (or itself) mid-fire by iterating a
        // copied list and re-checking m_callbacks.contains() before each
        // invocation. Verify that contract holds: detaching cb2 from inside
        // cb1 must NOT invoke cb2, but cb3 (still registered) must still run.
        MockTransport t;
        MockScreenProvider s;
        TopologyCoordinator coord(&s, &t, {20, false});

        TopologyCoordinator::CallbackId id2 = 0;
        int cb1Runs = 0, cb2Runs = 0, cb3Runs = 0;

        coord.attachSyncCallback([&] {
            ++cb1Runs;
            coord.detachSyncCallback(id2);
        });
        id2 = coord.attachSyncCallback([&] {
            ++cb2Runs;
        });
        coord.attachSyncCallback([&] {
            ++cb3Runs;
        });

        s.emitScreensChanged();
        QSignalSpy done(&coord, &TopologyCoordinator::screensChanged);
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 500);

        QCOMPARE(cb1Runs, 1);
        QCOMPARE(cb2Runs, 0); // detached mid-fire, never invoked
        QCOMPARE(cb3Runs, 1);
    }

    void nullNotifierFromProviderDoesNotCrash()
    {
        // topologycoordinator.cpp has an explicit null-notifier branch
        // (provider ships nullptr when it can't track screens). Verify the
        // coordinator constructs and destructs cleanly in that regime.
        struct NoNotifierProvider : public IScreenProvider
        {
            QList<QScreen*> screens() const override
            {
                return {};
            }
            QScreen* primary() const override
            {
                return nullptr;
            }
            QScreen* focused() const override
            {
                return nullptr;
            }
            ScreenProviderNotifier* notifier() const override
            {
                return nullptr;
            }
        };
        NoNotifierProvider p;
        MockTransport t;
        TopologyCoordinator coord(&p, &t, {20, false});
        // No signal wiring available; callbacks never fire but attaching
        // must still work without UB.
        int runs = 0;
        const auto id = coord.attachSyncCallback([&] {
            ++runs;
        });
        coord.detachSyncCallback(id);
        QCOMPARE(runs, 0);
    }
};

QTEST_MAIN(TestTopology)
#include "test_topology.moc"
