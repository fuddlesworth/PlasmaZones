// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// ISettings — PZ global settings facade.
//
// Split out of interfaces.h so consumers can include just the settings
// contract without pulling zone/layout/overlay interfaces. ISettings is
// explicitly PZ-owned and is NOT a candidate for the phosphor-zones
// extraction — zones that need a tuning value should take it directly
// (see PhosphorZones::ZoneDetector::setAdjacentThreshold for the pattern).

#include "plasmazones_export.h"
#include "enums.h"
#include "settings_interfaces.h"

#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>

namespace PlasmaZones {

/**
 * @brief Abstract interface for settings management
 *
 * Allows dependency inversion - components depend on this interface
 * rather than concrete Settings implementation. Inherits from focused
 * sub-interfaces so components can depend on just what they need.
 */
class PLASMAZONES_EXPORT ISettings : public QObject,
                                     public IZoneActivationSettings,
                                     public IZoneVisualizationSettings,
                                     public IZoneGeometrySettings,
                                     public IWindowExclusionSettings,
                                     public IZoneSelectorSettings,
                                     public IWindowBehaviorSettings,
                                     public IDefaultLayoutSettings,
                                     public IOrderingSettings
{
    Q_OBJECT

public:
    explicit ISettings(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    ~ISettings() override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Composite convenience accessors — implemented inline to enforce the
    // parent-gate invariant that was previously consumer-side.
    //
    // Nested settings like zoneSelectorEnabled live under the Snapping.*
    // config tree but were checked independently by consumers, so a consumer
    // could read zoneSelectorEnabled=true even with the top-level
    // Snapping.Enabled=false (exactly the reporter's config in #310 —
    // Snapping.Enabled=false + Snapping.ZoneSelector.Enabled=true left the
    // effect in a confused state that the drag path didn't handle).
    //
    // These composite methods make the parent gate compile-time: a consumer
    // can't forget to check snappingEnabled() when they read
    // isZoneSelectorActive().
    // ═══════════════════════════════════════════════════════════════════════════

    /// True only when the zone selector is enabled AND snapping is enabled
    /// at the top level. Use this in consumers instead of
    /// zoneSelectorEnabled() unless you need the raw child flag value.
    bool isZoneSelectorActive() const
    {
        return snappingEnabled() && zoneSelectorEnabled();
    }

    /// True only when snap assist is enabled AND snapping is enabled at
    /// the top level. Same pattern as isZoneSelectorActive.
    bool isSnapAssistActive() const
    {
        return snappingEnabled() && snapAssistEnabled();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // All settings methods are inherited from the segregated interfaces:
    //   - IZoneActivationSettings: drag modifiers, activation triggers
    //   - IZoneVisualizationSettings: colors, opacity, blur, shader effects
    //   - IZoneGeometrySettings: padding, gaps, thresholds, performance
    //   - IWindowExclusionSettings: excluded apps/classes, size filters
    //   - IZoneSelectorSettings: zone selector UI configuration
    //   - IWindowBehaviorSettings: snap restore, sticky handling
    //   - IDefaultLayoutSettings: default layout ID
    //
    // See settings_interfaces.h for the full API.
    // ═══════════════════════════════════════════════════════════════════════════

    // Animation settings (global — applies to snapping and autotiling)
    virtual bool animationsEnabled() const = 0;
    virtual void setAnimationsEnabled(bool enabled) = 0;
    virtual int animationDuration() const = 0;
    virtual void setAnimationDuration(int duration) = 0;
    virtual QString animationEasingCurve() const = 0;
    virtual void setAnimationEasingCurve(const QString& curve) = 0;
    virtual int animationMinDistance() const = 0;
    virtual void setAnimationMinDistance(int distance) = 0;
    virtual int animationSequenceMode() const = 0;
    virtual void setAnimationSequenceMode(int mode) = 0;
    virtual int animationStaggerInterval() const = 0;
    virtual void setAnimationStaggerInterval(int ms) = 0;

    // Autotile decoration settings (fetched by KWin effect via D-Bus)
    virtual bool autotileFocusFollowsMouse() const = 0;
    virtual void setAutotileFocusFollowsMouse(bool enabled) = 0;
    virtual bool autotileHideTitleBars() const = 0;
    virtual void setAutotileHideTitleBars(bool hide) = 0;
    virtual bool autotileShowBorder() const = 0;
    virtual void setAutotileShowBorder(bool show) = 0;
    virtual int autotileBorderWidth() const = 0;
    virtual void setAutotileBorderWidth(int width) = 0;
    virtual int autotileBorderRadius() const = 0;
    virtual void setAutotileBorderRadius(int radius) = 0;
    virtual QColor autotileBorderColor() const = 0;
    virtual void setAutotileBorderColor(const QColor& color) = 0;
    virtual QColor autotileInactiveBorderColor() const = 0;
    virtual void setAutotileInactiveBorderColor(const QColor& color) = 0;
    virtual bool autotileUseSystemBorderColors() const = 0;
    virtual void setAutotileUseSystemBorderColors(bool use) = 0;
    virtual StickyWindowHandling autotileStickyWindowHandling() const = 0;
    virtual void setAutotileStickyWindowHandling(StickyWindowHandling handling) = 0;
    virtual AutotileDragBehavior autotileDragBehavior() const = 0;
    virtual void setAutotileDragBehavior(AutotileDragBehavior behavior) = 0;
    virtual AutotileOverflowBehavior autotileOverflowBehavior() const = 0;
    virtual void setAutotileOverflowBehavior(AutotileOverflowBehavior behavior) = 0;

    // Autotile drag-insert triggers: hold-to-activate list for live
    // re-inserting a dragged window into the autotile stack.
    virtual QVariantList autotileDragInsertTriggers() const = 0;
    virtual void setAutotileDragInsertTriggers(const QVariantList& triggers) = 0;
    virtual bool autotileDragInsertToggle() const = 0;
    virtual void setAutotileDragInsertToggle(bool enable) = 0;

    // Rendering backend (pipeline-level, not specific to any sub-interface)
    virtual QString renderingBackend() const = 0;
    virtual void setRenderingBackend(const QString& backend) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Per-screen overrides — category-keyed maps of setting key → value that
    // live alongside the global setting. Defaults are no-op bodies so backends
    // that don't persist per-screen state (test stubs) can inherit the
    // interface without implementing these. The concrete Settings class
    // overrides every method; the D-Bus SettingsAdaptor depends only on
    // these virtuals so it doesn't need a qobject_cast<Settings*>.
    // The has*() query methods are virtual with a "nothing persisted"
    // default so the SettingsAdaptor can answer existence questions
    // through the interface too.
    // ═══════════════════════════════════════════════════════════════════════════
    virtual QVariantMap getPerScreenAutotileSettings(const QString& /*screenIdOrName*/) const
    {
        return {};
    }
    virtual void setPerScreenAutotileSetting(const QString& /*screenIdOrName*/, const QString& /*key*/,
                                             const QVariant& /*value*/)
    {
    }
    virtual void clearPerScreenAutotileSettings(const QString& /*screenIdOrName*/)
    {
    }
    virtual bool hasPerScreenAutotileSettings(const QString& /*screenIdOrName*/) const
    {
        return false;
    }

    virtual QVariantMap getPerScreenZoneSelectorSettings(const QString& /*screenIdOrName*/) const
    {
        return {};
    }
    virtual void setPerScreenZoneSelectorSetting(const QString& /*screenIdOrName*/, const QString& /*key*/,
                                                 const QVariant& /*value*/)
    {
    }
    virtual void clearPerScreenZoneSelectorSettings(const QString& /*screenIdOrName*/)
    {
    }
    virtual bool hasPerScreenZoneSelectorSettings(const QString& /*screenIdOrName*/) const
    {
        return false;
    }

    QVariantMap getPerScreenSnappingSettings(const QString& /*screenIdOrName*/) const override
    {
        return {};
    }
    virtual void setPerScreenSnappingSetting(const QString& /*screenIdOrName*/, const QString& /*key*/,
                                             const QVariant& /*value*/)
    {
    }
    virtual void clearPerScreenSnappingSettings(const QString& /*screenIdOrName*/)
    {
    }
    virtual bool hasPerScreenSnappingSettings(const QString& /*screenIdOrName*/) const
    {
        return false;
    }

    // Persistence (unique to ISettings)
    virtual void load() = 0;
    virtual void save() = 0;
    virtual void reset() = 0;

Q_SIGNALS:
    void settingsChanged();
    void dragActivationTriggersChanged();
    void autotileDragInsertTriggersChanged();
    void autotileDragInsertToggleChanged();
    void zoneSpanEnabledChanged();
    void zoneSpanModifierChanged();
    void zoneSpanTriggersChanged();
    void toggleActivationChanged();
    void snappingEnabledChanged();
    void showZonesOnAllMonitorsChanged();
    // The per-mode disable signals carry the mode that flipped so listeners can
    // re-read only the relevant list. Pre-v3 these were no-arg signals; the mode
    // argument was added when the storage was split into per-mode lists.
    void disabledMonitorsChanged(PhosphorZones::AssignmentEntry::Mode mode);
    void disabledDesktopsChanged(PhosphorZones::AssignmentEntry::Mode mode);
    void disabledActivitiesChanged(PhosphorZones::AssignmentEntry::Mode mode);
    void showZoneNumbersChanged();
    void flashZonesOnSwitchChanged();
    void showOsdOnLayoutSwitchChanged();
    void showOsdOnDesktopSwitchChanged();
    void showNavigationOsdChanged();
    void osdStyleChanged();
    void overlayDisplayModeChanged();
    void useSystemColorsChanged();
    void highlightColorChanged();
    void inactiveColorChanged();
    void borderColorChanged();
    void labelFontColorChanged();
    void activeOpacityChanged();
    void inactiveOpacityChanged();
    void borderWidthChanged();
    void borderRadiusChanged();
    void enableBlurChanged();
    void labelFontFamilyChanged();
    void labelFontSizeScaleChanged();
    void labelFontWeightChanged();
    void labelFontItalicChanged();
    void labelFontUnderlineChanged();
    void labelFontStrikeoutChanged();
    void zonePaddingChanged();
    void outerGapChanged();
    void usePerSideOuterGapChanged();
    void outerGapTopChanged();
    void outerGapBottomChanged();
    void outerGapLeftChanged();
    void outerGapRightChanged();
    void adjacentThresholdChanged();
    void pollIntervalMsChanged();
    void minimumZoneSizePxChanged();
    void minimumZoneDisplaySizePxChanged();
    void keepWindowsInZonesOnResolutionChangeChanged();
    void moveNewWindowsToLastZoneChanged();
    void restoreOriginalSizeOnUnsnapChanged();
    void stickyWindowHandlingChanged();
    void restoreWindowsToZonesOnLoginChanged();
    void autoAssignAllLayoutsChanged();
    void snapAssistFeatureEnabledChanged();
    void snapAssistEnabledChanged();
    void snapAssistTriggersChanged();
    void defaultLayoutIdChanged();
    void filterLayoutsByAspectRatioChanged();
    void excludedApplicationsChanged();
    void excludedWindowClassesChanged();
    void excludeTransientWindowsChanged();
    void minimumWindowWidthChanged();
    void minimumWindowHeightChanged();
    void zoneSelectorEnabledChanged();
    void zoneSelectorTriggerDistanceChanged();
    void zoneSelectorPositionChanged();
    void zoneSelectorLayoutModeChanged();
    void zoneSelectorPreviewWidthChanged();
    void zoneSelectorPreviewHeightChanged();
    void zoneSelectorPreviewLockAspectChanged();
    void zoneSelectorGridColumnsChanged();
    void zoneSelectorSizeModeChanged();
    void zoneSelectorMaxRowsChanged();
    void perScreenZoneSelectorSettingsChanged();
    void perScreenAutotileSettingsChanged();
    void perScreenSnappingSettingsChanged();
    // Rendering
    void renderingBackendChanged();
    // Shader effects
    void enableShaderEffectsChanged();
    void shaderFrameRateChanged();
    void enableAudioVisualizerChanged();
    void audioSpectrumBarCountChanged();
    // Global shortcuts
    void openEditorShortcutChanged();
    void openSettingsShortcutChanged();
    void previousLayoutShortcutChanged();
    void nextLayoutShortcutChanged();
    void quickLayout1ShortcutChanged();
    void quickLayout2ShortcutChanged();
    void quickLayout3ShortcutChanged();
    void quickLayout4ShortcutChanged();
    void quickLayout5ShortcutChanged();
    void quickLayout6ShortcutChanged();
    void quickLayout7ShortcutChanged();
    void quickLayout8ShortcutChanged();
    void quickLayout9ShortcutChanged();

    // Keyboard Navigation Shortcuts
    void moveWindowLeftShortcutChanged();
    void moveWindowRightShortcutChanged();
    void moveWindowUpShortcutChanged();
    void moveWindowDownShortcutChanged();
    void focusZoneLeftShortcutChanged();
    void focusZoneRightShortcutChanged();
    void focusZoneUpShortcutChanged();
    void focusZoneDownShortcutChanged();
    void pushToEmptyZoneShortcutChanged();
    void restoreWindowSizeShortcutChanged();
    void toggleWindowFloatShortcutChanged();

    // Swap Window Shortcuts
    void swapWindowLeftShortcutChanged();
    void swapWindowRightShortcutChanged();
    void swapWindowUpShortcutChanged();
    void swapWindowDownShortcutChanged();

    // Snap to PhosphorZones::Zone by Number Shortcuts
    void snapToZone1ShortcutChanged();
    void snapToZone2ShortcutChanged();
    void snapToZone3ShortcutChanged();
    void snapToZone4ShortcutChanged();
    void snapToZone5ShortcutChanged();
    void snapToZone6ShortcutChanged();
    void snapToZone7ShortcutChanged();
    void snapToZone8ShortcutChanged();
    void snapToZone9ShortcutChanged();

    // Rotate Windows Shortcuts
    void rotateWindowsClockwiseShortcutChanged();
    void rotateWindowsCounterclockwiseShortcutChanged();

    // Cycle Windows in PhosphorZones::Zone Shortcuts
    void cycleWindowForwardShortcutChanged();
    void cycleWindowBackwardShortcutChanged();

    // Resnap to New PhosphorZones::Layout Shortcut
    void resnapToNewLayoutShortcutChanged();

    // Snap All Windows Shortcut
    void snapAllWindowsShortcutChanged();

    // PhosphorZones::Layout Picker Shortcut
    void layoutPickerShortcutChanged();

    // Toggle PhosphorZones::Layout Lock Shortcut
    void toggleLayoutLockShortcutChanged();

    // Virtual Screen Swap / Rotate Shortcuts
    void swapVirtualScreenLeftShortcutChanged();
    void swapVirtualScreenRightShortcutChanged();
    void swapVirtualScreenUpShortcutChanged();
    void swapVirtualScreenDownShortcutChanged();
    void rotateVirtualScreensClockwiseShortcutChanged();
    void rotateVirtualScreensCounterclockwiseShortcutChanged();

    // Autotile settings
    void autotileEnabledChanged();
    void defaultAutotileAlgorithmChanged();
    void autotileSplitRatioChanged();
    void autotileSplitRatioStepChanged();
    void autotileMasterCountChanged();
    void autotilePerAlgorithmSettingsChanged();
    void autotileInnerGapChanged();
    void autotileOuterGapChanged();
    void autotileUsePerSideOuterGapChanged();
    void autotileOuterGapTopChanged();
    void autotileOuterGapBottomChanged();
    void autotileOuterGapLeftChanged();
    void autotileOuterGapRightChanged();
    void autotileSmartGapsChanged();
    void autotileMaxWindowsChanged();
    void autotileFocusNewWindowsChanged();
    void autotileInsertPositionChanged();
    void autotileRespectMinimumSizeChanged();
    void autotileFocusFollowsMouseChanged();
    void autotileHideTitleBarsChanged();
    void autotileShowBorderChanged();
    void autotileBorderWidthChanged();
    void autotileBorderRadiusChanged();
    void autotileBorderColorChanged();
    void autotileInactiveBorderColorChanged();
    void autotileUseSystemBorderColorsChanged();
    void autotileStickyWindowHandlingChanged();
    void autotileDragBehaviorChanged();
    void autotileOverflowBehaviorChanged();
    void lockedScreensChanged();
    void virtualScreenConfigsChanged();
    // Ordering
    void snappingLayoutOrderChanged();
    void tilingAlgorithmOrderChanged();
    // Animation settings (general)
    void animationsEnabledChanged();
    void animationDurationChanged();
    void animationEasingCurveChanged();
    void animationMinDistanceChanged();
    void animationSequenceModeChanged();
    void animationStaggerIntervalChanged();

    // Autotile shortcuts
    void autotileToggleShortcutChanged();
    void autotileRetileShortcutChanged();
    void autotileFocusMasterShortcutChanged();
    void autotileSwapMasterShortcutChanged();
    void autotileIncMasterCountShortcutChanged();
    void autotileDecMasterCountShortcutChanged();
    void autotileIncMasterRatioShortcutChanged();
    void autotileDecMasterRatioShortcutChanged();
};

} // namespace PlasmaZones
