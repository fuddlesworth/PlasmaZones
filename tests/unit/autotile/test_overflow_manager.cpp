// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Qt headers
#include <QTest>

// Project headers
#include "autotile/OverflowManager.h"

using namespace PlasmaZones;

static const QString kScreen1 = QStringLiteral("Screen1");
static const QString kScreen2 = QStringLiteral("Screen2");
static const QString kWin1 = QStringLiteral("win-1");
static const QString kWin2 = QStringLiteral("win-2");
static const QString kWin3 = QStringLiteral("win-3");
static const QString kWin4 = QStringLiteral("win-4");

class TestOverflowManager : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testMarkAndClear()
    {
        OverflowManager mgr;
        mgr.markOverflow(kWin1, kScreen1);

        QVERIFY(mgr.isOverflow(kWin1));
        QVERIFY(!mgr.isEmpty());

        mgr.clearOverflow(kWin1);

        QVERIFY(!mgr.isOverflow(kWin1));
        QVERIFY(mgr.isEmpty());
    }

    void testIsOverflow_unknownWindow()
    {
        OverflowManager mgr;
        QVERIFY(!mgr.isOverflow(QStringLiteral("nonexistent")));
    }

    void testMarkOverflow_emptyInputsIgnored()
    {
        OverflowManager mgr;
        // Empty windowId or screenName should be a no-op
        mgr.markOverflow(QString(), kScreen1);
        QVERIFY(mgr.isEmpty());

        mgr.markOverflow(kWin1, QString());
        QVERIFY(mgr.isEmpty());

        mgr.markOverflow(QString(), QString());
        QVERIFY(mgr.isEmpty());
    }

    void testApplyOverflow_tracksExcessWindows()
    {
        OverflowManager mgr;
        QStringList windows = {kWin1, kWin2, kWin3};

        // 2 zones means window at index 2 overflows
        QStringList overflowed = mgr.applyOverflow(kScreen1, windows, 2);

        QCOMPARE(overflowed.size(), 1);
        QCOMPARE(overflowed.first(), kWin3);
        QVERIFY(mgr.isOverflow(kWin3));
        // State mutation is NOT done by OverflowManager — caller does it
    }

    void testApplyOverflow_skipsAlreadyOverflowed()
    {
        OverflowManager mgr;
        QStringList windows = {kWin1, kWin2, kWin3};

        // First apply
        QStringList first = mgr.applyOverflow(kScreen1, windows, 2);
        QCOMPARE(first.size(), 1);

        // Calling again should not re-track the same window
        QStringList second = mgr.applyOverflow(kScreen1, windows, 2);
        QCOMPARE(second.size(), 0);
    }

    void testApplyOverflow_emptyScreenNameIgnored()
    {
        OverflowManager mgr;
        QStringList windows = {kWin1, kWin2, kWin3};

        QStringList overflowed = mgr.applyOverflow(QString(), windows, 2);
        QCOMPARE(overflowed.size(), 0);
        QVERIFY(mgr.isEmpty());
    }

    void testRecoverIfRoom_basicRecovery()
    {
        OverflowManager mgr;
        mgr.markOverflow(kWin2, kScreen1);

        // tiledCount=1, maxWindows=2 → room for 1
        // win-2 is floating and in state
        QStringList recovered = mgr.recoverIfRoom(
            kScreen1, /*tiledCount=*/1, /*maxWindows=*/2,
            [](const QString& wid) {
                return wid == kWin2;
            }, // isFloating
            [](const QString&) {
                return true;
            }); // containsWindow

        QCOMPARE(recovered.size(), 1);
        QCOMPARE(recovered.first(), kWin2);
        QVERIFY(!mgr.isOverflow(kWin2));
    }

    void testRecoverIfRoom_noRoom()
    {
        OverflowManager mgr;
        mgr.markOverflow(kWin2, kScreen1);

        // tiledCount=1, maxWindows=1 → no room
        QStringList recovered = mgr.recoverIfRoom(
            kScreen1, 1, 1,
            [](const QString&) {
                return true;
            },
            [](const QString&) {
                return true;
            });

        QCOMPARE(recovered.size(), 0);
        QVERIFY(mgr.isOverflow(kWin2));
    }

    void testRecoverIfRoom_partialRoom()
    {
        OverflowManager mgr;
        mgr.markOverflow(kWin2, kScreen1);
        mgr.markOverflow(kWin3, kScreen1);

        // tiledCount=1, maxWindows=2 → room for 1 of 2
        QStringList recovered = mgr.recoverIfRoom(
            kScreen1, 1, 2,
            [](const QString&) {
                return true;
            },
            [](const QString&) {
                return true;
            });

        QCOMPARE(recovered.size(), 1);
        // Verify the recovered window is one of the two overflow windows
        QVERIFY2(recovered.first() == kWin2 || recovered.first() == kWin3,
                 qPrintable(QStringLiteral("Recovered window '%1' is neither win-2 nor win-3").arg(recovered.first())));
        // Verify exactly one window remains in overflow
        QVERIFY(mgr.isOverflow(recovered.first() == kWin2 ? kWin3 : kWin2));
    }

    void testRecoverIfRoom_purgesStaleEntries()
    {
        OverflowManager mgr;
        // Mark win-2 as overflow but it's NOT in the state at all
        mgr.markOverflow(kWin2, kScreen1);

        // containsWindow returns false for win-2 — stale, should be purged
        QStringList recovered = mgr.recoverIfRoom(
            kScreen1, 1, 3,
            [](const QString&) {
                return false;
            },
            [](const QString&) {
                return false;
            });

        QCOMPARE(recovered.size(), 0);
        QVERIFY(!mgr.isOverflow(kWin2));
        QVERIFY(mgr.isEmpty());
    }

    void testRecoverIfRoom_purgesExternallyUnfloated()
    {
        OverflowManager mgr;
        // Window is in PhosphorTiles::TilingState but NOT floating (externally unfloated)
        mgr.markOverflow(kWin2, kScreen1);

        QStringList recovered = mgr.recoverIfRoom(
            kScreen1, 1, 3,
            [](const QString&) {
                return false;
            }, // not floating
            [](const QString&) {
                return true;
            }); // but in state

        // Should be purged as stale (tiled but still overflow-tracked)
        QCOMPARE(recovered.size(), 0);
        QVERIFY(!mgr.isOverflow(kWin2));
        QVERIFY(mgr.isEmpty());
    }

    void testRecoverIfRoom_roundTrip()
    {
        OverflowManager mgr;
        QStringList windows = {kWin1, kWin2, kWin3, kWin4};

        // Apply: overflow win-3 and win-4 (tileCount=2)
        QStringList overflowed = mgr.applyOverflow(kScreen1, windows, 2);
        QCOMPARE(overflowed.size(), 2);
        QVERIFY(mgr.isOverflow(kWin3));
        QVERIFY(mgr.isOverflow(kWin4));

        // Recover with room for 1: should get 1 back
        QSet<QString> floatingSet = {kWin3, kWin4};
        QStringList recovered1 = mgr.recoverIfRoom(
            kScreen1, 2, 3,
            [&floatingSet](const QString& wid) {
                return floatingSet.contains(wid);
            },
            [](const QString&) {
                return true;
            });
        QCOMPARE(recovered1.size(), 1);
        floatingSet.remove(recovered1.first());

        // Recover with room for 1 more: should get the other
        QStringList recovered2 = mgr.recoverIfRoom(
            kScreen1, 3, 4,
            [&floatingSet](const QString& wid) {
                return floatingSet.contains(wid);
            },
            [](const QString&) {
                return true;
            });
        QCOMPARE(recovered2.size(), 1);

        QVERIFY(mgr.isEmpty());
    }

    void testTakeForScreen_removesAllEntries()
    {
        OverflowManager mgr;
        mgr.markOverflow(kWin1, kScreen1);
        mgr.markOverflow(kWin2, kScreen1);
        mgr.markOverflow(kWin3, kScreen2);

        QSet<QString> taken = mgr.takeForScreen(kScreen1);

        QCOMPARE(taken.size(), 2);
        QVERIFY(taken.contains(kWin1));
        QVERIFY(taken.contains(kWin2));
        QVERIFY(!mgr.isOverflow(kWin1));
        QVERIFY(!mgr.isOverflow(kWin2));
        QVERIFY(mgr.isOverflow(kWin3));
    }

    void testMigrateWindow_clearsOverflow()
    {
        OverflowManager mgr;
        mgr.markOverflow(kWin1, kScreen1);

        mgr.migrateWindow(kWin1);

        // After migration, overflow status is cleared (window re-enters normal flow)
        QVERIFY(!mgr.isOverflow(kWin1));
        QVERIFY(mgr.isEmpty());
    }

    void testMigrateWindow_nonOverflowNoop()
    {
        OverflowManager mgr;
        // Migrating a window that isn't overflow should be a no-op
        mgr.migrateWindow(kWin1);
        QVERIFY(mgr.isEmpty());
    }

    void testMigrateWindow_usesReverseIndex()
    {
        OverflowManager mgr;
        // Mark on Screen1 — the internal reverse index tracks the screen
        mgr.markOverflow(kWin1, kScreen1);

        // migrateWindow uses the reverse index, not a caller-supplied screen
        mgr.migrateWindow(kWin1);

        QVERIFY(!mgr.isOverflow(kWin1));
        // Screen1 set should be empty (no ghost entries)
        QSet<QString> taken = mgr.takeForScreen(kScreen1);
        QCOMPARE(taken.size(), 0);
    }

    void testMarkOverflow_crossScreenConsistency()
    {
        OverflowManager mgr;
        // Mark on Screen1, then re-mark on Screen2 — old entry should be cleaned
        mgr.markOverflow(kWin1, kScreen1);
        QVERIFY(mgr.isOverflow(kWin1));

        mgr.markOverflow(kWin1, kScreen2);
        QVERIFY(mgr.isOverflow(kWin1));

        // takeForScreen("Screen1") should return empty — the old entry was cleaned
        QSet<QString> screen1 = mgr.takeForScreen(kScreen1);
        QCOMPARE(screen1.size(), 0);

        // The window should still be tracked under Screen2
        QVERIFY(mgr.isOverflow(kWin1));
        QSet<QString> screen2 = mgr.takeForScreen(kScreen2);
        QCOMPARE(screen2.size(), 1);
        QVERIFY(screen2.contains(kWin1));
    }

    void testClearForRemovedScreens()
    {
        OverflowManager mgr;
        mgr.markOverflow(kWin1, kScreen1);
        mgr.markOverflow(kWin2, kScreen1);
        mgr.markOverflow(kWin3, kScreen2);

        // Only Screen2 remains active
        mgr.clearForRemovedScreens({kScreen2});

        QVERIFY(!mgr.isOverflow(kWin1));
        QVERIFY(!mgr.isOverflow(kWin2));
        QVERIFY(mgr.isOverflow(kWin3));
    }

    void testClearForRemovedScreens_allActive()
    {
        OverflowManager mgr;
        mgr.markOverflow(kWin1, kScreen1);
        mgr.markOverflow(kWin2, kScreen2);

        // Both screens active — nothing should be removed
        mgr.clearForRemovedScreens({kScreen1, kScreen2});

        QVERIFY(mgr.isOverflow(kWin1));
        QVERIFY(mgr.isOverflow(kWin2));
    }

    void testClearOverflow_unknownWindow()
    {
        OverflowManager mgr;
        // Should not crash
        mgr.clearOverflow(QStringLiteral("nonexistent"));
        QVERIFY(mgr.isEmpty());
    }
};

QTEST_MAIN(TestOverflowManager)
#include "test_overflow_manager.moc"
