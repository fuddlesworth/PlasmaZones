// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// Shared settings bridge for all tiling-mode pages.
// Forwards settingsController / appSettings properties and tiling-specific
// methods so that child card components (MonitorAssignmentsCard,
// QuickLayoutSlotsCard, etc.) can bind to `appSettings`.
QtObject {
    // --- Monitor disabled ---
    // --- Tiling screen assignments ---
    // --- Screen locking ---
    // --- Per-desktop tiling assignments ---
    // --- Per-activity tiling assignments ---
    // --- Quick layout slots (tiling) ---
    // lockedScreensChanged is forwarded via _externalSignals Connections
    // lockedScreensChanged is forwarded via _externalSignals Connections

    readonly property bool autotileEnabled: appSettings.autotileEnabled
    readonly property string autotileAlgorithm: appSettings.autotileAlgorithm
    readonly property string defaultLayoutId: appSettings.defaultLayoutId
    readonly property var screens: settingsController.screens
    readonly property var layouts: settingsController.layouts
    readonly property int assignmentViewMode: 1
    readonly property int virtualDesktopCount: settingsController.virtualDesktopCount
    readonly property var virtualDesktopNames: settingsController.virtualDesktopNames
    readonly property var disabledMonitors: appSettings.disabledMonitors
    readonly property bool activitiesAvailable: settingsController.activitiesAvailable
    readonly property var activities: settingsController.activities
    readonly property string currentActivity: settingsController.currentActivity
    // Forward external lock/slot changes (daemon shortcuts) to QML consumers
    property Connections _externalSignals

    _externalSignals: Connections {
        function onLockedScreensChanged() {
            lockedScreensChanged();
        }

        function onQuickLayoutSlotsChanged() {
            quickLayoutSlotsChanged();
        }

        function onScreenLayoutChanged() {
            screenAssignmentsChanged();
        }

        target: settingsController
    }

    signal screenAssignmentsChanged()
    signal tilingScreenAssignmentsChanged()
    signal tilingDesktopAssignmentsChanged()
    signal lockedScreensChanged()
    signal activityAssignmentsChanged()
    signal tilingActivityAssignmentsChanged()
    signal quickLayoutSlotsChanged()
    signal tilingQuickLayoutSlotsChanged()

    function isMonitorDisabled(name) {
        return settingsController.isMonitorDisabled(name);
    }

    function setMonitorDisabled(name, disabled) {
        settingsController.setMonitorDisabled(name, disabled);
    }

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

    function isScreenLocked(screen, mode) {
        return settingsController.isScreenLocked(screen, mode);
    }

    function toggleScreenLock(screen, mode) {
        settingsController.toggleScreenLock(screen, mode);
    }

    function isContextLocked(screen, desktop, activity, mode) {
        return settingsController.isContextLocked(screen, desktop, activity, mode);
    }

    function toggleContextLock(screen, desktop, activity, mode) {
        settingsController.toggleContextLock(screen, desktop, activity, mode);
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

    function getQuickLayoutShortcut(n) {
        return settingsController.getQuickLayoutShortcut(n);
    }

    function getTilingQuickLayoutSlot(n) {
        return settingsController.getTilingQuickLayoutSlot(n);
    }

    function setTilingQuickLayoutSlot(n, id) {
        settingsController.setTilingQuickLayoutSlot(n, id);
        tilingQuickLayoutSlotsChanged();
    }

}
