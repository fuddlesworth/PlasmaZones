// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../core/interfaces.h"
#include "../core/constants.h"
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorTileEngine/IAutotileSettings.h>
#include <PhosphorSnapEngine/ISnapSettings.h>
#include <PhosphorScreens/VirtualScreen.h>
#include "configdefaults.h"
#include "configbackends.h"

#include <PhosphorConfig/Store.h>

#include <memory>
#include <QFont>
#include <QHash>
#include <QVariantMap>

namespace PlasmaZones {

/**
 * @brief Global settings for PlasmaZones
 *
 * Implements the ISettings interface with PhosphorConfig::IBackend persistence.
 * Supports ricer-friendly customization with color themes, opacity,
 * and integration with system color schemes.
 *
 * Note: This class does NOT use the singleton pattern. Create instances
 * where needed and pass via dependency injection.
 */
class PLASMAZONES_EXPORT Settings : public ISettings,
                                    public PhosphorEngineApi::IAutotileSettings,
                                    public PhosphorEngineApi::ISnapSettings
{
    Q_OBJECT
    Q_INTERFACES(PhosphorEngineApi::IAutotileSettings PhosphorEngineApi::ISnapSettings)

public:
    /** Maximum number of activation triggers per action (drag, multi-zone, zone span).
     *  Source of truth lives in ConfigDefaults::maxTriggersPerAction() so both this
     *  public accessor and the schema-side validator (canonicalTriggerList) read
     *  from the same constant without either side depending on the other. */
    static constexpr int MaxTriggersPerAction = ConfigDefaults::maxTriggersPerAction();

    /**
     * @brief Construct with an external (non-owned) config backend
     *
     * Used by the daemon to share a single PhosphorConfig::IBackend across
     * Settings, PhosphorZones::LayoutRegistry, and other components. Eliminates Qt's
     * QConfFile cache conflicts from multiple QSettings instances per file.
     *
     * CONTRACT: @p backend MUST be pointing at a config file that has
     * already been migrated to the current schema. Callers are responsible
     * for invoking ConfigMigration::ensureJsonConfig() before constructing
     * the backend. Production entry points (daemon/main, settings/main,
     * editor controller) already do this at startup.
     *
     * @param backend Non-owned backend pointer (must outlive this Settings)
     * @param curveRegistry Non-owned curve registry pointer used for Profile
     *                      JSON parsing. If nullptr, falls back to a local
     *                      static registry (standalone tests). Must outlive
     *                      this Settings. Supplying at construction time
     *                      guarantees the initial `load()` resolves curves
     *                      through the caller's registry, preserving
     *                      `shared_ptr<const Curve>` identity across the
     *                      Settings ↔ daemon boundary.
     * @param parent Parent QObject
     */
    Settings(PhosphorConfig::IBackend* backend, PhosphorAnimation::CurveRegistry* curveRegistry, QObject* parent);

    // Activation settings
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
        QStringList disabledDesktops READ disabledDesktops WRITE setDisabledDesktops NOTIFY disabledDesktopsChanged)
    Q_PROPERTY(QStringList disabledActivities READ disabledActivities WRITE setDisabledActivities NOTIFY
                   disabledActivitiesChanged)
    Q_PROPERTY(bool showZoneNumbers READ showZoneNumbers WRITE setShowZoneNumbers NOTIFY showZoneNumbersChanged)
    Q_PROPERTY(
        bool flashZonesOnSwitch READ flashZonesOnSwitch WRITE setFlashZonesOnSwitch NOTIFY flashZonesOnSwitchChanged)
    Q_PROPERTY(bool showOsdOnLayoutSwitch READ showOsdOnLayoutSwitch WRITE setShowOsdOnLayoutSwitch NOTIFY
                   showOsdOnLayoutSwitchChanged)
    Q_PROPERTY(bool showOsdOnDesktopSwitch READ showOsdOnDesktopSwitch WRITE setShowOsdOnDesktopSwitch NOTIFY
                   showOsdOnDesktopSwitchChanged)
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

    // PhosphorZones::Zone settings
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
    Q_PROPERTY(bool autoAssignAllLayouts READ autoAssignAllLayouts WRITE setAutoAssignAllLayouts NOTIFY
                   autoAssignAllLayoutsChanged)
    Q_PROPERTY(bool snapAssistFeatureEnabled READ snapAssistFeatureEnabled WRITE setSnapAssistFeatureEnabled NOTIFY
                   snapAssistFeatureEnabledChanged)
    Q_PROPERTY(bool snapAssistEnabled READ snapAssistEnabled WRITE setSnapAssistEnabled NOTIFY snapAssistEnabledChanged)
    Q_PROPERTY(QVariantList snapAssistTriggers READ snapAssistTriggers WRITE setSnapAssistTriggers NOTIFY
                   snapAssistTriggersChanged)

    // Default layout (used when no explicit assignment exists)
    Q_PROPERTY(QString defaultLayoutId READ defaultLayoutId WRITE setDefaultLayoutId NOTIFY defaultLayoutIdChanged)

    // PhosphorZones::Layout filtering
    Q_PROPERTY(bool filterLayoutsByAspectRatio READ filterLayoutsByAspectRatio WRITE setFilterLayoutsByAspectRatio
                   NOTIFY filterLayoutsByAspectRatioChanged)

    // Ordering (manual sort order for cycling / zone selector / overlay)
    Q_PROPERTY(QStringList snappingLayoutOrder READ snappingLayoutOrder WRITE setSnappingLayoutOrder NOTIFY
                   snappingLayoutOrderChanged)
    Q_PROPERTY(QStringList tilingAlgorithmOrder READ tilingAlgorithmOrder WRITE setTilingAlgorithmOrder NOTIFY
                   tilingAlgorithmOrderChanged)

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

    // PhosphorZones::Zone Selector
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
    Q_PROPERTY(QString defaultAutotileAlgorithm READ defaultAutotileAlgorithm WRITE setDefaultAutotileAlgorithm NOTIFY
                   defaultAutotileAlgorithmChanged)
    Q_PROPERTY(
        qreal autotileSplitRatio READ autotileSplitRatio WRITE setAutotileSplitRatio NOTIFY autotileSplitRatioChanged)
    Q_PROPERTY(qreal autotileSplitRatioStep READ autotileSplitRatioStep WRITE setAutotileSplitRatioStep NOTIFY
                   autotileSplitRatioStepChanged)
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
    Q_PROPERTY(QVariantList autotileDragInsertTriggers READ autotileDragInsertTriggers WRITE
                   setAutotileDragInsertTriggers NOTIFY autotileDragInsertTriggersChanged)
    Q_PROPERTY(bool autotileDragInsertToggle READ autotileDragInsertToggle WRITE setAutotileDragInsertToggle NOTIFY
                   autotileDragInsertToggleChanged)

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
    Q_PROPERTY(int autotileStickyWindowHandling READ autotileStickyWindowHandlingInt WRITE
                   setAutotileStickyWindowHandlingInt NOTIFY autotileStickyWindowHandlingChanged)
    Q_PROPERTY(int autotileDragBehavior READ autotileDragBehaviorInt WRITE setAutotileDragBehaviorInt NOTIFY
                   autotileDragBehaviorChanged)
    Q_PROPERTY(int autotileOverflowBehavior READ autotileOverflowBehaviorInt WRITE setAutotileOverflowBehaviorInt NOTIFY
                   autotileOverflowBehaviorChanged)
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

    // Rendering
    Q_PROPERTY(QString renderingBackend READ renderingBackend WRITE setRenderingBackend NOTIFY renderingBackendChanged)

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

    // Snap to PhosphorZones::Zone by Number Shortcuts (Meta+Ctrl+1-9)
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

    // Cycle Windows in PhosphorZones::Zone Shortcuts (Meta+Alt+. / Meta+Alt+,)
    // Cycles focus between windows stacked in the same zone
    Q_PROPERTY(QString cycleWindowForwardShortcut READ cycleWindowForwardShortcut WRITE setCycleWindowForwardShortcut
                   NOTIFY cycleWindowForwardShortcutChanged)
    Q_PROPERTY(QString cycleWindowBackwardShortcut READ cycleWindowBackwardShortcut WRITE setCycleWindowBackwardShortcut
                   NOTIFY cycleWindowBackwardShortcutChanged)

    // Resnap to New PhosphorZones::Layout (Meta+Ctrl+Z, easy pinky key)
    Q_PROPERTY(QString resnapToNewLayoutShortcut READ resnapToNewLayoutShortcut WRITE setResnapToNewLayoutShortcut
                   NOTIFY resnapToNewLayoutShortcutChanged)

    // Snap All Windows (Meta+Ctrl+S — same namespace as rotate/resnap batch ops)
    Q_PROPERTY(QString snapAllWindowsShortcut READ snapAllWindowsShortcut WRITE setSnapAllWindowsShortcut NOTIFY
                   snapAllWindowsShortcutChanged)

    // PhosphorZones::Layout Picker (Meta+Alt+Space — browse and switch layouts interactively)
    Q_PROPERTY(QString layoutPickerShortcut READ layoutPickerShortcut WRITE setLayoutPickerShortcut NOTIFY
                   layoutPickerShortcutChanged)

    // Toggle PhosphorZones::Layout Lock (Meta+Ctrl+L)
    Q_PROPERTY(QString toggleLayoutLockShortcut READ toggleLayoutLockShortcut WRITE setToggleLayoutLockShortcut NOTIFY
                   toggleLayoutLockShortcutChanged)

    // Virtual Screen Swap / Rotate (Meta+Ctrl+Shift+Arrow, Meta+Ctrl+Shift+[/])
    Q_PROPERTY(QString swapVirtualScreenLeftShortcut READ swapVirtualScreenLeftShortcut WRITE
                   setSwapVirtualScreenLeftShortcut NOTIFY swapVirtualScreenLeftShortcutChanged)
    Q_PROPERTY(QString swapVirtualScreenRightShortcut READ swapVirtualScreenRightShortcut WRITE
                   setSwapVirtualScreenRightShortcut NOTIFY swapVirtualScreenRightShortcutChanged)
    Q_PROPERTY(QString swapVirtualScreenUpShortcut READ swapVirtualScreenUpShortcut WRITE setSwapVirtualScreenUpShortcut
                   NOTIFY swapVirtualScreenUpShortcutChanged)
    Q_PROPERTY(QString swapVirtualScreenDownShortcut READ swapVirtualScreenDownShortcut WRITE
                   setSwapVirtualScreenDownShortcut NOTIFY swapVirtualScreenDownShortcutChanged)
    Q_PROPERTY(QString rotateVirtualScreensClockwiseShortcut READ rotateVirtualScreensClockwiseShortcut WRITE
                   setRotateVirtualScreensClockwiseShortcut NOTIFY rotateVirtualScreensClockwiseShortcutChanged)
    Q_PROPERTY(
        QString rotateVirtualScreensCounterclockwiseShortcut READ rotateVirtualScreensCounterclockwiseShortcut WRITE
            setRotateVirtualScreensCounterclockwiseShortcut NOTIFY rotateVirtualScreensCounterclockwiseShortcutChanged)

public:
    explicit Settings(QObject* parent = nullptr);
    ~Settings() override = default;

    // No singleton - use dependency injection instead

    // Activation — PhosphorConfig::Store-backed.
    QVariantList dragActivationTriggers() const override;
    void setDragActivationTriggers(const QVariantList& triggers) override;
    bool zoneSpanEnabled() const override;
    void setZoneSpanEnabled(bool enabled) override;
    DragModifier zoneSpanModifier() const override;
    void setZoneSpanModifier(DragModifier modifier) override;
    int zoneSpanModifierInt() const;
    void setZoneSpanModifierInt(int modifier);
    QVariantList zoneSpanTriggers() const override;
    void setZoneSpanTriggers(const QVariantList& triggers) override;
    bool toggleActivation() const override;
    void setToggleActivation(bool enable) override;
    bool snappingEnabled() const override;
    void setSnappingEnabled(bool enabled) override;

    // Display — PhosphorConfig::Store-backed.
    bool showZonesOnAllMonitors() const override;
    void setShowZonesOnAllMonitors(bool show) override;
    QStringList disabledMonitors() const override;
    void setDisabledMonitors(const QStringList& screenIdOrNames) override;
    bool isMonitorDisabled(const QString& screenIdOrName) const override;
    QStringList disabledDesktops() const override;
    void setDisabledDesktops(const QStringList& entries) override;
    bool isDesktopDisabled(const QString& screenIdOrName, int desktop) const override;
    QStringList disabledActivities() const override;
    void setDisabledActivities(const QStringList& entries) override;
    bool isActivityDisabled(const QString& screenIdOrName, const QString& activityId) const override;
    bool showZoneNumbers() const override;
    void setShowZoneNumbers(bool show) override;
    bool flashZonesOnSwitch() const override;
    void setFlashZonesOnSwitch(bool flash) override;
    bool showOsdOnLayoutSwitch() const override;
    void setShowOsdOnLayoutSwitch(bool show) override;
    bool showOsdOnDesktopSwitch() const override;
    void setShowOsdOnDesktopSwitch(bool show) override;
    bool showNavigationOsd() const override;
    void setShowNavigationOsd(bool show) override;
    OsdStyle osdStyle() const override;
    void setOsdStyle(OsdStyle style) override;
    int osdStyleInt() const;
    void setOsdStyleInt(int style);
    OverlayDisplayMode overlayDisplayMode() const override;
    void setOverlayDisplayMode(OverlayDisplayMode mode) override;
    int overlayDisplayModeInt() const;
    void setOverlayDisplayModeInt(int mode);

    // Appearance — backed by PhosphorConfig::Store (see settingsschema.cpp).
    // Getters read through the store on demand with validator-driven
    // clamping; setters go through the store so persistence is immediate.
    bool useSystemColors() const override;
    void setUseSystemColors(bool use) override;
    QColor highlightColor() const override;
    void setHighlightColor(const QColor& color) override;
    QColor inactiveColor() const override;
    void setInactiveColor(const QColor& color) override;
    QColor borderColor() const override;
    void setBorderColor(const QColor& color) override;
    QColor labelFontColor() const override;
    void setLabelFontColor(const QColor& color) override;
    qreal activeOpacity() const override;
    void setActiveOpacity(qreal opacity) override;
    qreal inactiveOpacity() const override;
    void setInactiveOpacity(qreal opacity) override;
    int borderWidth() const override;
    void setBorderWidth(int width) override;
    int borderRadius() const override;
    void setBorderRadius(int radius) override;
    bool enableBlur() const override;
    void setEnableBlur(bool enable) override;
    QString labelFontFamily() const override;
    void setLabelFontFamily(const QString& family) override;
    qreal labelFontSizeScale() const override;
    void setLabelFontSizeScale(qreal scale) override;
    int labelFontWeight() const override;
    void setLabelFontWeight(int weight) override;
    bool labelFontItalic() const override;
    void setLabelFontItalic(bool italic) override;
    bool labelFontUnderline() const override;
    void setLabelFontUnderline(bool underline) override;
    bool labelFontStrikeout() const override;
    void setLabelFontStrikeout(bool strikeout) override;

    // PhosphorZones::Zone geometry (Snapping.Gaps) — PhosphorConfig::Store-backed.
    int zonePadding() const override;
    void setZonePadding(int padding) override;
    int outerGap() const override;
    void setOuterGap(int gap) override;
    bool usePerSideOuterGap() const override;
    void setUsePerSideOuterGap(bool enabled) override;
    int outerGapTop() const override;
    void setOuterGapTop(int gap) override;
    int outerGapBottom() const override;
    void setOuterGapBottom(int gap) override;
    int outerGapLeft() const override;
    void setOuterGapLeft(int gap) override;
    int outerGapRight() const override;
    void setOuterGapRight(int gap) override;
    int adjacentThreshold() const override;
    void setAdjacentThreshold(int threshold) override;

    // Performance — PhosphorConfig::Store-backed (see settingsschema.cpp).
    int pollIntervalMs() const override;
    void setPollIntervalMs(int interval) override;
    int minimumZoneSizePx() const override;
    void setMinimumZoneSizePx(int size) override;
    int minimumZoneDisplaySizePx() const override;
    void setMinimumZoneDisplaySizePx(int size) override;

    // Behavior — PhosphorConfig::Store-backed.
    bool keepWindowsInZonesOnResolutionChange() const override;
    void setKeepWindowsInZonesOnResolutionChange(bool keep) override;
    bool moveNewWindowsToLastZone() const override;
    void setMoveNewWindowsToLastZone(bool move) override;
    bool restoreOriginalSizeOnUnsnap() const override;
    void setRestoreOriginalSizeOnUnsnap(bool restore) override;
    StickyWindowHandling stickyWindowHandling() const override;
    void setStickyWindowHandling(StickyWindowHandling handling) override;
    int stickyWindowHandlingInt() const;
    void setStickyWindowHandlingInt(int handling);
    bool restoreWindowsToZonesOnLogin() const override;
    void setRestoreWindowsToZonesOnLogin(bool restore) override;
    bool autoAssignAllLayouts() const override;
    void setAutoAssignAllLayouts(bool enabled) override;
    bool snapAssistFeatureEnabled() const override;
    void setSnapAssistFeatureEnabled(bool enabled) override;
    bool snapAssistEnabled() const override;
    void setSnapAssistEnabled(bool enabled) override;
    QVariantList snapAssistTriggers() const override;
    void setSnapAssistTriggers(const QVariantList& triggers) override;
    QString defaultLayoutId() const override;
    void setDefaultLayoutId(const QString& layoutId) override;

    bool filterLayoutsByAspectRatio() const override;
    void setFilterLayoutsByAspectRatio(bool filter) override;

    // Ordering — PhosphorConfig::Store-backed (see settingsschema.cpp).
    // Wire format is a comma-joined QString; the canonicalCommaList
    // validator trims/dedups on every read and write.
    QStringList snappingLayoutOrder() const override;
    void setSnappingLayoutOrder(const QStringList& order) override;
    QStringList tilingAlgorithmOrder() const override;
    void setTilingAlgorithmOrder(const QStringList& order) override;

    // Exclusions — PhosphorConfig::Store-backed.
    QStringList excludedApplications() const override;
    void setExcludedApplications(const QStringList& apps) override;
    Q_INVOKABLE void addExcludedApplication(const QString& app);
    Q_INVOKABLE void removeExcludedApplicationAt(int index);
    QStringList excludedWindowClasses() const override;
    void setExcludedWindowClasses(const QStringList& classes) override;
    Q_INVOKABLE void addExcludedWindowClass(const QString& cls);
    Q_INVOKABLE void removeExcludedWindowClassAt(int index);
    bool excludeTransientWindows() const override;
    void setExcludeTransientWindows(bool exclude) override;
    int minimumWindowWidth() const override;
    void setMinimumWindowWidth(int width) override;
    int minimumWindowHeight() const override;
    void setMinimumWindowHeight(int height) override;

    // PhosphorZones::Zone Selector — PhosphorConfig::Store-backed.
    bool zoneSelectorEnabled() const override;
    void setZoneSelectorEnabled(bool enabled) override;
    int zoneSelectorTriggerDistance() const override;
    void setZoneSelectorTriggerDistance(int distance) override;
    ZoneSelectorPosition zoneSelectorPosition() const override;
    void setZoneSelectorPosition(ZoneSelectorPosition position) override;
    int zoneSelectorPositionInt() const;
    void setZoneSelectorPositionInt(int position);
    ZoneSelectorLayoutMode zoneSelectorLayoutMode() const override;
    void setZoneSelectorLayoutMode(ZoneSelectorLayoutMode mode) override;
    int zoneSelectorLayoutModeInt() const;
    void setZoneSelectorLayoutModeInt(int mode);
    int zoneSelectorPreviewWidth() const override;
    void setZoneSelectorPreviewWidth(int width) override;
    int zoneSelectorPreviewHeight() const override;
    void setZoneSelectorPreviewHeight(int height) override;
    bool zoneSelectorPreviewLockAspect() const override;
    void setZoneSelectorPreviewLockAspect(bool locked) override;
    int zoneSelectorGridColumns() const override;
    void setZoneSelectorGridColumns(int columns) override;
    ZoneSelectorSizeMode zoneSelectorSizeMode() const override;
    void setZoneSelectorSizeMode(ZoneSelectorSizeMode mode) override;
    int zoneSelectorSizeModeInt() const;
    void setZoneSelectorSizeModeInt(int mode);
    int zoneSelectorMaxRows() const override;
    void setZoneSelectorMaxRows(int rows) override;

    // Per-screen zone selector config (override > global fallback)
    ZoneSelectorConfig resolvedZoneSelectorConfig(const QString& screenIdOrName) const override;
    Q_INVOKABLE QVariantMap getPerScreenZoneSelectorSettings(const QString& screenIdOrName) const override;
    Q_INVOKABLE void setPerScreenZoneSelectorSetting(const QString& screenIdOrName, const QString& key,
                                                     const QVariant& value) override;
    Q_INVOKABLE void clearPerScreenZoneSelectorSettings(const QString& screenIdOrName) override;
    Q_INVOKABLE bool hasPerScreenZoneSelectorSettings(const QString& screenIdOrName) const override;
    Q_INVOKABLE QStringList screensWithZoneSelectorOverrides() const;

    // Per-screen autotile config (override > global fallback)
    Q_INVOKABLE QVariantMap getPerScreenAutotileSettings(const QString& screenIdOrName) const override;
    Q_INVOKABLE void setPerScreenAutotileSetting(const QString& screenIdOrName, const QString& key,
                                                 const QVariant& value) override;
    Q_INVOKABLE void clearPerScreenAutotileSettings(const QString& screenIdOrName) override;
    Q_INVOKABLE bool hasPerScreenAutotileSettings(const QString& screenIdOrName) const override;

    // Per-screen snapping config (override > global fallback)
    Q_INVOKABLE QVariantMap getPerScreenSnappingSettings(const QString& screenIdOrName) const override;
    Q_INVOKABLE void setPerScreenSnappingSetting(const QString& screenIdOrName, const QString& key,
                                                 const QVariant& value) override;
    Q_INVOKABLE void clearPerScreenSnappingSettings(const QString& screenIdOrName) override;
    Q_INVOKABLE bool hasPerScreenSnappingSettings(const QString& screenIdOrName) const override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Autotiling Settings (ISettings interface)
    // ═══════════════════════════════════════════════════════════════════════════

    // Autotiling — PhosphorConfig::Store-backed.
    bool autotileEnabled() const;
    void setAutotileEnabled(bool enabled);
    QString defaultAutotileAlgorithm() const override;
    void setDefaultAutotileAlgorithm(const QString& algorithm) override;
    qreal autotileSplitRatio() const override;
    void setAutotileSplitRatio(qreal ratio) override;
    qreal autotileSplitRatioStep() const override;
    void setAutotileSplitRatioStep(qreal step);
    int autotileMasterCount() const override;
    void setAutotileMasterCount(int count) override;
    QVariantMap autotilePerAlgorithmSettings() const override;
    void setAutotilePerAlgorithmSettings(const QVariantMap& settings) override;
    int autotileInnerGap() const override;
    void setAutotileInnerGap(int gap);
    int autotileOuterGap() const override;
    void setAutotileOuterGap(int gap);
    bool autotileUsePerSideOuterGap() const override;
    void setAutotileUsePerSideOuterGap(bool enabled);
    int autotileOuterGapTop() const override;
    void setAutotileOuterGapTop(int gap);
    int autotileOuterGapBottom() const override;
    void setAutotileOuterGapBottom(int gap);
    int autotileOuterGapLeft() const override;
    void setAutotileOuterGapLeft(int gap);
    int autotileOuterGapRight() const override;
    void setAutotileOuterGapRight(int gap);
    bool autotileFocusNewWindows() const override;
    void setAutotileFocusNewWindows(bool focus);
    bool autotileSmartGaps() const override;
    void setAutotileSmartGaps(bool smart);
    int autotileMaxWindows() const override;
    void setAutotileMaxWindows(int count) override;

    using AutotileInsertPosition = PhosphorTiles::AutotileInsertPosition;
    PhosphorTiles::AutotileInsertPosition autotileInsertPosition() const override;
    void setAutotileInsertPosition(PhosphorTiles::AutotileInsertPosition position);
    int autotileInsertPositionInt() const;
    void setAutotileInsertPositionInt(int position);

    QVariantList autotileDragInsertTriggers() const override;
    void setAutotileDragInsertTriggers(const QVariantList& triggers) override;
    bool autotileDragInsertToggle() const override;
    void setAutotileDragInsertToggle(bool enable) override;

    // Autotile Shortcuts — PhosphorConfig::Store-backed.
    QString autotileToggleShortcut() const;
    void setAutotileToggleShortcut(const QString& shortcut);
    QString autotileFocusMasterShortcut() const;
    void setAutotileFocusMasterShortcut(const QString& shortcut);
    QString autotileSwapMasterShortcut() const;
    void setAutotileSwapMasterShortcut(const QString& shortcut);
    QString autotileIncMasterRatioShortcut() const;
    void setAutotileIncMasterRatioShortcut(const QString& shortcut);
    QString autotileDecMasterRatioShortcut() const;
    void setAutotileDecMasterRatioShortcut(const QString& shortcut);
    QString autotileIncMasterCountShortcut() const;
    void setAutotileIncMasterCountShortcut(const QString& shortcut);
    QString autotileDecMasterCountShortcut() const;
    void setAutotileDecMasterCountShortcut(const QString& shortcut);
    QString autotileRetileShortcut() const;
    void setAutotileRetileShortcut(const QString& shortcut);

    // Animation Settings (applies to both snapping and autotiling geometry
    // changes) — backed by PhosphorConfig::Store (see settingsschema.cpp).
    // Phase 4 sub-commit 6: storage format migrated to a single Profile
    // JSON blob; per-field accessors below are projections. The `animationProfile`
    // getter / setter is the new canonical surface for consumers that want
    // the whole Profile atomically (daemon's WindowAnimator config-reload path,
    // plugin-authored profile presets).
    bool animationsEnabled() const override;
    void setAnimationsEnabled(bool enabled) override;
    /// The single Profile blob backing every per-field accessor below.
    /// Not a Q_PROPERTY — QML consumers that need the typed Profile
    /// should use the sub-commit-2 `PhosphorProfile` Q_GADGET; this
    /// returns a C++-only PhosphorAnimation::Profile value.
    PhosphorAnimation::Profile animationProfile() const;
    void setAnimationProfile(const PhosphorAnimation::Profile& profile);
    int animationDuration() const override;
    void setAnimationDuration(int duration) override;
    QString animationEasingCurve() const override;
    void setAnimationEasingCurve(const QString& curve) override;
    int animationMinDistance() const override;
    void setAnimationMinDistance(int distance) override;
    int animationSequenceMode() const override;
    void setAnimationSequenceMode(int mode) override;
    int animationStaggerInterval() const override;
    void setAnimationStaggerInterval(int ms) override;

    // Additional Autotiling Settings — PhosphorConfig::Store-backed.
    bool autotileFocusFollowsMouse() const override;
    void setAutotileFocusFollowsMouse(bool focus) override;
    bool autotileRespectMinimumSize() const override;
    void setAutotileRespectMinimumSize(bool respect);
    bool autotileHideTitleBars() const override;
    void setAutotileHideTitleBars(bool hide) override;
    bool autotileShowBorder() const override;
    void setAutotileShowBorder(bool show) override;
    int autotileBorderWidth() const override;
    void setAutotileBorderWidth(int width) override;
    int autotileBorderRadius() const override;
    void setAutotileBorderRadius(int radius) override;
    QColor autotileBorderColor() const override;
    void setAutotileBorderColor(const QColor& color) override;
    QColor autotileInactiveBorderColor() const override;
    void setAutotileInactiveBorderColor(const QColor& color) override;
    bool autotileUseSystemBorderColors() const override;
    void setAutotileUseSystemBorderColors(bool use) override;
    StickyWindowHandling autotileStickyWindowHandling() const override;
    void setAutotileStickyWindowHandling(StickyWindowHandling handling) override;
    int autotileStickyWindowHandlingInt() const;
    void setAutotileStickyWindowHandlingInt(int handling);
    AutotileDragBehavior autotileDragBehavior() const override;
    void setAutotileDragBehavior(AutotileDragBehavior behavior) override;
    int autotileDragBehaviorInt() const;
    void setAutotileDragBehaviorInt(int behavior);
    AutotileOverflowBehavior autotileOverflowBehavior() const override;
    void setAutotileOverflowBehavior(AutotileOverflowBehavior behavior) override;
    int autotileOverflowBehaviorInt() const;
    void setAutotileOverflowBehaviorInt(int behavior);
    QStringList lockedScreens() const override;
    void setLockedScreens(const QStringList& screens) override;
    bool isScreenLocked(const QString& screenIdOrName) const override;
    void setScreenLocked(const QString& screenIdOrName, bool locked) override;
    bool isContextLocked(const QString& screenIdOrName, int virtualDesktop, const QString& activity) const override;
    void setContextLocked(const QString& screenIdOrName, int virtualDesktop, const QString& activity,
                          bool locked) override;

    // Virtual screen configuration
    QHash<QString, Phosphor::Screens::VirtualScreenConfig> virtualScreenConfigs() const;
    void setVirtualScreenConfigs(const QHash<QString, Phosphor::Screens::VirtualScreenConfig>& configs);
    /// Returns true on success, false if the config was rejected by
    /// Phosphor::Screens::VirtualScreenConfig::isValid (or empty physicalScreenId). An
    /// already-current value is treated as a successful no-op.
    bool setVirtualScreenConfig(const QString& physicalScreenId, const Phosphor::Screens::VirtualScreenConfig& config);
    Phosphor::Screens::VirtualScreenConfig virtualScreenConfig(const QString& physicalScreenId) const;

    /// Atomically re-key a persisted VS config from @p oldPhysicalScreenId to
    /// @p newPhysicalScreenId. Used when a screen's disambiguation-aware
    /// identifier flips (same-model hotplug) so the user's existing
    /// subdivision doesn't orphan under the old key. No-op if no config
    /// exists at @p oldPhysicalScreenId. Emits @ref virtualScreenConfigsChanged
    /// exactly once (not twice, as remove+set would).
    /// @return true on success or when nothing to migrate.
    bool renameVirtualScreenConfig(const QString& oldPhysicalScreenId, const QString& newPhysicalScreenId);

    // Rendering (ISettings)
    // Rendering backend — PhosphorConfig::Store-backed; the schema's
    // validator runs normalizeRenderingBackend on every read/write so
    // unknown strings are coerced to a valid choice.
    QString renderingBackend() const override;
    void setRenderingBackend(const QString& backend) override;

    // Shader Effects — backed by PhosphorConfig::Store (see settingsschema.cpp).
    // Getters read through the store (validator clamps FrameRate and BarCount
    // ranges uniformly); setters route the write through the store so the
    // value is coerced + persisted in memory on the same call, with
    // syncConfig()/save() flushing to disk.
    bool enableShaderEffects() const override;
    void setEnableShaderEffects(bool enable) override;
    int shaderFrameRate() const override;
    void setShaderFrameRate(int fps) override;
    bool enableAudioVisualizer() const override;
    void setEnableAudioVisualizer(bool enable) override;
    int audioSpectrumBarCount() const override;
    void setAudioSpectrumBarCount(int count) override;

    // Global Shortcuts (for KGlobalAccel) — PhosphorConfig::Store-backed.
    QString openEditorShortcut() const;
    void setOpenEditorShortcut(const QString& shortcut);
    QString openSettingsShortcut() const;
    void setOpenSettingsShortcut(const QString& shortcut);
    QString previousLayoutShortcut() const;
    void setPreviousLayoutShortcut(const QString& shortcut);
    QString nextLayoutShortcut() const;
    void setNextLayoutShortcut(const QString& shortcut);
    QString quickLayout1Shortcut() const;
    void setQuickLayout1Shortcut(const QString& shortcut);
    QString quickLayout2Shortcut() const;
    void setQuickLayout2Shortcut(const QString& shortcut);
    QString quickLayout3Shortcut() const;
    void setQuickLayout3Shortcut(const QString& shortcut);
    QString quickLayout4Shortcut() const;
    void setQuickLayout4Shortcut(const QString& shortcut);
    QString quickLayout5Shortcut() const;
    void setQuickLayout5Shortcut(const QString& shortcut);
    QString quickLayout6Shortcut() const;
    void setQuickLayout6Shortcut(const QString& shortcut);
    QString quickLayout7Shortcut() const;
    void setQuickLayout7Shortcut(const QString& shortcut);
    QString quickLayout8Shortcut() const;
    void setQuickLayout8Shortcut(const QString& shortcut);
    QString quickLayout9Shortcut() const;
    void setQuickLayout9Shortcut(const QString& shortcut);
    // Helper accessors by 0-based index.
    QString quickLayoutShortcut(int index) const;
    void setQuickLayoutShortcut(int index, const QString& shortcut);

    // Keyboard Navigation Shortcuts
    QString moveWindowLeftShortcut() const;
    void setMoveWindowLeftShortcut(const QString& shortcut);
    QString moveWindowRightShortcut() const;
    void setMoveWindowRightShortcut(const QString& shortcut);
    QString moveWindowUpShortcut() const;
    void setMoveWindowUpShortcut(const QString& shortcut);
    QString moveWindowDownShortcut() const;
    void setMoveWindowDownShortcut(const QString& shortcut);
    QString focusZoneLeftShortcut() const;
    void setFocusZoneLeftShortcut(const QString& shortcut);
    QString focusZoneRightShortcut() const;
    void setFocusZoneRightShortcut(const QString& shortcut);
    QString focusZoneUpShortcut() const;
    void setFocusZoneUpShortcut(const QString& shortcut);
    QString focusZoneDownShortcut() const;
    void setFocusZoneDownShortcut(const QString& shortcut);
    QString pushToEmptyZoneShortcut() const;
    void setPushToEmptyZoneShortcut(const QString& shortcut);
    QString restoreWindowSizeShortcut() const;
    void setRestoreWindowSizeShortcut(const QString& shortcut);
    QString toggleWindowFloatShortcut() const;
    void setToggleWindowFloatShortcut(const QString& shortcut);

    QString swapWindowLeftShortcut() const;
    void setSwapWindowLeftShortcut(const QString& shortcut);
    QString swapWindowRightShortcut() const;
    void setSwapWindowRightShortcut(const QString& shortcut);
    QString swapWindowUpShortcut() const;
    void setSwapWindowUpShortcut(const QString& shortcut);
    QString swapWindowDownShortcut() const;
    void setSwapWindowDownShortcut(const QString& shortcut);

    QString snapToZone1Shortcut() const;
    void setSnapToZone1Shortcut(const QString& shortcut);
    QString snapToZone2Shortcut() const;
    void setSnapToZone2Shortcut(const QString& shortcut);
    QString snapToZone3Shortcut() const;
    void setSnapToZone3Shortcut(const QString& shortcut);
    QString snapToZone4Shortcut() const;
    void setSnapToZone4Shortcut(const QString& shortcut);
    QString snapToZone5Shortcut() const;
    void setSnapToZone5Shortcut(const QString& shortcut);
    QString snapToZone6Shortcut() const;
    void setSnapToZone6Shortcut(const QString& shortcut);
    QString snapToZone7Shortcut() const;
    void setSnapToZone7Shortcut(const QString& shortcut);
    QString snapToZone8Shortcut() const;
    void setSnapToZone8Shortcut(const QString& shortcut);
    QString snapToZone9Shortcut() const;
    void setSnapToZone9Shortcut(const QString& shortcut);
    QString snapToZoneShortcut(int index) const;
    void setSnapToZoneShortcut(int index, const QString& shortcut);

    QString rotateWindowsClockwiseShortcut() const;
    void setRotateWindowsClockwiseShortcut(const QString& shortcut);
    QString rotateWindowsCounterclockwiseShortcut() const;
    void setRotateWindowsCounterclockwiseShortcut(const QString& shortcut);
    QString cycleWindowForwardShortcut() const;
    void setCycleWindowForwardShortcut(const QString& shortcut);
    QString cycleWindowBackwardShortcut() const;
    void setCycleWindowBackwardShortcut(const QString& shortcut);
    QString resnapToNewLayoutShortcut() const;
    void setResnapToNewLayoutShortcut(const QString& shortcut);
    QString snapAllWindowsShortcut() const;
    void setSnapAllWindowsShortcut(const QString& shortcut);
    QString layoutPickerShortcut() const;
    void setLayoutPickerShortcut(const QString& shortcut);
    QString toggleLayoutLockShortcut() const;
    void setToggleLayoutLockShortcut(const QString& shortcut);

    QString swapVirtualScreenLeftShortcut() const;
    void setSwapVirtualScreenLeftShortcut(const QString& shortcut);
    QString swapVirtualScreenRightShortcut() const;
    void setSwapVirtualScreenRightShortcut(const QString& shortcut);
    QString swapVirtualScreenUpShortcut() const;
    void setSwapVirtualScreenUpShortcut(const QString& shortcut);
    QString swapVirtualScreenDownShortcut() const;
    void setSwapVirtualScreenDownShortcut(const QString& shortcut);
    QString rotateVirtualScreensClockwiseShortcut() const;
    void setRotateVirtualScreensClockwiseShortcut(const QString& shortcut);
    QString rotateVirtualScreensCounterclockwiseShortcut() const;
    void setRotateVirtualScreensCounterclockwiseShortcut(const QString& shortcut);

    // ═══════════════════════════════════════════════════════════════════════════
    // Editor Settings (shared [Editor] group in config.json)
    // PhosphorConfig::Store-backed.
    // ═══════════════════════════════════════════════════════════════════════════
    QString editorDuplicateShortcut() const;
    void setEditorDuplicateShortcut(const QString& shortcut);
    QString editorSplitHorizontalShortcut() const;
    void setEditorSplitHorizontalShortcut(const QString& shortcut);
    QString editorSplitVerticalShortcut() const;
    void setEditorSplitVerticalShortcut(const QString& shortcut);
    QString editorFillShortcut() const;
    void setEditorFillShortcut(const QString& shortcut);
    bool editorGridSnappingEnabled() const;
    void setEditorGridSnappingEnabled(bool enabled);
    bool editorEdgeSnappingEnabled() const;
    void setEditorEdgeSnappingEnabled(bool enabled);
    qreal editorSnapIntervalX() const;
    void setEditorSnapIntervalX(qreal interval);
    qreal editorSnapIntervalY() const;
    void setEditorSnapIntervalY(qreal interval);
    int editorSnapOverrideModifier() const;
    void setEditorSnapOverrideModifier(int mod);
    bool fillOnDropEnabled() const;
    void setFillOnDropEnabled(bool enabled);
    int fillOnDropModifier() const;
    void setFillOnDropModifier(int mod);

    // Old inline accessors replaced above — kept anchors below so the second
    // half of the replaced region can be collapsed in one edit pass.

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
    /// Emitted when the whole animation Profile blob is replaced via
    /// `setAnimationProfile`. Fires alongside every per-field *Changed
    /// signal. Consumers that want to observe the Profile atomically
    /// (e.g., daemon's WindowAnimator re-configuring its MotionSpec
    /// defaults) prefer this signal; Q_PROPERTY consumers bound to the
    /// per-field surface get re-triggered through the individual
    /// signals per the existing NOTIFY wiring.
    void animationProfileChanged();

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
    void virtualScreenConfigsChanged();

private:
    /// Member-function-pointer alias used by the indexed shortcut setters
    /// (quickLayoutShortcut / snapToZoneShortcut) when fanning out to the
    /// per-index NOTIFY signal.
    using ShortcutSignalFn = void (Settings::*)();

    /// Member-function-pointer alias for the per-trigger-list NOTIFY signal
    /// passed into @ref writeTriggerList.
    using TriggerListSignalFn = void (Settings::*)();

    /// Shared trigger-list setter used by the four "plain" setters
    /// (activation, deactivation, snap-assist, autotile-insert). Caps at
    /// @c MaxTriggersPerAction, round-trips through the schema's validator,
    /// and only emits @p specificSignal + @c settingsChanged on a real change.
    /// @c setZoneSpanTriggers does its own dance because it also synchronises
    /// the legacy single-modifier key.
    void writeTriggerList(const QString& group, const QString& key, const QVariantList& triggers,
                          TriggerListSignalFn specificSignal);

    // ─── load() / save() helpers ────────────────────────────────────────
    // Only non-Store groups need dedicated helpers now. Store-backed groups
    // (Activation/Display/ZoneGeometry/Behavior/ZoneSelector/Shortcut/
    // Autotiling/Editor/Appearance/Rendering/Shaders/Ordering) persist via
    // setters and flush via m_configBackend->sync() in save().
    void loadPerScreenOverrides(PhosphorConfig::IBackend* backend);
    void loadVirtualScreenConfigs(PhosphorConfig::IBackend* backend);
    void saveAllPerScreenOverrides(PhosphorConfig::IBackend* backend);
    void saveVirtualScreenConfigs(PhosphorConfig::IBackend* backend);

    // Groups that save() writes exhaustively (excludes unmanaged groups).
    static QStringList managedGroupNames();
    // Delete all per-screen override groups (ZoneSelector:*, AutotileScreen:*, SnappingScreen:*).
    static void deletePerScreenGroups(PhosphorConfig::IBackend* backend);
    // Purge stale keys from all managed groups before save() rewrites them.
    void purgeStaleKeys();

    // Patch one field of the Profile JSON blob and emit the canonical
    // signal trio (per-field NOTIFY + animationProfileChanged + settingsChanged).
    //
    // Hot path: per-field setters fired by settings-UI sliders at ~30 Hz.
    // The helper consolidates the 5 near-identical animation field setters
    // (duration / easing-curve / min-distance / sequence-mode / stagger-
    // interval) so the merge contract (read existing blob → insert one
    // field → write back, preserving every other on-disk key) is in one
    // place rather than copy-pasted five times.
    //
    // T must be comparable (`operator==`) and convertible to QJsonValue
    // (the QJsonObject::insert overload set covers int / double / bool /
    // QString / QJsonValue / QJsonArray / QJsonObject).
    //
    // - @p jsonFieldName     The Profile JSON key (raw `const char*` —
    //                        wrapped via `QLatin1String` at the insert site
    //                        to satisfy Qt6's deleted raw-string ctor).
    // - @p currentValue      What the corresponding getter returns BEFORE
    //                        the write — supplied by the caller because the
    //                        getter shape varies per field (int via
    //                        `effectiveDuration`, QString via raw blob
    //                        read for the curve case, etc.). The no-op
    //                        guard short-circuits when `currentValue ==
    //                        newValue` so a slider drag at constant value
    //                        wakes no observers.
    // - @p newValue          The post-clamp / post-resolve value to insert.
    //                        Pre-processing (clamping for numeric setters,
    //                        registry resolution for the easing setter)
    //                        stays at the call site — the helper is a pure
    //                        merge primitive, not a validator.
    // - @p fieldChangedSignal The per-field NOTIFY pointer. Fires alongside
    //                         animationProfileChanged + settingsChanged on
    //                         every successful write.
    template<typename T>
    void patchProfileField(const char* jsonFieldName, const T& currentValue, const T& newValue,
                           void (Settings::*fieldChangedSignal)());

    // Config backend — owned (standalone) or non-owned (shared from Daemon)
    std::unique_ptr<PhosphorConfig::IBackend> m_ownedBackend;
    PhosphorConfig::IBackend* m_configBackend = nullptr; // always valid after construction

    // Declarative store built on top of the backend. Every setting migrated
    // from hand-written load*/save* functions routes through here. See
    // settingsschema.cpp for the current list of migrated groups.
    std::unique_ptr<PhosphorConfig::Store> m_store;
    static QString normalizeUuidString(const QString& uuidStr);

    // Activation
    // Activation is stored in m_store.

    // Display
    // Display is stored in m_store; no cached members here.

    // Appearance
    // Appearance + Labels + Opacity + Border + Effects.Blur are stored in
    // m_store; no cached members here.

    // PhosphorZones::Zone settings
    // PhosphorZones::Zone geometry is stored in m_store; no cached members here.

    // Performance and behavior
    // Performance is stored in m_store; no cached members here.

    // Window behavior
    // Behavior (WindowHandling + SnapAssist) is stored in m_store.

    // Default layout (used when no explicit assignment exists)
    // defaultLayoutId stored in m_store.

    // PhosphorZones::Layout filtering
    // filterLayoutsByAspectRatio is stored in m_store.

    // Ordering
    // Ordering is stored in m_store; no cached members here.

    // Exclusions
    // Exclusions are stored in m_store; no cached members here.

    // PhosphorZones::Zone Selector
    // PhosphorZones::Zone selector is stored in m_store.
    // (remaining zone selector members stored in m_store)

    // Virtual screen configurations (physicalScreenId -> config)
    QHash<QString, Phosphor::Screens::VirtualScreenConfig> m_virtualScreenConfigs;

    // Per-screen zone selector overrides (screenIdOrName -> settings map)
    QHash<QString, QVariantMap> m_perScreenZoneSelectorSettings;

    // Per-screen autotile overrides (screenIdOrName -> settings map)
    QHash<QString, QVariantMap> m_perScreenAutotileSettings;

    // Per-screen snapping overrides (screenIdOrName -> settings map)
    QHash<QString, QVariantMap> m_perScreenSnappingSettings;

    // Autotiling Settings
    // Autotiling stored in m_store.

    // Animation Settings (applies to both snapping and autotiling geometry changes)
    // Animations are stored in m_store; no cached members here.

    // Non-owned CurveRegistry for animation profile parsing. Injected
    // at construction (daemon composition root); null for standalone
    // Settings instances that fall back to a local static registry.
    // Injection is constructor-only — there is no post-construction
    // setter because `load()` runs during ctor and would leave cached
    // Profile state parsed through the wrong registry if a setter ran
    // after. Callers must pass the registry up front or accept the
    // fallback contract.
    PhosphorAnimation::CurveRegistry* m_curveRegistry = nullptr;

    // Additional Autotiling Settings
    // Autotiling (continued) stored in m_store.
};

} // namespace PlasmaZones
