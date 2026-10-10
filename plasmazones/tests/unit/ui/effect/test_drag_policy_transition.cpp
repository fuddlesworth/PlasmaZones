// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// DragPolicyTransition: what the effect does when it adopts a drag policy,
// from the beginDrag reply or a mid-drag flip. Each row names the HEAD
// behaviour it reverses where there was one; the old code was a KWin-typed
// slot with no unit seam.

#include "plasmazoneseffect/dragpolicytransition.h"

#include <QtTest>

using namespace PlasmaZones;
using namespace PlasmaZones::DragPolicyTransition;
using R = PhosphorProtocol::DragBypassReason;

namespace {

PhosphorProtocol::DragPolicy policy(R reason, const QString& screen, bool grab, bool floatOnStart = false)
{
    PhosphorProtocol::DragPolicy p;
    p.bypassReason = reason;
    p.screenId = screen;
    p.grabKeyboard = grab;
    p.immediateFloatOnStart = floatOnStart;
    return p;
}

Input input(R oldReason, const PhosphorProtocol::DragPolicy& p, bool latched, bool grabbed,
            Source source = Source::Flip)
{
    Input in;
    in.oldReason = oldReason;
    in.policy = p;
    in.source = source;
    in.bypassLatched = latched;
    in.keyboardGrabbed = grabbed;
    in.daemonUp = true;
    return in;
}

} // namespace

class TestDragPolicyTransition : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// F69 + F378: reaching an engine screen latches the bypass and lets go of
    /// a grab the daemon declined. HEAD kept the grab and sent cancelSnap,
    /// whose latch left the return to the snapping screen unable to snap; the
    /// plan has no daemon call at all.
    void snapToEngine_entersBypassAndReleasesADeclinedGrab()
    {
        const Plan p = plan(input(R::None, policy(R::EngineOwnedScreen, QStringLiteral("DP-2"), false), false, true));
        QVERIFY(p.enterBypass);
        QCOMPARE(p.leave, Leave::None);
        QCOMPARE(p.bypassScreen, std::optional<QString>(QStringLiteral("DP-2")));
        QVERIFY(p.ungrab);
        QVERIFY(!p.grab);
    }

    /// F69: an engine screen to one that wants the keyboard (always-on
    /// re-insert) grabs. HEAD took the same-reason early return and never did.
    void engineToReorderEngine_grabs()
    {
        const Plan p =
            plan(input(R::EngineOwnedScreen, policy(R::EngineOwnedScreen, QStringLiteral("DP-3"), true), true, false));
        QVERIFY(!p.enterBypass);
        QCOMPARE(p.leave, Leave::None);
        QCOMPARE(p.bypassScreen, std::optional<QString>(QStringLiteral("DP-3")));
        QVERIFY(p.grab);
        QVERIFY(!p.ungrab);
    }

    /// F638: back on the snap path mid-drag, the tile is suspended, not
    /// released (HEAD relayed releaseWindowTracking and the drop back on the
    /// stack found nothing to reorder), and the snap path's grab is taken.
    void engineToSnap_suspendsTheTileOnAFlip()
    {
        const Plan p =
            plan(input(R::EngineOwnedScreen, policy(R::None, QStringLiteral("DP-1"), true), true, false, Source::Flip));
        QCOMPARE(p.leave, Leave::SuspendTile);
        QCOMPARE(p.bypassScreen, std::optional<QString>(QString()));
        QVERIFY(!p.enterBypass);
        QVERIFY(p.grab);
    }

    /// The reply corrects a stale fast-path latch: no engine owns the start,
    /// so the effect's own tracking goes.
    void engineToSnap_untracksOnTheReply()
    {
        const Plan p = plan(input(R::EngineOwnedScreen, policy(R::None, QStringLiteral("DP-1"), true), true, false,
                                  Source::BeginDragReply));
        QCOMPARE(p.leave, Leave::Untrack);
        QCOMPARE(p.bypassScreen, std::optional<QString>(QString()));
    }

    /// The latch, not the previous reason, says whether there is a bypass to
    /// leave: the fast path latches while the policy holds a different reason.
    void contextDisabledLatchLeavesTowardSnap()
    {
        const Plan p = plan(input(R::ContextDisabled, policy(R::None, QStringLiteral("DP-1"), true), true, false));
        QCOMPARE(p.leave, Leave::SuspendTile);
    }

    /// Pass-18 fold: a dead reason keeps the latch and only reconciles the
    /// grab. HEAD un-bypassed and grabbed for any non-engine reason.
    void deadDragKeepsTheLatch_data()
    {
        QTest::addColumn<R>("reason");
        QTest::newRow("context disabled") << R::ContextDisabled;
        QTest::newRow("snapping disabled") << R::SnappingDisabled;
        QTest::newRow("layout suppressed") << R::LayoutSuppressed;
    }
    void deadDragKeepsTheLatch()
    {
        QFETCH(R, reason);
        const Plan p = plan(input(R::EngineOwnedScreen, policy(reason, QStringLiteral("DP-1"), false), true, true));
        QCOMPARE(p.leave, Leave::None);
        QVERIFY(!p.bypassScreen.has_value());
        QVERIFY(!p.enterBypass);
        QVERIFY(p.ungrab);
        QVERIFY(!p.grab);
    }

    /// F383: a tile reaching a screen whose engine floats on drag takes its
    /// free size at once. HEAD read the flag only at drag start.
    void floatOnStartAcrossEngineScreens_data()
    {
        QTest::addColumn<bool>("windowLive");
        QTest::addColumn<bool>("tileHeld");
        QTest::addColumn<bool>("floating");
        QTest::addColumn<bool>("floatedThisDrag");
        QTest::addColumn<int>("source");
        QTest::addColumn<bool>("engineReason");
        QTest::addColumn<bool>("floatNow");
        const int flip = int(Source::Flip);
        const int reply = int(Source::BeginDragReply);
        QTest::newRow("tile crossing floats") << true << true << false << false << flip << true << true;
        QTest::newRow("already floating") << true << true << true << false << flip << true << false;
        QTest::newRow("floated this drag") << true << true << false << true << flip << true << false;
        QTest::newRow("window gone") << false << true << false << false << flip << true << false;
        QTest::newRow("flip, not a tile") << true << false << false << false << flip << true << false;
        QTest::newRow("reply, not a tile") << true << false << false << false << reply << true << true;
        QTest::newRow("snap path") << true << true << false << false << flip << false << false;
    }
    void floatOnStartAcrossEngineScreens()
    {
        QFETCH(bool, windowLive);
        QFETCH(bool, tileHeld);
        QFETCH(bool, floating);
        QFETCH(bool, floatedThisDrag);
        QFETCH(int, source);
        QFETCH(bool, engineReason);
        QFETCH(bool, floatNow);
        Input in = input(R::EngineOwnedScreen,
                         policy(engineReason ? R::EngineOwnedScreen : R::None, QStringLiteral("DP-2"), false, true),
                         true, false, Source(source));
        in.windowLive = windowLive;
        in.tileHeld = tileHeld;
        in.floating = floating;
        in.floatedThisDrag = floatedThisDrag;
        QCOMPARE(plan(in).floatNow, floatNow);
    }

    /// F574: with the daemon gone nothing will answer, so no grab is taken and
    /// one held is let go.
    void noDaemonNeverGrabs()
    {
        Input in = input(R::None, policy(R::None, QStringLiteral("DP-1"), true), false, false);
        in.daemonUp = false;
        QVERIFY(!plan(in).grab);
        in.keyboardGrabbed = true;
        QVERIFY(plan(in).ungrab);
    }

    void unchangedPolicyIsInert()
    {
        const Plan p = plan(input(R::None, policy(R::None, QStringLiteral("DP-1"), true), false, true));
        QVERIFY(!p.enterBypass);
        QCOMPARE(p.leave, Leave::None);
        QVERIFY(!p.bypassScreen.has_value());
        QVERIFY(!p.floatNow);
        QVERIFY(!p.grab);
        QVERIFY(!p.ungrab);
    }
};

QTEST_MAIN(TestDragPolicyTransition)
#include "test_drag_policy_transition.moc"
