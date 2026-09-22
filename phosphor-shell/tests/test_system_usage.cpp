// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for SystemUsage's /proc parsing. This is the arithmetic that
// used to live in QML and was moved to C++ precisely so it could be pinned:
// the jiffy accounting, the busy-rate delta arithmetic (including the
// no-usable-rate sentinel and both clamps), the malformed-layout
// rejections, the interval floor, and the change-notification guards. The
// parse and rate entry points are static and take values, so no filesystem
// or timer is involved.

#include <PhosphorShell/SystemUsage.h>

#include <QByteArray>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

using PhosphorShell::SystemUsage;

class TestSystemUsage : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void cpuTotalsSumBusyAndIdle();
    void cpuTotalsExcludeGuestColumns();
    void cpuTotalsAcceptAMinimalLine();
    void cpuTotalsRejectMalformed_data();
    void cpuTotalsRejectMalformed();
    void memoryTotalsParseBothFields();
    void memoryTotalsTolerateExtraSpacing();
    void memoryTotalsRejectMissingAvailable();
    void intervalIsClampedToTheFloor();
    void cpuPercentBetweenComputesTheBusyFraction_data();
    void cpuPercentBetweenComputesTheBusyFraction();
    void cpuPercentBetweenRejectsUnusablePairs_data();
    void cpuPercentBetweenRejectsUnusablePairs();
    void repeatedSamplesDoNotReEmit();
    void reEnablingPublishesNoRate();
};

void TestSystemUsage::cpuTotalsSumBusyAndIdle()
{
    // user nice system idle iowait irq softirq steal
    const QByteArray body = "cpu  100 20 30 700 40 5 5 0\ncpu0 1 2 3 4\n";
    SystemUsage::CpuTotals totals;
    QVERIFY(SystemUsage::parseCpuTotals(body, &totals));

    // idle is idle + iowait; total is every counted column.
    QCOMPARE(totals.idle, 740.0);
    QCOMPARE(totals.total, 900.0);
}

void TestSystemUsage::cpuTotalsExcludeGuestColumns()
{
    // The kernel already counts guest inside user and guest_nice inside
    // nice, so summing those trailing columns again would inflate the total
    // and understate busy% on a VM host. Same counters as the case above
    // plus guest columns: the totals must not move.
    const QByteArray withGuest = "cpu  100 20 30 700 40 5 5 0 60 7\n";
    SystemUsage::CpuTotals totals;
    QVERIFY(SystemUsage::parseCpuTotals(withGuest, &totals));

    QCOMPARE(totals.total, 900.0);
    QCOMPARE(totals.idle, 740.0);
}

void TestSystemUsage::cpuTotalsAcceptAMinimalLine()
{
    // user nice system idle only: the shortest layout that still yields a
    // rate (no iowait column).
    const QByteArray body = "cpu  10 10 10 70\n";
    SystemUsage::CpuTotals totals;
    QVERIFY(SystemUsage::parseCpuTotals(body, &totals));

    QCOMPARE(totals.total, 100.0);
    QCOMPARE(totals.idle, 70.0);
}

void TestSystemUsage::cpuTotalsRejectMalformed_data()
{
    QTest::addColumn<QByteArray>("body");

    QTest::newRow("empty") << QByteArray();
    QTest::newRow("no cpu line") << QByteArray("intr 1 2 3\n");
    QTest::newRow("too few fields") << QByteArray("cpu  1 2 3\n");
    QTest::newRow("non-numeric field") << QByteArray("cpu  1 2 three 4 5\n");
    // toDouble accepts these tokens, so they have to be rejected as
    // non-finite rather than propagated into the delta.
    QTest::newRow("nan field") << QByteArray("cpu  1 2 nan 4 5\n");
    QTest::newRow("inf field") << QByteArray("cpu  1 2 inf 4 5\n");
    // A prefix match must not be mistaken for the aggregate line.
    QTest::newRow("per-core line only") << QByteArray("cpu0 1 2 3 4 5\n");
}

void TestSystemUsage::cpuTotalsRejectMalformed()
{
    QFETCH(QByteArray, body);

    SystemUsage::CpuTotals totals;
    totals.total = 12345;
    totals.idle = 678;

    QVERIFY(!SystemUsage::parseCpuTotals(body, &totals));
    // A rejected parse must leave the caller's snapshot untouched, so the
    // previous baseline survives a transient bad read.
    QCOMPARE(totals.total, 12345.0);
    QCOMPARE(totals.idle, 678.0);
}

void TestSystemUsage::memoryTotalsParseBothFields()
{
    const QByteArray body =
        "MemTotal:       16311472 kB\n"
        "MemFree:         1000000 kB\n"
        "MemAvailable:    8155736 kB\n";
    SystemUsage::MemoryTotals totals;
    QVERIFY(SystemUsage::parseMemoryTotals(body, &totals));

    QCOMPARE(totals.totalKb, 16311472.0);
    QCOMPARE(totals.availableKb, 8155736.0);
}

void TestSystemUsage::memoryTotalsTolerateExtraSpacing()
{
    // Leading whitespace and irregular inner spacing must not shift which
    // field is read.
    const QByteArray body =
        "   MemTotal:  1000 kB\n"
        "  MemAvailable:      250 kB\n";
    SystemUsage::MemoryTotals totals;
    QVERIFY(SystemUsage::parseMemoryTotals(body, &totals));

    QCOMPARE(totals.totalKb, 1000.0);
    QCOMPARE(totals.availableKb, 250.0);
}

void TestSystemUsage::memoryTotalsRejectMissingAvailable()
{
    // A kernel without MemAvailable would otherwise leave available at 0
    // and report a bogus 100% used.
    const QByteArray body = "MemTotal:  1000 kB\nMemFree:  400 kB\n";
    SystemUsage::MemoryTotals totals;
    QVERIFY(!SystemUsage::parseMemoryTotals(body, &totals));

    // A zero or absent total is equally unusable.
    const QByteArray zeroTotal = "MemTotal:  0 kB\nMemAvailable:  0 kB\n";
    QVERIFY(!SystemUsage::parseMemoryTotals(zeroTotal, &totals));

    // Order dependence, pinned as a CONTRACT: the scan stops at
    // MemAvailable, so an input carrying it BEFORE MemTotal is rejected
    // even though both fields are present. Real /proc/meminfo always
    // orders MemTotal first (the kernel emits it first), so accepting the
    // reversed order would only ever legitimise synthetic input; if the
    // kernel ever reorders, this case is the loud signal to revisit.
    const QByteArray reversed = "MemAvailable:  250 kB\nMemTotal:  1000 kB\n";
    QVERIFY(!SystemUsage::parseMemoryTotals(reversed, &totals));
}

void TestSystemUsage::intervalIsClampedToTheFloor()
{
    SystemUsage usage;
    const int original = usage.interval();

    // Below the floor the request is clamped, not ignored: the property has
    // to agree with the binding that wrote it.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("below the")));
    usage.setInterval(1);
    QCOMPARE(usage.interval(), SystemUsage::MinIntervalMs);
    QVERIFY(usage.interval() != original);

    // A sane value is taken verbatim.
    usage.setInterval(5000);
    QCOMPARE(usage.interval(), 5000);
}

void TestSystemUsage::cpuPercentBetweenComputesTheBusyFraction_data()
{
    QTest::addColumn<double>("prevTotal");
    QTest::addColumn<double>("prevIdle");
    QTest::addColumn<double>("curTotal");
    QTest::addColumn<double>("curIdle");
    QTest::addColumn<int>("expected");

    // 100 jiffies elapsed, all idle.
    QTest::newRow("fully idle") << 1000.0 << 900.0 << 1100.0 << 1000.0 << 0;
    // 100 elapsed, none idle.
    QTest::newRow("fully busy") << 1000.0 << 900.0 << 1100.0 << 900.0 << 100;
    // 100 elapsed, 75 idle.
    QTest::newRow("quarter busy") << 1000.0 << 900.0 << 1100.0 << 975.0 << 25;
    // Rounding: 1/3 busy rounds to 33.
    QTest::newRow("rounds to nearest") << 0.0 << 0.0 << 300.0 << 200.0 << 33;
    // Idle exceeding total cannot yield a negative reading.
    QTest::newRow("clamped at zero") << 0.0 << 0.0 << 100.0 << 250.0 << 0;
    // The mirror case: only the idle counter goes backwards (reachable on
    // the same suspend/resume shape), which would compute 150 unclamped.
    QTest::newRow("clamped at one hundred") << 1000.0 << 900.0 << 1100.0 << 850.0 << 100;
}

void TestSystemUsage::cpuPercentBetweenComputesTheBusyFraction()
{
    QFETCH(double, prevTotal);
    QFETCH(double, prevIdle);
    QFETCH(double, curTotal);
    QFETCH(double, curIdle);
    QFETCH(int, expected);

    const SystemUsage::CpuTotals prev{prevTotal, prevIdle};
    const SystemUsage::CpuTotals current{curTotal, curIdle};

    QCOMPARE(SystemUsage::cpuPercentBetween(prev, current), expected);
}

void TestSystemUsage::cpuPercentBetweenRejectsUnusablePairs_data()
{
    QTest::addColumn<double>("curTotal");
    QTest::addColumn<double>("curIdle");

    // Counters that did not advance: no elapsed time, so no rate. This is
    // also what a second sample taken microseconds after the first looks
    // like, which is why the delta arithmetic has to be tested here rather
    // than by driving the live sampler.
    QTest::newRow("no advance") << 1000.0 << 900.0;
    // Counters that went backwards (suspend/resume, namespace reset, wrap).
    QTest::newRow("went backwards") << 500.0 << 400.0;
}

void TestSystemUsage::cpuPercentBetweenRejectsUnusablePairs()
{
    QFETCH(double, curTotal);
    QFETCH(double, curIdle);

    const SystemUsage::CpuTotals prev{1000.0, 900.0};
    const SystemUsage::CpuTotals current{curTotal, curIdle};

    // -1 is the "publish nothing, re-baseline" signal.
    QCOMPARE(SystemUsage::cpuPercentBetween(prev, current), -1);
}

void TestSystemUsage::repeatedSamplesDoNotReEmit()
{
    // The readouts are Q_PROPERTYs that QML binds, so they must only notify
    // on an actual change. The memory half carries the weight here: it is
    // recomputed from every sample, so a missing change-guard would fire on
    // an unchanged value. The CPU half is covered by the re-baseline (which
    // publishes nothing at all on this path) rather than by its guard.
    SystemUsage usage;
    QSignalSpy cpuSpy(&usage, &SystemUsage::cpuPercentChanged);
    QSignalSpy memorySpy(&usage, &SystemUsage::memoryPercentChanged);

    const int cpuBefore = usage.cpuPercent();
    const int memoryBefore = usage.memoryPercent();

    // Re-enabling forces a fresh sample without waiting for the timer.
    usage.setEnabled(false);
    usage.setEnabled(true);

    if (usage.cpuPercent() == cpuBefore) {
        QCOMPARE(cpuSpy.count(), 0);
    }
    if (usage.memoryPercent() == memoryBefore) {
        QCOMPARE(memorySpy.count(), 0);
    }
    // Deliberately narrowed to values that did not move, so on a host where
    // the fresh sample changed BOTH readouts this body asserted nothing.
    // Leave a trace when that happens rather than a fully-silent pass.
    if (usage.cpuPercent() != cpuBefore && usage.memoryPercent() != memoryBefore) {
        QSKIP("both readouts moved on the fresh sample; the change-guard was not exercised this run");
    }
}

void TestSystemUsage::reEnablingPublishesNoRate()
{
    // Re-enabling re-baselines rather than diffing against the pre-pause
    // sample, so it publishes no CPU rate.
    //
    // Deliberately narrow: this pins the observable outcome only. It does
    // NOT discriminate the re-baseline itself, because real jiffies do not
    // advance across a pause this short, so an unbaselined diff would report
    // nothing either. The re-baseline arithmetic is pinned instead by
    // cpuPercentBetweenRejectsUnusablePairs, which can drive the exact
    // snapshot pair that case turns on.
    SystemUsage usage;
    usage.setEnabled(false);

    const int before = usage.cpuPercent();
    QSignalSpy cpuSpy(&usage, &SystemUsage::cpuPercentChanged);

    usage.setEnabled(true);

    QCOMPARE(usage.cpuPercent(), before);
    QCOMPARE(cpuSpy.count(), 0);
}

QTEST_MAIN(TestSystemUsage)

#include "test_system_usage.moc"
