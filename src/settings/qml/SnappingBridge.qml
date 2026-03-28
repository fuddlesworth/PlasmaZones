// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// Snapping-mode settings bridge.
// Extends SharedBridge with snapping-specific assignment, quick-slot,
// and app-rule methods.
SharedBridge {
    // ─── Screen assignments (snapping) ──────────────────────────────
    // ─── Per-desktop assignments (snapping) ─────────────────────────
    // ─── Per-activity assignments (snapping) ────────────────────────
    // ─── Quick layout slots (snapping) ──────────────────────────────
    // ─── App rules ──────────────────────────────────────────────────

    signal appRulesRefreshed()

    function assignLayoutToScreen(screen, layout) {
        settingsController.assignLayoutToScreen(screen, layout);
        screenAssignmentsChanged();
    }

    function clearScreenAssignment(screen) {
        settingsController.clearScreenAssignment(screen);
        screenAssignmentsChanged();
    }

    function getLayoutForScreen(screen) {
        return settingsController.getLayoutForScreen(screen);
    }

    function hasExplicitAssignmentForScreenDesktop(screen, desktop) {
        return settingsController.hasExplicitAssignmentForScreenDesktop(screen, desktop);
    }

    function getLayoutForScreenDesktop(screen, desktop) {
        return settingsController.getLayoutForScreenDesktop(screen, desktop);
    }

    function getSnappingLayoutForScreenDesktop(screen, desktop) {
        return settingsController.getSnappingLayoutForScreenDesktop(screen, desktop);
    }

    function clearScreenDesktopAssignment(screen, desktop) {
        settingsController.clearScreenDesktopAssignment(screen, desktop);
        screenAssignmentsChanged();
    }

    function assignLayoutToScreenDesktop(screen, desktop, layout) {
        settingsController.assignLayoutToScreenDesktop(screen, desktop, layout);
        screenAssignmentsChanged();
    }

    function hasExplicitAssignmentForScreenActivity(screen, activity) {
        return settingsController.hasExplicitAssignmentForScreenActivity(screen, activity);
    }

    function getLayoutForScreenActivity(screen, activity) {
        return settingsController.getLayoutForScreenActivity(screen, activity);
    }

    function getSnappingLayoutForScreenActivity(screen, activity) {
        return settingsController.getSnappingLayoutForScreenActivity(screen, activity);
    }

    function clearScreenActivityAssignment(screen, activity) {
        settingsController.clearScreenActivityAssignment(screen, activity);
        activityAssignmentsChanged();
    }

    function assignLayoutToScreenActivity(screen, activity, layout) {
        settingsController.assignLayoutToScreenActivity(screen, activity, layout);
        activityAssignmentsChanged();
    }

    function getQuickLayoutSlot(n) {
        return settingsController.getQuickLayoutSlot(n);
    }

    function setQuickLayoutSlot(n, id) {
        settingsController.setQuickLayoutSlot(n, id);
        quickLayoutSlotsChanged();
    }

    function getAppRulesForLayout(id) {
        return settingsController.getAppRulesForLayout(id);
    }

    function addAppRuleToLayout(id, pattern, zone, screen) {
        settingsController.addAppRuleToLayout(id, pattern, zone, screen);
        appRulesRefreshed();
    }

    function removeAppRuleFromLayout(id, idx) {
        settingsController.removeAppRuleFromLayout(id, idx);
        appRulesRefreshed();
    }

    function getRunningWindows() {
        return settingsController.getRunningWindows();
    }

    assignmentViewMode: 0
}
