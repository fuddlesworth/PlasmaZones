// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pure-logic tests for the desktop-switch pre-autotile restore decision
// (tilinghandler/pretiledecisions.h). Same header-only reach as
// test_scroll_decisions: kwin-effect has no linkable test target, so the pure
// half is extracted into a header this test includes directly.

#include <tilinghandler/pretiledecisions.h>

#include <QTest>

using namespace PlasmaZones::PreTileDecisions;

class TestPreTileDecisions : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // The full truth table over the four inputs. Data-driven with a row per
    // combination so a failing row does not mask the rest. The
    // rectIsThisScreen column is only meaningful when a rect was found, so
    // the no-rect rows fix it at true (the call site passes
    // `bucket == screenId` over an empty bucket string, which is false there,
    // and both are covered below).
    void restoreTruthTable_data()
    {
        QTest::addColumn<bool>("haveLocalRect");
        QTest::addColumn<bool>("rectIsThisScreen");
        QTest::addColumn<bool>("wasTracked");
        QTest::addColumn<bool>("wasWindowedFs");
        QTest::addColumn<int>("expected");

        const auto e = [](PreTileRestore r) {
            return static_cast<int>(r);
        };

        // No rect at all: a tracked window falls back to the daemon's
        // placement store (it was snap-managed when it entered autotile);
        // an untracked one is left alone.
        QTest::newRow("no rect, untracked") << false << true << false << false << e(PreTileRestore::None);
        QTest::newRow("no rect, untracked, bucket miss") << false << false << false << false << e(PreTileRestore::None);
        QTest::newRow("no rect, tracked") << false << true << true << false << e(PreTileRestore::AskDaemon);
        QTest::newRow("no rect, tracked, bucket miss")
            << false << false << true << false << e(PreTileRestore::AskDaemon);
        QTest::newRow("no rect, tracked, windowed fs") << false << true << true << true << e(PreTileRestore::AskDaemon);

        // Same-screen rect: the ordinary un-tiling this pass exists to do.
        QTest::newRow("same-screen rect, tracked") << true << true << true << false << e(PreTileRestore::Apply);
        QTest::newRow("same-screen rect, tracked, windowed fs")
            << true << true << true << true << e(PreTileRestore::QueueForWindowedFullscreen);
        QTest::newRow("same-screen rect, untracked") << true << true << false << false << e(PreTileRestore::None);

        // Discussion #1028 follow-up: a rect from ANOTHER monitor's bucket is
        // declined outright. Bucket rects are absolute compositor
        // coordinates, so applying one would move the window to that output —
        // the "window thrown to the other monitor on a desktop switch" report.
        // The decline outranks every other term, tracked-ness included.
        QTest::newRow("cross-screen rect, tracked")
            << true << false << true << false << e(PreTileRestore::DeclineCrossScreen);
        QTest::newRow("cross-screen rect, tracked, windowed fs")
            << true << false << true << true << e(PreTileRestore::DeclineCrossScreen);
        QTest::newRow("cross-screen rect, untracked")
            << true << false << false << false << e(PreTileRestore::DeclineCrossScreen);
    }

    void restoreTruthTable()
    {
        QFETCH(bool, haveLocalRect);
        QFETCH(bool, rectIsThisScreen);
        QFETCH(bool, wasTracked);
        QFETCH(bool, wasWindowedFs);
        QFETCH(int, expected);

        QCOMPARE(static_cast<int>(resolvePreTileRestore(haveLocalRect, rectIsThisScreen, wasTracked, wasWindowedFs)),
                 expected);
    }

    // The decline must be its OWN outcome, never collapsed into None. None
    // falls through to the daemon fallback at the call site when the window
    // was tracked, and the daemon resolves the geometry against its own
    // screenForWindow() — so treating a declined cross-screen rect as "no
    // rect" would hand back the very rect this pass refused.
    void declineIsDistinctFromNone()
    {
        const PreTileRestore declined = resolvePreTileRestore(/*haveLocalRect=*/true, /*rectIsThisScreen=*/false,
                                                              /*wasTracked=*/true, /*wasWindowedFs=*/false);
        QVERIFY(declined != PreTileRestore::None);
        QVERIFY(declined != PreTileRestore::AskDaemon);
        QCOMPARE(declined, PreTileRestore::DeclineCrossScreen);
    }
};

QTEST_MAIN(TestPreTileDecisions)
#include "test_pretile_decisions.moc"
