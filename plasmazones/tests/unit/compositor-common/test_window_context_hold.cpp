// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_window_context_hold.cpp
 * @brief Why a window's desktop or activity edit waits for the move that
 *        caused it (EE): the drag subject until its drop is settled, a
 *        visible window whose move to another output the client has not
 *        committed yet, and a window whose crossing waits for the
 *        screen-change settle. Each row pins one condition.
 */

#include <QTest>

#include "plasmazoneseffect/windowcontexthold.h"

using namespace PlasmaZones::WindowContextHold;

class TestWindowContextHold : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // F622: KWin re-desks the dragged window mid-drag, and the edit waits for
    // the drop's outcome whatever else is true.
    void dragLatchHolds()
    {
        Inputs in;
        in.dragLatched = true;
        QCOMPARE(int(reasonsFor(in)), int(DragSubject));
    }

    // F650: a Move to Screen that resizes re-desks before the client acks,
    // and a window seen where it was is held until the ack.
    void visibleInFlightHolds()
    {
        Inputs in;
        in.outputInFlight = true;
        in.inViewByStamp = true;
        QCOMPARE(int(reasonsFor(in)), int(OutputInFlight));
    }

    // F711, F687: a window already hidden when the move began never acks a
    // grow, so it is not held.
    void hiddenInFlightDoesNotHold()
    {
        Inputs in;
        in.outputInFlight = true;
        in.inViewByStamp = false;
        QCOMPARE(int(reasonsFor(in)), 0);
    }

    // F666: inside a screen change the crossing is deferred to the settle,
    // and the edit waits with it.
    void settlePendingHolds()
    {
        Inputs in;
        in.crossingAwaitsSettle = true;
        QCOMPARE(int(reasonsFor(in)), int(SettlePending));
    }

    // An edit with no condition runs at once; being in view alone holds
    // nothing.
    void noConditionNoHold()
    {
        QCOMPARE(int(reasonsFor(Inputs{})), 0);
        Inputs in;
        in.inViewByStamp = true;
        QCOMPARE(int(reasonsFor(in)), 0);
    }
};

QTEST_GUILESS_MAIN(TestWindowContextHold)
#include "test_window_context_hold.moc"
