// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>

#include "tilinghandler/untiledecisions.h"

using PlasmaZones::UntileDecisions::SwitchAnnounceLedger;
using PlasmaZones::UntileDecisions::untilesOnBatchScreen;

/**
 * @brief The untile diff's jurisdiction: which windows a tile batch no longer
 *        carries are untiled on its screen.
 */
class TestUntileDecisions : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // Control: a window on the batch's desktop and activity, not dragged, not
    // moving, is untiled.
    void anOrdinaryWindowIsUntiled()
    {
        QVERIFY(untilesOnBatchScreen(true, true, true, false, false));
    }

    // A window that no longer resolves is untiled, so its tracking goes.
    void anUnresolvedWindowIsUntiled()
    {
        QVERIFY(untilesOnBatchScreen(false, false, false, false, false));
    }

    // The dragged window keeps its tiled state until its drag ends (F185).
    // The old diff had no drag term.
    void aDraggedWindowIsNotUntiled()
    {
        QVERIFY(!untilesOnBatchScreen(true, true, true, /*beingDragged=*/true, false));
    }

    // A window whose output move the daemon armed from this screen belongs to
    // its destination's batch (F283).
    void aWindowMovingOffThisScreenIsNotUntiled()
    {
        QVERIFY(!untilesOnBatchScreen(true, true, true, false, /*movingOffThisScreen=*/true));
    }

    // The desktop the batch's OUTPUT shows decides, not the session's current
    // one (F211): a window on the global current desktop that the batch's
    // output is not showing is left alone, and the reverse is untiled.
    void theBatchOutputsDesktopDecides()
    {
        QVERIFY(!untilesOnBatchScreen(true, /*onDesktopShownOnBatchOutput=*/false, true, false, false));
        QVERIFY(untilesOnBatchScreen(true, /*onDesktopShownOnBatchOutput=*/true, true, false, false));
    }

    // Another activity is another context (#808).
    void anotherActivityIsNotUntiled()
    {
        QVERIFY(!untilesOnBatchScreen(true, true, /*onCurrentActivity=*/false, false, false));
    }

    // An output the effect saw switch is owed until the daemon's announce
    // (F793): a batch processed meanwhile was built for the context it left.
    void switchLedger_noteThenOwed()
    {
        SwitchAnnounceLedger ledger;
        ledger.noteSwitch(QStringLiteral("DP-1"));
        QVERIFY(ledger.isOwed(QStringLiteral("DP-1")));
    }

    // The announce settles every owed output and hands them back, so the
    // switch voids only their applies (F213), and reports that an edge was seen.
    void switchLedger_settleClears()
    {
        SwitchAnnounceLedger ledger;
        ledger.noteSwitch(QStringLiteral("DP-1"));
        bool sawEdge = false;
        QCOMPARE(ledger.settle(&sawEdge), (QSet<QString>{QStringLiteral("DP-1")}));
        QVERIFY(sawEdge);
        QVERIFY(!ledger.isOwed(QStringLiteral("DP-1")));
        QVERIFY(ledger.settle(&sawEdge).isEmpty());
        QVERIFY(!sawEdge);
    }

    // A switch on one output leaves another output's batches their say.
    void switchLedger_otherOutputNotOwed()
    {
        SwitchAnnounceLedger ledger;
        ledger.noteSwitch(QStringLiteral("DP-1"));
        QVERIFY(!ledger.isOwed(QStringLiteral("HDMI-A-1")));
    }

    // A switch on an output with no managed screen owes nothing, but the
    // announce it causes still voids no other output's cascade (F213).
    void switchLedger_edgeWithoutOwedOutput()
    {
        SwitchAnnounceLedger ledger;
        ledger.noteEdge();
        bool sawEdge = false;
        QVERIFY(ledger.settle(&sawEdge).isEmpty());
        QVERIFY(sawEdge);
    }
};

QTEST_GUILESS_MAIN(TestUntileDecisions)
#include "test_untile_decisions.moc"
