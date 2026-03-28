// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../core/interfaces.h"
#include "../core/constants.h"
#include "configdefaults.h"
#include "configbackend_qsettings.h"
#include <memory>
#include <optional>
#include <QFont>
#include <QHash>
#include <QVariantMap>

namespace PlasmaZones {

/**
 * @brief Global settings for PlasmaZones
 *
 * Implements the ISettings interface with QSettingsConfigBackend persistence.
 * Supports ricer-friendly customization with color themes, opacity,
 * and integration with system color schemes.
 *
 * Note: This class does NOT use the singleton pattern. Create instances
 * where needed and pass via dependency injection.
 */
class PLASMAZONES_EXPORT Settings : public ISettings
{
    Q_OBJECT

public:
    /** Maximum number of activation triggers per action (drag, multi-zone, zone span) */
    static constexpr int MaxTriggersPerAction = 4;

    // Activation settings
    Q_PROPERTY(bool shiftDragToActivate READ shiftDragToActivate WRITE setShiftDragToActivate NOTIFY
                   shiftDragToActivateChanged)
    Q_PROPERTY(QVariantList dragActivationTriggers READ dragActivationTriggers WRITE setDragActivationTriggers NOTIFY
                   dragActivationTriggersChanged)
    Q_PROPERTY(bool zoneSpanEnabled READ zoneSpanEnabled WRITE setZoneSpanEnabled NOTIFY zoneSpanEnabledChanged)
    Q_PROPERTY(
        int zoneSpanModifier READ zoneSpanModifierInt WRITE setZoneSpanModifierInt NOTIFY zoneSpanModifierChanged)
    Q_PROPERTY(
        QVariantList zoneSpanTriggers READ zoneSpanTriggers WRITE setZoneSpanTriggers NOTIFY zoneSpanTriggersChanged)
    Q_PROPERTY(bool toggleActivation READ toggleActivation WRITE setToggleActivation NOTIFY toggleActivationChanged)
    Q_PROPERTY(bool snappingEnabled READ snappingEnabled WRITE setSnappingEnabled NOTIFY snappingEnabledChanged)

    // Display settings
    Q_PROPERTY(bool showZonesOnAllMonitors READ showZonesOnAllMonitors WRITE setShowZonesOnAllMonitors NOTIFY
                   showZonesOnAllMonitorsChanged)
    Q_PROPERTY(
        QStringList disabledMonitors READ disabledMonitors WRITE setDisabledMonitors NOTIFY disabledMonitorsChanged)
    Q_PROPERTY(
        QList<int> disabledDesktops READ disabledDesktops WRITE setDisabledDesktops NOTIFY disabledDesktopsChanged)
    Q_PROPERTY(QStringList disabledActivities READ disabledActivities WRITE setDisabledActivities NOTIFY
                   disabledActivitiesChanged)
    Q_PROPERTY(bool showZoneNumbers READ showZoneNumbers WRITE setShowZoneNumbers NOTIFY showZoneNumbersChanged)
    Q_PROPERTY(
        bool flashZonesOnSwitch READ flashZonesOnSwitch WRITE setFlashZonesOnSwitch NOTIFY flashZonesOnSwitchChanged)
    Q_PROPERTY(bool showOsdOnLayoutSwitch READ showOsdOnLayoutSwitch WRITE setShowOsdOnLayoutSwitch NOTIFY
                   showOsdOnLayoutSwitchChanged)
    Q_PROPERTY(bool showNavigationOsd READ showNavigationOsd WRITE setShowNavigationOsd NOTIFY showNavigationOsdChanged)
    Q_PROPERTY(int osdStyle READ osdStyleInt WRITE setOsdStyleInt NOTIFY osdStyleChanged)
    Q_PROPERTY(int overlayDisplayMode READ overlayDisplayModeInt WRITE setOverlayDisplayModeInt NOTIFY
                   overlayDisplayModeChanged)

    // Appearance (ricer-friendly)
    Q_PROPERTY(bool useSystemColors READ useSystemColors WRITE setUseSystemColors NOTIFY useSystemColorsChanged)
    Q_PROPERTY(QColor highlightColor READ highlightColor WRITE setHighlightColor NOTIFY highlightColorChanged)
    Q_PROPERTY(QColor inactiveColor READ inactiveColor WRITE setInactiveColor NOTIFY inactiveColorChanged)
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor NOTIFY borderColorChanged)
    Q_PROPERTY(QColor labelFontColor READ labelFontColor WRITE setLabelFontColor NOTIFY labelFontColorChanged)
    Q_PROPERTY(qreal activeOpacity READ activeOpacity WRITE setActiveOpacity NOTIFY activeOpacityChanged)
    Q_PROPERTY(qreal inactiveOpacity READ inactiveOpacity WRITE setInactiveOpacity NOTIFY inactiveOpacityChanged)
    Q_PROPERTY(int borderWidth READ borderWidth WRITE setBorderWidth NOTIFY borderWidthChanged)
    Q_PROPERTY(int borderRadius READ borderRadius WRITE setBorderRadius NOTIFY borderRadiusChanged)
    Q_PROPERTY(bool enableBlur READ enableBlur WRITE setEnableBlur NOTIFY enableBlurChanged)
    Q_PROPERTY(QString labelFontFamily READ labelFontFamily WRITE setLabelFontFamily NOTIFY labelFontFamilyChanged)
    Q_PROPERTY(
        qreal labelFontSizeScale READ labelFontSizeScale WRITE setLabelFontSizeScale NOTIFY labelFontSizeScaleChanged)
    Q_PROPERTY(int labelFontWeight READ labelFontWeight WRITE setLabelFontWeight NOTIFY labelFontWeightChanged)
    Q_PROPERTY(bool labelFontItalic READ labelFontItalic WRITE setLabelFontItalic NOTIFY labelFontItalicChanged)
    Q_PROPERTY(
        bool labelFontUnderline READ labelFontUnderline WRITE setLabelFontUnderline NOTIFY labelFontUnderlineChanged)
    Q_PROPERTY(
        bool labelFontStrikeout READ labelFontStrikeout WRITE setLabelFontStrikeout NOTIFY labelFontStrikeoutChanged)

    // Zone settings
    Q_PROPERTY(int zonePadding READ zonePadding WRITE setZonePadding NOTIFY zonePaddingChanged)
    Q_PROPERTY(int outerGap READ outerGap WRITE setOuterGap NOTIFY outerGapChanged)
    Q_PROPERTY(
        bool usePerSideOuterGap READ usePerSideOuterGap WRITE setUsePerSideOuterGap NOTIFY usePerSideOuterGapChanged)
    Q_PROPERTY(int outerGapTop READ outerGapTop WRITE setOuterGapTop NOTIFY outerGapTopChanged)
    Q_PROPERTY(int outerGapBottom READ outerGapBottom WRITE setOuterGapBottom NOTIFY outerGapBottomChanged)
    Q_PROPERTY(int outerGapLeft READ outerGapLeft WRITE setOuterGapLeft NOTIFY outerGapLeftChanged)
    Q_PROPERTY(int outerGapRight READ outerGapRight WRITE setOuterGapRight NOTIFY outerGapRightChanged)
    Q_PROPERTY(int adjacentThreshold READ adjacentThreshold WRITE setAdjacentThreshold NOTIFY adjacentThresholdChanged)

    // Performance and behavior (configurable constants)
    Q_PROPERTY(int pollIntervalMs READ pollIntervalMs WRITE setPollIntervalMs NOTIFY pollIntervalMsChanged)
    Q_PROPERTY(int minimumZoneSizePx READ minimumZoneSizePx WRITE setMinimumZoneSizePx NOTIFY minimumZoneSizePxChanged)
    Q_PROPERTY(int minimumZoneDisplaySizePx READ minimumZoneDisplaySizePx WRITE setMinimumZoneDisplaySizePx NOTIFY
                   minimumZoneDisplaySizePxChanged)

    // Window behavior
    Q_PROPERTY(bool keepWindowsInZonesOnResolutionChange READ keepWindowsInZonesOnResolutionChange WRITE
                   setKeepWindowsInZonesOnResolutionChange NOTIFY keepWindowsInZonesOnResolutionChangeChanged)
    Q_PROPERTY(bool moveNewWindowsToLastZone READ moveNewWindowsToLastZone WRITE setMoveNewWindowsToLastZone NOTIFY
                   moveNewWindowsToLastZoneChanged)
    Q_PROPERTY(bool restoreOriginalSizeOnUnsnap READ restoreOriginalSizeOnUnsnap WRITE setRestoreOriginalSizeOnUnsnap
                   NOTIFY restoreOriginalSizeOnUnsnapChanged)
    Q_PROPERTY(int stickyWindowHandling READ stickyWindowHandlingInt WRITE setStickyWindowHandlingInt NOTIFY
                   stickyWindowHandlingChanged)
    Q_PROPERTY(bool restoreWindowsToZonesOnLogin READ restoreWindowsToZonesOnLogin WRITE setRestoreWindowsToZonesOnLogin
                   NOTIFY restoreWindowsToZonesOnLoginChanged)
    Q_PROPERTY(bool snapAssistFeatureEnabled READ snapAssistFeatureEnabled WRITE setSnapAssistFeatureEnabled NOTIFY
                   snapAssistFeatureEnabledChanged)
    Q_PROPERTY(bool snapAssistEnabled READ snapAssistEnabled WRITE setSnapAssistEnabled NOTIFY snapAssistEnabledChanged)
    Q_PROPERTY(QVariantList snapAssistTriggers READ snapAssistTriggers WRITE setSnapAssistTriggers NOTIFY
                   snapAssistTriggersChanged)

    // Default layout (used when no explicit assignment exists)
    Q_PROPERTY(QString defaultLayoutId READ defaultLayoutId WRITE setDefaultLayoutId NOTIFY defaultLayoutIdChanged)

    // Layout filtering
    Q_PROPERTY(bool filterLayoutsByAspectRatio READ filterLayoutsByAspectRatio WRITE setFilterLayoutsByAspectRatio
                   NOTIFY filterLayoutsByAspectRatioChanged)

    // Exclusions
    Q_PROPERTY(QStringList excludedApplications READ excludedApplications WRITE setExcludedApplications NOTIFY
                   excludedApplicationsChanged)
    Q_PROPERTY(QStringList excludedWindowClasses READ excludedWindowClasses WRITE setExcludedWindowClasses NOTIFY
                   excludedWindowClassesChanged)
    Q_PROPERTY(bool excludeTransientWindows READ excludeTransientWindows WRITE setExcludeTransientWindows NOTIFY
                   excludeTransientWindowsChanged)
    Q_PROPERTY(
        int minimumWindowWidth READ minimumWindowWidth WRITE setMinimumWindowWidth NOTIFY minimumWindowWidthChanged)
    Q_PROPERTY(
        int minimumWindowHeight READ minimumWindowHeight WRITE setMinimumWindowHeight NOTIFY minimumWindowHeightChanged)

    // Zone Selector
    Q_PROPERTY(bool zoneSelectorEnabled READ zoneSelectorEnabled WRITE setZoneSelectorEnabled NOTIFY
                   zoneSelectorEnabledChanged)
    Q_PROPERTY(int zoneSelectorTriggerDistance READ zoneSelectorTriggerDistance WRITE setZoneSelectorTriggerDistance
                   NOTIFY zoneSelectorTriggerDistanceChanged)
    Q_PROPERTY(int zoneSelectorPosition READ zoneSelectorPositionInt WRITE setZoneSelectorPositionInt NOTIFY
                   zoneSelectorPositionChanged)
    Q_PROPERTY(int zoneSelectorLayoutMode READ zoneSelectorLayoutModeInt WRITE setZoneSelectorLayoutModeInt NOTIFY
                   zoneSelectorLayoutModeChanged)
    Q_PROPERTY(int zoneSelectorSizeMode READ zoneSelectorSizeModeInt WRITE setZoneSelectorSizeModeInt NOTIFY
                   zoneSelectorSizeModeChanged)
    Q_PROPERTY(
        int zoneSelectorMaxRows READ zoneSelectorMaxRows WRITE setZoneSelectorMaxRows NOTIFY zoneSelectorMaxRowsChanged)
    Q_PROPERTY(int zoneSelectorPreviewWidth READ zoneSelectorPreviewWidth WRITE setZoneSelectorPreviewWidth NOTIFY
                   zoneSelectorPreviewWidthChanged)
    Q_PROPERTY(int zoneSelectorPreviewHeight READ zoneSelectorPreviewHeight WRITE setZoneSelectorPreviewHeight NOTIFY
                   zoneSelectorPreviewHeightChanged)
    Q_PROPERTY(bool zoneSelectorPreviewLockAspect READ zoneSelectorPreviewLockAspect WRITE
                   setZoneSelectorPreviewLockAspect NOTIFY zoneSelectorPreviewLockAspectChanged)
    Q_PROPERTY(int zoneSelectorGridColumns READ zoneSelectorGridColumns WRITE setZoneSelectorGridColumns NOTIFY
                   zoneSelectorGridColumnsChanged)

    // Autotiling Settings
    Q_PROPERTY(bool autotileEnabled READ autotileEnabled WRITE setAutotileEnabled NOTIFY autotileEnabledChanged)
    Q_PROPERTY(
        QString autotileAlgorithm READ autotileAlgorithm WRITE setAutotileAlgorithm NOTIFY autotileAlgorithmChanged)
    Q_PROPERTY(
        qreal autotileSplitRatio READ autotileSplitRatio WRITE setAutotileSplitRatio NOTIFY autotileSplitRatioChanged)
    Q_PROPERTY(
        int autotileMasterCount READ autotileMasterCount WRITE setAutotileMasterCount NOTIFY autotileMasterCountChanged)
    Q_PROPERTY(QVariantMap autotilePerAlgorithmSettings READ autotilePerAlgorithmSettings WRITE
                   setAutotilePerAlgorithmSettings NOTIFY autotilePerAlgorithmSettingsChanged)
    Q_PROPERTY(int autotileInnerGap READ autotileInnerGap WRITE setAutotileInnerGap NOTIFY autotileInnerGapChanged)
    Q_PROPERTY(int autotileOuterGap READ autotileOuterGap WRITE setAutotileOuterGap NOTIFY autotileOuterGapChanged)
    Q_PROPERTY(bool autotileUsePerSideOuterGap READ autotileUsePerSideOuterGap WRITE setAutotileUsePerSideOuterGap
                   NOTIFY autotileUsePerSideOuterGapChanged)
    Q_PROPERTY(
        int autotileOuterGapTop READ autotileOuterGapTop WRITE setAutotileOuterGapTop NOTIFY autotileOuterGapTopChanged)
    Q_PROPERTY(int autotileOuterGapBottom READ autotileOuterGapBottom WRITE setAutotileOuterGapBottom NOTIFY
                   autotileOuterGapBottomChanged)
    Q_PROPERTY(int autotileOuterGapLeft READ autotileOuterGapLeft WRITE setAutotileOuterGapLeft NOTIFY
                   autotileOuterGapLeftChanged)
    Q_PROPERTY(int autotileOuterGapRight READ autotileOuterGapRight WRITE setAutotileOuterGapRight NOTIFY
                   autotileOuterGapRightChanged)
    Q_PROPERTY(bool autotileFocusNewWindows READ autotileFocusNewWindows WRITE setAutotileFocusNewWindows NOTIFY
                   autotileFocusNewWindowsChanged)
    Q_PROPERTY(bool autotileSmartGaps READ autotileSmartGaps WRITE setAutotileSmartGaps NOTIFY autotileSmartGapsChanged)
    Q_PROPERTY(
        int autotileMaxWindows READ autotileMaxWindows WRITE setAutotileMaxWindows NOTIFY autotileMaxWindowsChanged)
    Q_PROPERTY(int autotileInsertPosition READ autotileInsertPositionInt WRITE setAutotileInsertPositionInt NOTIFY
                   autotileInsertPositionChanged)

    // Animation Settings (applies to both snapping and autotiling geometry changes)
    Q_PROPERTY(bool animationsEnabled READ animationsEnabled WRITE setAnimationsEnabled NOTIFY animationsEnabledChanged)
    Q_PROPERTY(int animationDuration READ animationDuration WRITE setAnimationDuration NOTIFY animationDurationChanged)
    Q_PROPERTY(QString animationEasingCurve READ animationEasingCurve WRITE setAnimationEasingCurve NOTIFY
                   animationEasingCurveChanged)
    Q_PROPERTY(int animationMinDistance READ animationMinDistance WRITE setAnimationMinDistance NOTIFY
                   animationMinDistanceChanged)
    Q_PROPERTY(int animationSequenceMode READ animationSequenceMode WRITE setAnimationSequenceMode NOTIFY
                   animationSequenceModeChanged)
    Q_PROPERTY(int animationStaggerInterval READ animationStaggerInterval WRITE setAnimationStaggerInterval NOTIFY
                   animationStaggerIntervalChanged)

    // Autotile Behavior and Visual Settings
    Q_PROPERTY(bool autotileFocusFollowsMouse READ autotileFocusFollowsMouse WRITE setAutotileFocusFollowsMouse NOTIFY
                   autotileFocusFollowsMouseChanged)
    Q_PROPERTY(bool autotileRespectMinimumSize READ autotileRespectMinimumSize WRITE setAutotileRespectMinimumSize
                   NOTIFY autotileRespectMinimumSizeChanged)
    Q_PROPERTY(bool autotileHideTitleBars READ autotileHideTitleBars WRITE setAutotileHideTitleBars NOTIFY
                   autotileHideTitleBarsChanged)
    Q_PROPERTY(
        bool autotileShowBorder READ autotileShowBorder WRITE setAutotileShowBorder NOTIFY autotileShowBorderChanged)
    Q_PROPERTY(
        int autotileBorderWidth READ autotileBorderWidth WRITE setAutotileBorderWidth NOTIFY autotileBorderWidthChanged)
    Q_PROPERTY(int autotileBorderRadius READ autotileBorderRadius WRITE setAutotileBorderRadius NOTIFY
                   autotileBorderRadiusChanged)
    Q_PROPERTY(QColor autotileBorderColor READ autotileBorderColor WRITE setAutotileBorderColor NOTIFY
                   autotileBorderColorChanged)
    Q_PROPERTY(QColor autotileInactiveBorderColor READ autotileInactiveBorderColor WRITE setAutotileInactiveBorderColor
                   NOTIFY autotileInactiveBorderColorChanged)
    Q_PROPERTY(bool autotileUseSystemBorderColors READ autotileUseSystemBorderColors WRITE
                   setAutotileUseSystemBorderColors NOTIFY autotileUseSystemBorderColorsChanged)
    // Autotile Shortcuts
    Q_PROPERTY(QString autotileToggleShortcut READ autotileToggleShortcut WRITE setAutotileToggleShortcut NOTIFY
                   autotileToggleShortcutChanged)
    Q_PROPERTY(QString autotileFocusMasterShortcut READ autotileFocusMasterShortcut WRITE setAutotileFocusMasterShortcut
                   NOTIFY autotileFocusMasterShortcutChanged)
    Q_PROPERTY(QString autotileSwapMasterShortcut READ autotileSwapMasterShortcut WRITE setAutotileSwapMasterShortcut
                   NOTIFY autotileSwapMasterShortcutChanged)
    Q_PROPERTY(QString autotileIncMasterRatioShortcut READ autotileIncMasterRatioShortcut WRITE
                   setAutotileIncMasterRatioShortcut NOTIFY autotileIncMasterRatioShortcutChanged)
    Q_PROPERTY(QString autotileDecMasterRatioShortcut READ autotileDecMasterRatioShortcut WRITE
                   setAutotileDecMasterRatioShortcut NOTIFY autotileDecMasterRatioShortcutChanged)
    Q_PROPERTY(QString autotileIncMasterCountShortcut READ autotileIncMasterCountShortcut WRITE
                   setAutotileIncMasterCountShortcut NOTIFY autotileIncMasterCountShortcutChanged)
    Q_PROPERTY(QString autotileDecMasterCountShortcut READ autotileDecMasterCountShortcut WRITE
                   setAutotileDecMasterCountShortcut NOTIFY autotileDecMasterCountShortcutChanged)
    Q_PROPERTY(QString autotileRetileShortcut READ autotileRetileShortcut WRITE setAutotileRetileShortcut NOTIFY
                   autotileRetileShortcutChanged)

    // Shader Effects
    Q_PROPERTY(bool enableShaderEffects READ enableShaderEffects WRITE setEnableShaderEffects NOTIFY
                   enableShaderEffectsChanged)
    Q_PROPERTY(int shaderFrameRate READ shaderFrameRate WRITE setShaderFrameRate NOTIFY shaderFrameRateChanged)
    Q_PROPERTY(bool enableAudioVisualizer READ enableAudioVisualizer WRITE setEnableAudioVisualizer NOTIFY
                   enableAudioVisualizerChanged)
    Q_PROPERTY(int audioSpectrumBarCount READ audioSpectrumBarCount WRITE setAudioSpectrumBarCount NOTIFY
                   audioSpectrumBarCountChanged)

    // Global Shortcuts (configurable via KCM, registered with KGlobalAccel)
    Q_PROPERTY(
        QString openEditorShortcut READ openEditorShortcut WRITE setOpenEditorShortcut NOTIFY openEditorShortcutChanged)
    Q_PROPERTY(QString openSettingsShortcut READ openSettingsShortcut WRITE setOpenSettingsShortcut NOTIFY
                   openSettingsShortcutChanged)
    Q_PROPERTY(QString previousLayoutShortcut READ previousLayoutShortcut WRITE setPreviousLayoutShortcut NOTIFY
                   previousLayoutShortcutChanged)
    Q_PROPERTY(
        QString nextLayoutShortcut READ nextLayoutShortcut WRITE setNextLayoutShortcut NOTIFY nextLayoutShortcutChanged)
    Q_PROPERTY(QString quickLayout1Shortcut READ quickLayout1Shortcut WRITE setQuickLayout1Shortcut NOTIFY
                   quickLayout1ShortcutChanged)
    Q_PROPERTY(QString quickLayout2Shortcut READ quickLayout2Shortcut WRITE setQuickLayout2Shortcut NOTIFY
                   quickLayout2ShortcutChanged)
    Q_PROPERTY(QString quickLayout3Shortcut READ quickLayout3Shortcut WRITE setQuickLayout3Shortcut NOTIFY
                   quickLayout3ShortcutChanged)
    Q_PROPERTY(QString quickLayout4Shortcut READ quickLayout4Shortcut WRITE setQuickLayout4Shortcut NOTIFY
                   quickLayout4ShortcutChanged)
    Q_PROPERTY(QString quickLayout5Shortcut READ quickLayout5Shortcut WRITE setQuickLayout5Shortcut NOTIFY
                   quickLayout5ShortcutChanged)
    Q_PROPERTY(QString quickLayout6Shortcut READ quickLayout6Shortcut WRITE setQuickLayout6Shortcut NOTIFY
                   quickLayout6ShortcutChanged)
    Q_PROPERTY(QString quickLayout7Shortcut READ quickLayout7Shortcut WRITE setQuickLayout7Shortcut NOTIFY
                   quickLayout7ShortcutChanged)
    Q_PROPERTY(QString quickLayout8Shortcut READ quickLayout8Shortcut WRITE setQuickLayout8Shortcut NOTIFY
                   quickLayout8ShortcutChanged)
    Q_PROPERTY(QString quickLayout9Shortcut READ quickLayout9Shortcut WRITE setQuickLayout9Shortcut NOTIFY
                   quickLayout9ShortcutChanged)

    // Keyboard Navigation Shortcuts
    Q_PROPERTY(QString moveWindowLeftShortcut READ moveWindowLeftShortcut WRITE setMoveWindowLeftShortcut NOTIFY
                   moveWindowLeftShortcutChanged)
    Q_PROPERTY(QString moveWindowRightShortcut READ moveWindowRightShortcut WRITE setMoveWindowRightShortcut NOTIFY
                   moveWindowRightShortcutChanged)
    Q_PROPERTY(QString moveWindowUpShortcut READ moveWindowUpShortcut WRITE setMoveWindowUpShortcut NOTIFY
                   moveWindowUpShortcutChanged)
    Q_PROPERTY(QString moveWindowDownShortcut READ moveWindowDownShortcut WRITE setMoveWindowDownShortcut NOTIFY
                   moveWindowDownShortcutChanged)
    Q_PROPERTY(QString focusZoneLeftShortcut READ focusZoneLeftShortcut WRITE setFocusZoneLeftShortcut NOTIFY
                   focusZoneLeftShortcutChanged)
    Q_PROPERTY(QString focusZoneRightShortcut READ focusZoneRightShortcut WRITE setFocusZoneRightShortcut NOTIFY
                   focusZoneRightShortcutChanged)
    Q_PROPERTY(QString focusZoneUpShortcut READ focusZoneUpShortcut WRITE setFocusZoneUpShortcut NOTIFY
                   focusZoneUpShortcutChanged)
    Q_PROPERTY(QString focusZoneDownShortcut READ focusZoneDownShortcut WRITE setFocusZoneDownShortcut NOTIFY
                   focusZoneDownShortcutChanged)
    Q_PROPERTY(QString pushToEmptyZoneShortcut READ pushToEmptyZoneShortcut WRITE setPushToEmptyZoneShortcut NOTIFY
                   pushToEmptyZoneShortcutChanged)
    Q_PROPERTY(QString restoreWindowSizeShortcut READ restoreWindowSizeShortcut WRITE setRestoreWindowSizeShortcut
                   NOTIFY restoreWindowSizeShortcutChanged)
    Q_PROPERTY(QString toggleWindowFloatShortcut READ toggleWindowFloatShortcut WRITE setToggleWindowFloatShortcut
                   NOTIFY toggleWindowFloatShortcutChanged)

    // Swap Window Shortcuts (Meta+Ctrl+Alt+Arrow)
    Q_PROPERTY(QString swapWindowLeftShortcut READ swapWindowLeftShortcut WRITE setSwapWindowLeftShortcut NOTIFY
                   swapWindowLeftShortcutChanged)
    Q_PROPERTY(QString swapWindowRightShortcut READ swapWindowRightShortcut WRITE setSwapWindowRightShortcut NOTIFY
                   swapWindowRightShortcutChanged)
    Q_PROPERTY(QString swapWindowUpShortcut READ swapWindowUpShortcut WRITE setSwapWindowUpShortcut NOTIFY
                   swapWindowUpShortcutChanged)
    Q_PROPERTY(QString swapWindowDownShortcut READ swapWindowDownShortcut WRITE setSwapWindowDownShortcut NOTIFY
                   swapWindowDownShortcutChanged)

    // Snap to Zone by Number Shortcuts (Meta+Ctrl+1-9)
    Q_PROPERTY(QString snapToZone1Shortcut READ snapToZone1Shortcut WRITE setSnapToZone1Shortcut NOTIFY
                   snapToZone1ShortcutChanged)
    Q_PROPERTY(QString snapToZone2Shortcut READ snapToZone2Shortcut WRITE setSnapToZone2Shortcut NOTIFY
                   snapToZone2ShortcutChanged)
    Q_PROPERTY(QString snapToZone3Shortcut READ snapToZone3Shortcut WRITE setSnapToZone3Shortcut NOTIFY
                   snapToZone3ShortcutChanged)
    Q_PROPERTY(QString snapToZone4Shortcut READ snapToZone4Shortcut WRITE setSnapToZone4Shortcut NOTIFY
                   snapToZone4ShortcutChanged)
    Q_PROPERTY(QString snapToZone5Shortcut READ snapToZone5Shortcut WRITE setSnapToZone5Shortcut NOTIFY
                   snapToZone5ShortcutChanged)
    Q_PROPERTY(QString snapToZone6Shortcut READ snapToZone6Shortcut WRITE setSnapToZone6Shortcut NOTIFY
                   snapToZone6ShortcutChanged)
    Q_PROPERTY(QString snapToZone7Shortcut READ snapToZone7Shortcut WRITE setSnapToZone7Shortcut NOTIFY
                   snapToZone7ShortcutChanged)
    Q_PROPERTY(QString snapToZone8Shortcut READ snapToZone8Shortcut WRITE setSnapToZone8Shortcut NOTIFY
                   snapToZone8ShortcutChanged)
    Q_PROPERTY(QString snapToZone9Shortcut READ snapToZone9Shortcut WRITE setSnapToZone9Shortcut NOTIFY
                   snapToZone9ShortcutChanged)

    // Rotate Windows Shortcuts (Meta+Ctrl+[ / Meta+Ctrl+])
    // Rotates all windows in the current layout clockwise or counterclockwise
    Q_PROPERTY(QString rotateWindowsClockwiseShortcut READ rotateWindowsClockwiseShortcut WRITE
                   setRotateWindowsClockwiseShortcut NOTIFY rotateWindowsClockwiseShortcutChanged)
    Q_PROPERTY(QString rotateWindowsCounterclockwiseShortcut READ rotateWindowsCounterclockwiseShortcut WRITE
                   setRotateWindowsCounterclockwiseShortcut NOTIFY rotateWindowsCounterclockwiseShortcutChanged)

    // Cycle Windows in Zone Shortcuts (Meta+Alt+. / Meta+Alt+,)
    // Cycles focus between windows stacked in the same zone
    Q_PROPERTY(QString cycleWindowForwardShortcut READ cycleWindowForwardShortcut WRITE setCycleWindowForwardShortcut
                   NOTIFY cycleWindowForwardShortcutChanged)
    Q_PROPERTY(QString cycleWindowBackwardShortcut READ cycleWindowBackwardShortcut WRITE setCycleWindowBackwardShortcut
                   NOTIFY cycleWindowBackwardShortcutChanged)

    // Resnap to New Layout (Meta+Ctrl+Z, easy pinky key)
    Q_PROPERTY(QString resnapToNewLayoutShortcut READ resnapToNewLayoutShortcut WRITE setResnapToNewLayoutShortcut
                   NOTIFY resnapToNewLayoutShortcutChanged)

    // Snap All Windows (Meta+Ctrl+S — same namespace as rotate/resnap batch ops)
    Q_PROPERTY(QString snapAllWindowsShortcut READ snapAllWindowsShortcut WRITE setSnapAllWindowsShortcut NOTIFY
                   snapAllWindowsShortcutChanged)

    // Layout Picker (Meta+Alt+Space — browse and switch layouts interactively)
    Q_PROPERTY(QString layoutPickerShortcut READ layoutPickerShortcut WRITE setLayoutPickerShortcut NOTIFY
                   layoutPickerShortcutChanged)

    // Toggle Layout Lock (Meta+Ctrl+L)
    Q_PROPERTY(QString toggleLayoutLockShortcut READ toggleLayoutLockShortcut WRITE setToggleLayoutLockShortcut NOTIFY
                   toggleLayoutLockShortcutChanged)

public:
    explicit Settings(QObject* parent = nullptr);
    ~Settings() override = default;

    // No singleton - use dependency injection instead

    // ISettings interface implementation
    bool shiftDragToActivate() const override
    {
        return m_shiftDragToActivate;
    }
    void setShiftDragToActivate(bool enable) override;

    QVariantList dragActivationTriggers() const override
    {
        return m_dragActivationTriggers;
    }
    void setDragActivationTriggers(const QVariantList& triggers) override;

    bool zoneSpanEnabled() const override
    {
        return m_zoneSpanEnabled;
    }
    void setZoneSpanEnabled(bool enabled) override;
    DragModifier zoneSpanModifier() const override
    {
        return m_zoneSpanModifier;
    }
    void setZoneSpanModifier(DragModifier modifier) override;
    QVariantList zoneSpanTriggers() const override
    {
        return m_zoneSpanTriggers;
    }
    void setZoneSpanTriggers(const QVariantList& triggers) override;
    bool toggleActivation() const override
    {
        return m_toggleActivation;
    }
    void setToggleActivation(bool enable) override;
    bool snappingEnabled() const override
    {
        return m_snappingEnabled;
    }
    void setSnappingEnabled(bool enabled) override;
    int zoneSpanModifierInt() const
    {
        return static_cast<int>(m_zoneSpanModifier);
    }
    void setZoneSpanModifierInt(int modifier);

    bool showZonesOnAllMonitors() const override
    {
        return m_showZonesOnAllMonitors;
    }
    void setShowZonesOnAllMonitors(bool show) override;

    QStringList disabledMonitors() const override
    {
        return m_disabledMonitors;
    }
    void setDisabledMonitors(const QStringList& screenIdOrNames) override;
    bool isMonitorDisabled(const QString& screenIdOrName) const override;

    QList<int> disabledDesktops() const override
    {
        return m_disabledDesktops;
    }
    void setDisabledDesktops(const QList<int>& desktops) override;
    bool isDesktopDisabled(int desktop) const override;

    QStringList disabledActivities() const override
    {
        return m_disabledActivities;
    }
    void setDisabledActivities(const QStringList& activityIds) override;
    bool isActivityDisabled(const QString& activityId) const override;

    bool showZoneNumbers() const override
    {
        return m_showZoneNumbers;
    }
    void setShowZoneNumbers(bool show) override;

    bool flashZonesOnSwitch() const override
    {
        return m_flashZonesOnSwitch;
    }
    void setFlashZonesOnSwitch(bool flash) override;

    bool showOsdOnLayoutSwitch() const override
    {
        return m_showOsdOnLayoutSwitch;
    }
    void setShowOsdOnLayoutSwitch(bool show) override;

    bool showNavigationOsd() const override
    {
        return m_showNavigationOsd;
    }
    void setShowNavigationOsd(bool show) override;

    OsdStyle osdStyle() const override
    {
        return m_osdStyle;
    }
    void setOsdStyle(OsdStyle style) override;
    int osdStyleInt() const
    {
        return static_cast<int>(m_osdStyle);
    }
    void setOsdStyleInt(int style);

    OverlayDisplayMode overlayDisplayMode() const override
    {
        return m_overlayDisplayMode;
    }
    void setOverlayDisplayMode(OverlayDisplayMode mode) override;
    int overlayDisplayModeInt() const
    {
        return static_cast<int>(m_overlayDisplayMode);
    }
    void setOverlayDisplayModeInt(int mode);

    bool useSystemColors() const override
    {
        return m_useSystemColors;
    }
    void setUseSystemColors(bool use) override;

    QColor highlightColor() const override
    {
        return m_highlightColor;
    }
    void setHighlightColor(const QColor& color) override;

    QColor inactiveColor() const override
    {
        return m_inactiveColor;
    }
    void setInactiveColor(const QColor& color) override;

    QColor borderColor() const override
    {
        return m_borderColor;
    }
    void setBorderColor(const QColor& color) override;

    QColor labelFontColor() const override
    {
        return m_labelFontColor;
    }
    void setLabelFontColor(const QColor& color) override;

    qreal activeOpacity() const override
    {
        return m_activeOpacity;
    }
    void setActiveOpacity(qreal opacity) override;

    qreal inactiveOpacity() const override
    {
        return m_inactiveOpacity;
    }
    void setInactiveOpacity(qreal opacity) override;

    int borderWidth() const override
    {
        return m_borderWidth;
    }
    void setBorderWidth(int width) override;

    int borderRadius() const override
    {
        return m_borderRadius;
    }
    void setBorderRadius(int radius) override;

    bool enableBlur() const override
    {
        return m_enableBlur;
    }
    void setEnableBlur(bool enable) override;

    QString labelFontFamily() const override
    {
        return m_labelFontFamily;
    }
    void setLabelFontFamily(const QString& family) override;
    qreal labelFontSizeScale() const override
    {
        return m_labelFontSizeScale;
    }
    void setLabelFontSizeScale(qreal scale) override;
    int labelFontWeight() const override
    {
        return m_labelFontWeight;
    }
    void setLabelFontWeight(int weight) override;
    bool labelFontItalic() const override
    {
        return m_labelFontItalic;
    }
    void setLabelFontItalic(bool italic) override;
    bool labelFontUnderline() const override
    {
        return m_labelFontUnderline;
    }
    void setLabelFontUnderline(bool underline) override;
    bool labelFontStrikeout() const override
    {
        return m_labelFontStrikeout;
    }
    void setLabelFontStrikeout(bool strikeout) override;

    int zonePadding() const override
    {
        return m_zonePadding;
    }
    void setZonePadding(int padding) override;

    int outerGap() const override
    {
        return m_outerGap;
    }
    void setOuterGap(int gap) override;

    bool usePerSideOuterGap() const override
    {
        return m_usePerSideOuterGap;
    }
    void setUsePerSideOuterGap(bool enabled) override;

    int outerGapTop() const override
    {
        return m_outerGapTop;
    }
    void setOuterGapTop(int gap) override;

    int outerGapBottom() const override
    {
        return m_outerGapBottom;
    }
    void setOuterGapBottom(int gap) override;

    int outerGapLeft() const override
    {
        return m_outerGapLeft;
    }
    void setOuterGapLeft(int gap) override;

    int outerGapRight() const override
    {
        return m_outerGapRight;
    }
    void setOuterGapRight(int gap) override;

    int adjacentThreshold() const override
    {
        return m_adjacentThreshold;
    }
    void setAdjacentThreshold(int threshold) override;

    int pollIntervalMs() const override
    {
        return m_pollIntervalMs;
    }
    void setPollIntervalMs(int interval) override;

    int minimumZoneSizePx() const override
    {
        return m_minimumZoneSizePx;
    }
    void setMinimumZoneSizePx(int size) override;

    int minimumZoneDisplaySizePx() const override
    {
        return m_minimumZoneDisplaySizePx;
    }
    void setMinimumZoneDisplaySizePx(int size) override;

    bool keepWindowsInZonesOnResolutionChange() const override
    {
        return m_keepWindowsInZonesOnResolutionChange;
    }
    void setKeepWindowsInZonesOnResolutionChange(bool keep) override;

    bool moveNewWindowsToLastZone() const override
    {
        return m_moveNewWindowsToLastZone;
    }
    void setMoveNewWindowsToLastZone(bool move) override;

    bool restoreOriginalSizeOnUnsnap() const override
    {
        return m_restoreOriginalSizeOnUnsnap;
    }
    void setRestoreOriginalSizeOnUnsnap(bool restore) override;
    StickyWindowHandling stickyWindowHandling() const override
    {
        return m_stickyWindowHandling;
    }
    void setStickyWindowHandling(StickyWindowHandling handling) override;
    int stickyWindowHandlingInt() const
    {
        return static_cast<int>(m_stickyWindowHandling);
    }
    void setStickyWindowHandlingInt(int handling);

    bool restoreWindowsToZonesOnLogin() const override
    {
        return m_restoreWindowsToZonesOnLogin;
    }
    void setRestoreWindowsToZonesOnLogin(bool restore) override;

    bool snapAssistFeatureEnabled() const override
    {
        return m_snapAssistFeatureEnabled;
    }
    void setSnapAssistFeatureEnabled(bool enabled) override;

    bool snapAssistEnabled() const override
    {
        return m_snapAssistEnabled;
    }
    void setSnapAssistEnabled(bool enabled) override;

    QVariantList snapAssistTriggers() const override
    {
        return m_snapAssistTriggers;
    }
    void setSnapAssistTriggers(const QVariantList& triggers) override;

    QString defaultLayoutId() const override
    {
        return m_defaultLayoutId;
    }
    void setDefaultLayoutId(const QString& layoutId) override;

    bool filterLayoutsByAspectRatio() const override
    {
        return m_filterLayoutsByAspectRatio;
    }
    void setFilterLayoutsByAspectRatio(bool filter) override;

    QStringList excludedApplications() const override
    {
        return m_excludedApplications;
    }
    void setExcludedApplications(const QStringList& apps) override;
    Q_INVOKABLE void addExcludedApplication(const QString& app);
    Q_INVOKABLE void removeExcludedApplicationAt(int index);

    QStringList excludedWindowClasses() const override
    {
        return m_excludedWindowClasses;
    }
    void setExcludedWindowClasses(const QStringList& classes) override;
    Q_INVOKABLE void addExcludedWindowClass(const QString& cls);
    Q_INVOKABLE void removeExcludedWindowClassAt(int index);

    bool excludeTransientWindows() const override
    {
        return m_excludeTransientWindows;
    }
    void setExcludeTransientWindows(bool exclude) override;

    int minimumWindowWidth() const override
    {
        return m_minimumWindowWidth;
    }
    void setMinimumWindowWidth(int width) override;

    int minimumWindowHeight() const override
    {
        return m_minimumWindowHeight;
    }
    void setMinimumWindowHeight(int height) override;

    // Zone Selector
    bool zoneSelectorEnabled() const override
    {
        return m_zoneSelectorEnabled;
    }
    void setZoneSelectorEnabled(bool enabled) override;
    int zoneSelectorTriggerDistance() const override
    {
        return m_zoneSelectorTriggerDistance;
    }
    void setZoneSelectorTriggerDistance(int distance) override;
    ZoneSelectorPosition zoneSelectorPosition() const override
    {
        return m_zoneSelectorPosition;
    }
    void setZoneSelectorPosition(ZoneSelectorPosition position) override;
    int zoneSelectorPositionInt() const
    {
        return static_cast<int>(m_zoneSelectorPosition);
    }
    void setZoneSelectorPositionInt(int position);
    ZoneSelectorLayoutMode zoneSelectorLayoutMode() const override
    {
        return m_zoneSelectorLayoutMode;
    }
    void setZoneSelectorLayoutMode(ZoneSelectorLayoutMode mode) override;
    int zoneSelectorLayoutModeInt() const
    {
        return static_cast<int>(m_zoneSelectorLayoutMode);
    }
    void setZoneSelectorLayoutModeInt(int mode);
    int zoneSelectorPreviewWidth() const override
    {
        return m_zoneSelectorPreviewWidth;
    }
    void setZoneSelectorPreviewWidth(int width) override;
    int zoneSelectorPreviewHeight() const override
    {
        return m_zoneSelectorPreviewHeight;
    }
    void setZoneSelectorPreviewHeight(int height) override;
    bool zoneSelectorPreviewLockAspect() const override
    {
        return m_zoneSelectorPreviewLockAspect;
    }
    void setZoneSelectorPreviewLockAspect(bool locked) override;
    int zoneSelectorGridColumns() const override
    {
        return m_zoneSelectorGridColumns;
    }
    void setZoneSelectorGridColumns(int columns) override;
    ZoneSelectorSizeMode zoneSelectorSizeMode() const override
    {
        return m_zoneSelectorSizeMode;
    }
    void setZoneSelectorSizeMode(ZoneSelectorSizeMode mode) override;
    int zoneSelectorSizeModeInt() const
    {
        return static_cast<int>(m_zoneSelectorSizeMode);
    }
    void setZoneSelectorSizeModeInt(int mode);
    int zoneSelectorMaxRows() const override
    {
        return m_zoneSelectorMaxRows;
    }
    void setZoneSelectorMaxRows(int rows) override;

    // Per-screen zone selector config (override > global fallback)
    ZoneSelectorConfig resolvedZoneSelectorConfig(const QString& screenIdOrName) const override;
    Q_INVOKABLE QVariantMap getPerScreenZoneSelectorSettings(const QString& screenIdOrName) const;
    Q_INVOKABLE void setPerScreenZoneSelectorSetting(const QString& screenIdOrName, const QString& key,
                                                     const QVariant& value);
    Q_INVOKABLE void clearPerScreenZoneSelectorSettings(const QString& screenIdOrName);
    Q_INVOKABLE bool hasPerScreenZoneSelectorSettings(const QString& screenIdOrName) const;
    Q_INVOKABLE QStringList screensWithZoneSelectorOverrides() const;

    // Per-screen autotile config (override > global fallback)
    Q_INVOKABLE QVariantMap getPerScreenAutotileSettings(const QString& screenIdOrName) const;
    Q_INVOKABLE void setPerScreenAutotileSetting(const QString& screenIdOrName, const QString& key,
                                                 const QVariant& value);
    Q_INVOKABLE void clearPerScreenAutotileSettings(const QString& screenIdOrName);
    Q_INVOKABLE bool hasPerScreenAutotileSettings(const QString& screenIdOrName) const;

    // Per-screen snapping config (override > global fallback)
    Q_INVOKABLE QVariantMap getPerScreenSnappingSettings(const QString& screenIdOrName) const override;
    Q_INVOKABLE void setPerScreenSnappingSetting(const QString& screenIdOrName, const QString& key,
                                                 const QVariant& value);
    Q_INVOKABLE void clearPerScreenSnappingSettings(const QString& screenIdOrName);
    Q_INVOKABLE bool hasPerScreenSnappingSettings(const QString& screenIdOrName) const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Autotiling Settings (ISettings interface)
    // ═══════════════════════════════════════════════════════════════════════════

    bool autotileEnabled() const
    {
        return m_autotileEnabled;
    }
    void setAutotileEnabled(bool enabled);

    QString autotileAlgorithm() const
    {
        return m_autotileAlgorithm;
    }
    void setAutotileAlgorithm(const QString& algorithm);

    qreal autotileSplitRatio() const
    {
        return m_autotileSplitRatio;
    }
    void setAutotileSplitRatio(qreal ratio);

    int autotileMasterCount() const
    {
        return m_autotileMasterCount;
    }
    void setAutotileMasterCount(int count);

    QVariantMap autotilePerAlgorithmSettings() const
    {
        return m_autotilePerAlgorithmSettings;
    }
    void setAutotilePerAlgorithmSettings(const QVariantMap& settings);

    int autotileInnerGap() const
    {
        return m_autotileInnerGap;
    }
    void setAutotileInnerGap(int gap);

    int autotileOuterGap() const
    {
        return m_autotileOuterGap;
    }
    void setAutotileOuterGap(int gap);

    bool autotileUsePerSideOuterGap() const
    {
        return m_autotileUsePerSideOuterGap;
    }
    void setAutotileUsePerSideOuterGap(bool enabled);

    int autotileOuterGapTop() const
    {
        return m_autotileOuterGapTop;
    }
    void setAutotileOuterGapTop(int gap);

    int autotileOuterGapBottom() const
    {
        return m_autotileOuterGapBottom;
    }
    void setAutotileOuterGapBottom(int gap);

    int autotileOuterGapLeft() const
    {
        return m_autotileOuterGapLeft;
    }
    void setAutotileOuterGapLeft(int gap);

    int autotileOuterGapRight() const
    {
        return m_autotileOuterGapRight;
    }
    void setAutotileOuterGapRight(int gap);

    bool autotileFocusNewWindows() const
    {
        return m_autotileFocusNewWindows;
    }
    void setAutotileFocusNewWindows(bool focus);

    bool autotileSmartGaps() const
    {
        return m_autotileSmartGaps;
    }
    void setAutotileSmartGaps(bool smart);

    int autotileMaxWindows() const
    {
        return m_autotileMaxWindows;
    }
    void setAutotileMaxWindows(int count);

    enum class AutotileInsertPosition {
        End = 0,
        AfterFocused = 1,
        AsMaster = 2
    };
    AutotileInsertPosition autotileInsertPosition() const
    {
        return m_autotileInsertPosition;
    }
    void setAutotileInsertPosition(AutotileInsertPosition position);
    int autotileInsertPositionInt() const
    {
        return static_cast<int>(m_autotileInsertPosition);
    }
    void setAutotileInsertPositionInt(int position);

    // Autotile Shortcuts
    QString autotileToggleShortcut() const
    {
        return m_autotileToggleShortcut;
    }
    void setAutotileToggleShortcut(const QString& shortcut);

    QString autotileFocusMasterShortcut() const
    {
        return m_autotileFocusMasterShortcut;
    }
    void setAutotileFocusMasterShortcut(const QString& shortcut);

    QString autotileSwapMasterShortcut() const
    {
        return m_autotileSwapMasterShortcut;
    }
    void setAutotileSwapMasterShortcut(const QString& shortcut);

    QString autotileIncMasterRatioShortcut() const
    {
        return m_autotileIncMasterRatioShortcut;
    }
    void setAutotileIncMasterRatioShortcut(const QString& shortcut);

    QString autotileDecMasterRatioShortcut() const
    {
        return m_autotileDecMasterRatioShortcut;
    }
    void setAutotileDecMasterRatioShortcut(const QString& shortcut);

    QString autotileIncMasterCountShortcut() const
    {
        return m_autotileIncMasterCountShortcut;
    }
    void setAutotileIncMasterCountShortcut(const QString& shortcut);

    QString autotileDecMasterCountShortcut() const
    {
        return m_autotileDecMasterCountShortcut;
    }
    void setAutotileDecMasterCountShortcut(const QString& shortcut);

    QString autotileRetileShortcut() const
    {
        return m_autotileRetileShortcut;
    }
    void setAutotileRetileShortcut(const QString& shortcut);

    // Animation Settings (applies to both snapping and autotiling geometry changes)
    bool animationsEnabled() const override
    {
        return m_animationsEnabled;
    }
    void setAnimationsEnabled(bool enabled) override;

    int animationDuration() const override
    {
        return m_animationDuration;
    }
    void setAnimationDuration(int duration) override;

    QString animationEasingCurve() const override
    {
        return m_animationEasingCurve;
    }
    void setAnimationEasingCurve(const QString& curve) override;

    int animationMinDistance() const override
    {
        return m_animationMinDistance;
    }
    void setAnimationMinDistance(int distance) override;

    int animationSequenceMode() const override
    {
        return m_animationSequenceMode;
    }
    void setAnimationSequenceMode(int mode) override;

    int animationStaggerInterval() const override
    {
        return m_animationStaggerInterval;
    }
    void setAnimationStaggerInterval(int ms) override;

    // Additional Autotiling Settings
    bool autotileFocusFollowsMouse() const override
    {
        return m_autotileFocusFollowsMouse;
    }
    void setAutotileFocusFollowsMouse(bool focus) override;

    bool autotileRespectMinimumSize() const
    {
        return m_autotileRespectMinimumSize;
    }
    void setAutotileRespectMinimumSize(bool respect);

    bool autotileHideTitleBars() const override
    {
        return m_autotileHideTitleBars;
    }
    void setAutotileHideTitleBars(bool hide) override;

    bool autotileShowBorder() const override
    {
        return m_autotileShowBorder;
    }
    void setAutotileShowBorder(bool show) override;

    int autotileBorderWidth() const override
    {
        return m_autotileBorderWidth;
    }
    void setAutotileBorderWidth(int width) override;

    int autotileBorderRadius() const override
    {
        return m_autotileBorderRadius;
    }
    void setAutotileBorderRadius(int radius) override;

    QColor autotileBorderColor() const override
    {
        return m_autotileBorderColor;
    }
    void setAutotileBorderColor(const QColor& color) override;

    QColor autotileInactiveBorderColor() const override
    {
        return m_autotileInactiveBorderColor;
    }
    void setAutotileInactiveBorderColor(const QColor& color) override;

    bool autotileUseSystemBorderColors() const override
    {
        return m_autotileUseSystemBorderColors;
    }
    void setAutotileUseSystemBorderColors(bool use) override;

    QStringList lockedScreens() const override
    {
        return m_lockedScreens;
    }
    void setLockedScreens(const QStringList& screens) override;
    bool isScreenLocked(const QString& screenIdOrName) const override;
    void setScreenLocked(const QString& screenIdOrName, bool locked) override;
    bool isContextLocked(const QString& screenIdOrName, int virtualDesktop, const QString& activity) const override;
    void setContextLocked(const QString& screenIdOrName, int virtualDesktop, const QString& activity,
                          bool locked) override;

    // Shader Effects
    bool enableShaderEffects() const override
    {
        return m_enableShaderEffects;
    }
    void setEnableShaderEffects(bool enable) override;
    int shaderFrameRate() const override
    {
        return m_shaderFrameRate;
    }
    void setShaderFrameRate(int fps) override;
    bool enableAudioVisualizer() const override
    {
        return m_enableAudioVisualizer;
    }
    void setEnableAudioVisualizer(bool enable) override;
    int audioSpectrumBarCount() const override
    {
        return m_audioSpectrumBarCount;
    }
    void setAudioSpectrumBarCount(int count) override;

    // Global Shortcuts (for KGlobalAccel)
    QString openEditorShortcut() const
    {
        return m_openEditorShortcut;
    }
    void setOpenEditorShortcut(const QString& shortcut);
    QString openSettingsShortcut() const
    {
        return m_openSettingsShortcut;
    }
    void setOpenSettingsShortcut(const QString& shortcut);
    QString previousLayoutShortcut() const
    {
        return m_previousLayoutShortcut;
    }
    void setPreviousLayoutShortcut(const QString& shortcut);
    QString nextLayoutShortcut() const
    {
        return m_nextLayoutShortcut;
    }
    void setNextLayoutShortcut(const QString& shortcut);
    QString quickLayout1Shortcut() const
    {
        return m_quickLayoutShortcuts[0];
    }
    void setQuickLayout1Shortcut(const QString& shortcut);
    QString quickLayout2Shortcut() const
    {
        return m_quickLayoutShortcuts[1];
    }
    void setQuickLayout2Shortcut(const QString& shortcut);
    QString quickLayout3Shortcut() const
    {
        return m_quickLayoutShortcuts[2];
    }
    void setQuickLayout3Shortcut(const QString& shortcut);
    QString quickLayout4Shortcut() const
    {
        return m_quickLayoutShortcuts[3];
    }
    void setQuickLayout4Shortcut(const QString& shortcut);
    QString quickLayout5Shortcut() const
    {
        return m_quickLayoutShortcuts[4];
    }
    void setQuickLayout5Shortcut(const QString& shortcut);
    QString quickLayout6Shortcut() const
    {
        return m_quickLayoutShortcuts[5];
    }
    void setQuickLayout6Shortcut(const QString& shortcut);
    QString quickLayout7Shortcut() const
    {
        return m_quickLayoutShortcuts[6];
    }
    void setQuickLayout7Shortcut(const QString& shortcut);
    QString quickLayout8Shortcut() const
    {
        return m_quickLayoutShortcuts[7];
    }
    void setQuickLayout8Shortcut(const QString& shortcut);
    QString quickLayout9Shortcut() const
    {
        return m_quickLayoutShortcuts[8];
    }
    void setQuickLayout9Shortcut(const QString& shortcut);

    // Helper to get quick layout shortcut by index (0-8)
    QString quickLayoutShortcut(int index) const;
    void setQuickLayoutShortcut(int index, const QString& shortcut);

    // Keyboard Navigation Shortcuts
    QString moveWindowLeftShortcut() const
    {
        return m_moveWindowLeftShortcut;
    }
    void setMoveWindowLeftShortcut(const QString& shortcut);
    QString moveWindowRightShortcut() const
    {
        return m_moveWindowRightShortcut;
    }
    void setMoveWindowRightShortcut(const QString& shortcut);
    QString moveWindowUpShortcut() const
    {
        return m_moveWindowUpShortcut;
    }
    void setMoveWindowUpShortcut(const QString& shortcut);
    QString moveWindowDownShortcut() const
    {
        return m_moveWindowDownShortcut;
    }
    void setMoveWindowDownShortcut(const QString& shortcut);
    QString focusZoneLeftShortcut() const
    {
        return m_focusZoneLeftShortcut;
    }
    void setFocusZoneLeftShortcut(const QString& shortcut);
    QString focusZoneRightShortcut() const
    {
        return m_focusZoneRightShortcut;
    }
    void setFocusZoneRightShortcut(const QString& shortcut);
    QString focusZoneUpShortcut() const
    {
        return m_focusZoneUpShortcut;
    }
    void setFocusZoneUpShortcut(const QString& shortcut);
    QString focusZoneDownShortcut() const
    {
        return m_focusZoneDownShortcut;
    }
    void setFocusZoneDownShortcut(const QString& shortcut);
    QString pushToEmptyZoneShortcut() const
    {
        return m_pushToEmptyZoneShortcut;
    }
    void setPushToEmptyZoneShortcut(const QString& shortcut);
    QString restoreWindowSizeShortcut() const
    {
        return m_restoreWindowSizeShortcut;
    }
    void setRestoreWindowSizeShortcut(const QString& shortcut);
    QString toggleWindowFloatShortcut() const
    {
        return m_toggleWindowFloatShortcut;
    }
    void setToggleWindowFloatShortcut(const QString& shortcut);

    // Swap Window Shortcuts (Meta+Ctrl+Alt+Arrow)
    QString swapWindowLeftShortcut() const
    {
        return m_swapWindowLeftShortcut;
    }
    void setSwapWindowLeftShortcut(const QString& shortcut);
    QString swapWindowRightShortcut() const
    {
        return m_swapWindowRightShortcut;
    }
    void setSwapWindowRightShortcut(const QString& shortcut);
    QString swapWindowUpShortcut() const
    {
        return m_swapWindowUpShortcut;
    }
    void setSwapWindowUpShortcut(const QString& shortcut);
    QString swapWindowDownShortcut() const
    {
        return m_swapWindowDownShortcut;
    }
    void setSwapWindowDownShortcut(const QString& shortcut);

    // Snap to Zone by Number Shortcuts (Meta+Ctrl+1-9)
    QString snapToZone1Shortcut() const
    {
        return m_snapToZoneShortcuts[0];
    }
    void setSnapToZone1Shortcut(const QString& shortcut);
    QString snapToZone2Shortcut() const
    {
        return m_snapToZoneShortcuts[1];
    }
    void setSnapToZone2Shortcut(const QString& shortcut);
    QString snapToZone3Shortcut() const
    {
        return m_snapToZoneShortcuts[2];
    }
    void setSnapToZone3Shortcut(const QString& shortcut);
    QString snapToZone4Shortcut() const
    {
        return m_snapToZoneShortcuts[3];
    }
    void setSnapToZone4Shortcut(const QString& shortcut);
    QString snapToZone5Shortcut() const
    {
        return m_snapToZoneShortcuts[4];
    }
    void setSnapToZone5Shortcut(const QString& shortcut);
    QString snapToZone6Shortcut() const
    {
        return m_snapToZoneShortcuts[5];
    }
    void setSnapToZone6Shortcut(const QString& shortcut);
    QString snapToZone7Shortcut() const
    {
        return m_snapToZoneShortcuts[6];
    }
    void setSnapToZone7Shortcut(const QString& shortcut);
    QString snapToZone8Shortcut() const
    {
        return m_snapToZoneShortcuts[7];
    }
    void setSnapToZone8Shortcut(const QString& shortcut);
    QString snapToZone9Shortcut() const
    {
        return m_snapToZoneShortcuts[8];
    }
    void setSnapToZone9Shortcut(const QString& shortcut);

    // Helper to get snap-to-zone shortcut by index (0-8)
    QString snapToZoneShortcut(int index) const;
    void setSnapToZoneShortcut(int index, const QString& shortcut);

    // Rotate Windows Shortcuts (Meta+Ctrl+[ / Meta+Ctrl+])
    QString rotateWindowsClockwiseShortcut() const
    {
        return m_rotateWindowsClockwiseShortcut;
    }
    void setRotateWindowsClockwiseShortcut(const QString& shortcut);
    QString rotateWindowsCounterclockwiseShortcut() const
    {
        return m_rotateWindowsCounterclockwiseShortcut;
    }
    void setRotateWindowsCounterclockwiseShortcut(const QString& shortcut);

    // Cycle Windows in Zone Shortcuts (Meta+Alt+. / Meta+Alt+,)
    QString cycleWindowForwardShortcut() const
    {
        return m_cycleWindowForwardShortcut;
    }
    void setCycleWindowForwardShortcut(const QString& shortcut);
    QString cycleWindowBackwardShortcut() const
    {
        return m_cycleWindowBackwardShortcut;
    }
    void setCycleWindowBackwardShortcut(const QString& shortcut);
    QString resnapToNewLayoutShortcut() const
    {
        return m_resnapToNewLayoutShortcut;
    }
    void setResnapToNewLayoutShortcut(const QString& shortcut);

    QString snapAllWindowsShortcut() const
    {
        return m_snapAllWindowsShortcut;
    }
    void setSnapAllWindowsShortcut(const QString& shortcut);

    QString layoutPickerShortcut() const
    {
        return m_layoutPickerShortcut;
    }
    void setLayoutPickerShortcut(const QString& shortcut);

    QString toggleLayoutLockShortcut() const
    {
        return m_toggleLayoutLockShortcut;
    }
    void setToggleLayoutLockShortcut(const QString& shortcut);

    // ═══════════════════════════════════════════════════════════════════════════
    // Editor Settings (shared [Editor] group in plasmazonesrc)
    // ═══════════════════════════════════════════════════════════════════════════

    QString editorDuplicateShortcut() const
    {
        return m_editorDuplicateShortcut;
    }
    void setEditorDuplicateShortcut(const QString& shortcut);
    QString editorSplitHorizontalShortcut() const
    {
        return m_editorSplitHorizontalShortcut;
    }
    void setEditorSplitHorizontalShortcut(const QString& shortcut);
    QString editorSplitVerticalShortcut() const
    {
        return m_editorSplitVerticalShortcut;
    }
    void setEditorSplitVerticalShortcut(const QString& shortcut);
    QString editorFillShortcut() const
    {
        return m_editorFillShortcut;
    }
    void setEditorFillShortcut(const QString& shortcut);
    bool editorGridSnappingEnabled() const
    {
        return m_editorGridSnappingEnabled;
    }
    void setEditorGridSnappingEnabled(bool enabled);
    bool editorEdgeSnappingEnabled() const
    {
        return m_editorEdgeSnappingEnabled;
    }
    void setEditorEdgeSnappingEnabled(bool enabled);
    qreal editorSnapIntervalX() const
    {
        return m_editorSnapIntervalX;
    }
    void setEditorSnapIntervalX(qreal interval);
    qreal editorSnapIntervalY() const
    {
        return m_editorSnapIntervalY;
    }
    void setEditorSnapIntervalY(qreal interval);
    int editorSnapOverrideModifier() const
    {
        return m_editorSnapOverrideModifier;
    }
    void setEditorSnapOverrideModifier(int mod);
    bool fillOnDropEnabled() const
    {
        return m_fillOnDropEnabled;
    }
    void setFillOnDropEnabled(bool enabled);
    int fillOnDropModifier() const
    {
        return m_fillOnDropModifier;
    }
    void setFillOnDropModifier(int mod);

    // TilingQuickLayoutSlots — read/write via the shared config backend
    QString readTilingQuickLayoutSlot(int slotNumber) const;
    void writeTilingQuickLayoutSlot(int slotNumber, const QString& layoutId);
    void syncConfig();

    // Persistence
    void load() override;
    void save() override;
    void reset() override;

    // Additional methods
    Q_INVOKABLE QString loadColorsFromFile(const QString& filePath);
    Q_INVOKABLE void applySystemColorScheme();
    void applyAutotileBorderSystemColor();

Q_SIGNALS:
    // Editor settings signals (not part of ISettings interface)
    void editorDuplicateShortcutChanged();
    void editorSplitHorizontalShortcutChanged();
    void editorSplitVerticalShortcutChanged();
    void editorFillShortcutChanged();
    void editorGridSnappingEnabledChanged();
    void editorEdgeSnappingEnabledChanged();
    void editorSnapIntervalXChanged();
    void editorSnapIntervalYChanged();
    void editorSnapOverrideModifierChanged();
    void fillOnDropEnabledChanged();
    void fillOnDropModifierChanged();
    void filterLayoutsByAspectRatioChanged();

private:
    // ═══════════════════════════════════════════════════════════════════════════════
    // Helper Methods for load()
    // ═══════════════════════════════════════════════════════════════════════════════

    /**
     * @brief Read and validate an integer from config with bounds checking
     * @param group QSettingsConfigGroup to read from
     * @param key Config key name
     * @param defaultValue Default if not found or invalid
     * @param min Minimum valid value
     * @param max Maximum valid value
     * @param settingName Human-readable name for warning messages
     * @return Validated integer value
     */
    int readValidatedInt(QSettingsConfigGroup& group, const char* key, int defaultValue, int min, int max,
                         const char* settingName);

    /**
     * @brief Read and validate a color from config
     * @param group QSettingsConfigGroup to read from
     * @param key Config key name
     * @param defaultValue Default if not found or invalid
     * @param settingName Human-readable name for warning messages
     * @return Validated QColor value
     */
    QColor readValidatedColor(QSettingsConfigGroup& group, const char* key, const QColor& defaultValue,
                              const char* settingName);

    /**
     * @brief Load indexed shortcuts (1-9) from config group
     * @param group QSettingsConfigGroup to read from
     * @param keyPattern Pattern with %1 placeholder (e.g., "QuickLayout%1Shortcut")
     * @param shortcuts Array of 9 QString to populate
     * @param defaults Array of 9 default values
     */
    void loadIndexedShortcuts(QSettingsConfigGroup& group, const QString& keyPattern, QString (&shortcuts)[9],
                              const QString (&defaults)[9]);

    /**
     * @brief Parse a trigger list from JSON string
     * @param json JSON array string
     * @return Parsed list (capped at MaxTriggersPerAction), or std::nullopt if empty/invalid
     */
    static std::optional<QVariantList> parseTriggerListJson(const QString& json);

    /**
     * @brief Load a trigger list from config JSON, with error handling and cap-at-4
     * @param group QSettingsConfigGroup to read from
     * @param key Config key for the JSON trigger list
     * @param legacyModifier Fallback modifier enum value if no JSON exists
     * @param legacyMouseButton Fallback mouse button if no JSON exists
     * @return Parsed trigger list (capped at 4 entries)
     */
    static QVariantList loadTriggerList(QSettingsConfigGroup& group, const QString& key, int legacyModifier,
                                        int legacyMouseButton);

    /**
     * @brief Save a trigger list to config as compact JSON
     * @param group QSettingsConfigGroup to write to
     * @param key Config key for the JSON trigger list
     * @param triggers The trigger list to serialize
     */
    static void saveTriggerList(QSettingsConfigGroup& group, const QString& key, const QVariantList& triggers);

    /** @brief Shared dispatcher for indexed shortcut arrays (quick-layout, snap-to-zone) */
    using ShortcutSignalFn = void (Settings::*)();
    void setIndexedShortcut(QString (&arr)[9], int index, const QString& shortcut,
                            const ShortcutSignalFn (&signals)[9]);

    // ─── load() helpers (decomposed for SRP) ─────────────────────────────
    void loadActivationConfig(QSettingsConfigGroup& activation);
    void loadDisplayConfig(QSettingsConfigGroup& display);
    void loadAppearanceConfig(QSettingsConfigGroup& appearance);
    void loadZoneGeometryConfig(QSettingsConfigGroup& zones);
    void loadBehaviorConfig(QSettingsConfigBackend* backend);
    void loadZoneSelectorConfig(QSettingsConfigGroup& zoneSelector);
    void loadPerScreenOverrides(QSettingsConfigBackend* backend);
    void loadShortcutConfig(QSettingsConfigGroup& globalShortcuts);
    void loadAutotilingConfig(QSettingsConfigBackend* backend);
    void loadEditorConfig(QSettingsConfigGroup& editor);

    // ─── save() helpers (decomposed for SRP) ────────────────────────────
    void saveActivationConfig(QSettingsConfigGroup& activation);
    void saveDisplayConfig(QSettingsConfigGroup& display);
    void saveAppearanceConfig(QSettingsConfigGroup& appearance);
    void saveZoneGeometryConfig(QSettingsConfigGroup& zones);
    void saveBehaviorConfig(QSettingsConfigBackend* backend);
    void saveZoneSelectorConfig(QSettingsConfigGroup& zoneSelector);
    void saveAllPerScreenOverrides(QSettingsConfigBackend* backend);
    void saveShortcutConfig(QSettingsConfigGroup& globalShortcuts);
    void saveAutotilingConfig(QSettingsConfigBackend* backend);
    void saveEditorConfig(QSettingsConfigGroup& editor);

    // Config backend (replaces KSharedConfig)
    std::unique_ptr<QSettingsConfigBackend> m_configBackend;
    static QString normalizeUuidString(const QString& uuidStr);

    // Activation
    bool m_shiftDragToActivate = ConfigDefaults::shiftDrag(); // Deprecated - kept for migration
    QVariantList m_dragActivationTriggers; // [{modifier: int, mouseButton: int}, ...]
    bool m_zoneSpanEnabled = ConfigDefaults::zoneSpanEnabled();
    DragModifier m_zoneSpanModifier = DragModifier::Ctrl;
    QVariantList m_zoneSpanTriggers; // [{modifier: int, mouseButton: int}, ...]
    bool m_toggleActivation = ConfigDefaults::toggleActivation();
    bool m_snappingEnabled = ConfigDefaults::snappingEnabled();

    // Display
    bool m_showZonesOnAllMonitors = ConfigDefaults::showOnAllMonitors();
    QStringList m_disabledMonitors;
    QList<int> m_disabledDesktops;
    QStringList m_disabledActivities;
    bool m_showZoneNumbers = ConfigDefaults::showNumbers();
    bool m_flashZonesOnSwitch = ConfigDefaults::flashOnSwitch();
    bool m_showOsdOnLayoutSwitch = ConfigDefaults::showOsdOnLayoutSwitch();
    bool m_showNavigationOsd = ConfigDefaults::showNavigationOsd();
    OsdStyle m_osdStyle = OsdStyle::Preview; // Default to visual preview
    OverlayDisplayMode m_overlayDisplayMode = OverlayDisplayMode::ZoneRectangles;

    // Appearance
    bool m_useSystemColors = ConfigDefaults::useSystemColors();
    QColor m_highlightColor = ConfigDefaults::highlightColor();
    QColor m_inactiveColor = ConfigDefaults::inactiveColor();
    QColor m_borderColor = ConfigDefaults::borderColor();
    QColor m_labelFontColor = ConfigDefaults::labelFontColor();
    qreal m_activeOpacity = ConfigDefaults::activeOpacity();
    qreal m_inactiveOpacity = ConfigDefaults::inactiveOpacity();
    int m_borderWidth = ConfigDefaults::borderWidth();
    int m_borderRadius = ConfigDefaults::borderRadius();
    bool m_enableBlur = ConfigDefaults::enableBlur();
    QString m_labelFontFamily;
    qreal m_labelFontSizeScale = ConfigDefaults::labelFontSizeScale();
    int m_labelFontWeight = QFont::Bold;
    bool m_labelFontItalic = ConfigDefaults::labelFontItalic();
    bool m_labelFontUnderline = ConfigDefaults::labelFontUnderline();
    bool m_labelFontStrikeout = ConfigDefaults::labelFontStrikeout();

    // Zone settings
    int m_zonePadding = ConfigDefaults::zonePadding();
    int m_outerGap = ConfigDefaults::outerGap();
    bool m_usePerSideOuterGap = ConfigDefaults::usePerSideOuterGap();
    int m_outerGapTop = ConfigDefaults::outerGapTop();
    int m_outerGapBottom = ConfigDefaults::outerGapBottom();
    int m_outerGapLeft = ConfigDefaults::outerGapLeft();
    int m_outerGapRight = ConfigDefaults::outerGapRight();
    int m_adjacentThreshold = ConfigDefaults::adjacentThreshold();

    // Performance and behavior
    int m_pollIntervalMs = ConfigDefaults::pollIntervalMs();
    int m_minimumZoneSizePx = ConfigDefaults::minimumZoneSizePx();
    int m_minimumZoneDisplaySizePx = ConfigDefaults::minimumZoneDisplaySizePx();

    // Window behavior
    bool m_keepWindowsInZonesOnResolutionChange = ConfigDefaults::keepWindowsInZonesOnResolutionChange();
    bool m_moveNewWindowsToLastZone = ConfigDefaults::moveNewWindowsToLastZone();
    bool m_restoreOriginalSizeOnUnsnap = ConfigDefaults::restoreOriginalSizeOnUnsnap();
    StickyWindowHandling m_stickyWindowHandling = StickyWindowHandling::TreatAsNormal;
    bool m_restoreWindowsToZonesOnLogin = ConfigDefaults::restoreWindowsToZonesOnLogin();
    bool m_snapAssistFeatureEnabled = ConfigDefaults::snapAssistFeatureEnabled();
    bool m_snapAssistEnabled = ConfigDefaults::snapAssistEnabled();
    QVariantList m_snapAssistTriggers; // [{modifier: int, mouseButton: int}, ...]

    // Default layout (used when no explicit assignment exists)
    QString m_defaultLayoutId;

    // Layout filtering
    bool m_filterLayoutsByAspectRatio = ConfigDefaults::filterLayoutsByAspectRatio();

    // Exclusions
    QStringList m_excludedApplications;
    QStringList m_excludedWindowClasses;
    bool m_excludeTransientWindows = ConfigDefaults::excludeTransientWindows();
    int m_minimumWindowWidth = ConfigDefaults::minimumWindowWidth();
    int m_minimumWindowHeight = ConfigDefaults::minimumWindowHeight();

    // Zone Selector
    bool m_zoneSelectorEnabled = ConfigDefaults::zoneSelectorEnabled();
    int m_zoneSelectorTriggerDistance = ConfigDefaults::triggerDistance();
    ZoneSelectorPosition m_zoneSelectorPosition = ZoneSelectorPosition::Top;
    ZoneSelectorLayoutMode m_zoneSelectorLayoutMode = ZoneSelectorLayoutMode::Grid;
    ZoneSelectorSizeMode m_zoneSelectorSizeMode = ZoneSelectorSizeMode::Auto;
    int m_zoneSelectorMaxRows = ConfigDefaults::maxRows();
    int m_zoneSelectorPreviewWidth = ConfigDefaults::previewWidth();
    int m_zoneSelectorPreviewHeight = ConfigDefaults::previewHeight();
    bool m_zoneSelectorPreviewLockAspect = ConfigDefaults::previewLockAspect();
    int m_zoneSelectorGridColumns = ConfigDefaults::gridColumns();

    // Per-screen zone selector overrides (screenIdOrName -> settings map)
    QHash<QString, QVariantMap> m_perScreenZoneSelectorSettings;

    // Per-screen autotile overrides (screenIdOrName -> settings map)
    QHash<QString, QVariantMap> m_perScreenAutotileSettings;

    // Per-screen snapping overrides (screenIdOrName -> settings map)
    QHash<QString, QVariantMap> m_perScreenSnappingSettings;

    // Autotiling Settings
    bool m_autotileEnabled = ConfigDefaults::autotileEnabled();
    QString m_autotileAlgorithm = QString(DBus::AutotileAlgorithm::BSP);
    qreal m_autotileSplitRatio = ConfigDefaults::autotileSplitRatio();
    int m_autotileMasterCount = ConfigDefaults::autotileMasterCount();
    QVariantMap m_autotilePerAlgorithmSettings;
    int m_autotileInnerGap = ConfigDefaults::autotileInnerGap();
    int m_autotileOuterGap = ConfigDefaults::autotileOuterGap();
    bool m_autotileUsePerSideOuterGap = ConfigDefaults::autotileUsePerSideOuterGap();
    int m_autotileOuterGapTop = ConfigDefaults::autotileOuterGapTop();
    int m_autotileOuterGapBottom = ConfigDefaults::autotileOuterGapBottom();
    int m_autotileOuterGapLeft = ConfigDefaults::autotileOuterGapLeft();
    int m_autotileOuterGapRight = ConfigDefaults::autotileOuterGapRight();
    bool m_autotileFocusNewWindows = ConfigDefaults::autotileFocusNewWindows();
    bool m_autotileSmartGaps = ConfigDefaults::autotileSmartGaps();
    int m_autotileMaxWindows = ConfigDefaults::autotileMaxWindows();
    AutotileInsertPosition m_autotileInsertPosition = AutotileInsertPosition::End;

    // Animation Settings (applies to both snapping and autotiling geometry changes)
    bool m_animationsEnabled = ConfigDefaults::animationsEnabled();
    int m_animationDuration = ConfigDefaults::animationDuration();
    QString m_animationEasingCurve = ConfigDefaults::animationEasingCurve();
    int m_animationMinDistance = ConfigDefaults::animationMinDistance();
    int m_animationSequenceMode = ConfigDefaults::animationSequenceMode();
    int m_animationStaggerInterval = ConfigDefaults::animationStaggerInterval();

    // Additional Autotiling Settings
    bool m_autotileFocusFollowsMouse = ConfigDefaults::autotileFocusFollowsMouse();
    bool m_autotileRespectMinimumSize = ConfigDefaults::autotileRespectMinimumSize();
    bool m_autotileHideTitleBars = ConfigDefaults::autotileHideTitleBars();
    bool m_autotileShowBorder = ConfigDefaults::autotileShowBorder();
    int m_autotileBorderWidth = ConfigDefaults::autotileBorderWidth();
    int m_autotileBorderRadius = ConfigDefaults::autotileBorderRadius();
    QColor m_autotileBorderColor = QColor(0, 120, 212, 128); // #800078D4 — same as highlightColor
    QColor m_autotileInactiveBorderColor = QColor(128, 128, 128, 64); // #40808080 — same as inactiveColor
    bool m_autotileUseSystemBorderColors = ConfigDefaults::autotileUseSystemBorderColors();
    QStringList m_lockedScreens;
    // Autotile Shortcuts (defaults from ConfigDefaults, canonical source)
    QString m_autotileToggleShortcut = ConfigDefaults::autotileToggleShortcut();
    QString m_autotileFocusMasterShortcut = ConfigDefaults::autotileFocusMasterShortcut();
    QString m_autotileSwapMasterShortcut = ConfigDefaults::autotileSwapMasterShortcut();
    QString m_autotileIncMasterRatioShortcut = ConfigDefaults::autotileIncMasterRatioShortcut();
    QString m_autotileDecMasterRatioShortcut = ConfigDefaults::autotileDecMasterRatioShortcut();
    QString m_autotileIncMasterCountShortcut = ConfigDefaults::autotileIncMasterCountShortcut();
    QString m_autotileDecMasterCountShortcut = ConfigDefaults::autotileDecMasterCountShortcut();
    QString m_autotileRetileShortcut = ConfigDefaults::autotileRetileShortcut();

    // Shader Effects
    bool m_enableShaderEffects = ConfigDefaults::enableShaderEffects();
    int m_shaderFrameRate = ConfigDefaults::shaderFrameRate();
    bool m_enableAudioVisualizer = ConfigDefaults::enableAudioVisualizer();
    int m_audioSpectrumBarCount = ConfigDefaults::audioSpectrumBarCount();

    // Global Shortcuts (defaults from ConfigDefaults, canonical source)
    QString m_openEditorShortcut = ConfigDefaults::openEditorShortcut();
    QString m_openSettingsShortcut = ConfigDefaults::openSettingsShortcut();
    QString m_previousLayoutShortcut = ConfigDefaults::previousLayoutShortcut();
    QString m_nextLayoutShortcut = ConfigDefaults::nextLayoutShortcut();
    QString m_quickLayoutShortcuts[9] = {ConfigDefaults::quickLayout1Shortcut(), ConfigDefaults::quickLayout2Shortcut(),
                                         ConfigDefaults::quickLayout3Shortcut(), ConfigDefaults::quickLayout4Shortcut(),
                                         ConfigDefaults::quickLayout5Shortcut(), ConfigDefaults::quickLayout6Shortcut(),
                                         ConfigDefaults::quickLayout7Shortcut(), ConfigDefaults::quickLayout8Shortcut(),
                                         ConfigDefaults::quickLayout9Shortcut()};

    // Keyboard Navigation Shortcuts
    // Meta+Shift+Left/Right conflicts with KDE's "Window to Next/Previous Screen";
    // we use Meta+Alt+Shift+Arrow instead.
    QString m_moveWindowLeftShortcut = ConfigDefaults::moveWindowLeftShortcut();
    QString m_moveWindowRightShortcut = ConfigDefaults::moveWindowRightShortcut();
    QString m_moveWindowUpShortcut = ConfigDefaults::moveWindowUpShortcut();
    QString m_moveWindowDownShortcut = ConfigDefaults::moveWindowDownShortcut();
    // Meta+Arrow conflicts with KDE's Quick Tile; we use Alt+Shift+Arrow instead.
    QString m_focusZoneLeftShortcut = ConfigDefaults::focusZoneLeftShortcut();
    QString m_focusZoneRightShortcut = ConfigDefaults::focusZoneRightShortcut();
    QString m_focusZoneUpShortcut = ConfigDefaults::focusZoneUpShortcut();
    QString m_focusZoneDownShortcut = ConfigDefaults::focusZoneDownShortcut();
    QString m_pushToEmptyZoneShortcut = ConfigDefaults::pushToEmptyZoneShortcut();
    QString m_restoreWindowSizeShortcut = ConfigDefaults::restoreWindowSizeShortcut();
    QString m_toggleWindowFloatShortcut = ConfigDefaults::toggleWindowFloatShortcut();

    // Swap Window Shortcuts (Meta+Ctrl+Alt+Arrow)
    // Swaps focused window with window in adjacent zone
    // Meta+Ctrl+Arrow conflicts with KDE's virtual desktop switching;
    // we add Alt to make Meta+Ctrl+Alt+Arrow for swap operations.
    QString m_swapWindowLeftShortcut = ConfigDefaults::swapWindowLeftShortcut();
    QString m_swapWindowRightShortcut = ConfigDefaults::swapWindowRightShortcut();
    QString m_swapWindowUpShortcut = ConfigDefaults::swapWindowUpShortcut();
    QString m_swapWindowDownShortcut = ConfigDefaults::swapWindowDownShortcut();

    // Snap to Zone by Number Shortcuts (Meta+Ctrl+1-9)
    // Meta+1-9 conflicts with KDE's virtual desktop switching; we use Meta+Ctrl+1-9 instead.
    QString m_snapToZoneShortcuts[9] = {ConfigDefaults::snapToZone1Shortcut(), ConfigDefaults::snapToZone2Shortcut(),
                                        ConfigDefaults::snapToZone3Shortcut(), ConfigDefaults::snapToZone4Shortcut(),
                                        ConfigDefaults::snapToZone5Shortcut(), ConfigDefaults::snapToZone6Shortcut(),
                                        ConfigDefaults::snapToZone7Shortcut(), ConfigDefaults::snapToZone8Shortcut(),
                                        ConfigDefaults::snapToZone9Shortcut()};

    // Rotate Windows Shortcuts (Meta+Ctrl+[ / Meta+Ctrl+])
    // Rotates all windows in the current layout clockwise or counterclockwise
    QString m_rotateWindowsClockwiseShortcut = ConfigDefaults::rotateWindowsClockwiseShortcut();
    QString m_rotateWindowsCounterclockwiseShortcut = ConfigDefaults::rotateWindowsCounterclockwiseShortcut();

    // Cycle Windows in Zone Shortcuts (Meta+Alt+. / Meta+Alt+,)
    // Cycles focus between windows stacked in the same zone (monocle-style navigation)
    QString m_cycleWindowForwardShortcut = ConfigDefaults::cycleWindowForwardShortcut();
    QString m_cycleWindowBackwardShortcut = ConfigDefaults::cycleWindowBackwardShortcut();

    // Resnap to New Layout (Meta+Ctrl+Z, easy pinky key)
    QString m_resnapToNewLayoutShortcut = ConfigDefaults::resnapToNewLayoutShortcut();

    // Snap All Windows (Meta+Ctrl+S — same namespace as rotate/resnap batch ops)
    QString m_snapAllWindowsShortcut = ConfigDefaults::snapAllWindowsShortcut();

    // Layout Picker (Meta+Alt+Space — browse and switch layouts interactively)
    QString m_layoutPickerShortcut = ConfigDefaults::layoutPickerShortcut();

    // Toggle Layout Lock (Meta+Ctrl+L)
    QString m_toggleLayoutLockShortcut = ConfigDefaults::toggleLayoutLockShortcut();

    // Editor Settings ([Editor] group in plasmazonesrc)
    QString m_editorDuplicateShortcut = ConfigDefaults::editorDuplicateShortcut();
    QString m_editorSplitHorizontalShortcut = ConfigDefaults::editorSplitHorizontalShortcut();
    QString m_editorSplitVerticalShortcut = ConfigDefaults::editorSplitVerticalShortcut();
    QString m_editorFillShortcut = ConfigDefaults::editorFillShortcut();
    bool m_editorGridSnappingEnabled = ConfigDefaults::editorGridSnappingEnabled();
    bool m_editorEdgeSnappingEnabled = ConfigDefaults::editorEdgeSnappingEnabled();
    qreal m_editorSnapIntervalX = ConfigDefaults::editorSnapInterval();
    qreal m_editorSnapIntervalY = ConfigDefaults::editorSnapInterval();
    int m_editorSnapOverrideModifier = static_cast<int>(Qt::ShiftModifier);
    bool m_fillOnDropEnabled = ConfigDefaults::fillOnDropEnabled();
    int m_fillOnDropModifier = static_cast<int>(Qt::ControlModifier);
};

} // namespace PlasmaZones
