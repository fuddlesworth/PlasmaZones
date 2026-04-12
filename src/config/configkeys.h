// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

// Macro to define a static config key accessor returning a QStringLiteral.
// Usage: PZ_CONFIG_KEY(snappingEnabledKey, "SnappingEnabled")
// Expands to: static QString snappingEnabledKey() { return QStringLiteral("SnappingEnabled"); }
#define PZ_CONFIG_KEY(name, str)                                                                                       \
    static QString name()                                                                                              \
    {                                                                                                                  \
        return QStringLiteral(str);                                                                                    \
    }

// Alias for group name accessors — identical expansion, separate macro for readability.
#define PZ_CONFIG_GROUP(name, str)                                                                                     \
    static QString name()                                                                                              \
    {                                                                                                                  \
        return QStringLiteral(str);                                                                                    \
    }

namespace PlasmaZones {

/**
 * @brief Static config group names, key strings, and JSON field name accessors.
 *
 * v2 schema: groups use dot-separated paths that mirror the settings UI hierarchy.
 * e.g. "Snapping.Behavior.ZoneSpan" → settings JSON: { "Snapping": { "Behavior": { "ZoneSpan": { ... } } } }
 *
 * ConfigDefaults inherits from ConfigKeys so all call sites
 * (e.g. ConfigDefaults::enabledKey()) continue to work.
 */
class ConfigKeys
{
public:
    // ═══════════════════════════════════════════════════════════════════════════
    // Config Group Names — v2 dot-path hierarchy
    // ═══════════════════════════════════════════════════════════════════════════

    // Schema version key (stored at JSON root)
    PZ_CONFIG_KEY(versionKey, "_version")

    // Top-level groups
    PZ_CONFIG_GROUP(generalGroup, "General")
    PZ_CONFIG_GROUP(snappingGroup, "Snapping")
    PZ_CONFIG_GROUP(tilingGroup, "Tiling")
    PZ_CONFIG_GROUP(exclusionsGroup, "Exclusions")
    PZ_CONFIG_GROUP(performanceGroup, "Performance")
    PZ_CONFIG_GROUP(renderingGroup, "Rendering")
    PZ_CONFIG_GROUP(shadersGroup, "Shaders")
    PZ_CONFIG_GROUP(animationsGroup, "Animations")
    PZ_CONFIG_GROUP(shortcutsGlobalGroup, "Shortcuts.Global")
    PZ_CONFIG_GROUP(shortcutsTilingGroup, "Shortcuts.Tiling")
    PZ_CONFIG_GROUP(orderingGroup, "Ordering")
    PZ_CONFIG_GROUP(updatesGroup, "Updates")

    // Snapping sub-groups
    PZ_CONFIG_GROUP(snappingAppearanceGroup, "Snapping.Appearance")
    PZ_CONFIG_GROUP(snappingBehaviorGroup, "Snapping.Behavior")
    PZ_CONFIG_GROUP(snappingBehaviorZoneSpanGroup, "Snapping.Behavior.ZoneSpan")
    PZ_CONFIG_GROUP(snappingBehaviorSnapAssistGroup, "Snapping.Behavior.SnapAssist")
    PZ_CONFIG_GROUP(snappingBehaviorDisplayGroup, "Snapping.Behavior.Display")
    PZ_CONFIG_GROUP(snappingBehaviorWindowHandlingGroup, "Snapping.Behavior.WindowHandling")
    PZ_CONFIG_GROUP(snappingAppearanceColorsGroup, "Snapping.Appearance.Colors")
    PZ_CONFIG_GROUP(snappingAppearanceOpacityGroup, "Snapping.Appearance.Opacity")
    PZ_CONFIG_GROUP(snappingAppearanceBorderGroup, "Snapping.Appearance.Border")
    PZ_CONFIG_GROUP(snappingAppearanceLabelsGroup, "Snapping.Appearance.Labels")
    PZ_CONFIG_GROUP(snappingEffectsGroup, "Snapping.Effects")
    PZ_CONFIG_GROUP(snappingZoneSelectorGroup, "Snapping.ZoneSelector")
    PZ_CONFIG_GROUP(snappingGapsGroup, "Snapping.Gaps")

    // Tiling sub-groups
    PZ_CONFIG_GROUP(tilingAppearanceGroup, "Tiling.Appearance")
    PZ_CONFIG_GROUP(tilingAlgorithmGroup, "Tiling.Algorithm")
    PZ_CONFIG_GROUP(tilingBehaviorGroup, "Tiling.Behavior")
    PZ_CONFIG_GROUP(tilingBehaviorTriggersGroup, "Tiling.Behavior.Triggers")
    PZ_CONFIG_GROUP(tilingAppearanceColorsGroup, "Tiling.Appearance.Colors")
    PZ_CONFIG_GROUP(tilingAppearanceDecorationsGroup, "Tiling.Appearance.Decorations")
    PZ_CONFIG_GROUP(tilingAppearanceBordersGroup, "Tiling.Appearance.Borders")
    PZ_CONFIG_GROUP(tilingGapsGroup, "Tiling.Gaps")

    // Parent groups (for purge enumeration — covers all sub-groups)
    PZ_CONFIG_GROUP(shortcutsGroup, "Shortcuts")
    PZ_CONFIG_GROUP(editorGroup, "Editor")

    // Editor sub-groups
    PZ_CONFIG_GROUP(editorShortcutsGroup, "Editor.Shortcuts")
    PZ_CONFIG_GROUP(editorSnappingGroup, "Editor.Snapping")
    PZ_CONFIG_GROUP(editorFillOnDropGroup, "Editor.FillOnDrop")

    // Unmanaged groups (not purged by save(), written independently)
    PZ_CONFIG_GROUP(tilingQuickLayoutSlotsGroup, "TilingQuickLayoutSlots")
    PZ_CONFIG_GROUP(windowTrackingGroup, "WindowTracking")

    // Assignment group prefix (used in assignments.json and migration code)
    PZ_CONFIG_GROUP(assignmentGroupPrefix, "Assignment:")
    PZ_CONFIG_GROUP(quickLayoutsGroup, "QuickLayouts")
    PZ_CONFIG_GROUP(modeTrackingGroup, "ModeTracking")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Window Tracking (session.json, written by WTA)
    //
    // The WindowTracking group stores ephemeral per-session window state that is
    // NOT part of user preferences. It lives in session.json (separate from
    // config.json) to avoid write contention with user preference saves.
    // Owned by WindowTrackingAdaptor and saved via its debounced save cycle.
    //
    // Autotile data is split across three locations:
    //   1. Tiling.Algorithm group (Settings, config.json) — global defaults:
    //      algorithm, splitRatio, masterCount, maxWindows, splitRatioStep,
    //      perAlgorithmSettings
    //   2. AutotileScreen:<id> groups (Settings, config.json) — per-screen
    //      overrides for masterCount, splitRatio, algorithm
    //   3. WindowTracking.AutotileWindowOrders (WTA, session.json) — per-context
    //      window order and floating state
    //
    // Settings (1, 2) are user preferences. WindowTracking (3) is session state.
    // Settings::reset() deletes session.json and per-screen groups.
    // ═══════════════════════════════════════════════════════════════════════════

    // Snap mode — zone assignments and restore queues
    PZ_CONFIG_KEY(activeLayoutIdKey, "ActiveLayoutId")
    PZ_CONFIG_KEY(windowZoneAssignmentsFullKey, "WindowZoneAssignmentsFull")
    PZ_CONFIG_KEY(pendingRestoreQueuesKey, "PendingRestoreQueues")

    // Snap mode — pre-tile geometry (for float-toggle restore)
    PZ_CONFIG_KEY(preTileGeometriesFullKey, "PreTileGeometriesFull")
    PZ_CONFIG_KEY(preTileGeometriesKey, "PreTileGeometries")

    // Snap mode — last used zone info
    PZ_CONFIG_KEY(lastUsedZoneIdKey, "LastUsedZoneId")
    PZ_CONFIG_KEY(lastUsedScreenNameKey, "LastUsedScreenName")
    PZ_CONFIG_KEY(lastUsedZoneClassKey, "LastUsedZoneClass")
    PZ_CONFIG_KEY(lastUsedDesktopKey, "LastUsedDesktop")

    // Snap mode — pre-float state (for unfloating after session restore)
    PZ_CONFIG_KEY(preFloatZoneAssignmentsKey, "PreFloatZoneAssignments")
    PZ_CONFIG_KEY(preFloatScreenAssignmentsKey, "PreFloatScreenAssignments")

    // Snap mode — user-snapped classes
    PZ_CONFIG_KEY(userSnappedClassesKey, "UserSnappedClasses")

    // Autotile mode — per-context window order and floating state
    PZ_CONFIG_KEY(autotileWindowOrdersKey, "AutotileWindowOrders")
    // Autotile mode — pending restore queue for close/reopen window preservation
    PZ_CONFIG_KEY(autotilePendingRestoresKey, "AutotilePendingRestores")

    // Obsolete keys (cleaned up on save to prevent stale data)
    PZ_CONFIG_KEY(obsoleteFloatingWindowsKey, "FloatingWindows")
    PZ_CONFIG_KEY(obsoletePendingWindowScreenAssignmentsKey, "PendingWindowScreenAssignments")
    PZ_CONFIG_KEY(obsoletePendingWindowDesktopAssignmentsKey, "PendingWindowDesktopAssignments")
    PZ_CONFIG_KEY(obsoletePendingWindowLayoutAssignmentsKey, "PendingWindowLayoutAssignments")
    PZ_CONFIG_KEY(obsoletePendingWindowZoneNumbersKey, "PendingWindowZoneNumbers")
    PZ_CONFIG_KEY(obsoleteWindowZoneAssignmentsKey, "WindowZoneAssignments")
    PZ_CONFIG_KEY(obsoleteWindowScreenAssignmentsKey, "WindowScreenAssignments")
    PZ_CONFIG_KEY(obsoleteWindowDesktopAssignmentsKey, "WindowDesktopAssignments")

    // ═══════════════════════════════════════════════════════════════════════════
    // Trigger JSON Field Names
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(triggerModifierField, "modifier")
    PZ_CONFIG_KEY(triggerMouseButtonField, "mouseButton")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Rendering
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(backendKey, "Backend")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping (top-level)
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(enabledKey, "Enabled")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.Behavior
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(triggersKey, "Triggers")
    PZ_CONFIG_KEY(toggleActivationKey, "ToggleActivation")

    // Snapping.Behavior.ZoneSpan
    // (uses enabledKey and triggersKey)

    // Snapping.Behavior.SnapAssist
    PZ_CONFIG_KEY(featureEnabledKey, "FeatureEnabled")
    // (also uses enabledKey and triggersKey)

    // Snapping.Behavior.Display
    PZ_CONFIG_KEY(showOnAllMonitorsKey, "ShowOnAllMonitors")
    PZ_CONFIG_KEY(disabledMonitorsKey, "DisabledMonitors")
    PZ_CONFIG_KEY(disabledDesktopsKey, "DisabledDesktops")
    PZ_CONFIG_KEY(disabledActivitiesKey, "DisabledActivities")
    PZ_CONFIG_KEY(filterByAspectRatioKey, "FilterByAspectRatio")

    // Snapping.Behavior.WindowHandling
    PZ_CONFIG_KEY(keepOnResolutionChangeKey, "KeepOnResolutionChange")
    PZ_CONFIG_KEY(moveNewToLastZoneKey, "MoveNewToLastZone")
    PZ_CONFIG_KEY(restoreOnUnsnapKey, "RestoreOnUnsnap")
    PZ_CONFIG_KEY(restoreOnLoginKey, "RestoreOnLogin")
    PZ_CONFIG_KEY(stickyWindowHandlingKey, "StickyWindowHandling")
    PZ_CONFIG_KEY(defaultLayoutIdKey, "DefaultLayoutId")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.Appearance.Colors
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(useSystemKey, "UseSystem")
    PZ_CONFIG_KEY(highlightKey, "Highlight")
    PZ_CONFIG_KEY(inactiveKey, "Inactive")
    PZ_CONFIG_KEY(borderKey, "Border")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.Appearance.Opacity
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(activeKey, "Active")
    // (also uses inactiveKey)

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.Appearance.Border
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(widthKey, "Width")
    PZ_CONFIG_KEY(radiusKey, "Radius")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.Appearance.Labels
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(fontColorKey, "FontColor")
    PZ_CONFIG_KEY(fontFamilyKey, "FontFamily")
    PZ_CONFIG_KEY(fontSizeScaleKey, "FontSizeScale")
    PZ_CONFIG_KEY(fontWeightKey, "FontWeight")
    PZ_CONFIG_KEY(fontItalicKey, "FontItalic")
    PZ_CONFIG_KEY(fontUnderlineKey, "FontUnderline")
    PZ_CONFIG_KEY(fontStrikeoutKey, "FontStrikeout")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.Effects
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(blurKey, "Blur")
    PZ_CONFIG_KEY(showNumbersKey, "ShowNumbers")
    PZ_CONFIG_KEY(flashOnSwitchKey, "FlashOnSwitch")
    PZ_CONFIG_KEY(osdOnLayoutSwitchKey, "OsdOnLayoutSwitch")
    PZ_CONFIG_KEY(navigationOsdKey, "NavigationOsd")
    PZ_CONFIG_KEY(osdStyleKey, "OsdStyle")
    PZ_CONFIG_KEY(overlayDisplayModeKey, "OverlayDisplayMode")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.ZoneSelector
    // ═══════════════════════════════════════════════════════════════════════════

    // (uses enabledKey)
    PZ_CONFIG_KEY(triggerDistanceKey, "TriggerDistance")
    PZ_CONFIG_KEY(positionKey, "Position")
    PZ_CONFIG_KEY(layoutModeKey, "LayoutMode")
    PZ_CONFIG_KEY(sizeModeKey, "SizeMode")
    PZ_CONFIG_KEY(maxRowsKey, "MaxRows")
    PZ_CONFIG_KEY(previewWidthKey, "PreviewWidth")
    PZ_CONFIG_KEY(previewHeightKey, "PreviewHeight")
    PZ_CONFIG_KEY(previewLockAspectKey, "PreviewLockAspect")
    PZ_CONFIG_KEY(gridColumnsKey, "GridColumns")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.Gaps
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(innerKey, "Inner")
    PZ_CONFIG_KEY(outerKey, "Outer")
    PZ_CONFIG_KEY(usePerSideKey, "UsePerSide")
    PZ_CONFIG_KEY(topKey, "Top")
    PZ_CONFIG_KEY(bottomKey, "Bottom")
    PZ_CONFIG_KEY(leftKey, "Left")
    PZ_CONFIG_KEY(rightKey, "Right")
    PZ_CONFIG_KEY(adjacentThresholdKey, "AdjacentThreshold")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Tiling (top-level)
    // ═══════════════════════════════════════════════════════════════════════════

    // (uses enabledKey)

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Tiling.Algorithm
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(defaultKey, "Default")
    PZ_CONFIG_KEY(splitRatioKey, "SplitRatio")
    PZ_CONFIG_KEY(splitRatioStepKey, "SplitRatioStep")
    PZ_CONFIG_KEY(masterCountKey, "MasterCount")
    PZ_CONFIG_KEY(maxWindowsKey, "MaxWindows")
    PZ_CONFIG_KEY(perAlgorithmSettingsKey, "PerAlgorithmSettings")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Tiling.Behavior
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(insertPositionKey, "InsertPosition")
    PZ_CONFIG_KEY(focusNewWindowsKey, "FocusNewWindows")
    PZ_CONFIG_KEY(focusFollowsMouseKey, "FocusFollowsMouse")
    PZ_CONFIG_KEY(respectMinimumSizeKey, "RespectMinimumSize")
    // (also uses stickyWindowHandlingKey)
    PZ_CONFIG_KEY(lockedScreensKey, "LockedScreens")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Tiling.Appearance.Colors
    // ═══════════════════════════════════════════════════════════════════════════

    // (uses useSystemKey, activeKey, inactiveKey)

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Tiling.Appearance.Decorations
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(hideTitleBarsKey, "HideTitleBars")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Tiling.Appearance.Borders
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(showBorderKey, "ShowBorder")
    // (also uses widthKey, radiusKey)

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Tiling.Gaps
    // ═══════════════════════════════════════════════════════════════════════════

    // (uses innerKey, outerKey, usePerSideKey, topKey, bottomKey, leftKey, rightKey)
    PZ_CONFIG_KEY(smartGapsKey, "SmartGaps")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Exclusions
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(transientWindowsKey, "TransientWindows")
    PZ_CONFIG_KEY(minimumWindowWidthKey, "MinimumWindowWidth")
    PZ_CONFIG_KEY(minimumWindowHeightKey, "MinimumWindowHeight")
    PZ_CONFIG_KEY(applicationsKey, "Applications")
    PZ_CONFIG_KEY(windowClassesKey, "WindowClasses")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Performance
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(pollIntervalMsKey, "PollIntervalMs")
    PZ_CONFIG_KEY(minimumZoneSizePxKey, "MinimumZoneSizePx")
    PZ_CONFIG_KEY(minimumZoneDisplaySizePxKey, "MinimumZoneDisplaySizePx")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Shaders
    // ═══════════════════════════════════════════════════════════════════════════

    // (uses enabledKey)
    PZ_CONFIG_KEY(frameRateKey, "FrameRate")
    PZ_CONFIG_KEY(audioVisualizerKey, "AudioVisualizer")
    PZ_CONFIG_KEY(audioSpectrumBarCountKey, "AudioSpectrumBarCount")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Animations
    // ═══════════════════════════════════════════════════════════════════════════

    // (uses enabledKey)
    PZ_CONFIG_KEY(durationKey, "Duration")
    PZ_CONFIG_KEY(easingCurveKey, "EasingCurve")
    PZ_CONFIG_KEY(minDistanceKey, "MinDistance")
    PZ_CONFIG_KEY(sequenceModeKey, "SequenceMode")
    PZ_CONFIG_KEY(staggerIntervalKey, "StaggerInterval")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Shortcuts.Global
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(openEditorKey, "OpenEditor")
    PZ_CONFIG_KEY(openSettingsKey, "OpenSettings")
    PZ_CONFIG_KEY(previousLayoutKey, "PreviousLayout")
    PZ_CONFIG_KEY(nextLayoutKey, "NextLayout")

    // Parameterized — uses the pattern accessor to avoid duplication
    PZ_CONFIG_KEY(quickLayoutKeyPattern, "QuickLayout%1")
    static QString quickLayoutKey(int n)
    {
        return quickLayoutKeyPattern().arg(n);
    }

    PZ_CONFIG_KEY(moveWindowLeftKey, "MoveWindowLeft")
    PZ_CONFIG_KEY(moveWindowRightKey, "MoveWindowRight")
    PZ_CONFIG_KEY(moveWindowUpKey, "MoveWindowUp")
    PZ_CONFIG_KEY(moveWindowDownKey, "MoveWindowDown")
    PZ_CONFIG_KEY(focusZoneLeftKey, "FocusZoneLeft")
    PZ_CONFIG_KEY(focusZoneRightKey, "FocusZoneRight")
    PZ_CONFIG_KEY(focusZoneUpKey, "FocusZoneUp")
    PZ_CONFIG_KEY(focusZoneDownKey, "FocusZoneDown")
    PZ_CONFIG_KEY(pushToEmptyZoneKey, "PushToEmptyZone")
    PZ_CONFIG_KEY(restoreWindowSizeKey, "RestoreWindowSize")
    PZ_CONFIG_KEY(toggleWindowFloatKey, "ToggleWindowFloat")
    PZ_CONFIG_KEY(swapWindowLeftKey, "SwapWindowLeft")
    PZ_CONFIG_KEY(swapWindowRightKey, "SwapWindowRight")
    PZ_CONFIG_KEY(swapWindowUpKey, "SwapWindowUp")
    PZ_CONFIG_KEY(swapWindowDownKey, "SwapWindowDown")

    // Parameterized — uses the pattern accessor to avoid duplication
    PZ_CONFIG_KEY(snapToZoneKeyPattern, "SnapToZone%1")
    static QString snapToZoneKey(int n)
    {
        return snapToZoneKeyPattern().arg(n);
    }

    PZ_CONFIG_KEY(rotateWindowsClockwiseKey, "RotateWindowsClockwise")
    PZ_CONFIG_KEY(rotateWindowsCounterclockwiseKey, "RotateWindowsCounterclockwise")
    PZ_CONFIG_KEY(cycleWindowForwardKey, "CycleWindowForward")
    PZ_CONFIG_KEY(cycleWindowBackwardKey, "CycleWindowBackward")
    PZ_CONFIG_KEY(resnapToNewLayoutKey, "ResnapToNewLayout")
    PZ_CONFIG_KEY(snapAllWindowsKey, "SnapAllWindows")
    PZ_CONFIG_KEY(layoutPickerKey, "LayoutPicker")
    PZ_CONFIG_KEY(toggleLayoutLockKey, "ToggleLayoutLock")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Shortcuts.Tiling
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(toggleKey, "Toggle")
    PZ_CONFIG_KEY(focusMasterKey, "FocusMaster")
    PZ_CONFIG_KEY(swapMasterKey, "SwapMaster")
    PZ_CONFIG_KEY(incMasterRatioKey, "IncMasterRatio")
    PZ_CONFIG_KEY(decMasterRatioKey, "DecMasterRatio")
    PZ_CONFIG_KEY(incMasterCountKey, "IncMasterCount")
    PZ_CONFIG_KEY(decMasterCountKey, "DecMasterCount")
    PZ_CONFIG_KEY(retileKey, "Retile")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Editor.Shortcuts
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(duplicateKey, "Duplicate")
    PZ_CONFIG_KEY(splitHorizontalKey, "SplitHorizontal")
    PZ_CONFIG_KEY(splitVerticalKey, "SplitVertical")
    PZ_CONFIG_KEY(fillKey, "Fill")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Editor.Snapping
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(gridEnabledKey, "GridEnabled")
    PZ_CONFIG_KEY(edgeEnabledKey, "EdgeEnabled")
    PZ_CONFIG_KEY(intervalKey, "Interval")
    PZ_CONFIG_KEY(intervalXKey, "IntervalX")
    PZ_CONFIG_KEY(intervalYKey, "IntervalY")
    PZ_CONFIG_KEY(overrideModifierKey, "OverrideModifier")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Editor.FillOnDrop
    // ═══════════════════════════════════════════════════════════════════════════

    // (uses enabledKey)
    PZ_CONFIG_KEY(modifierKey, "Modifier")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Virtual Screens
    // The "VirtualScreen:" prefix is intentional — parsing must use
    // groupName.mid(prefix.size()) to extract the physical screen ID,
    // NOT split on ':',  because screen IDs themselves may contain colons
    // (e.g. "BNQ:BenQ PD3220U:serial").
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(virtualScreenGroupPrefix, "VirtualScreen:")
    PZ_CONFIG_KEY(virtualScreenCountKey, "count")
    PZ_CONFIG_KEY(virtualScreenXKey, "x")
    PZ_CONFIG_KEY(virtualScreenYKey, "y")
    PZ_CONFIG_KEY(virtualScreenWidthKey, "width")
    PZ_CONFIG_KEY(virtualScreenHeightKey, "height")
    PZ_CONFIG_KEY(virtualScreenNameKey, "name")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Ordering
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(snappingLayoutOrderKey, "SnappingLayoutOrder")
    PZ_CONFIG_KEY(tilingAlgorithmOrderKey, "TilingAlgorithmOrder")

    // ═══════════════════════════════════════════════════════════════════════════
    // Per-Screen Config Group Prefixes
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_GROUP(zoneSelectorGroupPrefix, "ZoneSelector:")
    PZ_CONFIG_GROUP(autotileScreenGroupPrefix, "AutotileScreen:")
    PZ_CONFIG_GROUP(snappingScreenGroupPrefix, "SnappingScreen:")

    // ═══════════════════════════════════════════════════════════════════════════
    // Legacy v1 key accessors — used ONLY by migration code.
    // Some names are identical to their v2 counterparts (marked "= v2") because
    // the group name didn't change — only the keys inside were restructured.
    // They exist as separate accessors so migration code reads unambiguously as
    // "reading from v1 source" vs "writing to v2 destination".
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_GROUP(v1ActivationGroup, "Activation")
    PZ_CONFIG_GROUP(v1DisplayGroup, "Display")
    PZ_CONFIG_GROUP(v1AppearanceGroup, "Appearance")
    PZ_CONFIG_GROUP(v1ZonesGroup, "Zones")
    PZ_CONFIG_GROUP(v1BehaviorGroup, "Behavior")
    PZ_CONFIG_GROUP(v1ExclusionsGroup, "Exclusions") // = v2 exclusionsGroup
    PZ_CONFIG_GROUP(v1ZoneSelectorGroup, "ZoneSelector")
    PZ_CONFIG_GROUP(v1AutotilingGroup, "Autotiling")
    PZ_CONFIG_GROUP(v1AutotileShortcutsGroup, "AutotileShortcuts")
    PZ_CONFIG_GROUP(v1AnimationsGroup, "Animations") // = v2 animationsGroup
    PZ_CONFIG_GROUP(v1GlobalShortcutsGroup, "GlobalShortcuts")
    PZ_CONFIG_GROUP(v1EditorGroup, "Editor") // = v2 editorGroup
    PZ_CONFIG_GROUP(v1OrderingGroup, "Ordering") // = v2 orderingGroup
    PZ_CONFIG_GROUP(v1RenderingGroup, "Rendering") // = v2 renderingGroup
    PZ_CONFIG_GROUP(v1ShadersGroup, "Shaders") // = v2 shadersGroup

private:
    // Non-instantiable
    ConfigKeys() = delete;
};

} // namespace PlasmaZones

#undef PZ_CONFIG_KEY
#undef PZ_CONFIG_GROUP
