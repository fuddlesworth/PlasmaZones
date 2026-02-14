// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones.h"  // Generated from plasmazones.kcfg via KConfigXT

#include <QColor>
#include <QString>
#include <QVariantMap>

namespace PlasmaZones {

/**
 * @brief Provides static access to default configuration values
 *
 * This class wraps the KConfigXT-generated PlasmaZonesConfig class to provide
 * static access to default values. The .kcfg file is the SINGLE SOURCE OF TRUTH
 * for all defaults - this class simply exposes those generated defaults.
 *
 * Usage:
 *   int cols = ConfigDefaults::gridColumns();  // Returns 5 (from .kcfg)
 *   int rows = ConfigDefaults::maxRows();      // Returns 4 (from .kcfg)
 *
 * Benefits:
 * - Single source of truth (.kcfg file)
 * - Compile-time type safety
 * - No magic numbers scattered across codebase
 * - Changes to .kcfg automatically propagate everywhere
 */
class ConfigDefaults
{
public:
    // ═══════════════════════════════════════════════════════════════════════════
    // Activation Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static bool shiftDrag() { return instance().defaultShiftDragValue(); }
    static int dragActivationModifier() { return instance().defaultDragActivationModifierValue(); }
    static int dragActivationMouseButton() { return instance().defaultDragActivationMouseButtonValue(); }
    static QVariantList dragActivationTriggers() {
        // Default: single trigger with Alt modifier, no mouse button
        QVariantMap trigger;
        trigger[QStringLiteral("modifier")] = dragActivationModifier();
        trigger[QStringLiteral("mouseButton")] = dragActivationMouseButton();
        return {trigger};
    }
    static bool toggleActivation() { return instance().defaultToggleActivationValue(); }
    static int zoneSpanModifier() { return instance().defaultZoneSpanModifierValue(); }
    static QVariantList zoneSpanTriggers() {
        QVariantMap trigger;
        trigger[QStringLiteral("modifier")] = zoneSpanModifier();
        trigger[QStringLiteral("mouseButton")] = 0;
        return {trigger};
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Display Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static bool showOnAllMonitors() { return instance().defaultShowOnAllMonitorsValue(); }
    static bool showNumbers() { return instance().defaultShowNumbersValue(); }
    static bool flashOnSwitch() { return instance().defaultFlashOnSwitchValue(); }
    static bool showOsdOnLayoutSwitch() { return instance().defaultShowOsdOnLayoutSwitchValue(); }
    static bool showNavigationOsd() { return instance().defaultShowNavigationOsdValue(); }
    static int osdStyle() { return instance().defaultOsdStyleValue(); }

    // ═══════════════════════════════════════════════════════════════════════════
    // Appearance Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static bool useSystemColors() { return instance().defaultUseSystemColorsValue(); }
    static QColor highlightColor() { return instance().defaultHighlightColorValue(); }
    static QColor inactiveColor() { return instance().defaultInactiveColorValue(); }
    static QColor borderColor() { return instance().defaultBorderColorValue(); }
    static QColor labelFontColor() { return instance().defaultLabelFontColorValue(); }
    static double activeOpacity() { return instance().defaultActiveOpacityValue(); }
    static double inactiveOpacity() { return instance().defaultInactiveOpacityValue(); }
    static int borderWidth() { return instance().defaultBorderWidthValue(); }
    static int borderRadius() { return instance().defaultBorderRadiusValue(); }
    static bool enableBlur() { return instance().defaultEnableBlurValue(); }
    // KConfigXT doesn't generate a public defaultLabelFontFamilyValue() for
    // String entries with empty <default></default>, so we hardcode here.
    static QString labelFontFamily() { return QString(); }
    static double labelFontSizeScale() { return instance().defaultLabelFontSizeScaleValue(); }
    static int labelFontWeight() { return instance().defaultLabelFontWeightValue(); }
    static bool labelFontItalic() { return instance().defaultLabelFontItalicValue(); }
    static bool labelFontUnderline() { return instance().defaultLabelFontUnderlineValue(); }
    static bool labelFontStrikeout() { return instance().defaultLabelFontStrikeoutValue(); }

    // ═══════════════════════════════════════════════════════════════════════════
    // Zone Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static int zonePadding() { return instance().defaultPaddingValue(); }
    static int outerGap() { return instance().defaultOuterGapValue(); }
    static int adjacentThreshold() { return instance().defaultAdjacentThresholdValue(); }

    // ═══════════════════════════════════════════════════════════════════════════
    // Performance Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static int pollIntervalMs() { return instance().defaultPollIntervalMsValue(); }
    static int minimumZoneSizePx() { return instance().defaultMinimumZoneSizePxValue(); }
    static int minimumZoneDisplaySizePx() { return instance().defaultMinimumZoneDisplaySizePxValue(); }

    // ═══════════════════════════════════════════════════════════════════════════
    // Window Behavior Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static bool keepWindowsInZonesOnResolutionChange() { return instance().defaultKeepOnResolutionChangeValue(); }
    static bool moveNewWindowsToLastZone() { return instance().defaultMoveNewToLastZoneValue(); }
    static bool restoreOriginalSizeOnUnsnap() { return instance().defaultRestoreSizeOnUnsnapValue(); }
    static int stickyWindowHandling() { return instance().defaultStickyWindowHandlingValue(); }
    static bool restoreWindowsToZonesOnLogin() { return instance().defaultRestoreWindowsToZonesOnLoginValue(); }
    static bool snapAssistEnabled() { return instance().defaultSnapAssistEnabledValue(); }

    // ═══════════════════════════════════════════════════════════════════════════
    // Exclusion Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static bool excludeTransientWindows() { return instance().defaultExcludeTransientWindowsValue(); }
    static int minimumWindowWidth() { return instance().defaultMinimumWindowWidthValue(); }
    static int minimumWindowHeight() { return instance().defaultMinimumWindowHeightValue(); }

    // ═══════════════════════════════════════════════════════════════════════════
    // Zone Selector Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static bool zoneSelectorEnabled() { return instance().defaultEnabledValue(); }
    static int triggerDistance() { return instance().defaultTriggerDistanceValue(); }
    static int position() { return instance().defaultPositionValue(); }
    static int layoutMode() { return instance().defaultLayoutModeValue(); }
    static int sizeMode() { return instance().defaultSizeModeValue(); }
    static int maxRows() { return instance().defaultMaxRowsValue(); }
    static int previewWidth() { return instance().defaultPreviewWidthValue(); }
    static int previewHeight() { return instance().defaultPreviewHeightValue(); }
    static bool previewLockAspect() { return instance().defaultPreviewLockAspectValue(); }
    static int gridColumns() { return instance().defaultGridColumnsValue(); }

    // ═══════════════════════════════════════════════════════════════════════════
    // Shader Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static bool enableShaderEffects() { return instance().defaultEnableShaderEffectsValue(); }
    static int shaderFrameRate() { return instance().defaultShaderFrameRateValue(); }
    static bool enableAudioVisualizer() { return instance().defaultEnableAudioVisualizerValue(); }
    static int audioSpectrumBarCount() { return instance().defaultAudioSpectrumBarCountValue(); }

    // ═══════════════════════════════════════════════════════════════════════════
    // Mode Tracking Settings
    // ═══════════════════════════════════════════════════════════════════════════

    // (LastManualLayoutId is read/written directly by ModeTracker, no default getter needed)

    // ═══════════════════════════════════════════════════════════════════════════
    // Global Shortcuts
    // ═══════════════════════════════════════════════════════════════════════════

    static QString openEditorShortcut() { return instance().defaultOpenEditorShortcutValue(); }
    static QString previousLayoutShortcut() { return instance().defaultPreviousLayoutShortcutValue(); }
    static QString nextLayoutShortcut() { return instance().defaultNextLayoutShortcutValue(); }
    static QString quickLayout1Shortcut() { return instance().defaultQuickLayout1ShortcutValue(); }
    static QString quickLayout2Shortcut() { return instance().defaultQuickLayout2ShortcutValue(); }
    static QString quickLayout3Shortcut() { return instance().defaultQuickLayout3ShortcutValue(); }
    static QString quickLayout4Shortcut() { return instance().defaultQuickLayout4ShortcutValue(); }
    static QString quickLayout5Shortcut() { return instance().defaultQuickLayout5ShortcutValue(); }
    static QString quickLayout6Shortcut() { return instance().defaultQuickLayout6ShortcutValue(); }
    static QString quickLayout7Shortcut() { return instance().defaultQuickLayout7ShortcutValue(); }
    static QString quickLayout8Shortcut() { return instance().defaultQuickLayout8ShortcutValue(); }
    static QString quickLayout9Shortcut() { return instance().defaultQuickLayout9ShortcutValue(); }

    // ═══════════════════════════════════════════════════════════════════════════
    // Navigation Shortcuts
    // ═══════════════════════════════════════════════════════════════════════════

    static QString moveWindowLeftShortcut() { return instance().defaultMoveWindowLeftValue(); }
    static QString moveWindowRightShortcut() { return instance().defaultMoveWindowRightValue(); }
    static QString moveWindowUpShortcut() { return instance().defaultMoveWindowUpValue(); }
    static QString moveWindowDownShortcut() { return instance().defaultMoveWindowDownValue(); }
    static QString swapWindowLeftShortcut() { return instance().defaultSwapWindowLeftValue(); }
    static QString swapWindowRightShortcut() { return instance().defaultSwapWindowRightValue(); }
    static QString swapWindowUpShortcut() { return instance().defaultSwapWindowUpValue(); }
    static QString swapWindowDownShortcut() { return instance().defaultSwapWindowDownValue(); }
    static QString focusZoneLeftShortcut() { return instance().defaultFocusZoneLeftValue(); }
    static QString focusZoneRightShortcut() { return instance().defaultFocusZoneRightValue(); }
    static QString focusZoneUpShortcut() { return instance().defaultFocusZoneUpValue(); }
    static QString focusZoneDownShortcut() { return instance().defaultFocusZoneDownValue(); }
    static QString pushToEmptyZoneShortcut() { return instance().defaultPushToEmptyZoneValue(); }
    static QString restoreWindowSizeShortcut() { return instance().defaultRestoreWindowSizeValue(); }
    static QString toggleWindowFloatShortcut() { return instance().defaultToggleWindowFloatValue(); }
    static QString snapToZone1Shortcut() { return instance().defaultSnapToZone1Value(); }
    static QString snapToZone2Shortcut() { return instance().defaultSnapToZone2Value(); }
    static QString snapToZone3Shortcut() { return instance().defaultSnapToZone3Value(); }
    static QString snapToZone4Shortcut() { return instance().defaultSnapToZone4Value(); }
    static QString snapToZone5Shortcut() { return instance().defaultSnapToZone5Value(); }
    static QString snapToZone6Shortcut() { return instance().defaultSnapToZone6Value(); }
    static QString snapToZone7Shortcut() { return instance().defaultSnapToZone7Value(); }
    static QString snapToZone8Shortcut() { return instance().defaultSnapToZone8Value(); }
    static QString snapToZone9Shortcut() { return instance().defaultSnapToZone9Value(); }
    static QString rotateWindowsClockwiseShortcut() { return instance().defaultRotateWindowsClockwiseValue(); }
    static QString rotateWindowsCounterclockwiseShortcut() { return instance().defaultRotateWindowsCounterclockwiseValue(); }
    static QString cycleWindowForwardShortcut() { return instance().defaultCycleWindowForwardValue(); }
    static QString cycleWindowBackwardShortcut() { return instance().defaultCycleWindowBackwardValue(); }
    static QString resnapToNewLayoutShortcut() { return instance().defaultResnapToNewLayoutShortcutValue(); }
    static QString snapAllWindowsShortcut() { return instance().defaultSnapAllWindowsShortcutValue(); }

private:
    // Lazily-initialized singleton instance
    static PlasmaZonesConfig& instance()
    {
        static PlasmaZonesConfig config;
        return config;
    }

    // Non-instantiable
    ConfigDefaults() = delete;
};

} // namespace PlasmaZones
