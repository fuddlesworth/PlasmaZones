// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <QtGlobal>

namespace PlasmaZones {

/**
 * @brief Pure, state-free resolver for the per-tick "should the snap overlay
 * be active right now" decision.
 *
 * Lives outside @c WindowDragAdaptor so the truth table can be exercised
 * directly in unit tests without standing up a drag adaptor + its compositor
 * dependencies. The adaptor calls this once per @c dragMoved tick with the
 * current input state and previous tick's latch state, and feeds the
 * returned latches back into its own members for the next tick.
 *
 * Toggle-mode rising-edge detection (@c triggerHeld going from false to
 * true while @c toggleMode is on) flips @c activationToggled.
 *
 * The activation list serves dual purpose (#249): when @p alwaysActiveOnDrag
 * is true (the AlwaysActive sentinel is in the list), the active output is
 * inverted — the same non-sentinel triggers that would activate the overlay
 * in normal mode now deactivate it (hold mode → trigger held hides overlay;
 * toggle mode → tap to toggle off the implicitly-on overlay). @p triggerHeld
 * MUST be computed from the non-sentinel entries only (see
 * @c WindowDragAdaptor::anyTriggerHeld with @c excludeSentinel = true) —
 * the sentinel matches every tick by definition, which would otherwise make
 * inversion read as "always held" and the overlay never show.
 */
struct ActivationDecision
{
    bool active = false; ///< Whether the overlay should be active this tick.
    bool nextPrevTriggerHeld = false; ///< Feedback for next tick's edge detection.
    bool nextActivationToggled = false; ///< Feedback for the toggle latch.
};

// PLASMAZONES_EXPORT keeps the symbol in plasmazones_core's dynamic symbol
// table so the unit test (test_drag_activation) can resolve it at runtime.
// Without it the build's effective hidden visibility makes the function
// local and the test fails with an undefined-symbol error.
PLASMAZONES_EXPORT ActivationDecision resolveActivationActive(bool triggerHeld, bool toggleMode,
                                                              bool alwaysActiveOnDrag, bool prevTriggerHeld,
                                                              bool activationToggled);

/// @brief Outcome of one hold-mode release-grace evaluation.
/// See @c resolveHoldGrace below for the contract that produces it.
struct HoldGraceDecision
{
    bool held = false; ///< Effective held state after the grace.
    qint64 nextLastHeldMs = -1; ///< Feedback: last tick the trigger was physically held.
    qint64 remainingMs = 0; ///< Grace left when @c held is true only through the grace, else 0.
};

/**
 * @brief Pure resolver for the hold-mode release grace.
 *
 * A trigger that pairs a mouse button with the drag (right button held
 * while dragging with the left) is released by the same hand that drops the
 * window, and the two buttons rarely let go on the same frame. When the
 * activation button lifts a few milliseconds before the drop, the release
 * tick reached the adaptor first, cleared the zone state, and the window
 * float-dropped even though the user meant to snap. This resolver keeps the
 * trigger reading as held for @p graceMs after the last tick that saw it
 * physically held, so a drop inside that window still snaps.
 *
 * Stateless like @c resolveActivationActive: the adaptor feeds in the
 * timestamp of the last physically-held tick and persists the returned one.
 * @p nowMs and @p lastHeldMs are on the same monotonic clock; a negative
 * @p lastHeldMs means the trigger has not been held during this drag, so
 * there is nothing to extend. A @p graceMs of zero disables the grace and
 * the effective state is the raw one.
 *
 * Only hold mode uses this. Toggle mode has no release to extend, and the
 * always-active inversion (#249) turns hold into deactivate-while-held,
 * where stretching the release would prolong the suppression the user just
 * ended. The caller applies the grace on the raw held state before the
 * toggle / inversion resolver runs, and only in plain hold mode.
 */
PLASMAZONES_EXPORT HoldGraceDecision resolveHoldGrace(bool rawHeld, qint64 nowMs, qint64 lastHeldMs, int graceMs);

/**
 * @brief Deadline for the expiry replay that closes a grace window.
 *
 * One millisecond PAST @p remainingMs so the replayed tick evaluates strictly
 * after the grace rather than on its last millisecond, where resolveHoldGrace
 * would still answer held and the replay would re-arm instead of resolving.
 * Floored at 1: a zero-length QTimer would re-enter on every event loop pass.
 * Saturates rather than overflowing when @p remainingMs is absurd.
 */
PLASMAZONES_EXPORT int graceExpiryDueMs(qint64 remainingMs);

/**
 * @brief Whether an arming request should (re)start the shared expiry timer.
 *
 * Three trigger families share one timer, so the EARLIER deadline has to win:
 * a family that re-arms with a later deadline must not push out a nearer one
 * that is already pending. The family whose deadline was overtaken re-arms
 * from its own replay tick, so nothing is lost by ignoring it here.
 */
PLASMAZONES_EXPORT bool shouldRearmGraceExpiry(bool timerActive, int timerRemainingMs, int dueMs);

} // namespace PlasmaZones
