// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>

#include "handlers/instantrestoredecisions.h"

using PlasmaZones::CachedSnapRestore;
using PlasmaZones::InstantRestoreDecisions::pickEntry;

/**
 * @brief The instant snap-restore pick: restores happen only where a window
 *        opens. An opener applies the newest entry saved on its own output,
 *        never one on another monitor, and nothing on an engine-managed
 *        screen.
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
};

QTEST_GUILESS_MAIN(TestInstantRestoreDecisions)
#include "test_instant_restore_decisions.moc"
