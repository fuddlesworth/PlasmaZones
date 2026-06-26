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
 *
 * Note on the sub-interface NOTIFY surface: the sub-interfaces
 * (IZoneActivationSettings, IZoneSelectorSettings, etc.) are
 * deliberately non-QObject and so cannot declare Q_SIGNALS of their
 * own. All notify signals live on this ISettings level. The codebase
 * idiom is for a consumer that needs both the value AND the signal to
 * hold BOTH pointers — `IZoneSelectorSettings*` for reads/writes,
 * `ISettings*` (or `QObject*`) for `connect()`:
 *
 * @code
 * class Consumer {
 * public:
 *     Consumer(ISettings* settings)
 *         : m_settings(settings),               // for connect()
 *           m_selector(settings)                // for value access
 *     {
 *         connect(m_settings, &ISettings::zoneSelectorEnabledChanged,
 *                 this, &Consumer::onChanged);
 *     }
 * private:
 *     ISettings* m_settings;
 *     IZoneSelectorSettings* m_selector;
 * };
 * @endcode
 *
 * A `dynamic_cast<ISettings*>(zoneSelectorSettingsPtr)` also works at the
 * call site since `Settings` (the only concrete subclass) inherits from both
 * bases, but holding both pointers from construction is cheaper and clearer.
 */
class PLASMAZONES_EXPORT ISettings : public QObject,
                                     public IZoneActivationSettings,
                                     public IZoneVisualizationSettings,
                                     public IZoneGeometrySettings,
                                     public IWindowExclusionSettings,
                                     public IZoneSelectorSettings,
                                     public IWindowBehaviorSettings,
                                     public IDefaultLayoutSettings,
                                     public IOrderingSettings,
                                     public IAnimationSettings
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
    ///
    /// Test-stub note: `StubSettings` (tests/unit/helpers/StubSettings.h)
    /// defaults `snappingEnabled() == true` and `zoneSelectorEnabled() == true`,
    /// so this returns true unless a test explicitly overrides one of the
    /// two flags. The same applies to `isSnapAssistActive` (defaults
    /// `snapAssistEnabled() == false` so it returns false until overridden).
    bool isZoneSelectorActive() const
    {
        return snappingEnabled() && zoneSelectorEnabled();
    }

    /// True only when snap assist is enabled AND snapping is enabled at
    /// the top level. Same pattern as isZoneSelectorActive (see that
    /// method's doc comment for the StubSettings default semantics).
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
    //   - IAnimationSettings: animation/shader-profile state + window filtering
    //
    // See settings_interfaces.h for the full API.
    // ═══════════════════════════════════════════════════════════════════════════

    // Animation + shader-profile settings are segregated into
    // IAnimationSettings (mixed in above). The matching NOTIFY signals stay on
    // this QObject (see Q_SIGNALS below): Qt forbids multiple QObject
    // inheritance, so a consumer that needs an animation signal depends on
    // ISettings, not IAnimationSettings alone.
    //
    // animationExcludedApplications / animationExcludedWindowClasses virtuals
    // retired in v4 — the lists folded into ExcludeAnimations WindowRules; the
    // KWin effect now derives its m_animationExclusionRuleSet from the unified
    // rule store via PhosphorWindowRules::ExclusionRules::excludeAnimationsRulesFrom.

    // Autotile decoration settings (fetched by KWin effect via D-Bus)
    virtual bool autotileFocusFollowsMouse() const = 0;
    virtual void setAutotileFocusFollowsMouse(bool enabled) = 0;
    // Snapping focus behavior. focusNewWindows is read daemon-side by SnapAdaptor
    // to activate auto-placed-on-open windows; focusFollowsMouse is fetched by the
    // KWin effect via D-Bus.
    virtual bool snappingFocusNewWindows() const = 0;
    virtual void setSnappingFocusNewWindows(bool enabled) = 0;
    virtual bool snappingFocusFollowsMouse() const = 0;
    virtual void setSnappingFocusFollowsMouse(bool enabled) = 0;
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

    // Snapping window decoration settings (fetched by KWin effect via D-Bus).
    // Parallel to autotile* — the snapped window's border / title-bar.
    virtual bool snappingHideTitleBars() const = 0;
    virtual void setSnappingHideTitleBars(bool hide) = 0;
    virtual bool snappingShowBorder() const = 0;
    virtual void setSnappingShowBorder(bool show) = 0;
    virtual int snappingBorderWidth() const = 0;
    virtual void setSnappingBorderWidth(int width) = 0;
    virtual int snappingBorderRadius() const = 0;
    virtual void setSnappingBorderRadius(int radius) = 0;
    virtual QColor snappingBorderColor() const = 0;
    virtual void setSnappingBorderColor(const QColor& color) = 0;
    virtual QColor snappingInactiveBorderColor() const = 0;
    virtual void setSnappingInactiveBorderColor(const QColor& color) = 0;
    virtual bool snappingUseSystemBorderColors() const = 0;
    virtual void setSnappingUseSystemBorderColors(bool use) = 0;

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

    // Per-algorithm autotile settings map. Settings inherits from
    // PhosphorEngine::IAutotileSettings (which also declares these),
    // so the override in Settings covers both bases — the redundant
    // declaration here is the price of letting page controllers
    // depend on ISettings without dragging in PhosphorTileEngine.
    virtual QVariantMap autotilePerAlgorithmSettings() const = 0;
    virtual void setAutotilePerAlgorithmSettings(const QVariantMap& settings) = 0;

    // Color-import helper used by SnappingZonesController. Returns
    // an empty string on success, a user-readable error message
    // otherwise. The signature mirrors Settings::loadColorsFromFile
    // exactly so its existing Q_INVOKABLE annotation overrides this.
    virtual QString loadColorsFromFile(const QString& filePath) = 0;

    // Snapping behavior triggers (dragActivation, zoneSpan, snapAssist)
    // are declared by the IZoneActivationSettings / IZoneSelectorSettings
    // sub-interfaces ISettings inherits from — see settings_interfaces.h.
    // Re-declaring them here would shadow the parent virtual.

    // Rendering backend (pipeline-level, not specific to any sub-interface)
    virtual QString renderingBackend() const = 0;
    virtual void setRenderingBackend(const QString& backend) = 0;

    // Editor settings — used by EditorPageController. Editor-scope rather
    // than Snapping/Tiling-scope, so they don't fit any sub-interface.
    virtual QString editorDuplicateShortcut() const = 0;
    virtual void setEditorDuplicateShortcut(const QString& shortcut) = 0;
    virtual QString editorSplitHorizontalShortcut() const = 0;
    virtual void setEditorSplitHorizontalShortcut(const QString& shortcut) = 0;
    virtual QString editorSplitVerticalShortcut() const = 0;
    virtual void setEditorSplitVerticalShortcut(const QString& shortcut) = 0;
    virtual QString editorFillShortcut() const = 0;
    virtual void setEditorFillShortcut(const QString& shortcut) = 0;
    virtual bool editorGridSnappingEnabled() const = 0;
    virtual void setEditorGridSnappingEnabled(bool enabled) = 0;
    virtual bool editorEdgeSnappingEnabled() const = 0;
    virtual void setEditorEdgeSnappingEnabled(bool enabled) = 0;
    virtual qreal editorSnapIntervalX() const = 0;
    virtual void setEditorSnapIntervalX(qreal interval) = 0;
    virtual qreal editorSnapIntervalY() const = 0;
    virtual void setEditorSnapIntervalY(qreal interval) = 0;
    virtual int editorSnapOverrideModifier() const = 0;
    virtual void setEditorSnapOverrideModifier(int mod) = 0;
    virtual bool fillOnDropEnabled() const = 0;
    virtual void setFillOnDropEnabled(bool enabled) = 0;
    virtual int fillOnDropModifier() const = 0;
    virtual void setFillOnDropModifier(int mod) = 0;

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

    // NOTE: only the getter `override`s — `getPerScreenSnappingSettings`
    // is the lone snapping accessor declared on
    // PhosphorEngine::IGeometrySettings (consumed by the geometry
    // pipeline). The set/clear/has triplet is ISettings-only (writers
    // live in settings + KCM, never reached from the geometry path),
    // so they're plain `virtual` with no base to override. Mirrors the
    // autotile + zone-selector blocks above where neither side declares
    // any of the four on IGeometrySettings.
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
    //
    // Borrowed-store contract for load(): when the concrete Settings was
    // constructed with an externally-owned `WindowRuleStore*` (e.g. the
    // daemon's shared store), load() MUST NOT reload that store — the owner
    // is the writer and an interleaved load() here would clobber unflushed
    // in-memory edits. Only the owning side (or a future cross-process
    // watcher) drives reloads on the borrowed path. Implementations that
    // own their store (the standard constructor) reload normally. See
    // Settings::load (settings.cpp) for the live guard against
    // `m_ownedWindowRuleStore`.
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
    void zoneSpanToggleModeChanged();
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
    void innerGapChanged();
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
    void snappingStickyWindowHandlingChanged();
    void restoreWindowsToZonesOnLoginChanged();
    void snappingRestoreFloatedWindowsOnLoginChanged();
    void autotileRestoreFloatedWindowsOnLoginChanged();
    void snapUnfloatFallbackToZoneChanged();
    void autoAssignAllLayoutsChanged();
    void snapAssistFeatureEnabledChanged();
    void snapAssistEnabledChanged();
    void snapAssistTriggersChanged();
    void defaultLayoutIdChanged();
    void suppressDefaultLayoutAssignmentChanged();
    void filterLayoutsByAspectRatioChanged();
    // excludedApplications / excludedWindowClasses signals retired in v4
    // — see settings_interfaces.h for the rationale (lists folded into
    // unified Exclude WindowRules; consumers subscribe to the rule store
    // through PhosphorWindowRules::WindowRuleStore::rulesChanged instead).
    void excludeTransientWindowsChanged();
    void minimumWindowWidthChanged();
    void minimumWindowHeightChanged();
    // Animation window filtering — paired with the IAnimationSettings
    // virtuals. Same shape as the snapping/tiling exclusion signals; lives
    // in its own change-set so animation-only consumers (the kwin-effect,
    // the AnimationsPageController) don't have to discriminate when
    // filtering NOTIFY traffic.
    void animationExcludeTransientWindowsChanged();
    void animationExcludeNotificationsAndOsdChanged();
    void animationMinimumWindowWidthChanged();
    void animationMinimumWindowHeightChanged();
    // animationExcludedApplicationsChanged / animationExcludedWindowClassesChanged
    // signals retired in v4 alongside the list virtuals above.
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
    // Editor
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
    // Autotile inner/outer gap change signals are unified with snapping —
    // listeners use innerGapChanged / outerGap*Changed above.
    void autotileSmartGapsChanged();
    void autotileMaxWindowsChanged();
    void autotileFocusNewWindowsChanged();
    void autotileInsertPositionChanged();
    void autotileRespectMinimumSizeChanged();
    void autotileFocusFollowsMouseChanged();
    void snappingFocusNewWindowsChanged();
    void snappingFocusFollowsMouseChanged();
    void autotileHideTitleBarsChanged();
    void autotileShowBorderChanged();
    void autotileBorderWidthChanged();
    void autotileBorderRadiusChanged();
    void autotileBorderColorChanged();
    void autotileInactiveBorderColorChanged();
    void autotileUseSystemBorderColorsChanged();
    void snappingHideTitleBarsChanged();
    void snappingShowBorderChanged();
    void snappingBorderWidthChanged();
    void snappingBorderRadiusChanged();
    void snappingBorderColorChanged();
    void snappingInactiveBorderColorChanged();
    void snappingUseSystemBorderColorsChanged();
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
    void shaderProfileTreeChanged();

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
