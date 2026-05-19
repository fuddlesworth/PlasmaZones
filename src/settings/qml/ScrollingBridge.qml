// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Scrolling-mode settings bridge.
// Adds nothing to SharedBridge beyond the view-mode selector: scroll mode has
// no per-context layout assignment (a screen is put into scroll mode through
// the Layouts picker, not a per-monitor dropdown), only the per-context
// enable/disable gates that SharedBridge already exposes.

import QtQuick

// assignmentViewMode 2 == PhosphorZones::AssignmentEntry::Scroll, so every
// isMonitorDisabled/setDesktopDisabled/... call routes to the scroll lists.
SharedBridge {
    assignmentViewMode: 2
}
