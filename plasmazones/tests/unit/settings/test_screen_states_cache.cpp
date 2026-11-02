// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ScreenStatesCache: the bookkeeping SettingsController::refreshScreenStates
// hands each getScreenStates reply to. Coalescing, request numbering, the
// failed-call arm and the publish-on-difference rule, driven without D-Bus.

#include "settings/controller/screenstatescache.h"

#include <QSignalSpy>
#include <QTest>

using namespace PlasmaZones;

namespace {

/// A one-screen getScreenStates answer on @p desktop.
QVariantList statesOnDesktop(int desktop)
{
    return {
        QVariantMap{{QStringLiteral("screenId"), QStringLiteral("DP-1")}, {QStringLiteral("virtualDesktop"), desktop}}};
}

} // namespace

class TestScreenStatesCache : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void scheduleCoalescesABurstIntoOneRefresh();
    void staleReplyIsDropped();
    void failedReplyKeepsTheLastAnswer();
    void publishesOnlyOnADifference();
};

void TestScreenStatesCache::scheduleCoalescesABurstIntoOneRefresh()
{
    ScreenStatesCache cache(10);
    QSignalSpy due(&cache, &ScreenStatesCache::refreshDue);
    cache.schedule();
    cache.schedule();
    cache.schedule();
    QCOMPARE(due.count(), 0);
    QTRY_COMPARE(due.count(), 1);
    // Single-shot: nothing more without another schedule().
    QTest::qWait(50);
    QCOMPARE(due.count(), 1);
}

void TestScreenStatesCache::staleReplyIsDropped()
{
    ScreenStatesCache cache(10);
    QSignalSpy changed(&cache, &ScreenStatesCache::statesChanged);

    // The older reply lands first, while a newer request is in flight.
    const quint64 older = cache.beginRequest();
    const quint64 newer = cache.beginRequest();
    QVERIFY(!cache.finishRequest(older, true, statesOnDesktop(1)));
    QVERIFY(cache.states().isEmpty());
    QCOMPARE(changed.count(), 0);

    QVERIFY(cache.finishRequest(newer, true, statesOnDesktop(2)));
    QCOMPARE(cache.states(), statesOnDesktop(2));
    QCOMPARE(changed.count(), 1);

    // The older reply lands after the newer one and must not overwrite it.
    const quint64 slow = cache.beginRequest();
    const quint64 fast = cache.beginRequest();
    QVERIFY(cache.finishRequest(fast, true, statesOnDesktop(3)));
    QVERIFY(!cache.finishRequest(slow, true, statesOnDesktop(4)));
    QCOMPARE(cache.states(), statesOnDesktop(3));
    QCOMPARE(changed.count(), 2);
}

void TestScreenStatesCache::failedReplyKeepsTheLastAnswer()
{
    ScreenStatesCache cache(10);
    QSignalSpy changed(&cache, &ScreenStatesCache::statesChanged);

    QVERIFY(cache.finishRequest(cache.beginRequest(), true, statesOnDesktop(2)));
    QCOMPARE(changed.count(), 1);

    // The caller passes an empty list with a failed reply. It is not an
    // answer, so the stored states stand and nothing is published.
    QVERIFY(!cache.finishRequest(cache.beginRequest(), false, QVariantList()));
    QCOMPARE(cache.states(), statesOnDesktop(2));
    QCOMPARE(changed.count(), 1);
}

void TestScreenStatesCache::publishesOnlyOnADifference()
{
    ScreenStatesCache cache(10);
    QSignalSpy changed(&cache, &ScreenStatesCache::statesChanged);

    // The construction-time seed is stored without a publish.
    cache.seed(statesOnDesktop(1));
    QCOMPARE(cache.states(), statesOnDesktop(1));
    QCOMPARE(changed.count(), 0);

    // An answer equal to the stored one publishes nothing.
    QVERIFY(!cache.finishRequest(cache.beginRequest(), true, statesOnDesktop(1)));
    QCOMPARE(changed.count(), 0);

    QVERIFY(cache.finishRequest(cache.beginRequest(), true, statesOnDesktop(2)));
    QCOMPARE(cache.states(), statesOnDesktop(2));
    QCOMPARE(changed.count(), 1);

    // An empty answer from a successful call is a real answer.
    QVERIFY(cache.finishRequest(cache.beginRequest(), true, QVariantList()));
    QVERIFY(cache.states().isEmpty());
    QCOMPARE(changed.count(), 2);
}

QTEST_GUILESS_MAIN(TestScreenStatesCache)
#include "test_screen_states_cache.moc"
