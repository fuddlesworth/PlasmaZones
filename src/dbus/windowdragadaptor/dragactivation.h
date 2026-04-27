// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

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

} // namespace PlasmaZones
