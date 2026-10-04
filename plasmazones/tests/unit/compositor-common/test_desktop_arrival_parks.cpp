// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_desktop_arrival_parks.cpp
 * @brief The desktop-arrival park keeps the cause each window was parked for
 *        (F415): an open's continuation is never downgraded by a later
 *        re-apply park, and spending a park answers its cause once.
 */

#include <QTest>

#include "handlers/desktoparrivalparks.h"

using PlasmaZones::DesktopArrivalParks;
using Cause = DesktopArrivalParks::Cause;

class TestDesktopArrivalParks : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void anOpenContinuationWinsEitherOrder()
    {
        DesktopArrivalParks parks;
        parks.arm(QStringLiteral("a"), Cause::ReapplyOnly);
        parks.arm(QStringLiteral("a"), Cause::OpenContinuation);
        QCOMPARE(parks.take(QStringLiteral("a")), std::optional<Cause>(Cause::OpenContinuation));

        parks.arm(QStringLiteral("b"), Cause::OpenContinuation);
        parks.arm(QStringLiteral("b"), Cause::ReapplyOnly);
        QCOMPARE(parks.take(QStringLiteral("b")), std::optional<Cause>(Cause::OpenContinuation));
    }

    void takeSpendsThePark()
    {
        DesktopArrivalParks parks;
        parks.arm(QStringLiteral("a"), Cause::ReapplyOnly);
        QCOMPARE(parks.take(QStringLiteral("a")), std::optional<Cause>(Cause::ReapplyOnly));
        QVERIFY(!parks.contains(QStringLiteral("a")));
        QCOMPARE(parks.take(QStringLiteral("a")), std::nullopt);
    }

    void cancelAndClearDropParks()
    {
        DesktopArrivalParks parks;
        parks.arm(QStringLiteral("a"), Cause::ReapplyOnly);
        parks.arm(QStringLiteral("b"), Cause::OpenContinuation);
        parks.cancel(QStringLiteral("a"));
        QCOMPARE(parks.ids(), QList<QString>{QStringLiteral("b")});
        parks.clear();
        QVERIFY(parks.isEmpty());
    }

    // A zone apply on a hidden desktop parks the window to re-apply its zone
    // when the desktop is shown: its suspended client may never ack the apply,
    // and a grow is dropped for good (F491, CS).
    void hiddenZoneApplyArmsAReapplyPark()
    {
        DesktopArrivalParks parks;
        parks.onZoneApplied(QStringLiteral("a"), /*visible=*/false);
        QCOMPARE(parks.take(QStringLiteral("a")), std::optional<Cause>(Cause::ReapplyOnly));
    }

    // ...and it keeps an open's continuation an open's continuation.
    void hiddenZoneApplyKeepsAnOpenContinuation()
    {
        DesktopArrivalParks parks;
        parks.arm(QStringLiteral("a"), Cause::OpenContinuation);
        parks.onZoneApplied(QStringLiteral("a"), /*visible=*/false);
        QCOMPARE(parks.take(QStringLiteral("a")), std::optional<Cause>(Cause::OpenContinuation));
    }

    // A zone apply in view lands, so the park is done with.
    void visibleZoneApplyCancelsIt()
    {
        DesktopArrivalParks parks;
        parks.arm(QStringLiteral("a"), Cause::OpenContinuation);
        parks.onZoneApplied(QStringLiteral("a"), /*visible=*/true);
        QVERIFY(parks.isEmpty());
    }

    void anEmptyIdIsNeverParked()
    {
        DesktopArrivalParks parks;
        parks.arm(QString(), Cause::OpenContinuation);
        QVERIFY(parks.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestDesktopArrivalParks)
#include "test_desktop_arrival_parks.moc"
