// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>

#include "tilinghandler/untiledecisions.h"

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
};

QTEST_GUILESS_MAIN(TestUntileDecisions)
#include "test_untile_decisions.moc"
