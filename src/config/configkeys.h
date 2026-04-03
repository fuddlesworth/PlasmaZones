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
 * Extracted from ConfigDefaults to keep that file under 500 lines.
 * ConfigDefaults inherits from ConfigKeys so all existing call sites
 * (e.g. ConfigDefaults::snappingEnabledKey()) continue to work.
 */
class ConfigKeys
{
public:
    // ═══════════════════════════════════════════════════════════════════════════
    // Config Group Names
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_GROUP(generalGroup, "General")
    PZ_CONFIG_GROUP(renderingGroup, "Rendering")
    PZ_CONFIG_GROUP(activationGroup, "Activation")
    PZ_CONFIG_GROUP(displayGroup, "Display")
    PZ_CONFIG_GROUP(appearanceGroup, "Appearance")
    PZ_CONFIG_GROUP(zonesGroup, "Zones")
    PZ_CONFIG_GROUP(behaviorGroup, "Behavior")
    PZ_CONFIG_GROUP(exclusionsGroup, "Exclusions")
    PZ_CONFIG_GROUP(zoneSelectorGroup, "ZoneSelector")
    PZ_CONFIG_GROUP(shadersGroup, "Shaders")
    PZ_CONFIG_GROUP(globalShortcutsGroup, "GlobalShortcuts")
    PZ_CONFIG_GROUP(autotilingGroup, "Autotiling")
    PZ_CONFIG_GROUP(autotileShortcutsGroup, "AutotileShortcuts")
    PZ_CONFIG_GROUP(animationsGroup, "Animations")
    PZ_CONFIG_GROUP(updatesGroup, "Updates")
    PZ_CONFIG_GROUP(editorGroup, "Editor")
    PZ_CONFIG_GROUP(tilingQuickLayoutSlotsGroup, "TilingQuickLayoutSlots")
    PZ_CONFIG_GROUP(orderingGroup, "Ordering")

    // ═══════════════════════════════════════════════════════════════════════════
    // Trigger JSON Field Names
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(triggerModifierField, "modifier")
    PZ_CONFIG_KEY(triggerMouseButtonField, "mouseButton")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Rendering
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(renderingBackendKey, "RenderingBackend")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Activation
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(dragActivationTriggersKey, "DragActivationTriggers")
    PZ_CONFIG_KEY(zoneSpanEnabledKey, "ZoneSpanEnabled")
    PZ_CONFIG_KEY(zoneSpanModifierKey, "ZoneSpanModifier")
    PZ_CONFIG_KEY(zoneSpanTriggersKey, "ZoneSpanTriggers")
    PZ_CONFIG_KEY(toggleActivationKey, "ToggleActivation")
    PZ_CONFIG_KEY(snappingEnabledKey, "SnappingEnabled")
    PZ_CONFIG_KEY(snapAssistFeatureEnabledKey, "SnapAssistFeatureEnabled")
    PZ_CONFIG_KEY(snapAssistEnabledKey, "SnapAssistEnabled")
    PZ_CONFIG_KEY(snapAssistTriggersKey, "SnapAssistTriggers")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Display
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(showOnAllMonitorsKey, "ShowOnAllMonitors")
    PZ_CONFIG_KEY(disabledMonitorsKey, "DisabledMonitors")
    PZ_CONFIG_KEY(disabledDesktopsKey, "DisabledDesktops")
    PZ_CONFIG_KEY(disabledActivitiesKey, "DisabledActivities")
    PZ_CONFIG_KEY(showNumbersKey, "ShowNumbers")
    PZ_CONFIG_KEY(flashOnSwitchKey, "FlashOnSwitch")
    PZ_CONFIG_KEY(showOsdOnLayoutSwitchKey, "ShowOsdOnLayoutSwitch")
    PZ_CONFIG_KEY(showNavigationOsdKey, "ShowNavigationOsd")
    PZ_CONFIG_KEY(osdStyleKey, "OsdStyle")
    PZ_CONFIG_KEY(overlayDisplayModeKey, "OverlayDisplayMode")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Appearance
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(useSystemColorsKey, "UseSystemColors")
    PZ_CONFIG_KEY(highlightColorKey, "HighlightColor")
    PZ_CONFIG_KEY(inactiveColorKey, "InactiveColor")
    PZ_CONFIG_KEY(borderColorKey, "BorderColor")
    PZ_CONFIG_KEY(labelFontColorKey, "LabelFontColor")
    PZ_CONFIG_KEY(activeOpacityKey, "ActiveOpacity")
    PZ_CONFIG_KEY(inactiveOpacityKey, "InactiveOpacity")
    PZ_CONFIG_KEY(borderWidthKey, "BorderWidth")
    PZ_CONFIG_KEY(borderRadiusKey, "BorderRadius")
    PZ_CONFIG_KEY(enableBlurKey, "EnableBlur")
    PZ_CONFIG_KEY(labelFontFamilyKey, "LabelFontFamily")
    PZ_CONFIG_KEY(labelFontSizeScaleKey, "LabelFontSizeScale")
    PZ_CONFIG_KEY(labelFontWeightKey, "LabelFontWeight")
    PZ_CONFIG_KEY(labelFontItalicKey, "LabelFontItalic")
    PZ_CONFIG_KEY(labelFontUnderlineKey, "LabelFontUnderline")
    PZ_CONFIG_KEY(labelFontStrikeoutKey, "LabelFontStrikeout")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Zones
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(zonePaddingKey, "Padding")
    PZ_CONFIG_KEY(outerGapKey, "OuterGap")
    PZ_CONFIG_KEY(usePerSideOuterGapKey, "UsePerSideOuterGap")
    PZ_CONFIG_KEY(outerGapTopKey, "OuterGapTop")
    PZ_CONFIG_KEY(outerGapBottomKey, "OuterGapBottom")
    PZ_CONFIG_KEY(outerGapLeftKey, "OuterGapLeft")
    PZ_CONFIG_KEY(outerGapRightKey, "OuterGapRight")
    PZ_CONFIG_KEY(adjacentThresholdKey, "AdjacentThreshold")
    PZ_CONFIG_KEY(pollIntervalMsKey, "PollIntervalMs")
    PZ_CONFIG_KEY(minimumZoneSizePxKey, "MinimumZoneSizePx")
    PZ_CONFIG_KEY(minimumZoneDisplaySizePxKey, "MinimumZoneDisplaySizePx")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Behavior
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(keepOnResolutionChangeKey, "KeepOnResolutionChange")
    PZ_CONFIG_KEY(moveNewToLastZoneKey, "MoveNewToLastZone")
    PZ_CONFIG_KEY(restoreSizeOnUnsnapKey, "RestoreSizeOnUnsnap")
    PZ_CONFIG_KEY(stickyWindowHandlingKey, "StickyWindowHandling")
    PZ_CONFIG_KEY(restoreWindowsToZonesOnLoginKey, "RestoreWindowsToZonesOnLogin")
    PZ_CONFIG_KEY(defaultLayoutIdKey, "DefaultLayoutId")
    PZ_CONFIG_KEY(filterLayoutsByAspectRatioKey, "FilterLayoutsByAspectRatio")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Exclusions
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(excludedApplicationsKey, "Applications")
    PZ_CONFIG_KEY(excludedWindowClassesKey, "WindowClasses")
    PZ_CONFIG_KEY(excludeTransientWindowsKey, "ExcludeTransientWindows")
    PZ_CONFIG_KEY(minimumWindowWidthKey, "MinimumWindowWidth")
    PZ_CONFIG_KEY(minimumWindowHeightKey, "MinimumWindowHeight")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — ZoneSelector
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(zoneSelectorEnabledKey, "Enabled")
    PZ_CONFIG_KEY(zoneSelectorTriggerDistanceKey, "TriggerDistance")
    PZ_CONFIG_KEY(zoneSelectorPositionKey, "Position")
    PZ_CONFIG_KEY(zoneSelectorLayoutModeKey, "LayoutMode")
    PZ_CONFIG_KEY(zoneSelectorPreviewWidthKey, "PreviewWidth")
    PZ_CONFIG_KEY(zoneSelectorPreviewHeightKey, "PreviewHeight")
    PZ_CONFIG_KEY(zoneSelectorPreviewLockAspectKey, "PreviewLockAspect")
    PZ_CONFIG_KEY(zoneSelectorGridColumnsKey, "GridColumns")
    PZ_CONFIG_KEY(zoneSelectorSizeModeKey, "SizeMode")
    PZ_CONFIG_KEY(zoneSelectorMaxRowsKey, "MaxRows")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Shaders
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(enableShaderEffectsKey, "EnableShaderEffects")
    PZ_CONFIG_KEY(shaderFrameRateKey, "ShaderFrameRate")
    PZ_CONFIG_KEY(enableAudioVisualizerKey, "EnableAudioVisualizer")
    PZ_CONFIG_KEY(audioSpectrumBarCountKey, "AudioSpectrumBarCount")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — GlobalShortcuts
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(openEditorShortcutKey, "OpenEditorShortcut")
    PZ_CONFIG_KEY(openSettingsShortcutKey, "OpenSettingsShortcut")
    PZ_CONFIG_KEY(previousLayoutShortcutKey, "PreviousLayoutShortcut")
    PZ_CONFIG_KEY(nextLayoutShortcutKey, "NextLayoutShortcut")

    // Parameterized — cannot use the macro
    static QString quickLayoutShortcutKey(int n)
    {
        return QStringLiteral("QuickLayout%1Shortcut").arg(n);
    }
    PZ_CONFIG_KEY(quickLayoutShortcutKeyPattern, "QuickLayout%1Shortcut")

    PZ_CONFIG_KEY(moveWindowLeftShortcutKey, "MoveWindowLeft")
    PZ_CONFIG_KEY(moveWindowRightShortcutKey, "MoveWindowRight")
    PZ_CONFIG_KEY(moveWindowUpShortcutKey, "MoveWindowUp")
    PZ_CONFIG_KEY(moveWindowDownShortcutKey, "MoveWindowDown")
    PZ_CONFIG_KEY(focusZoneLeftShortcutKey, "FocusZoneLeft")
    PZ_CONFIG_KEY(focusZoneRightShortcutKey, "FocusZoneRight")
    PZ_CONFIG_KEY(focusZoneUpShortcutKey, "FocusZoneUp")
    PZ_CONFIG_KEY(focusZoneDownShortcutKey, "FocusZoneDown")
    PZ_CONFIG_KEY(pushToEmptyZoneShortcutKey, "PushToEmptyZone")
    PZ_CONFIG_KEY(restoreWindowSizeShortcutKey, "RestoreWindowSize")
    PZ_CONFIG_KEY(toggleWindowFloatShortcutKey, "ToggleWindowFloat")
    PZ_CONFIG_KEY(swapWindowLeftShortcutKey, "SwapWindowLeft")
    PZ_CONFIG_KEY(swapWindowRightShortcutKey, "SwapWindowRight")
    PZ_CONFIG_KEY(swapWindowUpShortcutKey, "SwapWindowUp")
    PZ_CONFIG_KEY(swapWindowDownShortcutKey, "SwapWindowDown")

    // Parameterized — cannot use the macro
    static QString snapToZoneShortcutKey(int n)
    {
        return QStringLiteral("SnapToZone%1").arg(n);
    }
    PZ_CONFIG_KEY(snapToZoneShortcutKeyPattern, "SnapToZone%1")

    PZ_CONFIG_KEY(rotateWindowsClockwiseShortcutKey, "RotateWindowsClockwise")
    PZ_CONFIG_KEY(rotateWindowsCounterclockwiseShortcutKey, "RotateWindowsCounterclockwise")
    PZ_CONFIG_KEY(cycleWindowForwardShortcutKey, "CycleWindowForward")
    PZ_CONFIG_KEY(cycleWindowBackwardShortcutKey, "CycleWindowBackward")
    PZ_CONFIG_KEY(resnapToNewLayoutShortcutKey, "ResnapToNewLayoutShortcut")
    PZ_CONFIG_KEY(snapAllWindowsShortcutKey, "SnapAllWindowsShortcut")
    PZ_CONFIG_KEY(layoutPickerShortcutKey, "LayoutPickerShortcut")
    PZ_CONFIG_KEY(toggleLayoutLockShortcutKey, "ToggleLayoutLockShortcut")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Autotiling
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(autotileEnabledKey, "AutotileEnabled")
    PZ_CONFIG_KEY(defaultAutotileAlgorithmKey, "DefaultAutotileAlgorithm")
    PZ_CONFIG_KEY(autotileSplitRatioKey, "AutotileSplitRatio")
    PZ_CONFIG_KEY(autotileMasterCountKey, "AutotileMasterCount")
    PZ_CONFIG_KEY(autotilePerAlgorithmSettingsKey, "AutotilePerAlgorithmSettings")
    PZ_CONFIG_KEY(autotileInnerGapKey, "AutotileInnerGap")
    PZ_CONFIG_KEY(autotileOuterGapKey, "AutotileOuterGap")
    PZ_CONFIG_KEY(autotileUsePerSideOuterGapKey, "AutotileUsePerSideOuterGap")
    PZ_CONFIG_KEY(autotileOuterGapTopKey, "AutotileOuterGapTop")
    PZ_CONFIG_KEY(autotileOuterGapBottomKey, "AutotileOuterGapBottom")
    PZ_CONFIG_KEY(autotileOuterGapLeftKey, "AutotileOuterGapLeft")
    PZ_CONFIG_KEY(autotileOuterGapRightKey, "AutotileOuterGapRight")
    PZ_CONFIG_KEY(autotileFocusNewWindowsKey, "AutotileFocusNewWindows")
    PZ_CONFIG_KEY(autotileSmartGapsKey, "AutotileSmartGaps")
    PZ_CONFIG_KEY(autotileMaxWindowsKey, "AutotileMaxWindows")
    PZ_CONFIG_KEY(autotileInsertPositionKey, "AutotileInsertPosition")
    PZ_CONFIG_KEY(autotileFocusFollowsMouseKey, "AutotileFocusFollowsMouse")
    PZ_CONFIG_KEY(autotileRespectMinimumSizeKey, "AutotileRespectMinimumSize")
    PZ_CONFIG_KEY(autotileHideTitleBarsKey, "AutotileHideTitleBars")
    PZ_CONFIG_KEY(autotileShowBorderKey, "AutotileShowBorder")
    PZ_CONFIG_KEY(autotileBorderWidthKey, "AutotileBorderWidth")
    PZ_CONFIG_KEY(autotileBorderRadiusKey, "AutotileBorderRadius")
    PZ_CONFIG_KEY(autotileBorderColorKey, "AutotileBorderColor")
    PZ_CONFIG_KEY(autotileInactiveBorderColorKey, "AutotileInactiveBorderColor")
    PZ_CONFIG_KEY(autotileUseSystemBorderColorsKey, "AutotileUseSystemBorderColors")
    PZ_CONFIG_KEY(autotileStickyWindowHandlingKey, "AutotileStickyWindowHandling")
    PZ_CONFIG_KEY(lockedScreensKey, "LockedScreens")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — AutotileShortcuts
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(autotileToggleShortcutKey, "ToggleShortcut")
    PZ_CONFIG_KEY(autotileFocusMasterShortcutKey, "FocusMasterShortcut")
    PZ_CONFIG_KEY(autotileSwapMasterShortcutKey, "SwapMasterShortcut")
    PZ_CONFIG_KEY(autotileIncMasterRatioShortcutKey, "IncMasterRatioShortcut")
    PZ_CONFIG_KEY(autotileDecMasterRatioShortcutKey, "DecMasterRatioShortcut")
    PZ_CONFIG_KEY(autotileIncMasterCountShortcutKey, "IncMasterCountShortcut")
    PZ_CONFIG_KEY(autotileDecMasterCountShortcutKey, "DecMasterCountShortcut")
    PZ_CONFIG_KEY(autotileRetileShortcutKey, "RetileShortcut")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Animations
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(animationsEnabledKey, "AnimationsEnabled")
    PZ_CONFIG_KEY(animationDurationKey, "AnimationDuration")
    PZ_CONFIG_KEY(animationEasingCurveKey, "AnimationEasingCurve")
    PZ_CONFIG_KEY(animationMinDistanceKey, "AnimationMinDistance")
    PZ_CONFIG_KEY(animationSequenceModeKey, "AnimationSequenceMode")
    PZ_CONFIG_KEY(animationStaggerIntervalKey, "AnimationStaggerInterval")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Editor
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(editorDuplicateShortcutKey, "EditorDuplicateShortcut")
    PZ_CONFIG_KEY(editorSplitHorizontalShortcutKey, "EditorSplitHorizontalShortcut")
    PZ_CONFIG_KEY(editorSplitVerticalShortcutKey, "EditorSplitVerticalShortcut")
    PZ_CONFIG_KEY(editorFillShortcutKey, "EditorFillShortcut")
    PZ_CONFIG_KEY(editorGridSnappingEnabledKey, "GridSnappingEnabled")
    PZ_CONFIG_KEY(editorEdgeSnappingEnabledKey, "EdgeSnappingEnabled")
    PZ_CONFIG_KEY(editorSnapIntervalXKey, "SnapIntervalX")
    PZ_CONFIG_KEY(editorSnapIntervalYKey, "SnapIntervalY")
    PZ_CONFIG_KEY(editorSnapIntervalKey, "SnapInterval")
    PZ_CONFIG_KEY(editorSnapOverrideModifierKey, "SnapOverrideModifier")
    PZ_CONFIG_KEY(fillOnDropEnabledKey, "FillOnDropEnabled")
    PZ_CONFIG_KEY(fillOnDropModifierKey, "FillOnDropModifier")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Group — Window Tracking
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_GROUP(windowTrackingGroup, "WindowTracking")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Window Tracking (persistence in QSettings)
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(activeLayoutIdKey, "ActiveLayoutId")
    PZ_CONFIG_KEY(windowZoneAssignmentsFullKey, "WindowZoneAssignmentsFull")
    PZ_CONFIG_KEY(pendingRestoreQueuesKey, "PendingRestoreQueues")
    PZ_CONFIG_KEY(preTileGeometriesFullKey, "PreTileGeometriesFull")
    PZ_CONFIG_KEY(preTileGeometriesKey, "PreTileGeometries")
    PZ_CONFIG_KEY(lastUsedZoneIdKey, "LastUsedZoneId")
    PZ_CONFIG_KEY(lastUsedScreenNameKey, "LastUsedScreenName")
    PZ_CONFIG_KEY(lastUsedZoneClassKey, "LastUsedZoneClass")
    PZ_CONFIG_KEY(lastUsedDesktopKey, "LastUsedDesktop")
    PZ_CONFIG_KEY(floatingWindowsKey, "FloatingWindows")
    PZ_CONFIG_KEY(preFloatZoneAssignmentsKey, "PreFloatZoneAssignments")
    PZ_CONFIG_KEY(preFloatScreenAssignmentsKey, "PreFloatScreenAssignments")
    PZ_CONFIG_KEY(userSnappedClassesKey, "UserSnappedClasses")

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

private:
    // Non-instantiable
    ConfigKeys() = delete;
};

} // namespace PlasmaZones

#undef PZ_CONFIG_KEY
#undef PZ_CONFIG_GROUP
