// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../core/constants.h"
#include "../core/interfaces.h"
#include "configbackends.h"
#include "configdefaults.h"

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorConfig/Store.h>
#include <PhosphorRules/RuleStore.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorSnapEngine/ISnapSettings.h>
#include <PhosphorTileEngine/IAutotileSettings.h>

#include <QFont>
#include <QHash>
#include <QJsonValue>
#include <QList>
#include <QPair>
#include <QUuid>
#include <QVariantMap>
#include <QVector>

#include <memory>
#include <optional>

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
                                    public PhosphorEngine::IAutotileSettings,
                                    public PhosphorEngine::ISnapSettings
{
    Q_OBJECT
    Q_INTERFACES(PhosphorEngine::IAutotileSettings PhosphorEngine::ISnapSettings)

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
     * @param ruleStore Non-owned RuleStore pointer. When non-null,
     *                        Settings shares this store rather than owning a
     *                        second instance pointed at the same file — a
     *                        dual-store setup races on disk (one store's
     *                        `setAllRules` clobbers the other's unflushed
     *                        edits because both rebuild `kept` from stale
     *                        in-memory snapshots). The daemon, which already
     *                        owns the canonical store for `LayoutRegistry`
     *                        and `RuleAdaptor`, passes it in here so
     *                        every in-process consumer mutates the same
     *                        ruleset. Standalone processes (settings app,
     *                        editor) pass nullptr and Settings falls back to
     *                        owning its own store. Must outlive this Settings
     *                        when non-null. The caller is responsible for
     *                        having already invoked `load()` on a non-null
     *                        store; this ctor does not reload it.
     * @param parent Parent QObject
     */
    Settings(PhosphorConfig::IBackend* backend, PhosphorAnimation::CurveRegistry* curveRegistry,
             PhosphorRules::RuleStore* ruleStore, QObject* parent);

    // Activation settings
    Q_PROPERTY(QVariantList dragActivationTriggers READ dragActivationTriggers WRITE setDragActivationTriggers NOTIFY
                   dragActivationTriggersChanged)
    Q_PROPERTY(bool zoneSpanEnabled READ zoneSpanEnabled WRITE setZoneSpanEnabled NOTIFY zoneSpanEnabledChanged)
    Q_PROPERTY(
        int zoneSpanModifier READ zoneSpanModifierInt WRITE setZoneSpanModifierInt NOTIFY zoneSpanModifierChanged)
    Q_PROPERTY(
        QVariantList zoneSpanTriggers READ zoneSpanTriggers WRITE setZoneSpanTriggers NOTIFY zoneSpanTriggersChanged)
    Q_PROPERTY(
        bool zoneSpanToggleMode READ zoneSpanToggleMode WRITE setZoneSpanToggleMode NOTIFY zoneSpanToggleModeChanged)
    Q_PROPERTY(bool toggleActivation READ toggleActivation WRITE setToggleActivation NOTIFY toggleActivationChanged)
    Q_PROPERTY(bool snappingEnabled READ snappingEnabled WRITE setSnappingEnabled NOTIFY snappingEnabledChanged)

    // Display settings
    Q_PROPERTY(bool showZonesOnAllMonitors READ showZonesOnAllMonitors WRITE setShowZonesOnAllMonitors NOTIFY
                   showZonesOnAllMonitorsChanged)
    // Per-mode disable lists are not exposed as Q_PROPERTYs because their
    // accessors take a Mode argument. QML / D-Bus consumers go through the
    // mode-aware Q_INVOKABLEs on SettingsController and the per-mode
    // registrations in SettingsAdaptor instead.
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
    Q_PROPERTY(QString labelFontFamily READ labelFontFamily WRITE setLabelFontFamily NOTIFY labelFontFamilyChanged)
    Q_PROPERTY(
        qreal labelFontSizeScale READ labelFontSizeScale WRITE setLabelFontSizeScale NOTIFY labelFontSizeScaleChanged)
    Q_PROPERTY(int labelFontWeight READ labelFontWeight WRITE setLabelFontWeight NOTIFY labelFontWeightChanged)
    Q_PROPERTY(bool labelFontItalic READ labelFontItalic WRITE setLabelFontItalic NOTIFY labelFontItalicChanged)
    Q_PROPERTY(
        bool labelFontUnderline READ labelFontUnderline WRITE setLabelFontUnderline NOTIFY labelFontUnderlineChanged)
    Q_PROPERTY(
        bool labelFontStrikeout READ labelFontStrikeout WRITE setLabelFontStrikeout NOTIFY labelFontStrikeoutChanged)

    // Zone settings — inner/outer gaps are the single shared model used by BOTH
    // snapping and tiling. Config-backed (the "Gaps" group in config.json),
    // like every other settings page. The autotile* gap forwarders below route
    // through these same getters, so the tile engine stays untouched.
    Q_PROPERTY(int innerGap READ innerGap WRITE setInnerGap NOTIFY innerGapChanged)
    Q_PROPERTY(int outerGap READ outerGap WRITE setOuterGap NOTIFY outerGapChanged)
    Q_PROPERTY(
        bool usePerSideOuterGap READ usePerSideOuterGap WRITE setUsePerSideOuterGap NOTIFY usePerSideOuterGapChanged)
    Q_PROPERTY(int outerGapTop READ outerGapTop WRITE setOuterGapTop NOTIFY outerGapTopChanged)
    Q_PROPERTY(int outerGapBottom READ outerGapBottom WRITE setOuterGapBottom NOTIFY outerGapBottomChanged)
    Q_PROPERTY(int outerGapLeft READ outerGapLeft WRITE setOuterGapLeft NOTIFY outerGapLeftChanged)
    Q_PROPERTY(int outerGapRight READ outerGapRight WRITE setOuterGapRight NOTIFY outerGapRightChanged)
    Q_PROPERTY(int adjacentThreshold READ adjacentThreshold WRITE setAdjacentThreshold NOTIFY adjacentThresholdChanged)

    // Window decoration appearance (tiled/snapped window border + title bar) —
    // config-backed (the "Windows" group). Distinct from the zone-overlay border
    // Q_PROPERTYs above (borderWidth/borderRadius/borderColor).
    Q_PROPERTY(bool showWindowBorder READ showWindowBorder WRITE setShowWindowBorder NOTIFY showWindowBorderChanged)
    Q_PROPERTY(
        QString windowBorderScope READ windowBorderScope WRITE setWindowBorderScope NOTIFY windowBorderScopeChanged)
    Q_PROPERTY(int windowBorderWidth READ windowBorderWidth WRITE setWindowBorderWidth NOTIFY windowBorderWidthChanged)
    Q_PROPERTY(
        int windowBorderRadius READ windowBorderRadius WRITE setWindowBorderRadius NOTIFY windowBorderRadiusChanged)
    Q_PROPERTY(QString windowBorderColorActive READ windowBorderColorActive WRITE setWindowBorderColorActive NOTIFY
                   windowBorderColorActiveChanged)
    Q_PROPERTY(QString windowBorderColorInactive READ windowBorderColorInactive WRITE setWindowBorderColorInactive
                   NOTIFY windowBorderColorInactiveChanged)
    Q_PROPERTY(bool hideWindowTitleBars READ hideWindowTitleBars WRITE setHideWindowTitleBars NOTIFY
                   hideWindowTitleBarsChanged)
    Q_PROPERTY(QString windowTitleBarScope READ windowTitleBarScope WRITE setWindowTitleBarScope NOTIFY
                   windowTitleBarScopeChanged)
    Q_PROPERTY(int focusFadeDuration READ focusFadeDuration WRITE setFocusFadeDuration NOTIFY focusFadeDurationChanged)
    Q_PROPERTY(bool showWindowOpacityTint READ showWindowOpacityTint WRITE setShowWindowOpacityTint NOTIFY
                   showWindowOpacityTintChanged)
    Q_PROPERTY(QString windowOpacityTintScope READ windowOpacityTintScope WRITE setWindowOpacityTintScope NOTIFY
                   windowOpacityTintScopeChanged)
    Q_PROPERTY(double windowOpacity READ windowOpacity WRITE setWindowOpacity NOTIFY windowOpacityChanged)
    Q_PROPERTY(
        double windowTintStrength READ windowTintStrength WRITE setWindowTintStrength NOTIFY windowTintStrengthChanged)
    Q_PROPERTY(QString windowTintColor READ windowTintColor WRITE setWindowTintColor NOTIFY windowTintColorChanged)

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
    Q_PROPERTY(int snappingStickyWindowHandling READ snappingStickyWindowHandlingInt WRITE
                   setSnappingStickyWindowHandlingInt NOTIFY snappingStickyWindowHandlingChanged)
    Q_PROPERTY(bool restoreWindowsToZonesOnLogin READ restoreWindowsToZonesOnLogin WRITE setRestoreWindowsToZonesOnLogin
                   NOTIFY restoreWindowsToZonesOnLoginChanged)
    Q_PROPERTY(bool snappingRestoreFloatedWindowsOnLogin READ snappingRestoreFloatedWindowsOnLogin WRITE
                   setSnappingRestoreFloatedWindowsOnLogin NOTIFY snappingRestoreFloatedWindowsOnLoginChanged)
    Q_PROPERTY(bool autotileRestoreFloatedWindowsOnLogin READ autotileRestoreFloatedWindowsOnLogin WRITE
                   setAutotileRestoreFloatedWindowsOnLogin NOTIFY autotileRestoreFloatedWindowsOnLoginChanged)
    Q_PROPERTY(bool snapUnfloatFallbackToZone READ snapUnfloatFallbackToZone WRITE setSnapUnfloatFallbackToZone NOTIFY
                   snapUnfloatFallbackToZoneChanged)
    Q_PROPERTY(bool autoAssignAllLayouts READ autoAssignAllLayouts WRITE setAutoAssignAllLayouts NOTIFY
                   autoAssignAllLayoutsChanged)
    Q_PROPERTY(bool snapAssistFeatureEnabled READ snapAssistFeatureEnabled WRITE setSnapAssistFeatureEnabled NOTIFY
                   snapAssistFeatureEnabledChanged)
    Q_PROPERTY(bool snapAssistEnabled READ snapAssistEnabled WRITE setSnapAssistEnabled NOTIFY snapAssistEnabledChanged)
    Q_PROPERTY(QVariantList snapAssistTriggers READ snapAssistTriggers WRITE setSnapAssistTriggers NOTIFY
                   snapAssistTriggersChanged)

    // Default layout (used when no explicit assignment exists)
    Q_PROPERTY(QString defaultLayoutId READ defaultLayoutId WRITE setDefaultLayoutId NOTIFY defaultLayoutIdChanged)
    Q_PROPERTY(bool suppressDefaultLayoutAssignment READ suppressDefaultLayoutAssignment WRITE
                   setSuppressDefaultLayoutAssignment NOTIFY suppressDefaultLayoutAssignmentChanged)

    // PhosphorZones::Layout filtering
    Q_PROPERTY(bool filterLayoutsByAspectRatio READ filterLayoutsByAspectRatio WRITE setFilterLayoutsByAspectRatio
                   NOTIFY filterLayoutsByAspectRatioChanged)

    // Ordering (manual sort order for cycling / zone selector / overlay)
    Q_PROPERTY(QStringList snappingLayoutOrder READ snappingLayoutOrder WRITE setSnappingLayoutOrder NOTIFY
                   snappingLayoutOrderChanged)
    Q_PROPERTY(QStringList tilingAlgorithmOrder READ tilingAlgorithmOrder WRITE setTilingAlgorithmOrder NOTIFY
                   tilingAlgorithmOrderChanged)

    // Window filtering — the global knobs. The per-application /
    // per-class exclusion list Q_PROPERTYs (excludedApplications,
    // excludedWindowClasses) retired in v4 along with the standalone
    // Exclusions settings page; the lists folded into Rules and
    // the daemon serves the runtime evaluator from
    // PhosphorRules::ExclusionRules over the unified rule store.
    Q_PROPERTY(bool excludeTransientWindows READ excludeTransientWindows WRITE setExcludeTransientWindows NOTIFY
                   excludeTransientWindowsChanged)
    Q_PROPERTY(
        int minimumWindowWidth READ minimumWindowWidth WRITE setMinimumWindowWidth NOTIFY minimumWindowWidthChanged)
    Q_PROPERTY(
        int minimumWindowHeight READ minimumWindowHeight WRITE setMinimumWindowHeight NOTIFY minimumWindowHeightChanged)

    // Animation window filtering — separate group from snapping/tiling
    // exclusions so the two filter sets can diverge.
    Q_PROPERTY(bool animationExcludeTransientWindows READ animationExcludeTransientWindows WRITE
                   setAnimationExcludeTransientWindows NOTIFY animationExcludeTransientWindowsChanged)
    Q_PROPERTY(bool animationExcludeNotificationsAndOsd READ animationExcludeNotificationsAndOsd WRITE
                   setAnimationExcludeNotificationsAndOsd NOTIFY animationExcludeNotificationsAndOsdChanged)
    Q_PROPERTY(int animationMinimumWindowWidth READ animationMinimumWindowWidth WRITE setAnimationMinimumWindowWidth
                   NOTIFY animationMinimumWindowWidthChanged)
    Q_PROPERTY(int animationMinimumWindowHeight READ animationMinimumWindowHeight WRITE setAnimationMinimumWindowHeight
                   NOTIFY animationMinimumWindowHeightChanged)

    // Decoration window filtering — separate group from snapping/tiling and
    // animation filtering so the border pass can be tuned independently.
    Q_PROPERTY(bool decorationExcludeTransientWindows READ decorationExcludeTransientWindows WRITE
                   setDecorationExcludeTransientWindows NOTIFY decorationExcludeTransientWindowsChanged)
    Q_PROPERTY(int decorationMinimumWindowWidth READ decorationMinimumWindowWidth WRITE setDecorationMinimumWindowWidth
                   NOTIFY decorationMinimumWindowWidthChanged)
    Q_PROPERTY(int decorationMinimumWindowHeight READ decorationMinimumWindowHeight WRITE
                   setDecorationMinimumWindowHeight NOTIFY decorationMinimumWindowHeightChanged)
    // The animationExcludedApplications / animationExcludedWindowClasses
    // Q_PROPERTYs retired in v4 — the lists folded into `ExcludeAnimations`
    // Rules and the effect's `shouldAnimateWindow` gate now resolves
    // against the slice
    // `PhosphorRules::ExclusionRules::excludeAnimationsRulesFrom`
    // produces from the unified rule store.

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
    // Autotile inner/outer gaps are unified with snapping — the settings UI binds
    // the shared innerGap / outerGap* properties above for both modes. The
    // IAutotileSettings gap getters (autotileInnerGap(), …) forward to them.
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
    // JSON string facade so the shader tree participates in the standard
    // Q_PROPERTY → SettingsController meta-object loop → setNeedsSave
    // wiring used by every other settings page (avoids per-feature
    // dirty-tracking / notifyReload plumbing).
    Q_PROPERTY(QString shaderProfileTreeJson READ shaderProfileTreeJson WRITE setShaderProfileTreeJson NOTIFY
                   shaderProfileTreeChanged)
    // JSON string facade for the per-surface decoration tree — same
    // meta-object dirty-tracking rationale as shaderProfileTreeJson above.
    Q_PROPERTY(QString decorationProfileTreeJson READ decorationProfileTreeJson WRITE setDecorationProfileTreeJson
                   NOTIFY decorationProfileTreeChanged)

    // Decorations.Performance — bounds on WHEN the decoration chain animates.
    Q_PROPERTY(bool decorationAnimateFocusedOnly READ decorationAnimateFocusedOnly WRITE setDecorationAnimateFocusedOnly
                   NOTIFY decorationAnimateFocusedOnlyChanged)
    Q_PROPERTY(bool decorationPauseWhenIdle READ decorationPauseWhenIdle WRITE setDecorationPauseWhenIdle NOTIFY
                   decorationPauseWhenIdleChanged)
    Q_PROPERTY(int decorationIdleTimeoutSec READ decorationIdleTimeoutSec WRITE setDecorationIdleTimeoutSec NOTIFY
                   decorationIdleTimeoutSecChanged)

    // Autotile Behavior and Visual Settings
    Q_PROPERTY(bool autotileFocusFollowsMouse READ autotileFocusFollowsMouse WRITE setAutotileFocusFollowsMouse NOTIFY
                   autotileFocusFollowsMouseChanged)
    Q_PROPERTY(bool snappingFocusNewWindows READ snappingFocusNewWindows WRITE setSnappingFocusNewWindows NOTIFY
                   snappingFocusNewWindowsChanged)
    Q_PROPERTY(bool snappingFocusFollowsMouse READ snappingFocusFollowsMouse WRITE setSnappingFocusFollowsMouse NOTIFY
                   snappingFocusFollowsMouseChanged)
    Q_PROPERTY(bool autotileRespectMinimumSize READ autotileRespectMinimumSize WRITE setAutotileRespectMinimumSize
                   NOTIFY autotileRespectMinimumSizeChanged)
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
    Q_PROPERTY(int shaderFrameRate READ shaderFrameRate WRITE setShaderFrameRate NOTIFY shaderFrameRateChanged)
    Q_PROPERTY(bool enableAudioVisualizer READ enableAudioVisualizer WRITE setEnableAudioVisualizer NOTIFY
                   enableAudioVisualizerChanged)
    Q_PROPERTY(int audioSpectrumBarCount READ audioSpectrumBarCount WRITE setAudioSpectrumBarCount NOTIFY
                   audioSpectrumBarCountChanged)
    Q_PROPERTY(bool audioAutosens READ audioAutosens WRITE setAudioAutosens NOTIFY audioAutosensChanged)
    Q_PROPERTY(int audioSensitivity READ audioSensitivity WRITE setAudioSensitivity NOTIFY audioSensitivityChanged)
    Q_PROPERTY(
        int audioNoiseReduction READ audioNoiseReduction WRITE setAudioNoiseReduction NOTIFY audioNoiseReductionChanged)
    Q_PROPERTY(
        int audioLowerCutoffHz READ audioLowerCutoffHz WRITE setAudioLowerCutoffHz NOTIFY audioLowerCutoffHzChanged)
    Q_PROPERTY(
        int audioHigherCutoffHz READ audioHigherCutoffHz WRITE setAudioHigherCutoffHz NOTIFY audioHigherCutoffHzChanged)
    Q_PROPERTY(bool audioMonstercat READ audioMonstercat WRITE setAudioMonstercat NOTIFY audioMonstercatChanged)
    Q_PROPERTY(bool audioWaves READ audioWaves WRITE setAudioWaves NOTIFY audioWavesChanged)
    Q_PROPERTY(QString audioChannelMode READ audioChannelMode WRITE setAudioChannelMode NOTIFY audioChannelModeChanged)
    Q_PROPERTY(bool audioReverse READ audioReverse WRITE setAudioReverse NOTIFY audioReverseChanged)
    Q_PROPERTY(
        int audioExtraSmoothing READ audioExtraSmoothing WRITE setAudioExtraSmoothing NOTIFY audioExtraSmoothingChanged)
    Q_PROPERTY(QString audioInputMethod READ audioInputMethod WRITE setAudioInputMethod NOTIFY audioInputMethodChanged)
    Q_PROPERTY(QString audioInputSource READ audioInputSource WRITE setAudioInputSource NOTIFY audioInputSourceChanged)

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

    // Virtual Screen Swap / Rotate (Meta+Ctrl+Alt+Shift+Arrow, Meta+Ctrl+Alt+[/]
    // — the Alt is mandatory: the Alt-less chords are KWin built-ins, see
    // configdefaults.h)
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

    /**
     * @brief Standalone ctor that owns its config backend but BORROWS a
     *        RuleStore.
     *
     * Same standalone semantics as Settings(QObject*) — owns a freshly migrated
     * config backend and leaves the CurveRegistry null (animation profiles parse
     * through the process-static fallback) — but takes its RuleStore from
     * the caller instead of owning one. The settings app uses this so a single
     * store is shared between this Settings instance (per-mode monitor disable
     * lists, which persist in rules.json) and the SettingsController's
     * in-process LayoutRegistry, eliminating the divergence two independent
     * stores over the same file would otherwise allow.
     *
     * @param ruleStore Borrowed store; must outlive this Settings. A null
     *        argument degrades to owning a store (same fallback the
     *        backend-injecting ctor uses) so a misuse still yields a working
     *        object.
     * @param parent Parent QObject.
     */
    Settings(PhosphorRules::RuleStore* ruleStore, QObject* parent);
    ~Settings() override = default;

    // No singleton - use dependency injection instead

    // Canonical storage-key form of a screen identifier: resolve a connector
    // name (e.g. "DP-2") to its stable EDID id (e.g. "Dell:U2722D:115107"),
    // preserving any virtual "/vs:N" suffix. Identifiers already in id form, and
    // connectors that don't currently resolve to a connected screen, pass
    // through unchanged. Per-screen overrides (gaps, autotile) are keyed by this
    // stable form so reads/writes/migration agree across connector renumbering.
    static QString canonicalPerScreenKey(const QString& screenIdOrName);

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
    bool zoneSpanToggleMode() const override;
    void setZoneSpanToggleMode(bool enable) override;
    bool toggleActivation() const override;
    void setToggleActivation(bool enable) override;
    bool snappingEnabled() const override;
    void setSnappingEnabled(bool enabled) override;

    // Display — PhosphorConfig::Store-backed.
    bool showZonesOnAllMonitors() const override;
    void setShowZonesOnAllMonitors(bool show) override;
    QStringList disabledMonitors(PhosphorZones::AssignmentEntry::Mode mode) const override;
    void setDisabledMonitors(PhosphorZones::AssignmentEntry::Mode mode, const QStringList& screenIdOrNames) override;
    bool isMonitorDisabled(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenIdOrName) const override;
    QStringList disabledDesktops(PhosphorZones::AssignmentEntry::Mode mode) const override;
    void setDisabledDesktops(PhosphorZones::AssignmentEntry::Mode mode, const QStringList& entries) override;
    bool isDesktopDisabled(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenIdOrName,
                           int desktop) const override;
    QStringList disabledActivities(PhosphorZones::AssignmentEntry::Mode mode) const override;
    void setDisabledActivities(PhosphorZones::AssignmentEntry::Mode mode, const QStringList& entries) override;
    bool isActivityDisabled(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenIdOrName,
                            const QString& activityId) const override;
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

    // Zone geometry (shared inner/outer gaps) — PhosphorConfig::Store-backed
    // (the "Gaps" group). The autotile* gap forwarders above route through these
    // same getters, so the tile engine resolves the same values as snapping.
    int innerGap() const override;
    void setInnerGap(int gap) override;
    int outerGap() const override;
    void setOuterGap(int gap) override;
    bool usePerSideOuterGap() const override;
    void setUsePerSideOuterGap(bool usePerSide) override;
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

    // Window decoration appearance (tiled/snapped window border + title bar) —
    // PhosphorConfig::Store-backed (the "Windows" group).
    bool showWindowBorder() const override;
    void setShowWindowBorder(bool show) override;
    QString windowBorderScope() const override;
    void setWindowBorderScope(const QString& scope) override;
    int windowBorderWidth() const override;
    void setWindowBorderWidth(int width) override;
    int windowBorderRadius() const override;
    void setWindowBorderRadius(int radius) override;
    QString windowBorderColorActive() const override;
    void setWindowBorderColorActive(const QString& color) override;
    QString windowBorderColorInactive() const override;
    void setWindowBorderColorInactive(const QString& color) override;
    bool hideWindowTitleBars() const override;
    void setHideWindowTitleBars(bool hide) override;
    QString windowTitleBarScope() const override;
    void setWindowTitleBarScope(const QString& scope) override;
    int focusFadeDuration() const override;
    void setFocusFadeDuration(int ms) override;
    // Plain opacity+tint layer (same "Windows" group).
    bool showWindowOpacityTint() const override;
    void setShowWindowOpacityTint(bool show) override;
    QString windowOpacityTintScope() const override;
    void setWindowOpacityTintScope(const QString& scope) override;
    double windowOpacity() const override;
    void setWindowOpacity(double opacity) override;
    double windowTintStrength() const override;
    void setWindowTintStrength(double strength) override;
    QString windowTintColor() const override;
    void setWindowTintColor(const QString& color) override;

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
    // ISnapSettings::focusNewWindows() — delegates to the Snapping.Behavior store
    // value (snappingFocusNewWindows). The snap engine reads this on AutoRestored
    // commits to focus auto-placed-on-open windows.
    bool focusNewWindows() const override;
    // ISnapSettings::unfloatFallbackToZone() — bridges to the Snapping.Behavior
    // store value (snapUnfloatFallbackToZone). The snap engine reads this in
    // unfloatToZone to decide whether a no-pre-float-zone unfloat snaps to a
    // fallback zone or stays floating.
    bool unfloatFallbackToZone() const override;
    void setMoveNewWindowsToLastZone(bool move) override;
    bool restoreOriginalSizeOnUnsnap() const override;
    void setRestoreOriginalSizeOnUnsnap(bool restore) override;
    StickyWindowHandling snappingStickyWindowHandling() const override;
    void setSnappingStickyWindowHandling(StickyWindowHandling handling) override;
    int snappingStickyWindowHandlingInt() const;
    void setSnappingStickyWindowHandlingInt(int handling);
    bool restoreWindowsToZonesOnLogin() const override;
    void setRestoreWindowsToZonesOnLogin(bool restore) override;
    bool snappingRestoreFloatedWindowsOnLogin() const override;
    void setSnappingRestoreFloatedWindowsOnLogin(bool restore) override;
    bool autotileRestoreFloatedWindowsOnLogin() const override;
    void setAutotileRestoreFloatedWindowsOnLogin(bool restore) override;
    bool snapUnfloatFallbackToZone() const override;
    void setSnapUnfloatFallbackToZone(bool enabled) override;
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
    bool suppressDefaultLayoutAssignment() const override;
    void setSuppressDefaultLayoutAssignment(bool suppress) override;

    bool filterLayoutsByAspectRatio() const override;
    void setFilterLayoutsByAspectRatio(bool filter) override;

    // Ordering — PhosphorConfig::Store-backed (see settingsschema.cpp).
    // Wire format is a comma-joined QString; the canonicalCommaList
    // validator trims/dedups on every read and write.
    QStringList snappingLayoutOrder() const override;
    void setSnappingLayoutOrder(const QStringList& order) override;
    QStringList tilingAlgorithmOrder() const override;
    void setTilingAlgorithmOrder(const QStringList& order) override;

    // Window filtering — PhosphorConfig::Store-backed. The per-app /
    // per-class exclusion list accessors retired in v4 — see the
    // Q_PROPERTY block above for the migration notes.
    bool excludeTransientWindows() const override;
    void setExcludeTransientWindows(bool exclude) override;
    int minimumWindowWidth() const override;
    void setMinimumWindowWidth(int width) override;
    int minimumWindowHeight() const override;
    void setMinimumWindowHeight(int height) override;

    // Animation window filtering — same shape as snapping/tiling
    // exclusions but stored under `Animations.WindowFiltering`.
    bool animationExcludeTransientWindows() const override;
    void setAnimationExcludeTransientWindows(bool exclude) override;
    bool animationExcludeNotificationsAndOsd() const override;
    void setAnimationExcludeNotificationsAndOsd(bool exclude) override;
    int animationMinimumWindowWidth() const override;
    void setAnimationMinimumWindowWidth(int width) override;
    int animationMinimumWindowHeight() const override;
    void setAnimationMinimumWindowHeight(int height) override;

    // Decoration window filtering — same shape as the animation filter but
    // stored under `Decorations.WindowFiltering`.
    bool decorationExcludeTransientWindows() const override;
    void setDecorationExcludeTransientWindows(bool exclude) override;
    int decorationMinimumWindowWidth() const override;
    void setDecorationMinimumWindowWidth(int width) override;
    int decorationMinimumWindowHeight() const override;
    void setDecorationMinimumWindowHeight(int height) override;
    // animationExcludedApplications / animationExcludedWindowClasses
    // (+ their add*/remove* convenience methods) retired in v4 — see the
    // Q_PROPERTY block above for the migration notes.

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

    // Per-screen autotile config (override > global fallback)
    Q_INVOKABLE QVariantMap getPerScreenAutotileSettings(const QString& screenIdOrName) const override;
    Q_INVOKABLE void setPerScreenAutotileSetting(const QString& screenIdOrName, const QString& key,
                                                 const QVariant& value) override;
    Q_INVOKABLE void clearPerScreenAutotileSettings(const QString& screenIdOrName) override;
    Q_INVOKABLE bool hasPerScreenAutotileSettings(const QString& screenIdOrName) const override;
    // Algorithm sub-domain of the shared per-screen autotile map: the Algorithm
    // card reports/clears only its own keys (the complement of the gap dimensions
    // and SmartGaps) so its reset never wipes another card's per-monitor overrides.
    bool hasPerScreenAutotileAlgorithmSettings(const QString& screenIdOrName) const;
    void clearPerScreenAutotileAlgorithmSettings(const QString& screenIdOrName);

    // Per-screen snapping gaps project the config-backed per-monitor gap
    // overrides (perScreenGapOverrides) — the geometry path only reads them, so
    // this is the sole accessor; writes go through setPerScreenAutotileSetting /
    // the perScreenGap* helpers, and the ISettings set/clear/has snapping triplet
    // stays as no-op defaults.
    Q_INVOKABLE QVariantMap getPerScreenSnappingSettings(const QString& screenIdOrName) const override;

    // Per-monitor gap overrides (config-backed, unified — one value per monitor
    // drives both snap and tile). Stored in the per-screen autotile store; these
    // accessors expose just the gap-dimension sub-domain. perScreenGapOverrides
    // returns the override keyed in the short engine form (InnerGap / OuterGap /
    // UsePerSideOuterGap / OuterGap{Top,Bottom,Left,Right}) — the SAME key strings
    // contextGapOverrideMap produces, so the geometry cascade merge is a plain
    // QVariantMap union. Empty map when the screen has no gap override. Writes go
    // through setPerScreenAutotileSetting.
    QVariantMap perScreenGapOverrides(const QString& screenIdOrName) const override;
    bool hasPerScreenGapOverride(const QString& screenIdOrName) const;
    void clearPerScreenGapOverride(const QString& screenIdOrName);

    /// True iff the gap-dimension subset of the per-screen store differs between
    /// @p before and @p after. load() uses it to fire perScreenSnappingSettingsChanged
    /// (the daemon's gap-resnap trigger for already-snapped windows) on a per-monitor
    /// gap change, without over-firing on an algorithm/split-only per-screen change.
    static bool perScreenGapDimensionsDiffer(const QHash<QString, QVariantMap>& before,
                                             const QHash<QString, QVariantMap>& after);

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
    // Autotile gap getters forward to the shared inner/outer gap model so tiling
    // and snapping always resolve the same values. There are no autotile-specific
    // gap setters: the settings UI writes the shared setInnerGap()/setOuterGap*().
    int autotileInnerGap() const override
    {
        return innerGap();
    }
    int autotileOuterGap() const override
    {
        return outerGap();
    }
    bool autotileUsePerSideOuterGap() const override
    {
        return usePerSideOuterGap();
    }
    int autotileOuterGapTop() const override
    {
        return outerGapTop();
    }
    int autotileOuterGapBottom() const override
    {
        return outerGapBottom();
    }
    int autotileOuterGapLeft() const override
    {
        return outerGapLeft();
    }
    int autotileOuterGapRight() const override
    {
        return outerGapRight();
    }
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

    PhosphorAnimationShaders::ShaderProfileTree shaderProfileTree() const override;
    void setShaderProfileTree(const PhosphorAnimationShaders::ShaderProfileTree& tree) override;

    /// The committed-baseline shader tree. Reads the ShaderProfileTree key from
    /// the same committed baseline `isKeyModified()` uses, and applies the
    /// identical supported-path prune as `shaderProfileTree()` so a
    /// per-surface Discard/dirty check compares live-vs-committed on equal
    /// footing. See `committedDecorationProfileTree()` for the sibling rationale.
    PhosphorAnimationShaders::ShaderProfileTree committedShaderProfileTree() const override;

    /// String facade for the shaderProfileTreeJson Q_PROPERTY. Routes
    /// through the existing tree accessors so persistence is identical;
    /// the Q_PROPERTY entry is purely so the meta-object dirty-tracking
    /// loop in SettingsController catches it.
    QString shaderProfileTreeJson() const;
    void setShaderProfileTreeJson(const QString& json);

    // Per-surface decoration tree (DecorationProfile: shader-pack chain + its
    // per-pack parameters), persisted under the Decorations group. Typed accessors
    // mirror shaderProfileTree; the JSON-string facade backs the Q_PROPERTY
    // above.
    PhosphorSurfaceShaders::DecorationProfileTree decorationProfileTree() const override;
    void setDecorationProfileTree(const PhosphorSurfaceShaders::DecorationProfileTree& tree) override;
    QString decorationProfileTreeJson() const override;
    void setDecorationProfileTreeJson(const QString& json) override;

    /// The committed-baseline decoration tree — the last-persisted value that
    /// per-page Discard reverts to and the per-surface decoration dirty check
    /// compares against. Reads the DecorationProfileTree key from the same
    /// committed baseline `isKeyModified()` uses, with the identical
    /// empty→ConfigDefaults fallback as `decorationProfileTree()`, so a
    /// subtree-scoped Discard sees baseline-vs-current on equal footing. Not on
    /// ISettings: the committed baseline is a Settings-internal dirty-tracking
    /// concept, and only SettingsController's per-page kebab consumes it.
    PhosphorSurfaceShaders::DecorationProfileTree committedDecorationProfileTree() const;

    // Decorations.Performance — PhosphorConfig::Store-backed.
    bool decorationAnimateFocusedOnly() const override;
    void setDecorationAnimateFocusedOnly(bool value) override;
    bool decorationPauseWhenIdle() const override;
    void setDecorationPauseWhenIdle(bool value) override;
    int decorationIdleTimeoutSec() const override;
    void setDecorationIdleTimeoutSec(int value) override;

    // Additional Autotiling Settings — PhosphorConfig::Store-backed.
    bool autotileFocusFollowsMouse() const override;
    void setAutotileFocusFollowsMouse(bool focus) override;
    bool snappingFocusNewWindows() const override;
    void setSnappingFocusNewWindows(bool focus) override;
    bool snappingFocusFollowsMouse() const override;
    void setSnappingFocusFollowsMouse(bool focus) override;
    bool autotileRespectMinimumSize() const override;
    void setAutotileRespectMinimumSize(bool respect);
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
    QHash<QString, PhosphorScreens::VirtualScreenConfig> virtualScreenConfigs() const;
    void setVirtualScreenConfigs(const QHash<QString, PhosphorScreens::VirtualScreenConfig>& configs);
    /// Returns true on success, false if the config was rejected by
    /// PhosphorScreens::VirtualScreenConfig::isValid (or empty physicalScreenId). An
    /// already-current value is treated as a successful no-op.
    bool setVirtualScreenConfig(const QString& physicalScreenId, const PhosphorScreens::VirtualScreenConfig& config);
    PhosphorScreens::VirtualScreenConfig virtualScreenConfig(const QString& physicalScreenId) const;

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
    // value is coerced + persisted in memory on the same call, with save()
    // flushing to disk.
    int shaderFrameRate() const override;
    void setShaderFrameRate(int fps) override;
    bool enableAudioVisualizer() const override;
    void setEnableAudioVisualizer(bool enable) override;
    int audioSpectrumBarCount() const override;
    void setAudioSpectrumBarCount(int count) override;
    bool audioAutosens() const override;
    void setAudioAutosens(bool enable) override;
    int audioSensitivity() const override;
    void setAudioSensitivity(int percent) override;
    int audioNoiseReduction() const override;
    void setAudioNoiseReduction(int value) override;
    int audioLowerCutoffHz() const override;
    void setAudioLowerCutoffHz(int hz) override;
    int audioHigherCutoffHz() const override;
    void setAudioHigherCutoffHz(int hz) override;
    bool audioMonstercat() const override;
    void setAudioMonstercat(bool enable) override;
    bool audioWaves() const override;
    void setAudioWaves(bool enable) override;
    QString audioChannelMode() const override;
    void setAudioChannelMode(const QString& mode) override;
    bool audioReverse() const override;
    void setAudioReverse(bool enable) override;
    int audioExtraSmoothing() const override;
    void setAudioExtraSmoothing(int percent) override;
    QString audioInputMethod() const override;
    void setAudioInputMethod(const QString& method) override;
    QString audioInputSource() const override;
    void setAudioInputSource(const QString& source) override;

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
    QString editorDuplicateShortcut() const override;
    void setEditorDuplicateShortcut(const QString& shortcut) override;
    QString editorSplitHorizontalShortcut() const override;
    void setEditorSplitHorizontalShortcut(const QString& shortcut) override;
    QString editorSplitVerticalShortcut() const override;
    void setEditorSplitVerticalShortcut(const QString& shortcut) override;
    QString editorFillShortcut() const override;
    void setEditorFillShortcut(const QString& shortcut) override;
    bool editorGridSnappingEnabled() const override;
    void setEditorGridSnappingEnabled(bool enabled) override;
    bool editorEdgeSnappingEnabled() const override;
    void setEditorEdgeSnappingEnabled(bool enabled) override;
    qreal editorSnapIntervalX() const override;
    void setEditorSnapIntervalX(qreal interval) override;
    qreal editorSnapIntervalY() const override;
    void setEditorSnapIntervalY(qreal interval) override;
    int editorSnapOverrideModifier() const override;
    void setEditorSnapOverrideModifier(int mod) override;
    bool fillOnDropEnabled() const override;
    void setFillOnDropEnabled(bool enabled) override;
    int fillOnDropModifier() const override;
    void setFillOnDropModifier(int mod) override;

    // Persistence
    void load() override;
    void save() override;
    void reset() override;

    /// Write the current settings to @p filePath as a standalone config file,
    /// without touching the live config or the per-page Discard baseline.
    ///
    /// This is deliberately not `save()`-then-copy. `save()` commits to
    /// ~/.config and re-baselines, so exporting would persist edits the user
    /// never chose to save and leave a later Discard with nothing to revert
    /// to. The values reaching the file are the same ones: Store-backed groups
    /// already write through to the backend's in-memory root on every setter,
    /// so a snapshot of it carries pending edits whether or not Save was
    /// pressed.
    ///
    /// The per-screen override and virtual-screen helpers can only stage into
    /// the live root, so exportTo snapshots the root (and the backend's dirty
    /// flag) before staging and restores both after taking the export
    /// snapshot. That snapshot/restore is what keeps the export side-effect
    /// free: without it the staged groups would sit in the live root and be
    /// committed by a later sync() or the backend's flush-on-destruction.
    ///
    /// Returns false if the write fails, or if the backend is not JSON-backed
    /// (exports are config.json documents).
    bool exportTo(const QString& filePath);

    // ── Per-page reset / discard support (settings app) ─────────────────────
    // A config key addressed as (group, key). A page "owns" a list of these;
    // SettingsController holds the page→keys manifest and drives the two
    // mutators below for the active page.
    using ConfigKey = QPair<QString, QString>;
    using ConfigKeyList = QList<ConfigKey>;

    /// True when the current in-memory value of (@p group, @p key) differs
    /// from the committed baseline — i.e. the key carries an unsaved edit.
    bool isKeyModified(const QString& group, const QString& key) const;

    /// Per-page Discard: revert each key to the committed baseline. Bracketed
    /// by the same Q_PROPERTY NOTIFY re-emit as load() so QML bindings update,
    /// and emits settingsChanged() once if anything actually changed.
    ///
    /// SCOPE: re-emits only Q_PROPERTY NOTIFY signals (+ settingsChanged). It
    /// does NOT re-emit the non-Q_PROPERTY change signals load() also fires —
    /// the per-mode disable-list signals (disabled*Changed) and the per-screen
    /// signals (perScreen*SettingsChanged). This is correct for the current
    /// per-page manifest (all Q_PROPERTY-backed keys); if a future page ever
    /// owns a disable-list or per-screen key, extend this to re-emit those.
    void discardKeys(const ConfigKeyList& keys);

    /// Per-page Reset: set each key to its schema default. Same NOTIFY /
    /// settingsChanged() re-emit contract — and the same scope limit — as
    /// discardKeys().
    void resetKeys(const ConfigKeyList& keys);

    // Additional methods
    Q_INVOKABLE QString loadColorsFromFile(const QString& filePath) override;
    /// Derives the four zone-color keys from the current application palette.
    /// Deliberately NOT Q_INVOKABLE: there is no QML caller, and an ad-hoc
    /// QML invocation would write the derived keys without the baseline /
    /// dirty-tracking bookkeeping its three C++ call sites provide (see the
    /// ownership rule at rebaselineDerivedColorKeys()).
    void applySystemColorScheme();

    /// Re-derives the system-scheme zone colors when the application palette
    /// changes at runtime (theme switch). Without this, every long-running
    /// process (daemon, settings app) keeps the palette SNAPSHOT taken at
    /// load() and serves stale zone colors until restart.
    bool eventFilter(QObject* watched, QEvent* event) override;

    /// True while eventFilter() is re-deriving the zone colors from a runtime
    /// ApplicationPaletteChange. The re-derive is palette-driven, not a user
    /// edit — SettingsController::onSettingsPropertyChanged() checks this to
    /// avoid flipping the global unsaved-changes footer on a theme switch
    /// (the baseline rebaseline alone keeps isKeyModified() honest, but the
    /// controller's NOTIFY-driven dirty flag fires before any value check).
    bool isApplyingSystemPalette() const
    {
        return m_applyingSystemPalette;
    }

Q_SIGNALS:
    /// Emitted when the whole animation Profile blob is replaced via
    /// `setAnimationProfile`. Fires alongside every per-field *Changed
    /// signal. Consumers that want to observe the Profile atomically
    /// (e.g., daemon's WindowAnimator re-configuring its MotionSpec
    /// defaults) prefer this signal; Q_PROPERTY consumers bound to the
    /// per-field surface get re-triggered through the individual
    /// signals per the existing NOTIFY wiring.
    void animationProfileChanged();

    // NOTE: do not redeclare signals already on ISettings here.
    // Re-declaring a base-class Q_SIGNAL produces a second moc index
    // and `connect(s, &ISettings::xChanged, ...)` then misses the
    // unqualified `Q_EMIT xChanged()` (which resolves to the derived
    // signal). All editor / fillOnDrop / filterLayoutsByAspectRatio /
    // virtualScreenConfigs signals live on ISettings and are inherited
    // here — see src/core/isettings.h.

private:
    /// Installs the QEvent::ApplicationPaletteChange filter on the application
    /// object (see eventFilter above). Called once per constructor, after load().
    void trackSystemPaletteChanges();

    /// Member-function-pointer alias used by the indexed shortcut setters
    /// (quickLayoutShortcut / snapToZoneShortcut) when fanning out to the
    /// per-index NOTIFY signal.
    using ShortcutSignalFn = void (Settings::*)();

    /// Member-function-pointer alias for the per-trigger-list NOTIFY signal
    /// passed into @ref writeTriggerList.
    using TriggerListSignalFn = void (Settings::*)();

    /// Shared trigger-list setter used by the three "plain" setters
    /// (activation, snap-assist, autotile-insert). Caps at
    /// @c MaxTriggersPerAction, round-trips through the schema's validator,
    /// and only emits @p specificSignal + @c settingsChanged on a real change.
    /// @c setZoneSpanTriggers does its own dance because it also synchronises
    /// the legacy single-modifier key.
    void writeTriggerList(const QString& group, const QString& key, const QVariantList& triggers,
                          TriggerListSignalFn specificSignal);

    /// Member-function-pointer alias for the three per-mode disable NOTIFY
    /// signals passed into @ref writeDisableEntries. The signals carry the mode
    /// that flipped so listeners only react to their own axis.
    using DisableModeSignalFn = void (Settings::*)(PhosphorZones::AssignmentEntry::Mode);

    /// Shared writer for the three per-mode disable lists. Replaces the whole
    /// `DisableEngine` context-rule family for (@p mode, @p axisInt) in the
    /// Rule store — @p axisInt is a `DisableAxis` value (file-local enum
    /// in settings.cpp; passed as an int so the header stays decoupled). Drops
    /// malformed composite entries, fires @p signalFn + @c settingsChanged
    /// only on a real (canonical-set) change.
    void writeDisableEntries(PhosphorZones::AssignmentEntry::Mode mode, int axisInt, const QStringList& entries,
                             DisableModeSignalFn signalFn);

    /// Shared reader for the three per-mode disable lists. Enumerates the
    /// `DisableEngine` context rules in the Rule store scoped to
    /// @p mode and @p axisInt (a `DisableAxis` value), projecting each rule's
    /// pinned context dimensions back to its list-entry string form. The
    /// monitor variant's connector-name → canonical-id resolution lives in
    /// @ref disabledMonitors itself, not here.
    QStringList disableEntriesFor(PhosphorZones::AssignmentEntry::Mode mode, int axisInt) const;

    // ─── load() / save() helpers ────────────────────────────────────────
    // Only non-Store groups need dedicated helpers now. Store-backed groups
    // (Activation/Display/ZoneGeometry/Behavior/ZoneSelector/Shortcut/
    // Autotiling/Editor/Appearance/Rendering/Shaders/Ordering) persist via
    // setters and flush via m_configBackend->commit() in save().
    void loadPerScreenOverrides(PhosphorConfig::IBackend* backend);
    void loadVirtualScreenConfigs(PhosphorConfig::IBackend* backend);
    void saveAllPerScreenOverrides(PhosphorConfig::IBackend* backend);
    void saveVirtualScreenConfigs(PhosphorConfig::IBackend* backend);

    // ── NOTIFY re-emit machinery (shared by load / discardKeys / resetKeys) ──
    // load() and the per-page mutators write backing state directly (bypassing
    // the property setters), so they must re-emit NOTIFY by hand or QML
    // bindings never refresh. snapshotNotifyProperties() captures every own
    // NOTIFY-able Q_PROPERTY value (index-aligned to the metaobject) BEFORE the
    // mutation; emitChangedNotifyProperties() fires the NOTIFY of each property
    // whose value changed and returns whether any fired.
    QVector<QVariant> snapshotNotifyProperties() const;
    bool emitChangedNotifyProperties(const QVector<QVariant>& before);

    // Refresh the committed baseline — the last-persisted value of every
    // schema-declared key. Called at the end of load() and save() (the only
    // points where the in-memory store equals disk); discardKeys() reverts to
    // this baseline and isKeyModified() compares against it. Private: mutating
    // the baseline anywhere but a load/save commit point desyncs dirty tracking.
    // ONE narrow exception: rebaselineDerivedColorKeys() below refreshes just
    // the four palette-derived zone-color entries after a runtime palette
    // re-derive — those keys are palette-owned, never user edits.
    void captureBaseline();

    // Refresh the baseline for ONLY the four palette-derived zone-color keys
    // after a runtime re-derive. System-colors mode owns these keys: they
    // follow the palette and are never user edits, so a theme switch must not
    // flip isKeyModified() (phantom unsaved-changes footer) or arm Discard
    // with the stale pre-switch colors. The one legitimate caller is the
    // ApplicationPaletteChange path in eventFilter(); see the definition for
    // why the setUseSystemColors() and load() paths must NOT call it.
    void rebaselineDerivedColorKeys();

    // Groups that reset() deletes exhaustively (excludes unmanaged groups like
    // Updates). NOT used by save() — save() iterates the schema and lets
    // purgeStaleKeys() handle cleanup.
    static QStringList managedGroupNames();
    // Delete all per-screen override groups by prefix (ZoneSelector:*,
    // AutotileScreen:*, and the legacy SnappingScreen:* which is no longer written
    // but is still swept to scrub any file an older build left behind).
    static void deletePerScreenGroups(PhosphorConfig::IBackend* backend);
    // Purge stale keys from all managed groups before save() rewrites them.
    void purgeStaleKeys();

    /// Reparse the backend from disk IF it holds no pending writes.
    /// Called by every composite-value setter (animation Profile blob,
    /// shader/decoration profile trees, autotile per-algorithm map,
    /// snapping trigger lists including the zoneSpan pair) before its
    /// stale-sensitive read; see the definition for the cross-process
    /// coherence rationale.
    void refreshCleanBackendFromDisk();

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

    // Committed baseline: the last-persisted value of every schema-declared
    // key, keyed group → {key → value}. Refreshed by captureBaseline() at the
    // end of load() and save() — plus one targeted exception: eventFilter()'s
    // ApplicationPaletteChange path calls rebaselineDerivedColorKeys() to
    // refresh ONLY the four palette-derived zone-color entries, so a runtime
    // theme switch doesn't read as an unsaved edit. Backs per-page Discard
    // (revert to baseline) and value-based per-page dirty checks
    // (isKeyModified).
    QHash<QString, QVariantMap> m_baseline;

    // Raised (RAII, via QScopedValueRollback) around eventFilter()'s runtime
    // palette re-derive; surfaced through isApplyingSystemPalette() so
    // NOTIFY-driven dirty tracking can tell a palette-driven refresh from a
    // user edit. Never true outside that synchronous window.
    bool m_applyingSystemPalette = false;

    // Raised (RAII, via QScopedValueRollback) around the two batched
    // applySystemColorScheme() call sites: load() and eventFilter()'s runtime
    // palette re-derive. The derive routes through the public color setters,
    // whose per-setter NOTIFY + settingsChanged emissions would duplicate the
    // caller's own snapshot-based announcement (each changed NOTIFY exactly
    // once plus a single settingsChanged). While true, the color setters
    // persist silently and the caller remains the sole announcer. Never true
    // outside those synchronous windows — setUseSystemColors relies on the
    // setters emitting normally.
    bool m_suppressDerivedColorEmissions = false;

    static QString normalizeUuidString(const QString& uuidStr);

    // Per-mode disable lists are stored as `DisableEngine` context rules in
    // the unified Rule store (rules.json), NOT in config.json.
    //
    // The store can be either owned (standalone settings app / editor / tests
    // that have no daemon counterpart) or borrowed (daemon process — the
    // daemon owns the canonical store for `LayoutRegistry` and
    // `RuleAdaptor`, and passes that same instance in so every
    // in-process writer mutates the same in-memory ruleset). Mirroring the
    // existing `LayoutRegistry`-via-borrowed-pointer pattern eliminates the
    // dual-store race where two stores pointed at the same file each rebuild
    // a `kept` list from a stale snapshot and clobber each other's writes.
    //
    // The owning ctor calls `load()` on the owned store; the borrowing ctor
    // does not — the owner is responsible for having loaded already.
    // load() reloads the active store from disk so cross-process deltas
    // surface; the disabled*/setDisabled*/is*Disabled accessors read/write
    // through `m_ruleStore` (a raw pointer that always tracks the
    // active store — owned or borrowed).
    std::unique_ptr<PhosphorRules::RuleStore> m_ownedRuleStore;
    PhosphorRules::RuleStore* m_ruleStore = nullptr;

    // Connect the active rule store's rulesChanged to onRuleStoreChanged and
    // seed the per-screen gap fingerprint. Called once at the end of every
    // constructor (after load()), so both the owned and borrowed store paths get
    // the reactive wiring.
    void connectRuleStoreGapReactivity();

    // React to a rule-store change. The GLOBAL inner/outer gaps and the
    // per-monitor gap overrides are config-backed, so their NOTIFY is owned by the
    // setters / load(), not this handler. What remains rule-backed is the CONTEXT
    // gap cascade (per-mode / desktop / activity gap Rules), so this re-syncs the
    // per-screen consumers — but only when a gap action somewhere in the rule set
    // actually changed, so a non-gap rule write (mode/assignment toggle) doesn't
    // spuriously fire settingsChanged. Bound to the store's rulesChanged signal.
    void onRuleStoreChanged();

    // Fingerprint of every gap action across the rule set, used to gate the
    // per-screen gap re-sync in onRuleStoreChanged so only real gap edits fire it.
    QString gapRulesFingerprint() const;

    // Snapshot of all gap actions across the rule set; gates the per-screen gap
    // re-sync in onRuleStoreChanged so non-gap rule writes (mode/assignment
    // toggles) don't fire it. Seeded in connectRuleStoreGapReactivity.
    QString m_cachedGapFingerprint;

    // Activation
    // Activation is stored in m_store.

    // Display
    // Display is stored in m_store; no cached members here.

    // Appearance
    // Appearance + Labels + Opacity + Border are stored in m_store; no
    // cached members here.

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

    // Virtual screen configurations (physicalScreenId -> config)
    QHash<QString, PhosphorScreens::VirtualScreenConfig> m_virtualScreenConfigs;

    // Per-screen zone selector overrides (screenIdOrName -> settings map)
    QHash<QString, QVariantMap> m_perScreenZoneSelectorSettings;

    // Per-screen autotile overrides (screenIdOrName -> settings map)
    QHash<QString, QVariantMap> m_perScreenAutotileSettings;

    // Per-monitor gaps are unified (one value per monitor drives both snap and
    // tile) and live in the map above; the gap-dimension sub-domain is projected
    // by perScreenGapOverrides and surfaced as getPerScreenSnappingSettings. There
    // is no separate per-screen snapping store.

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
