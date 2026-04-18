// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/StaggerTimer.h>

#include <QCoreApplication>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

class TestStaggerTimer : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testZeroCountFiresOnComplete()
    {
        int onCompleteCount = 0;
        PhosphorAnimation::applyStaggeredOrImmediate(
            this, 0, 1, 30,
            [](int) {
                QFAIL("applyFn should not be called for count=0");
            },
            [&onCompleteCount]() {
                ++onCompleteCount;
            });
        QCOMPARE(onCompleteCount, 1);
    }

    void testImmediateModeCallsAllSynchronously()
    {
        QVector<int> calls;
        int completions = 0;
        PhosphorAnimation::applyStaggeredOrImmediate(
            this, 5, 0 /*sequenceMode=all-at-once*/, 30,
            [&calls](int i) {
                calls.append(i);
            },
            [&completions]() {
                ++completions;
            });
        QCOMPARE(calls, QVector<int>({0, 1, 2, 3, 4}));
        QCOMPARE(completions, 1);
    }

    void testStaggerModeDefersSubsequentCalls()
    {
        // First call runs synchronously; later calls fire via QTimer.
        QVector<int> calls;
        int completions = 0;
        PhosphorAnimation::applyStaggeredOrImmediate(
            this, 3, 1 /*sequenceMode=one-by-one*/, 10,
            [&calls](int i) {
                calls.append(i);
            },
            [&completions]() {
                ++completions;
            });

        // First call fires immediately.
        QCOMPARE(calls.size(), 1);
        QCOMPARE(calls.at(0), 0);
        QCOMPARE(completions, 0);

        // Process the pending timers.
        QTest::qWait(100);

        QCOMPARE(calls, QVector<int>({0, 1, 2}));
        QCOMPARE(completions, 1);
    }

    void testSingleItemSkipsStaggerPath()
    {
        QVector<int> calls;
        int completions = 0;
        PhosphorAnimation::applyStaggeredOrImmediate(
            this, 1, 1, 1000 /*1s delay — would timeout if we hit the stagger branch*/,
            [&calls](int i) {
                calls.append(i);
            },
            [&completions]() {
                ++completions;
            });
        // count==1 always goes through the synchronous path.
        QCOMPARE(calls, QVector<int>({0}));
        QCOMPARE(completions, 1);
    }

    void testNullParentFallsBackToSynchronous()
    {
        // parent==nullptr disables stagger (would crash QTimer otherwise).
        QVector<int> calls;
        PhosphorAnimation::applyStaggeredOrImmediate(
            nullptr, 3, 1, 10,
            [&calls](int i) {
                calls.append(i);
            },
            nullptr);
        QCOMPARE(calls, QVector<int>({0, 1, 2}));
    }

    void testParentDestroyedCancelsPendingFires()
    {
        // Scope-own parent so the cascade's QTimer context is destroyed
        // after the first call. Remaining timers must not fire.
        auto* parent = new QObject();
        QVector<int> calls;
        PhosphorAnimation::applyStaggeredOrImmediate(
            parent, 5, 1, 50,
            [&calls](int i) {
                calls.append(i);
            },
            nullptr);

        // Destroy parent before any timers fire.
        delete parent;
        QTest::qWait(300);

        // Only the synchronous first call should have run.
        QCOMPARE(calls.size(), 1);
        QCOMPARE(calls.at(0), 0);
    }
};

QTEST_MAIN(TestStaggerTimer)
#include "test_staggertimer.moc"
