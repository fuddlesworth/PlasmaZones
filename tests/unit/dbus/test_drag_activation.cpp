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
 * compositor dependencies. Pinning the table here means the runtime gate
 * for the deactivate-while-held trigger (#249) — including the
 * always-active gate that keeps it from silently suppressing the overlay
 * for hold-to-activate users — can't drift.
 */

#include <QTest>
#include <QObject>

#include "dbus/windowdragadaptor/dragactivation.h"

using namespace PlasmaZones;

class TestDragActivation : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ─── Hold-to-activate (toggleMode = false) ─────────────────────────────

    void hold_triggerHeld_active()
    {
        const auto d = resolveActivationActive(/*triggerHeld=*/true, /*deactivateHeld=*/false,
                                               /*toggleMode=*/false, /*alwaysActive=*/false,
                                               /*prevTriggerHeld=*/false, /*activationToggled=*/false);
        QVERIFY(d.active);
        QVERIFY(d.nextPrevTriggerHeld);
        QVERIFY(!d.nextActivationToggled);
    }

    void hold_triggerReleased_inactive()
    {
        const auto d = resolveActivationActive(false, false, false, false, true, false);
        QVERIFY(!d.active);
        QVERIFY(!d.nextPrevTriggerHeld);
    }

    // ─── Toggle mode rising-edge latch ─────────────────────────────────────

    void toggle_risingEdge_flipsLatch()
    {
        const auto d = resolveActivationActive(/*triggerHeld=*/true, false, /*toggleMode=*/true, false,
                                               /*prevTriggerHeld=*/false, /*activationToggled=*/false);
        QVERIFY(d.active);
        QVERIFY(d.nextActivationToggled);
    }

    void toggle_releasedAfterToggleOn_staysActive()
    {
        // Trigger released after a previous tick toggled the latch on — the
        // overlay must remain on. This is the core toggle-mode contract.
        const auto d = resolveActivationActive(false, false, true, false, true, true);
        QVERIFY(d.active);
        QVERIFY(d.nextActivationToggled);
        QVERIFY(!d.nextPrevTriggerHeld);
    }

    void toggle_secondPress_flipsBackOff()
    {
        const auto d = resolveActivationActive(true, false, true, false, false, true);
        QVERIFY(!d.active);
        QVERIFY(!d.nextActivationToggled);
    }

    void toggle_heldContinuously_doesNotReToggle()
    {
        // Same trigger held across consecutive ticks must NOT re-flip the
        // latch — only the rising edge counts.
        const auto d = resolveActivationActive(true, false, true, false, true, true);
        QVERIFY(d.active);
        QVERIFY(d.nextActivationToggled);
    }

    // ─── Deactivation override (#249) — gated on alwaysActiveOnDrag ───────

    void deactivation_alwaysActiveOff_neverSuppresses()
    {
        // Hold-to-activate user who somehow has a deactivation trigger
        // configured (e.g. set it while in always-active mode then switched
        // back) must not get silent suppression — the runtime gate matches
        // the UI gate.
        const auto d = resolveActivationActive(/*triggerHeld=*/true, /*deactivateHeld=*/true, false,
                                               /*alwaysActive=*/false, true, false);
        QVERIFY2(d.active,
                 "Deactivation must be a no-op outside always-active mode (#249) — UI hides the row, runtime matches");
    }

    void deactivation_alwaysActiveOn_suppresses()
    {
        const auto d =
            resolveActivationActive(true, /*deactivateHeld=*/true, false, /*alwaysActive=*/true, false, false);
        QVERIFY(!d.active);
    }

    void deactivation_alwaysActiveOn_releasedRestoresActive()
    {
        // Always-active user releases the deactivation trigger — the overlay
        // returns immediately because activation is implicit.
        const auto d = resolveActivationActive(true, /*deactivateHeld=*/false, false, true, false, false);
        QVERIFY(d.active);
    }

    void deactivation_doesNotMutateToggleLatch()
    {
        // Toggle-mode + always-active + deactivation: the latch must survive
        // an entire press/release cycle of the deactivation trigger so the
        // overlay returns to its toggled-on state on release. This is the
        // load-bearing claim from the PR description.
        const auto pressed =
            resolveActivationActive(/*triggerHeld=*/false, /*deactivateHeld=*/true, /*toggleMode=*/true,
                                    /*alwaysActive=*/true, /*prevTriggerHeld=*/false,
                                    /*activationToggled=*/true);
        QVERIFY2(!pressed.active, "Overlay must hide while deactivation is held");
        QVERIFY2(pressed.nextActivationToggled, "Toggle latch must NOT flip — release should restore prior state");

        const auto released = resolveActivationActive(false, false, true, true, false, pressed.nextActivationToggled);
        QVERIFY2(released.active, "Overlay must return to its pre-deactivation toggled-on state");
    }
};

QTEST_MAIN(TestDragActivation)
#include "test_drag_activation.moc"
