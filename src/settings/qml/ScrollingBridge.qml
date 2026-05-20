// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Scrolling-mode settings bridge.
// Adds nothing to SharedBridge beyond the view-mode selector: scroll mode has
// no per-context layout assignment (a screen is put into scroll mode through
// the Layouts picker, not a per-monitor dropdown), only the per-context
// enable/disable gates that SharedBridge already exposes.

import QtQuick
import org.plasmazones.settings

// AssignmentEntry.Scroll names the same integer as the C++ enum so every
// isMonitorDisabled / setDesktopDisabled / ... call routes to the scroll
// disable lists — a magic literal would silently desync if the C++ enum
// values are ever renumbered.
SharedBridge {
    id: bridge

    assignmentViewMode: AssignmentEntry.Scroll

    // Tick counters bumped from the SharedBridge notify signals so QML
    // bindings that consult `isMonitorDisabled(name)` etc. (no QML-side
    // dependency on a mutating property otherwise) re-evaluate when the
    // settings controller flips a disable list. Without this, the binding
    // is one-shot — the page used to cache the value into a local property
    // and re-read it from a `Connections` handler, which races the
    // initial signal-target wiring.
    property int disabledMonitorsTick: 0
    property int disabledDesktopsTick: 0
    property int disabledActivitiesTick: 0

    onDisabledMonitorsChanged: bridge.disabledMonitorsTick++
    onDisabledDesktopsChanged: bridge.disabledDesktopsTick++
    onDisabledActivitiesChanged: bridge.disabledActivitiesTick++
}
