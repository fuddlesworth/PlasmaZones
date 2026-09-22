// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>

#include <QSet>

#include <PhosphorIdentity/WindowId.h>

#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include "helpers/WindowPlacementBuilders.h"

using PhosphorEngine::WindowPlacement;
using PhosphorEngine::WindowPlacementStore;
using PlasmaZones::TestHelpers::makePlacement;

/**
 * @brief The store's live-sibling contract on the snap open path (#1106): a
 *        record bound to a still-open window is never consumed by a same-app
 *        sibling's open, and is the record such a sibling inherits its free
 *        size from.
 */
class TestWindowPlacementStoreSiblings : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // take()'s appId FIFO (the snap engine's open-path consumer) applies the
    // same live exclusion takeForReopen does: a second instance opened beside
    // a snapped first must not consume the record just re-bound to the first.
    // With no other record it consumes nothing and is a fresh window.
    void testTake_fifoSkipsRecordBoundToLiveSibling()
    {
        WindowPlacementStore store;
        QSet<QString> liveInstances;
        store.setLiveInstanceProbe([&liveInstances](const QString& windowId) {
            return liveInstances.contains(PhosphorIdentity::WindowId::extractInstanceId(windowId));
        });
        store.record(makePlacement(QStringLiteral("app|first"), QStringLiteral("app"), WindowPlacement::stateSnapped(),
                                   WindowPlacement::snapEngineId(), QStringLiteral("DP-1"), QRect(0, 0, 10, 10)));
        liveInstances.insert(QStringLiteral("first"));

        QVERIFY2(!store.take(QStringLiteral("app|second"), QStringLiteral("app")).has_value(),
                 "a live sibling's record is never handed to a fresh same-app window");
        QCOMPARE(store.size(), 1);

        // The window's OWN record is still its history, live or not (the
        // daemon-restart shape), and a closed sibling's record is fair game.
        QVERIFY(store.take(QStringLiteral("app|first"), QStringLiteral("app")).has_value());
        store.record(makePlacement(QStringLiteral("app|gone"), QStringLiteral("app"), WindowPlacement::stateFloating(),
                                   WindowPlacement::snapEngineId(), QStringLiteral("DP-1"), QRect(0, 0, 10, 10)));
        QVERIFY(store.take(QStringLiteral("app|second"), QStringLiteral("app")).has_value());
        QCOMPARE(store.size(), 0);
    }

    void testPeekLiveSibling_newestLiveOtherInstanceOnly()
    {
        WindowPlacementStore store;
        QSet<QString> liveInstances;
        // Without a probe liveness cannot be established, so there is no sibling.
        store.record(makePlacement(QStringLiteral("app|a"), QStringLiteral("app"), WindowPlacement::stateSnapped(),
                                   WindowPlacement::snapEngineId(), QStringLiteral("DP-1"), QRect(0, 0, 10, 10)));
        QVERIFY(!store.peekLiveSibling(QStringLiteral("app|new"), QStringLiteral("app")).has_value());

        store.setLiveInstanceProbe([&liveInstances](const QString& windowId) {
            return liveInstances.contains(PhosphorIdentity::WindowId::extractInstanceId(windowId));
        });
        store.record(makePlacement(QStringLiteral("app|b"), QStringLiteral("app"), WindowPlacement::stateSnapped(),
                                   WindowPlacement::snapEngineId(), QStringLiteral("DP-1"), QRect(50, 50, 20, 20)));
        // Nothing live: no sibling. A closed sibling's record is reopen memory,
        // not a window to inherit from.
        QVERIFY(!store.peekLiveSibling(QStringLiteral("app|new"), QStringLiteral("app")).has_value());

        liveInstances.insert(QStringLiteral("a"));
        liveInstances.insert(QStringLiteral("b"));
        const auto sibling = store.peekLiveSibling(QStringLiteral("app|new"), QStringLiteral("app"));
        QVERIFY(sibling.has_value());
        QCOMPARE(sibling->windowId, QStringLiteral("app|b")); // newest live
        // The asking window's own instance is never its own sibling.
        const auto other = store.peekLiveSibling(QStringLiteral("app|b"), QStringLiteral("app"));
        QVERIFY(other.has_value());
        QCOMPARE(other->windowId, QStringLiteral("app|a"));
        // Non-consuming.
        QCOMPARE(store.size(), 2);
    }
};

QTEST_GUILESS_MAIN(TestWindowPlacementStoreSiblings)
#include "test_window_placement_store_siblings.moc"
