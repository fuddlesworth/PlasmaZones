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
};

QTEST_MAIN(TestTopology)
#include "test_topology.moc"
