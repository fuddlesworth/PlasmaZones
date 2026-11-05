// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_window_context_hold.cpp
 * @brief Why a window's desktop or activity edit waits for the move that
 *        caused it (EE): the drag subject until its drop is settled, a
 *        visible window whose move to another output the client has not
 *        committed yet, and a window whose crossing waits for the
 *        screen-change settle. Each row pins one condition. Also the desktop
 *        set PlasmaZones' own move to another output re-desks the window to
 *        (pzReDesk, F161, F693, F719).
 */

#include <QStringList>
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

    // F161: PlasmaZones' own move to another output swaps the desktop the
    // source showed for the one the destination shows, as KWin's Move to
    // Screen does, and keeps the window's other desktops.
    void reDeskSwapsSourceForDestination()
    {
        const auto single = pzReDesk(QStringList{dA()}, dA(), dB(), /*visibleOnSource=*/true, /*pinned=*/false);
        QVERIFY(single.has_value());
        QCOMPARE(*single, QStringList{dB()});
        const auto several = pzReDesk(QStringList{dA(), dC()}, dA(), dB(), /*visibleOnSource=*/true, /*pinned=*/false);
        QVERIFY(several.has_value());
        QCOMPARE(*several, (QStringList{dC(), dB()}));
    }

    // F693: a window already hidden on its source stays on its desktops.
    void hiddenOnSourceIsNotReDesked()
    {
        QVERIFY(!pzReDesk(QStringList{dA()}, dA(), dB(), /*visibleOnSource=*/false, /*pinned=*/false));
    }

    // F719: a placement that routed the window to a desktop keeps it there.
    void pinnedDesktopKept()
    {
        QVERIFY(!pzReDesk(QStringList{dA()}, dA(), dB(), /*visibleOnSource=*/true, /*pinned=*/true));
    }

    void alreadyOnDestinationUnchanged()
    {
        QVERIFY(!pzReDesk(QStringList{dA(), dB()}, dA(), dB(), /*visibleOnSource=*/true, /*pinned=*/false));
        QVERIFY(!pzReDesk(QStringList{dB()}, dA(), dB(), /*visibleOnSource=*/true, /*pinned=*/false));
    }

    // A window on every desktop (the empty set) is in view on any output.
    void stickyUnchanged()
    {
        QVERIFY(!pzReDesk(QStringList{}, dA(), dB(), /*visibleOnSource=*/true, /*pinned=*/false));
    }

private:
    static QString dA()
    {
        return QStringLiteral("desktop-a");
    }
    static QString dB()
    {
        return QStringLiteral("desktop-b");
    }
    static QString dC()
    {
        return QStringLiteral("desktop-c");
    }
};

QTEST_GUILESS_MAIN(TestWindowContextHold)
#include "test_window_context_hold.moc"
