// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_connect_sweep_ordering.cpp
 * @brief A blanket `disconnect(sender, &Signal, this, nullptr)` must precede
 *        every `connect` to that same (sender, signal) pair in the file.
 *
 * This pins a defect that already shipped into a PR branch and was invisible to
 * the entire suite.
 *
 * The daemon's long-lived dependencies (the rule store, the window registry)
 * are built once in the ctor and SURVIVE a stop() → init() cycle, so init has
 * to sever its own subscriptions before re-establishing them or every cycle
 * stacks another copy. The idiom for that is a blanket disconnect naming the
 * (sender, signal, receiver) triple, immediately before the connect.
 *
 * The trap is that the blanket form cannot single out ONE subscription. It
 * removes every connection from that sender's signal to that receiver. So it is
 * only correct at the TOP of the block that establishes ALL of them. Adding a
 * second one next to a later connect silently severs its siblings — which is
 * exactly what happened: a new subscription copied the idiom from a neighbouring
 * pair and killed the exclude refilter, the overlay refresh and the assignment
 * reconcile, all of which sat above it.
 *
 * Nothing caught it. The suite passed identically with and without the bug,
 * because no test asserts that daemon signal subscriptions survive init, and
 * standing up a Daemon in a fixture is not currently possible.
 *
 * So this scrapes the source instead. That is a real limitation and worth
 * stating plainly: it pins a textual PATTERN, not a behaviour, and it is
 * brittle against reformatting of the connect calls it matches. It is here
 * because the defect class is severe (silent, and it disables features that
 * look wired), mechanical enough to detect textually, and otherwise unguarded.
 * If a Daemon fixture is ever built for another reason, the honest replacement
 * is `store.receivers(SIGNAL(rulesChanged(bool)))` after init, which would also
 * catch the duplicate-stacking direction this cannot see.
 */

#include <QFile>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTest>

namespace {

/// Files whose init/teardown wiring uses the blanket-sweep idiom. Deliberately
/// a short explicit list rather than a glob: the pattern below is only
/// meaningful for a "sever then re-establish" block, and pointing it at every
/// file in the tree would produce noise from teardown-only sweeps (stop(),
/// setters replacing a dependency), which are a different, correct shape.
const QStringList& scannedFiles()
{
    static const QStringList files = {
        QStringLiteral("/src/daemon/daemon/init_engines.cpp"),
    };
    return files;
}

struct Occurrence
{
    QString pair; ///< "sender::Signal", the (sender, signal) identity
    int line = 0;
};

} // namespace

class TestConnectSweepOrdering : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void blanketSweepPrecedesEveryConnectOnTheSamePair()
    {
        // `disconnect(m_x.get(), &Ns::Cls::sig, this, nullptr);`
        static const QRegularExpression sweepRe(
            QStringLiteral(R"(\bdisconnect\(\s*([A-Za-z_][\w.()>-]*?)\s*,\s*&([\w:]+)\s*,\s*this\s*,\s*nullptr\s*\))"));
        // `connect(m_x.get(), &Ns::Cls::sig, this,` — the receiver-`this` form
        // is the only one a `this`-targeted sweep can remove.
        static const QRegularExpression connectRe(
            QStringLiteral(R"(\bconnect\(\s*([A-Za-z_][\w.()>-]*?)\s*,\s*&([\w:]+)\s*,\s*this\s*,)"));

        int filesScanned = 0;
        int sweepsSeen = 0;
        QStringList offenders;

        for (const QString& relative : scannedFiles()) {
            const QString path = QStringLiteral(P_SOURCE_DIR) + relative;
            QFile file(path);
            QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(path));
            const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
            ++filesScanned;

            QList<Occurrence> sweeps;
            QList<Occurrence> connects;
            for (int i = 0; i < lines.size(); ++i) {
                const QString& text = lines.at(i);
                // Skip comment lines so the explanatory prose describing this
                // very trap does not register as code.
                if (text.trimmed().startsWith(QLatin1String("//"))) {
                    continue;
                }
                if (const auto m = sweepRe.match(text); m.hasMatch()) {
                    sweeps.append({m.captured(1) + QLatin1String("::") + m.captured(2), i + 1});
                }
                if (const auto m = connectRe.match(text); m.hasMatch()) {
                    connects.append({m.captured(1) + QLatin1String("::") + m.captured(2), i + 1});
                }
            }
            sweepsSeen += sweeps.size();

            for (const Occurrence& sweep : sweeps) {
                // Exactly one blanket sweep per pair. Two is the defect: the
                // later one necessarily runs after connects the earlier one
                // established.
                int samePair = 0;
                for (const Occurrence& other : sweeps) {
                    if (other.pair == sweep.pair) {
                        ++samePair;
                    }
                }
                if (samePair > 1) {
                    offenders.append(QStringLiteral("%1:%2 — %3 has %4 blanket sweeps; only the first can be "
                                                    "correct, the rest sever subscriptions made in between")
                                         .arg(relative)
                                         .arg(sweep.line)
                                         .arg(sweep.pair)
                                         .arg(samePair));
                    continue;
                }
                // And it must come before every connect on that pair.
                for (const Occurrence& conn : connects) {
                    if (conn.pair == sweep.pair && conn.line < sweep.line) {
                        offenders.append(
                            QStringLiteral("%1:%2 — blanket sweep of %3 runs AFTER the connect at line %4, which it "
                                           "therefore severs. Move it above every connect on this pair.")
                                .arg(relative)
                                .arg(sweep.line)
                                .arg(sweep.pair)
                                .arg(conn.line));
                    }
                }
            }
        }

        QCOMPARE(filesScanned, scannedFiles().size());
        // If the regex stops matching (a reformat, a renamed idiom), this test
        // would silently pass while checking nothing. Fail loudly instead —
        // the same "make the unrecognised case LOUD" rule the D-Bus scraping
        // tests are built on.
        QVERIFY2(sweepsSeen > 0,
                 "Found no blanket disconnect(sender, &Signal, this, nullptr) calls at all. Either the idiom was "
                 "renamed or the scrape broke; in both cases this test is no longer checking anything.");
        QVERIFY2(offenders.isEmpty(), qPrintable(offenders.join(QStringLiteral("\n  "))));
    }
};

QTEST_GUILESS_MAIN(TestConnectSweepOrdering)
#include "test_connect_sweep_ordering.moc"
