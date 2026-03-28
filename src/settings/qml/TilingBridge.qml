// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// Tiling-mode settings bridge.
// Extends SharedBridge with tiling-specific assignment and quick-slot methods.
SharedBridge {
    // ─── Screen assignments (tiling) ────────────────────────────────
    // ─── Per-desktop assignments (tiling) ───────────────────────────
    // ─── Per-activity assignments (tiling) ──────────────────────────
    // ─── Quick layout slots (tiling) ────────────────────────────────

    function assignTilingLayoutToScreen(screen, layout) {
        settingsController.assignTilingLayoutToScreen(screen, layout);
        tilingScreenAssignmentsChanged();
    }

    function clearTilingScreenAssignment(screen) {
        settingsController.clearTilingScreenAssignment(screen);
        tilingScreenAssignmentsChanged();
    }

    function getTilingLayoutForScreen(screen) {
        return settingsController.getTilingLayoutForScreen(screen);
    }

    function hasExplicitTilingAssignmentForScreenDesktop(screen, desktop) {
        return settingsController.hasExplicitTilingAssignmentForScreenDesktop(screen, desktop);
    }

    function getTilingLayoutForScreenDesktop(screen, desktop) {
        return settingsController.getTilingLayoutForScreenDesktop(screen, desktop);
    }

    function assignTilingLayoutToScreenDesktop(screen, desktop, layout) {
        settingsController.assignTilingLayoutToScreenDesktop(screen, desktop, layout);
        tilingScreenAssignmentsChanged();
    }

    function clearTilingScreenDesktopAssignment(screen, desktop) {
        settingsController.clearTilingScreenDesktopAssignment(screen, desktop);
        tilingScreenAssignmentsChanged();
    }

    function hasExplicitTilingAssignmentForScreenActivity(screen, activity) {
        return settingsController.hasExplicitTilingAssignmentForScreenActivity(screen, activity);
    }

    function getTilingLayoutForScreenActivity(screen, activity) {
        return settingsController.getTilingLayoutForScreenActivity(screen, activity);
    }

    function assignTilingLayoutToScreenActivity(screen, activity, layout) {
        settingsController.assignTilingLayoutToScreenActivity(screen, activity, layout);
        tilingActivityAssignmentsChanged();
    }

    function clearTilingScreenActivityAssignment(screen, activity) {
        settingsController.clearTilingScreenActivityAssignment(screen, activity);
        tilingActivityAssignmentsChanged();
    }

    function getTilingQuickLayoutSlot(n) {
        return settingsController.getTilingQuickLayoutSlot(n);
    }

    function setTilingQuickLayoutSlot(n, id) {
        settingsController.setTilingQuickLayoutSlot(n, id);
        tilingQuickLayoutSlotsChanged();
    }

    assignmentViewMode: 1
}
