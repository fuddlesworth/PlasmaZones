// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>

#include "plasmazoneseffect/placementstatement.h"

using PlasmaZones::PlacementStatement::fullscreenBails;

/**
 * @brief The pure placement-statement decisions: what a placement does to a
 *        window's KWin fullscreen and maximize state.
 */
class TestPlacementStatement : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // A window that requested fullscreen is fullscreen before the client
    // commits it: the apply bails (F496). The old gate tested the committed
    // bit first and moveResized a window on its way into fullscreen.
    void aRequestedFullscreenBailsBeforeTheClientCommits()
    {
        QVERIFY(fullscreenBails(/*hasKWinWindow=*/true, /*committed=*/false, /*requested=*/true, /*member=*/false));
    }

    // The windowed-fullscreen exit: requested off while the committed bit
    // drains. The restoring rect must land.
    void aRequestedExitDoesNotBail()
    {
        QVERIFY(!fullscreenBails(true, /*committed=*/true, /*requested=*/false, false));
    }

    // A windowed-fullscreen member holds fullscreen at its column rect on
    // purpose: its applies never bail.
    void aWindowedFullscreenMemberDoesNotBail()
    {
        QVERIFY(!fullscreenBails(true, true, true, /*member=*/true));
    }

    // Without a KWin window the committed bit decides.
    void withoutAKWinWindowTheCommittedStateDecides()
    {
        QVERIFY(fullscreenBails(/*hasKWinWindow=*/false, /*committed=*/true, /*requested=*/false, false));
        QVERIFY(!fullscreenBails(false, false, true, false));
    }
};

QTEST_GUILESS_MAIN(TestPlacementStatement)
#include "test_placement_statement.moc"
