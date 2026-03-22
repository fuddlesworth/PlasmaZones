// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// Shared settings bridge for all snapping-mode pages.
// Forwards settingsController / appSettings properties and snapping-specific
// methods so that child card components (MonitorAssignmentsCard,
// QuickLayoutSlotsCard, AppRulesCard, etc.) can bind to `appSettings`.
QtObject {
    // --- Monitor disabled ---
    // --- Screen assignments (snapping) ---
    // --- Screen locking ---
    // --- Per-desktop assignments (snapping) ---
    // --- Per-activity assignments (snapping) ---
    // --- Quick layout slots (snapping) ---
    // --- App rules ---

    readonly property bool autotileEnabled: appSettings.autotileEnabled
    readonly property string autotileAlgorithm: appSettings.autotileAlgorithm
    readonly property string defaultLayoutId: appSettings.defaultLayoutId
    readonly property var screens: settingsController.screens
    readonly property var layouts: settingsController.layouts
    readonly property int assignmentViewMode: 0
    readonly property int virtualDesktopCount: settingsController.virtualDesktopCount
    readonly property var virtualDesktopNames: settingsController.virtualDesktopNames
    readonly property var disabledMonitors: appSettings.disabledMonitors
    readonly property bool activitiesAvailable: settingsController.activitiesAvailable
    readonly property var activities: settingsController.activities
    readonly property string currentActivity: settingsController.currentActivity

    signal screenAssignmentsChanged()
    signal tilingScreenAssignmentsChanged()
    signal tilingDesktopAssignmentsChanged()
    signal lockedScreensChanged()
    signal activityAssignmentsChanged()
    signal tilingActivityAssignmentsChanged()
    signal quickLayoutSlotsChanged()
    signal tilingQuickLayoutSlotsChanged()
    signal appRulesRefreshed()

    function isMonitorDisabled(name) {
        return settingsController.isMonitorDisabled(name);
    }

    function setMonitorDisabled(name, disabled) {
        settingsController.setMonitorDisabled(name, disabled);
    }

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

    function isScreenLocked(screen, mode) {
        return settingsController.isScreenLocked(screen, mode);
    }

    function toggleScreenLock(screen, mode) {
        settingsController.toggleScreenLock(screen, mode);
        lockedScreensChanged();
    }

    function isContextLocked(screen, desktop, activity, mode) {
        return settingsController.isContextLocked(screen, desktop, activity, mode);
    }

    function toggleContextLock(screen, desktop, activity, mode) {
        settingsController.toggleContextLock(screen, desktop, activity, mode);
        lockedScreensChanged();
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

    function getQuickLayoutShortcut(n) {
        return settingsController.getQuickLayoutShortcut(n);
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

}
