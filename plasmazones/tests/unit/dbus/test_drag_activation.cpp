// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_drag_activation.cpp
 * @brief Guards the two pure per-tick resolvers the WindowDragAdaptor decides
 *        drag trigger state with.
 *
 * resolveActivationActive() decides whether the snap overlay should be shown.
 * resolveHoldGrace() decides whether a physically-released hold-mode trigger
 * still counts as held, which is what lets a drop that lands just after the
 * button lifts still snap.
 *
 * Both live in src/dbus/windowdragadaptor/dragactivation.cpp so they can be
 * exercised without standing up the adaptor + its compositor dependencies.
 * Pinning the activation table here means the always-active inversion (#249),
 * where the same activation triggers serve double duty as
 * deactivate-while-held / toggle-off, can't drift. Pinning the grace contract
 * means the boundary cases stay fixed: zero disables it, a never-held trigger
 * has nothing to extend, and the window closes strictly after graceMs.
 */

#include <QTest>
#include <limits>
#include <QObject>

#include "dbus/windowdragadaptor/dragactivation.h"

using namespace PlasmaZones;

class TestDragActivation : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ─── Hold-to-activate (toggleMode = false, alwaysActive = false) ──────

    void hold_triggerHeld_active()
    {
        const auto d = resolveActivationActive(/*triggerHeld=*/true, /*toggleMode=*/false,
                                               /*alwaysActive=*/false, /*prevTriggerHeld=*/false,
                                               /*activationToggled=*/false);
        QVERIFY(d.active);
        QVERIFY(d.nextPrevTriggerHeld);
        QVERIFY(!d.nextActivationToggled);
    }

    void hold_triggerReleased_inactive()
    {
        const auto d = resolveActivationActive(false, false, false, true, false);
        QVERIFY(!d.active);
        QVERIFY(!d.nextPrevTriggerHeld);
    }

    // ─── Toggle mode rising-edge latch (alwaysActive = false) ─────────────

    void toggle_risingEdge_flipsLatch()
    {
        const auto d = resolveActivationActive(/*triggerHeld=*/true, /*toggleMode=*/true,
                                               /*alwaysActive=*/false, /*prevTriggerHeld=*/false,
                                               /*activationToggled=*/false);
        QVERIFY(d.active);
        QVERIFY(d.nextActivationToggled);
    }

    void toggle_releasedAfterToggleOn_staysActive()
    {
        // Trigger released after a previous tick toggled the latch on — the
        // overlay must remain on. Core toggle-mode contract.
        const auto d = resolveActivationActive(false, true, false, true, true);
        QVERIFY(d.active);
        QVERIFY(d.nextActivationToggled);
        QVERIFY(!d.nextPrevTriggerHeld);
    }

    void toggle_secondPress_flipsBackOff()
    {
        const auto d = resolveActivationActive(true, true, false, false, true);
        QVERIFY(!d.active);
        QVERIFY(!d.nextActivationToggled);
    }

    void toggle_heldContinuously_doesNotReToggle()
    {
        // Same trigger held across consecutive ticks must NOT re-flip the
        // latch — only the rising edge counts.
        const auto d = resolveActivationActive(true, true, false, true, true);
        QVERIFY(d.active);
        QVERIFY(d.nextActivationToggled);
    }

    // ─── Always-active inversion (#249) — hold mode ───────────────────────

    void alwaysActive_holdMode_noTrigger_active()
    {
        // alwaysActive on, no non-sentinel trigger held: overlay implicitly
        // on. This is the typical config for users who want the overlay on
        // every drag without configuring a deactivate trigger.
        const auto d = resolveActivationActive(/*triggerHeld=*/false, /*toggleMode=*/false,
                                               /*alwaysActive=*/true, false, false);
        QVERIFY(d.active);
    }

    void alwaysActive_holdMode_triggerHeld_inactive()
    {
        // alwaysActive on, non-sentinel trigger held: hold-to-deactivate.
        // Overlay hides while the trigger is held.
        const auto d = resolveActivationActive(/*triggerHeld=*/true, false, /*alwaysActive=*/true, false, false);
        QVERIFY(!d.active);
    }

    void alwaysActive_holdMode_triggerReleased_restoresActive()
    {
        // Always-active user releases the deactivate trigger — overlay
        // returns immediately because activation is implicit.
        const auto d = resolveActivationActive(false, false, true, true, false);
        QVERIFY(d.active);
    }

    // ─── Always-active inversion — toggle mode ────────────────────────────

    void alwaysActive_toggleMode_default_active()
    {
        // alwaysActive + toggle, latch=false (default). Overlay is on.
        const auto d = resolveActivationActive(false, /*toggleMode=*/true, /*alwaysActive=*/true, false, false);
        QVERIFY(d.active);
    }

    void alwaysActive_toggleMode_firstPress_togglesOff()
    {
        // First rising edge of the trigger: latch flips true → overlay off.
        const auto d = resolveActivationActive(/*triggerHeld=*/true, true, true, /*prevTriggerHeld=*/false,
                                               /*activationToggled=*/false);
        QVERIFY(!d.active);
        QVERIFY(d.nextActivationToggled);
    }

    void alwaysActive_toggleMode_releasedAfterFlip_staysOff()
    {
        // After flipping off, releasing the trigger keeps the overlay off
        // (toggle-mode latch survives release).
        const auto d = resolveActivationActive(false, true, true, true, true);
        QVERIFY(!d.active);
        QVERIFY(d.nextActivationToggled);
    }

    void alwaysActive_toggleMode_secondPress_flipsBackOn()
    {
        // Second rising edge: latch flips back to false → overlay on.
        const auto d = resolveActivationActive(true, true, true, false, true);
        QVERIFY(d.active);
        QVERIFY(!d.nextActivationToggled);
    }

    // ─── Symmetry guard: latch survives mode switches ─────────────────────

    void latch_survivesAlwaysActiveSwitch()
    {
        // toggleMode=true, latch=true. Overlay state depends on
        // alwaysActiveOnDrag. Switching the always-active bit between calls
        // (e.g. user changes settings mid-drag — unlikely but the resolver
        // is stateless wrt the bit) flips the displayed active state
        // without disturbing the latch.
        const auto normal = resolveActivationActive(false, true, /*alwaysActive=*/false, false, true);
        QVERIFY(normal.active); // latch=true in normal mode → overlay on

        const auto inverted = resolveActivationActive(false, true, /*alwaysActive=*/true, false, true);
        QVERIFY(!inverted.active); // latch=true in always-active mode → overlay off
        QVERIFY(inverted.nextActivationToggled); // latch unchanged
    }

    // ─── Hold-mode release grace (resolveHoldGrace) ───────────────────────

    void grace_rawHeld_stampsNow()
    {
        const auto g = resolveHoldGrace(/*rawHeld=*/true, /*nowMs=*/1000, /*lastHeldMs=*/-1, /*graceMs=*/150);
        QVERIFY(g.held);
        QCOMPARE(g.nextLastHeldMs, 1000);
        QCOMPARE(g.remainingMs, 0);
    }

    void grace_releasedInsideWindow_staysHeld()
    {
        const auto g = resolveHoldGrace(false, 1100, 1000, 150);
        QVERIFY(g.held);
        QCOMPARE(g.nextLastHeldMs, 1000); // stamp is not refreshed by a grace tick
        QCOMPARE(g.remainingMs, 50);
    }

    void grace_releasedAtBoundary_staysHeld()
    {
        const auto g = resolveHoldGrace(false, 1150, 1000, 150);
        QVERIFY(g.held);
        QCOMPARE(g.remainingMs, 0);
    }

    void grace_releasedPastWindow_released()
    {
        const auto g = resolveHoldGrace(false, 1151, 1000, 150);
        QVERIFY(!g.held);
        QCOMPARE(g.nextLastHeldMs, 1000);
    }

    void grace_zero_isRaw()
    {
        // A zero grace is the off switch: the tick right after release reads
        // released, whatever the stamp says.
        const auto g = resolveHoldGrace(false, 1001, 1000, 0);
        QVERIFY(!g.held);
    }

    void grace_neverHeld_released()
    {
        // No physically-held tick this drag: nothing to extend, however
        // large the grace.
        const auto g = resolveHoldGrace(false, 5, -1, 1000);
        QVERIFY(!g.held);
        QCOMPARE(g.nextLastHeldMs, -1);
    }

    void grace_clockWentBackwards_released()
    {
        // A stamp from the future (clock restarted by a new drag without the
        // stamp being re-seeded) must fail closed, not extend forever.
        const auto g = resolveHoldGrace(false, 10, 500, 150);
        QVERIFY(!g.held);
    }

    /**
     * Two full grace cycles inside ONE drag, driven as a tick sequence.
     *
     * The single-shot slots above each exercise one invocation, but the
     * feature's whole point is the repeat: a re-press has to REFRESH the stamp
     * so the second release gets a full grace of its own rather than inheriting
     * the first one's remaining time. Feeding nextLastHeldMs forward the way
     * the adaptor does is what makes that visible.
     */
    void grace_twoCyclesInOneDrag_secondReleaseGetsAFullWindow()
    {
        const int graceMs = 150;
        qint64 lastHeld = -1;

        // Held at t=0, released at t=10. Still inside the first window at 100.
        auto g = resolveHoldGrace(true, 0, lastHeld, graceMs);
        lastHeld = g.nextLastHeldMs;
        QCOMPARE(lastHeld, qint64(0));

        g = resolveHoldGrace(false, 100, lastHeld, graceMs);
        lastHeld = g.nextLastHeldMs;
        QVERIFY2(g.held, "100ms after the last held tick is inside a 150ms grace");
        QCOMPARE(g.remainingMs, qint64(50));
        // A released tick must NOT advance the stamp, or the window would
        // slide forward on every tick and never close.
        QCOMPARE(lastHeld, qint64(0));

        // Re-pressed at t=120, inside the first grace. The stamp refreshes.
        g = resolveHoldGrace(true, 120, lastHeld, graceMs);
        lastHeld = g.nextLastHeldMs;
        QVERIFY(g.held);
        QCOMPARE(lastHeld, qint64(120));

        // t=200 is 200ms after the ORIGINAL press, so the first window is long
        // gone — but only 80ms after the re-press, so this is still held. This
        // is the assertion a single-invocation test cannot make.
        g = resolveHoldGrace(false, 200, lastHeld, graceMs);
        lastHeld = g.nextLastHeldMs;
        QVERIFY2(g.held, "the re-press must start a fresh window, not inherit the first one's remainder");
        QCOMPARE(g.remainingMs, qint64(70));

        // t=271 is one past the second window. Now it resolves released, and
        // the stamp stays put so no later tick can resurrect it.
        g = resolveHoldGrace(false, 271, lastHeld, graceMs);
        QVERIFY(!g.held);
        QCOMPARE(g.remainingMs, qint64(0));
        QCOMPARE(g.nextLastHeldMs, qint64(120));
    }

    /**
     * The replay deadline is one PAST the remaining grace.
     *
     * Landing exactly on the deadline would replay a tick that resolveHoldGrace
     * still answers "held" for (elapsed == graceMs is inside the window, pinned
     * by grace_atExactBoundary_held), so the replay would re-arm instead of
     * resolving the release, and the drag would never settle.
     */
    void graceExpiryDue_isOnePastTheDeadline()
    {
        QCOMPARE(graceExpiryDueMs(50), 51);
        QCOMPARE(graceExpiryDueMs(149), 150);
        // Floored at 1: a 0ms single-shot would re-enter every event loop pass.
        QCOMPARE(graceExpiryDueMs(0), 1);
        QCOMPARE(graceExpiryDueMs(-5), 1);
        // Saturates instead of overflowing to a negative interval.
        QCOMPARE(graceExpiryDueMs(std::numeric_limits<qint64>::max()), std::numeric_limits<int>::max());
    }

    /**
     * Earlier deadline wins when the three families share one timer.
     *
     * A family arming with a LATER deadline must not push out a nearer pending
     * one, or the nearer family's release resolves late. The overtaken family
     * re-arms from its own replay tick, so ignoring it here loses nothing.
     */
    void graceRearm_keepsTheEarlierDeadline()
    {
        // Nothing pending: always arm.
        QVERIFY(shouldRearmGraceExpiry(/*timerActive=*/false, /*timerRemainingMs=*/0, /*dueMs=*/151));
        QVERIFY(shouldRearmGraceExpiry(false, 999, 1));
        // Pending deadline is LATER than the new one: the new one wins.
        QVERIFY(shouldRearmGraceExpiry(true, 151, 41));
        // Pending deadline is EARLIER: leave it alone.
        QVERIFY(!shouldRearmGraceExpiry(true, 41, 151));
        // Equal: no reason to restart, and restarting would push the deadline
        // out by the time already elapsed.
        QVERIFY(!shouldRearmGraceExpiry(true, 100, 100));
    }
};

QTEST_MAIN(TestDragActivation)
#include "test_drag_activation.moc"
