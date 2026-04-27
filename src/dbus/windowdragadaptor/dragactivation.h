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
 * true while @c toggleMode is on) flips @c activationToggled. The
 * deactivation gate (#249) is applied AFTER toggle resolution so a
 * toggled-on overlay also hides while held; releasing the deactivation
 * trigger restores the prior toggle state without flipping it.
 *
 * Deactivation is gated on @p alwaysActiveOnDrag — it's only meaningful
 * when "Activate on every drag" is on, matching the UI surface.
 */
struct ActivationDecision
{
    bool active = false; ///< Whether the overlay should be active this tick.
    bool nextPrevTriggerHeld = false; ///< Feedback for next tick's edge detection.
    bool nextActivationToggled = false; ///< Feedback for the toggle latch.
};

PLASMAZONES_EXPORT ActivationDecision resolveActivationActive(bool triggerHeld, bool deactivateHeld, bool toggleMode,
                                                              bool alwaysActiveOnDrag, bool prevTriggerHeld,
                                                              bool activationToggled);

} // namespace PlasmaZones
