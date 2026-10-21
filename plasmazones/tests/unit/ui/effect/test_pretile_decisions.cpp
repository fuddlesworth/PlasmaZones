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

    // The output moved since the capture (F807): it now sits at x=3440, so a
    // rect taken at x=1320 is from where it used to be and would move the
    // window onto the other monitor. Only the size survives, at the live
    // position.
    void rect_sameOutputMovedAway_keepsSizeDropsOrigin()
    {
        const QRectF saved(1320, 386, 800, 628);
        const QRectF live(3448, 8, 2562, 1384);
        const QRectF output(3440, 0, 2560, 1440);
        const QRectF got = applicablePreTileRect(saved, /*sameOutput=*/true, live, output);
        QCOMPARE(got.topLeft(), live.topLeft());
        QCOMPARE(got.size(), saved.size());
    }

    // A tile in the right third of its output keeps a saved size larger than
    // the room to its right: kept at the live position, the window's centre
    // would sit on the next monitor and the overflow float would hand it to
    // that monitor's engine. It is moved back inside its own output (F1076).
    void rect_degradedSizeStaysOnItsOutput()
    {
        const QRectF saved(4000, 100, 1200, 800);
        const QRectF live(1440, 8, 472, 1064);
        const QRectF output(0, 0, 1920, 1080);
        QCOMPARE(applicablePreTileRect(saved, /*sameOutput=*/false, live, output), QRectF(720, 8, 1200, 800));
    }

    // An output that did not move still holds the rect's centre: applied whole.
    void rect_sameOutputStillInside_appliedWhole()
    {
        const QRectF saved(1320, 386, 800, 628);
        const QRectF output(0, 0, 3440, 1440);
        QCOMPARE(applicablePreTileRect(saved, /*sameOutput=*/true, QRectF(8, 8, 2562, 1384), output), saved);
    }

    // A caller with no output geometry gets the same-output answer as before.
    void rect_noOutputGeometry_appliedWhole()
    {
        const QRectF saved(1320, 386, 800, 628);
        QCOMPARE(applicablePreTileRect(saved, /*sameOutput=*/true, QRectF(8, 8, 2562, 1384), QRectF()), saved);
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
        QTest::addColumn<bool>("wasTiled");
        QTest::addColumn<int>("expected");

        const auto e = [](PreTileRestore r) {
            return static_cast<int>(r);
        };

        // Total over the four booleans: all 2^4 rows, so a reader checking the
        // claim finds every combination rather than re-deriving which were
        // left out and why they did not matter.
        for (const bool tiled : {false, true}) {
            const char* const t = tiled ? ", tiled" : ", not tiled";
            // Untracked: the buckets survive non-destructively, so a window
            // already restored by an earlier switch must not be re-teleported
            // on every later switch onto this desktop. Untracked short-circuits
            // before the other terms are read.
            QTest::addRow("untracked, no rect%s", t) << false << false << false << tiled << e(PreTileRestore::None);
            QTest::addRow("untracked, rect%s", t) << true << false << false << tiled << e(PreTileRestore::None);
            QTest::addRow("untracked, rect, windowed fs%s", t)
                << true << false << true << tiled << e(PreTileRestore::None);
            QTest::addRow("untracked, no rect, windowed fs%s", t)
                << false << false << true << tiled << e(PreTileRestore::None);

            // A local rect is only stored by a window that was tiled, so it
            // is applied whatever the live fact says.
            QTest::addRow("tracked, rect%s", t) << true << true << false << tiled << e(PreTileRestore::Apply);
            QTest::addRow("tracked, rect, windowed fs%s", t)
                << true << true << true << tiled << e(PreTileRestore::QueueForWindowedFullscreen);
        }

        // Tracked and tiled with nothing stored: the window was snap-managed
        // when it entered autotile, so the daemon's record is the only source.
        QTest::newRow("tracked, no rect, tiled") << false << true << false << true << e(PreTileRestore::AskDaemon);
        QTest::newRow("tracked, no rect, windowed fs, tiled")
            << false << true << true << true << e(PreTileRestore::AskDaemon);
        // Tracked but never tiled: only the announce trail, from an open the
        // daemon refused, or a window floating in the tiling mode. The
        // daemon's record is mode-shared, so it is not asked (#1151).
        QTest::newRow("tracked, no rect, not tiled") << false << true << false << false << e(PreTileRestore::None);
        QTest::newRow("tracked, no rect, windowed fs, not tiled")
            << false << true << true << false << e(PreTileRestore::None);
    }

    void restoreTruthTable()
    {
        QFETCH(bool, haveLocalRect);
        QFETCH(bool, wasTracked);
        QFETCH(bool, wasWindowedFs);
        QFETCH(bool, wasTiled);
        QFETCH(int, expected);

        QCOMPARE(static_cast<int>(resolvePreTileRestore(haveLocalRect, wasTracked, wasWindowedFs, wasTiled)), expected);
    }

    // AskDaemon is for a genuinely EMPTY bucket, never for a rect that was
    // degraded. The daemon resolves geometry against its own possibly-stale
    // tracked screen, so reaching it with a usable local rect in hand would
    // trade a good answer for a worse one.
    void askDaemonOnlyWhenNothingIsStored()
    {
        QCOMPARE(resolvePreTileRestore(/*haveLocalRect=*/true, true, false, true), PreTileRestore::Apply);
        QCOMPARE(resolvePreTileRestore(/*haveLocalRect=*/false, true, false, true), PreTileRestore::AskDaemon);
    }
};

QTEST_MAIN(TestPreTileDecisions)
#include "test_pretile_decisions.moc"
