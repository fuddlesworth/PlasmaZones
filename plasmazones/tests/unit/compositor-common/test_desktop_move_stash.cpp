// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_desktop_move_stash.cpp
 * @brief The free geometry a window takes with it when it leaves a tiling
 *        desktop: folded back on a tiling arrival, owed once on a desktop
 *        that does not tile, and never handed back across a monitor.
 */

#include <QTest>

#include "tilinghandler/desktopmovestash.h"

using PlasmaZones::DesktopMoveStash;

namespace {
const QString kWin = QStringLiteral("app|w");
const QRectF kRect(100, 80, 640, 480);
} // namespace

class TestDesktopMoveStash : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void managedArrivalConsumesASamePhysicalRect()
    {
        DesktopMoveStash stash;
        stash.stash(kWin, QStringLiteral("DP-1"), kRect);
        QCOMPARE(stash.consumeForManagedArrival(kWin, QStringLiteral("DP-1")), std::optional<QRectF>(kRect));
        QCOMPARE(stash.consumeForManagedArrival(kWin, QStringLiteral("DP-1")), std::optional<QRectF>());
    }

    // Consumed either way: a rect from another monitor has no later use.
    void managedArrivalDropsAnotherMonitorsRect()
    {
        DesktopMoveStash stash;
        stash.stash(kWin, QStringLiteral("DP-1"), kRect);
        QCOMPARE(stash.consumeForManagedArrival(kWin, QStringLiteral("DP-2")), std::optional<QRectF>());
        QCOMPARE(stash.consumeForManagedArrival(kWin, QStringLiteral("DP-1")), std::optional<QRectF>());
    }

    void virtualScreenRekeyIsTheSameMonitor()
    {
        DesktopMoveStash stash;
        stash.stash(kWin, QStringLiteral("DP-1/vs:0"), kRect);
        QCOMPARE(stash.consumeForManagedArrival(kWin, QStringLiteral("DP-1/vs:1")), std::optional<QRectF>(kRect));
    }

    void owedPlacementIsPaidOnce()
    {
        DesktopMoveStash stash;
        stash.stash(kWin, QStringLiteral("DP-1"), kRect);
        stash.setOwed(kWin, true);
        bool owed = false;
        QCOMPARE(stash.takeOwedPlacement(kWin, QStringLiteral("DP-1"), &owed), std::optional<QRectF>(kRect));
        QVERIFY(owed);
        QCOMPARE(stash.takeOwedPlacement(kWin, QStringLiteral("DP-1"), &owed), std::optional<QRectF>());
        QVERIFY(!owed);
    }

    // The pay keeps the entry, so a later move back onto a tiling desktop
    // still folds the free rect into the window's pre-tile record.
    void owedPlacementKeepsTheStashForALaterManagedArrival()
    {
        DesktopMoveStash stash;
        stash.stash(kWin, QStringLiteral("DP-1"), kRect);
        stash.setOwed(kWin, true);
        bool owed = false;
        stash.takeOwedPlacement(kWin, QStringLiteral("DP-1"), &owed);
        QCOMPARE(stash.consumeForManagedArrival(kWin, QStringLiteral("DP-1")), std::optional<QRectF>(kRect));
    }

    void owedPlacementAnswersNoRectFromAnotherMonitor()
    {
        DesktopMoveStash stash;
        stash.stash(kWin, QStringLiteral("DP-1"), kRect);
        stash.setOwed(kWin, true);
        bool owed = false;
        QCOMPARE(stash.takeOwedPlacement(kWin, QStringLiteral("DP-2"), &owed), std::optional<QRectF>());
        QVERIFY(owed);
    }

    void forgetDropsEntryAndOwed()
    {
        DesktopMoveStash stash;
        stash.stash(kWin, QStringLiteral("DP-1"), kRect);
        stash.setOwed(kWin, true);
        stash.forget(kWin);
        QVERIFY(!stash.isOwed(kWin));
        QCOMPARE(stash.consumeForManagedArrival(kWin, QStringLiteral("DP-1")), std::optional<QRectF>());
    }

    // A window with no free rect is still owed a placement: the off-output
    // fallback brings it onto its output.
    void noValidRectStillRecordsOwed()
    {
        DesktopMoveStash stash;
        stash.stash(kWin, QStringLiteral("DP-1"), QRectF());
        stash.setOwed(kWin, true);
        bool owed = false;
        QCOMPARE(stash.takeOwedPlacement(kWin, QStringLiteral("DP-1"), &owed), std::optional<QRectF>());
        QVERIFY(owed);
    }

    // A managed arrival spends the owed placement with the entry.
    void managedArrivalClearsOwed()
    {
        DesktopMoveStash stash;
        stash.setOwed(kWin, true);
        stash.consumeForManagedArrival(kWin, QStringLiteral("DP-1"));
        QVERIFY(!stash.isOwed(kWin));
    }
};

QTEST_GUILESS_MAIN(TestDesktopMoveStash)
#include "test_desktop_move_stash.moc"
