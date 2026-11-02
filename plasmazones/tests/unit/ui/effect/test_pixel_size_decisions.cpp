// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_pixel_size_decisions.cpp
 * @brief Which output pixel sizes the effect owes the daemon, and when the
 *        screen-shape rule verdicts drop (plasmazoneseffect/pixelsizedecisions.h),
 *        driven through the repeats: a second flush, a removal after the
 *        report, a daemon restart and the push to the next one.
 */

#include "plasmazoneseffect/pixelsizedecisions.h"

#include <QTest>

using namespace PlasmaZones::PixelSizeDecisions;

namespace {

const QString DP1 = QStringLiteral("DP-1");
const QString DP2 = QStringLiteral("DP-2");

OutputPixelSize output(const QString& connector, const QSize& size)
{
    return OutputPixelSize{connector, size, false};
}

// The daemon's side: each report applied in order, a 0x0 clearing the entry.
void apply(QHash<QString, QSize>& daemon, const QList<Report>& reports)
{
    for (const Report& report : reports) {
        if (report.pixelSize.isEmpty()) {
            daemon.remove(report.connector);
        } else {
            daemon.insert(report.connector, report.pixelSize);
        }
    }
}

} // namespace

class TestPixelSizeDecisions : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // The first flush reports each output and drops the verdicts. A second
    // flush over the same sizes sends nothing: the daemon already holds them.
    void secondFlushOfTheSameSizesSendsNothing()
    {
        Ledger ledger;
        const QList<OutputPixelSize> outputs = {output(DP1, QSize(3840, 2160)), output(DP2, QSize(1440, 2560))};

        const FlushOutcome first = ledger.flush(true, outputs);
        QCOMPARE(first.reports.size(), 2);
        QVERIFY(first.reports.at(0) == (Report{DP1, QSize(3840, 2160)}));
        QVERIFY(first.reports.at(1) == (Report{DP2, QSize(1440, 2560)}));
        QVERIFY(first.shapeChanged);

        const FlushOutcome second = ledger.flush(true, outputs);
        QVERIFY(second.reports.isEmpty());
        QVERIFY(!second.shapeChanged);
    }

    // A refresh-rate change emits currentModeChanged and moves no size: no
    // report and no verdict drop. A real mode change does both, once.
    void refreshOnlyModeChangeKeepsTheVerdicts()
    {
        Ledger ledger;
        ledger.flush(true, {output(DP1, QSize(2560, 1440))});

        const FlushOutcome refresh = ledger.flush(true, {output(DP1, QSize(2560, 1440))});
        QVERIFY(refresh.reports.isEmpty());
        QVERIFY(!refresh.shapeChanged);

        const FlushOutcome rotate = ledger.flush(true, {output(DP1, QSize(1440, 2560))});
        QCOMPARE(rotate.reports.size(), 1);
        QVERIFY(rotate.reports.at(0) == (Report{DP1, QSize(1440, 2560)}));
        QVERIFY(rotate.shapeChanged);

        const FlushOutcome again = ledger.flush(true, {output(DP1, QSize(1440, 2560))});
        QVERIFY(again.reports.isEmpty());
        QVERIFY(!again.shapeChanged);
    }

    // Report, lose the daemon, report again: the new daemon holds nothing, so
    // the push after its bridge comes up sends every output again, even at a
    // size the old daemon was sent.
    void daemonRestartResendsEveryOutput()
    {
        Ledger ledger;
        const QList<OutputPixelSize> outputs = {output(DP1, QSize(3840, 2160))};
        QCOMPARE(ledger.flush(true, outputs).reports.size(), 1);

        ledger.forgetSent();

        const QList<Report> pushed = ledger.push(true, outputs);
        QCOMPARE(pushed.size(), 1);
        QVERIFY(pushed.at(0) == (Report{DP1, QSize(3840, 2160)}));
        // Nothing a rule reads moved, so a later flush neither resends nor
        // drops the verdicts.
        const FlushOutcome later = ledger.flush(true, outputs);
        QVERIFY(later.reports.isEmpty());
        QVERIFY(!later.shapeChanged);
    }

    // A size read while the bridge is down is still owed: the push sends it
    // once the bridge is up, though the flush already recorded it as seen.
    void sizeSeenWhileTheBridgeIsDownIsStillOwed()
    {
        Ledger ledger;
        const QList<OutputPixelSize> outputs = {output(DP1, QSize(2560, 1440))};

        const FlushOutcome down = ledger.flush(false, outputs);
        QVERIFY(down.reports.isEmpty());
        QVERIFY(down.shapeChanged);
        QVERIFY(ledger.push(false, outputs).isEmpty());

        const QList<Report> pushed = ledger.push(true, outputs);
        QCOMPARE(pushed.size(), 1);
        QVERIFY(pushed.at(0) == (Report{DP1, QSize(2560, 1440)}));
    }

    // An unplugged output's connector is cleared at the daemon, once. A
    // second removal of the same connector owes nothing.
    void removalClearsAReportedConnectorOnce()
    {
        Ledger ledger;
        QHash<QString, QSize> daemon;
        apply(daemon, ledger.flush(true, {output(DP1, QSize(3840, 2160))}).reports);
        QCOMPARE(daemon.value(DP1), QSize(3840, 2160));

        const RemovalOutcome removal = ledger.remove(true, DP1, false);
        QVERIFY(removal.report.has_value());
        QVERIFY(*removal.report == (Report{DP1, QSize(0, 0)}));
        QVERIFY(!removal.flushOwed);
        apply(daemon, {*removal.report});
        QVERIFY(!daemon.contains(DP1));

        const RemovalOutcome repeat = ledger.remove(true, DP1, false);
        QVERIFY(!repeat.report.has_value());
        QVERIFY(!repeat.flushOwed);
    }

    // A connector this bridge never sent a size has nothing to clear.
    void removalOfAnUnreportedConnectorSendsNothing()
    {
        Ledger ledger;
        const RemovalOutcome removal = ledger.remove(true, DP2, false);
        QVERIFY(!removal.report.has_value());
        QVERIFY(!removal.flushOwed);
    }

    // A monitor swapped on the same port: KWin adds the new output before it
    // removes the old one, and the add queues a flush. When the removal runs
    // in the same turn, that flush has not run yet
    // (removalBeforeTheAddsFlushReportsTheLiveSize). This models the other
    // order, where the flush has already reported the new size by the time
    // the removal runs. A clear for the connector would land after that report
    // and wipe it at the daemon, so the removal sends nothing and owes a flush
    // instead, which re-reads the live output.
    void removalWithALiveConnectorKeepsTheLiveReport()
    {
        Ledger ledger;
        QHash<QString, QSize> daemon;
        apply(daemon, ledger.flush(true, {output(DP1, QSize(3840, 2160))}).reports);
        apply(daemon, ledger.flush(true, {output(DP1, QSize(2560, 1440))}).reports);
        QCOMPARE(daemon.value(DP1), QSize(2560, 1440));

        const RemovalOutcome removal = ledger.remove(true, DP1, true);
        QVERIFY(!removal.report.has_value());
        QVERIFY(removal.flushOwed);

        // The owed flush finds the daemon already holding the live size.
        QVERIFY(ledger.flush(true, {output(DP1, QSize(2560, 1440))}).reports.isEmpty());
        QCOMPARE(daemon.value(DP1), QSize(2560, 1440));
    }

    // The same swap with a monitor of the same size, in the same order. Its
    // report was deduped, and a clear would have dropped the size at the
    // daemon and un-recorded the connector's sent size. Nothing would queue a
    // flush to send it again, though: the add's flush already ran and the live
    // output's mode never moves. The owed flush is the one that would be
    // missing, and it finds the daemon holding the live size.
    void removalWithALiveSameSizeConnectorKeepsTheReport()
    {
        Ledger ledger;
        QHash<QString, QSize> daemon;
        apply(daemon, ledger.flush(true, {output(DP1, QSize(3840, 2160))}).reports);
        QVERIFY(ledger.flush(true, {output(DP1, QSize(3840, 2160))}).reports.isEmpty());

        const RemovalOutcome removal = ledger.remove(true, DP1, true);
        QVERIFY(!removal.report.has_value());
        QVERIFY(removal.flushOwed);

        QVERIFY(ledger.flush(true, {output(DP1, QSize(3840, 2160))}).reports.isEmpty());
        QCOMPARE(daemon.value(DP1), QSize(3840, 2160));
    }

    // The swap with the removal in the same turn as the add, so the add's
    // queued flush has not run and the daemon still holds the old monitor's
    // size. The removal sends nothing and owes a flush, which collapses with
    // the queued one, and that flush reports the live output's size.
    void removalBeforeTheAddsFlushReportsTheLiveSize()
    {
        Ledger ledger;
        QHash<QString, QSize> daemon;
        apply(daemon, ledger.flush(true, {output(DP1, QSize(3840, 2160))}).reports);

        const RemovalOutcome removal = ledger.remove(true, DP1, true);
        QVERIFY(!removal.report.has_value());
        QVERIFY(removal.flushOwed);

        const FlushOutcome flushed = ledger.flush(true, {output(DP1, QSize(2560, 1440))});
        QCOMPARE(flushed.reports.size(), 1);
        QVERIFY(flushed.reports.at(0) == (Report{DP1, QSize(2560, 1440)}));
        QVERIFY(flushed.shapeChanged);
        apply(daemon, flushed.reports);
        QCOMPARE(daemon.value(DP1), QSize(2560, 1440));
    }

    // KWin's placeholder output stands in while every monitor is off and
    // describes no screen: never reported, never a verdict drop.
    void placeholderIsNeitherReportedNorSeen()
    {
        Ledger ledger;
        const QList<OutputPixelSize> outputs = {
            OutputPixelSize{QStringLiteral("Placeholder-1"), QSize(1024, 768), true}};

        const FlushOutcome flushed = ledger.flush(true, outputs);
        QVERIFY(flushed.reports.isEmpty());
        QVERIFY(!flushed.shapeChanged);
        QVERIFY(ledger.push(true, outputs).isEmpty());
    }
};

QTEST_MAIN(TestPixelSizeDecisions)
#include "test_pixel_size_decisions.moc"
