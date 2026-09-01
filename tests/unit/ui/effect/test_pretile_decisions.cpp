// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pure-logic tests for the pre-autotile restore decisions
// (tilinghandler/pretiledecisions.h): which rect may safely be applied for a
// window on a given output, and what the desktop-switch demote pass does with
// the answer. Same header-only reach as test_scroll_decisions: kwin-effect has
// no linkable test target, so the pure halves are extracted into a header this
// test includes directly.

#include <tilinghandler/pretiledecisions.h>

#include <QTest>

using namespace PlasmaZones::PreTileDecisions;

class TestPreTileDecisions : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ── applicablePreTileRect ────────────────────────────────────────────────
    // The rule that fixes discussion #1028's second report: a bucket rect from
    // another OUTPUT keeps its size and loses its origin, because bucket rects
    // are absolute compositor coordinates.

    void rect_sameOutput_appliedWhole()
    {
        const QRectF saved(1320, 386, 800, 628);
        const QRectF live(8, 8, 2562, 1384); // the tiled frame it is leaving
        QCOMPARE(applicablePreTileRect(saved, /*sameOutput=*/true, live), saved);
    }

    // A virtual-screen re-key names the SAME output and shares its coordinate
    // space, so the caller compares physical ids and passes true — the rect
    // still applies whole. This is the case the all-bucket reader policy
    // exists for, and declining it would leave the window at its tiled frame.
    void rect_virtualScreenReKey_appliedWhole()
    {
        const QRectF saved(100, 200, 640, 480);
        QCOMPARE(applicablePreTileRect(saved, /*sameOutput=*/true, QRectF(0, 0, 3440, 1400)), saved);
    }

    // The teleport. The stored rect sits at x=4000, which is on the SECOND
    // monitor; the window is on the first. Applying it whole would move the
    // window there. Only the size survives, at the live position.
    void rect_otherOutput_keepsSizeDropsOrigin()
    {
        const QRectF saved(4000, 1284, 800, 628);
        const QRectF live(1168, 448, 2562, 1384);
        const QRectF got = applicablePreTileRect(saved, /*sameOutput=*/false, live);

        QCOMPARE(got.topLeft(), live.topLeft());
        QCOMPARE(got.size(), saved.size());
        QVERIFY(got.left() < 3440); // still on the window's own monitor
    }

    void rect_noSavedRect_invalid()
    {
        QVERIFY(!applicablePreTileRect(QRectF(), true, QRectF(0, 0, 800, 600)).isValid());
        QVERIFY(!applicablePreTileRect(QRectF(), false, QRectF(0, 0, 800, 600)).isValid());
    }

    // A degrade needs a live frame to take the position from. Without one
    // there is no safe answer, so nothing is applied — better than falling
    // back to the foreign origin.
    void rect_otherOutputWithoutLiveFrame_invalid()
    {
        QVERIFY(!applicablePreTileRect(QRectF(4000, 1284, 800, 628), /*sameOutput=*/false, QRectF()).isValid());
    }

    // ── announceMatchesReportedDesktops ────────────────────────
    // The managed-set announce is asynchronous; KWin's desktopChanged is not.

    void announce_matchingStamp_accepted()
    {
        const QHash<QString, int> announced{{QStringLiteral("DP-1"), 3}};
        const QHash<QString, int> reported{{QStringLiteral("DP-1"), 3}};
        QVERIFY(announceMatchesReportedDesktops(announced, reported));
    }

    // The race, exactly: the announce was resolved for desktop 1, the effect
    // has since reported 3. Acting on it would install desktop 1's managed set
    // while every window filter below reads desktop 3.
    void announce_overtakenByNewerSwitch_rejected()
    {
        const QHash<QString, int> announced{{QStringLiteral("DP-1"), 1}};
        const QHash<QString, int> reported{{QStringLiteral("DP-1"), 3}};
        QVERIFY(!announceMatchesReportedDesktops(announced, reported));
    }

    // Per-output virtual desktops: one screen disagreeing is enough, because
    // the announce is a single set covering all of them.
    void announce_oneScreenDisagrees_rejected()
    {
        const QHash<QString, int> announced{{QStringLiteral("DP-1"), 3}, {QStringLiteral("DP-2"), 1}};
        const QHash<QString, int> reported{{QStringLiteral("DP-1"), 3}, {QStringLiteral("DP-2"), 2}};
        QVERIFY(!announceMatchesReportedDesktops(announced, reported));
    }

    // A screen on only one side is not a disagreement. The daemon stamps only
    // the screens it announced, so an unmanaged screen is absent by
    // construction, and a screen the effect has never reported has nothing to
    // disagree with. Treating absence as a mismatch would reject nearly every
    // announce and wedge the seam shut.
    void announce_partialOverlap_accepted()
    {
        const QHash<QString, int> announced{{QStringLiteral("DP-1"), 3}, {QStringLiteral("DP-9"), 3}};
        const QHash<QString, int> reported{{QStringLiteral("DP-1"), 3}, {QStringLiteral("DP-2"), 1}};
        QVERIFY(announceMatchesReportedDesktops(announced, reported));
    }

    // An empty stamp accepts. A peer that sends none must not have every
    // announce dropped — the gate is a staleness filter, not a handshake.
    void announce_emptyStamp_accepted()
    {
        QVERIFY(announceMatchesReportedDesktops({}, {{QStringLiteral("DP-1"), 3}}));
        QVERIFY(announceMatchesReportedDesktops({}, {}));
    }

    // ── resolvePreTileRestore ────────────────────────────────────────────────

    void restoreTruthTable_data()
    {
        QTest::addColumn<bool>("haveLocalRect");
        QTest::addColumn<bool>("wasTracked");
        QTest::addColumn<bool>("wasWindowedFs");
        QTest::addColumn<int>("expected");

        const auto e = [](PreTileRestore r) {
            return static_cast<int>(r);
        };

        // Untracked: the buckets survive non-destructively, so a window already
        // restored by an earlier switch must not be re-teleported on every
        // later switch onto this desktop.
        QTest::newRow("untracked, no rect") << false << false << false << e(PreTileRestore::None);
        QTest::newRow("untracked, rect") << true << false << false << e(PreTileRestore::None);
        QTest::newRow("untracked, rect, windowed fs") << true << false << true << e(PreTileRestore::None);

        // Tracked with nothing stored: the window was snap-managed when it
        // entered autotile, so the daemon's record is the only source.
        QTest::newRow("tracked, no rect") << false << true << false << e(PreTileRestore::AskDaemon);
        QTest::newRow("tracked, no rect, windowed fs") << false << true << true << e(PreTileRestore::AskDaemon);

        QTest::newRow("tracked, rect") << true << true << false << e(PreTileRestore::Apply);
        QTest::newRow("tracked, rect, windowed fs")
            << true << true << true << e(PreTileRestore::QueueForWindowedFullscreen);
    }

    void restoreTruthTable()
    {
        QFETCH(bool, haveLocalRect);
        QFETCH(bool, wasTracked);
        QFETCH(bool, wasWindowedFs);
        QFETCH(int, expected);

        QCOMPARE(static_cast<int>(resolvePreTileRestore(haveLocalRect, wasTracked, wasWindowedFs)), expected);
    }

    // AskDaemon is for a genuinely EMPTY bucket, never for a rect that was
    // degraded. The daemon resolves geometry against its own possibly-stale
    // tracked screen, so reaching it with a usable local rect in hand would
    // trade a good answer for a worse one.
    void askDaemonOnlyWhenNothingIsStored()
    {
        QCOMPARE(resolvePreTileRestore(/*haveLocalRect=*/true, true, false), PreTileRestore::Apply);
        QCOMPARE(resolvePreTileRestore(/*haveLocalRect=*/false, true, false), PreTileRestore::AskDaemon);
    }
};

QTEST_MAIN(TestPreTileDecisions)
#include "test_pretile_decisions.moc"
