// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>

#include "handlers/instantrestoredecisions.h"

using PlasmaZones::CachedSnapRestore;
using PlasmaZones::InstantRestoreDecisions::belowMinimumSize;
using PlasmaZones::InstantRestoreDecisions::pickEntry;

/**
 * @brief The instant snap-restore pick: restores happen only where a window
 *        opens. An opener applies the newest entry saved on its own output,
 *        never one on another monitor, and nothing on an engine-managed
 *        screen. A window under the minimum size takes none (F735).
 */
class TestInstantRestoreDecisions : public QObject
{
    Q_OBJECT

    static CachedSnapRestore entry(const QString& screenId, const QRect& geometry = QRect(0, 0, 100, 100))
    {
        return CachedSnapRestore{geometry, screenId};
    }

    static bool noneManaged(const QString&)
    {
        return false;
    }

private Q_SLOTS:
    void testEntryOnAnotherOutputIsNeverApplied()
    {
        QCOMPARE(pickEntry({entry(QStringLiteral("DP-2"))}, QStringLiteral("DP-1"), false, noneManaged), -1);
    }

    void testNewestEntryOnTheOpenersOutputWins()
    {
        const QList<CachedSnapRestore> newestFirst{entry(QStringLiteral("DP-2")), entry(QStringLiteral("DP-1")),
                                                   entry(QStringLiteral("DP-1"), QRect(5, 5, 50, 50))};
        QCOMPARE(pickEntry(newestFirst, QStringLiteral("DP-1"), false, noneManaged), 1);
        QCOMPARE(pickEntry(newestFirst, QStringLiteral("DP-2"), false, noneManaged), 0);
    }

    void testManagedOpenerTakesNothing()
    {
        QCOMPARE(pickEntry({entry(QStringLiteral("DP-1"))}, QStringLiteral("DP-1"), true, noneManaged), -1);
    }

    void testManagedSavedScreenIsSkipped()
    {
        const auto managedVs0 = [](const QString& screenId) {
            return screenId == QStringLiteral("DP-1/vs:0");
        };
        const QList<CachedSnapRestore> newestFirst{entry(QStringLiteral("DP-1/vs:0")),
                                                   entry(QStringLiteral("DP-1/vs:1"))};
        QCOMPARE(pickEntry(newestFirst, QStringLiteral("DP-1"), false, managedVs0), 1);
    }

    // A split monitor: an entry on another virtual screen of the opener's own
    // output is on its output.
    void testVirtualScreenOfTheSameOutputQualifies()
    {
        QCOMPARE(pickEntry({entry(QStringLiteral("DP-1/vs:1"))}, QStringLiteral("DP-1"), false, noneManaged), 0);
    }

    void testInvalidEntriesAndUnknownOpenerAreSkipped()
    {
        const QList<CachedSnapRestore> broken{entry(QStringLiteral("DP-1"), QRect()), entry(QString())};
        QCOMPARE(pickEntry(broken, QStringLiteral("DP-1"), false, noneManaged), -1);
        QCOMPARE(pickEntry({entry(QStringLiteral("DP-1"))}, QString(), false, noneManaged), -1);
    }

    // A window under the minimum size on either axis takes no cached restore,
    // as the snap engine's exclusion refuses it a remembered zone (F735).
    void testBelowMinimumSizeOnEitherAxis()
    {
        QVERIFY(belowMinimumSize(QSizeF(150, 400), 200, 150));
        QVERIFY(belowMinimumSize(QSizeF(400, 100), 200, 150));
        QVERIFY(!belowMinimumSize(QSizeF(200, 150), 200, 150));
    }

    // A zero limit turns that axis off.
    void testZeroMinimumIsNoLimit()
    {
        QVERIFY(!belowMinimumSize(QSizeF(10, 10), 0, 0));
        QVERIFY(belowMinimumSize(QSizeF(10, 400), 200, 0));
    }

    // The open suppression's settle rule (F759): released when the window
    // leaves the spawn point, or reaches the size it was sent to there, never
    // on a size the client chose itself.
    void suppressionSettled_data()
    {
        QTest::addColumn<QRect>("spawn");
        QTest::addColumn<QRect>("target");
        QTest::addColumn<QRect>("live");
        QTest::addColumn<bool>("settled");
        const QRect spawn(700, 400, 500, 400);
        QTest::newRow("moved") << spawn << QRect(0, 0, 960, 1080) << QRect(0, 0, 500, 400) << true;
        QTest::newRow("clientOwnSize") << spawn << QRect(0, 0, 960, 1080) << QRect(700, 400, 900, 700) << false;
        QTest::newRow("sizedToTarget") << spawn << QRect(700, 400, 960, 1080) << QRect(700, 400, 960, 1080) << true;
        QTest::newRow("fractionalJitter") << spawn << QRect(700, 400, 960, 1080) << QRect(700, 400, 959, 1081) << true;
        QTest::newRow("unchanged") << spawn << QRect(0, 0, 960, 1080) << spawn << false;
        QTest::newRow("unchangedAtTargetSize") << spawn << QRect(0, 0, 500, 400) << spawn << false;
    }

    void suppressionSettled()
    {
        QFETCH(QRect, spawn);
        QFETCH(QRect, target);
        QFETCH(QRect, live);
        QFETCH(bool, settled);
        QCOMPARE(PlasmaZones::InstantRestoreDecisions::suppressionSettled(spawn, target, live), settled);
    }
};

QTEST_GUILESS_MAIN(TestInstantRestoreDecisions)
#include "test_instant_restore_decisions.moc"
