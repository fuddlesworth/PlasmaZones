// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Base settings bridge shared by SnappingBridge and TilingBridge.
// Contains all properties, signals, and methods that are identical across
// both snapping and tiling assignment pages: monitor/desktop/activity
// disable state, screen locking, external signal forwarding, etc.

import QtQuick

// Subclasses set assignmentViewMode and add mode-specific methods
// (e.g. assignLayoutToScreen vs assignTilingLayoutToScreen).
QtObject {
    // ─── Monitor disable ────────────────────────────────────────────
    // ─── Desktop disable ────────────────────────────────────────────
    // ─── Activity disable ───────────────────────────────────────────
    // ─── Screen locking ─────────────────────────────────────────────
    // ─── Shortcuts ──────────────────────────────────────────────────
    // ─── External signal forwarding ─────────────────────────────────
    // assignmentViewMode is the per-page mode set by the SnappingBridge /
    // TilingBridge subclasses (0 = snapping, 1 = tiling) and forwarded into
    // every disable check / mutation so the snapping page only reads-and-writes
    // the snapping list, the tiling page only the tiling list.

    // 0 = snapping, 1 = tiling — overridden by subclass
    property int assignmentViewMode: -1
    readonly property bool autotileEnabled: appSettings.autotileEnabled
    readonly property string defaultAutotileAlgorithm: appSettings.defaultAutotileAlgorithm
    readonly property string defaultLayoutId: appSettings.defaultLayoutId
    readonly property var screens: settingsController.screens
    readonly property var layouts: settingsController.layouts
    readonly property int virtualDesktopCount: settingsController.virtualDesktopCount
    readonly property var virtualDesktopNames: settingsController.virtualDesktopNames
    readonly property bool activitiesAvailable: settingsController.activitiesAvailable
    readonly property var activities: settingsController.activities
    readonly property string currentActivity: settingsController.currentActivity
    // Forward external changes (daemon shortcuts) to QML consumers
    property Connections _externalSignals

    signal disabledDesktopsChanged()
    signal disabledActivitiesChanged()
    signal screenAssignmentsChanged()
    signal tilingScreenAssignmentsChanged()
    signal tilingDesktopAssignmentsChanged()
    signal lockedScreensChanged()
    signal activityAssignmentsChanged()
    signal tilingActivityAssignmentsChanged()
    signal quickLayoutSlotsChanged()
    signal tilingQuickLayoutSlotsChanged()

    function isMonitorDisabled(name) {
        return settingsController.isMonitorDisabled(assignmentViewMode, name);
    }

    function setMonitorDisabled(name, disabled) {
        settingsController.setMonitorDisabled(assignmentViewMode, name, disabled);
    }

    function isDesktopDisabled(screenName, desktop) {
        return settingsController.isDesktopDisabled(assignmentViewMode, screenName, desktop);
    }

    function setDesktopDisabled(screenName, desktop, disabled) {
        settingsController.setDesktopDisabled(assignmentViewMode, screenName, desktop, disabled);
    }

    function isActivityDisabled(screenName, activityId) {
        return settingsController.isActivityDisabled(assignmentViewMode, screenName, activityId);
    }

    function setActivityDisabled(screenName, activityId, disabled) {
        settingsController.setActivityDisabled(assignmentViewMode, screenName, activityId, disabled);
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

    function getQuickLayoutShortcut(n) {
        return settingsController.getQuickLayoutShortcut(n);
    }

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

        // Per-mode signals carry the mode that flipped. Forward only when the
        // change is for the page's mode — the other page's bridge will see it
        // on its own connection.
        function onDisabledDesktopsChanged(viewMode) {
            if (viewMode === assignmentViewMode)
                disabledDesktopsChanged();

        }

        function onDisabledActivitiesChanged(viewMode) {
            if (viewMode === assignmentViewMode)
                disabledActivitiesChanged();

        }

        target: settingsController
    }

}
