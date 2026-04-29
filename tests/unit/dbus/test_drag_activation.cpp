// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_drag_activation.cpp
 * @brief Truth-table guard for resolveActivationActive() — the pure
 *        per-tick decision the WindowDragAdaptor uses to decide whether
 *        the snap overlay should be shown.
 *
 * The function lives in src/dbus/windowdragadaptor/dragactivation.cpp so the
 * truth table can be exercised without standing up the adaptor + its
 * compositor dependencies. Pinning the table here means the always-active
 * inversion (#249) — where the same activation triggers serve double duty
 * as deactivate-while-held / toggle-off — can't drift.
 */

#include <QTest>
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
};

QTEST_MAIN(TestDragActivation)
#include "test_drag_activation.moc"
