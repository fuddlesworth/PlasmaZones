// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../settings.h"
#include "macros.h"
#include "../../core/constants.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../autotile/AlgorithmRegistry.h"

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Activation setters
// ═══════════════════════════════════════════════════════════════════════════════

void Settings::setShiftDragToActivate(bool enable)
{
    if (m_shiftDragToActivate != enable) {
        m_shiftDragToActivate = enable;
        Q_EMIT shiftDragToActivateChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setDragActivationTriggers(const QVariantList& triggers)
{
    QVariantList capped = triggers.mid(0, MaxTriggersPerAction);
    if (m_dragActivationTriggers != capped) {
        m_dragActivationTriggers = capped;
        Q_EMIT dragActivationTriggersChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setZoneSpanModifier(DragModifier modifier)
{
    if (m_zoneSpanModifier != modifier) {
        m_zoneSpanModifier = modifier;
        // Keep triggers in sync: update first trigger's modifier to match
        if (!m_zoneSpanTriggers.isEmpty()) {
            auto first = m_zoneSpanTriggers.first().toMap();
            first[QStringLiteral("modifier")] = static_cast<int>(modifier);
            m_zoneSpanTriggers[0] = first;
        } else {
            QVariantMap trigger;
            trigger[QStringLiteral("modifier")] = static_cast<int>(modifier);
            trigger[QStringLiteral("mouseButton")] = 0;
            m_zoneSpanTriggers = {trigger};
        }
        Q_EMIT zoneSpanModifierChanged();
        Q_EMIT zoneSpanTriggersChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setZoneSpanTriggers(const QVariantList& triggers)
{
    QVariantList capped = triggers.mid(0, MaxTriggersPerAction);
    if (m_zoneSpanTriggers != capped) {
        m_zoneSpanTriggers = capped;
        // Sync legacy zoneSpanModifier from first trigger with a non-zero modifier
        DragModifier synced = DragModifier::Disabled;
        for (const auto& t : capped) {
            int mod = t.toMap().value(QStringLiteral("modifier"), 0).toInt();
            if (mod != 0) {
                synced = static_cast<DragModifier>(qBound(0, mod, static_cast<int>(DragModifier::CtrlAltMeta)));
                break;
            }
        }
        m_zoneSpanModifier = synced;
        Q_EMIT zoneSpanTriggersChanged();
        Q_EMIT zoneSpanModifierChanged();
        Q_EMIT settingsChanged();
    }
}

SETTINGS_SETTER_ENUM_INT(ZoneSpanModifier, DragModifier, 0, static_cast<int>(DragModifier::CtrlAltMeta))

SETTINGS_SETTER(bool, ZoneSpanEnabled, m_zoneSpanEnabled, zoneSpanEnabledChanged)
SETTINGS_SETTER(bool, ToggleActivation, m_toggleActivation, toggleActivationChanged)
SETTINGS_SETTER(bool, SnappingEnabled, m_snappingEnabled, snappingEnabledChanged)

// ═══════════════════════════════════════════════════════════════════════════════
// Display setters
// ═══════════════════════════════════════════════════════════════════════════════

SETTINGS_SETTER(bool, ShowZonesOnAllMonitors, m_showZonesOnAllMonitors, showZonesOnAllMonitorsChanged)
SETTINGS_SETTER(const QStringList&, DisabledMonitors, m_disabledMonitors, disabledMonitorsChanged)

bool Settings::isMonitorDisabled(const QString& screenName) const
{
    if (m_disabledMonitors.contains(screenName)) {
        return true;
    }
    // Backward compat: if screenName looks like a connector name (no colons),
    // resolve to stable EDID-based screen ID and check again
    if (Utils::isConnectorName(screenName)) {
        QString resolved = Utils::screenIdForName(screenName);
        if (resolved != screenName && m_disabledMonitors.contains(resolved)) {
            return true;
        }
    } else {
        // screenName is a screen ID — try reverse lookup to connector name
        // (covers unmigrated entries from screens disconnected during load)
        QString connector = Utils::screenNameForId(screenName);
        if (!connector.isEmpty() && m_disabledMonitors.contains(connector)) {
            return true;
        }
    }
    return false;
}

SETTINGS_SETTER(bool, ShowZoneNumbers, m_showZoneNumbers, showZoneNumbersChanged)
SETTINGS_SETTER(bool, FlashZonesOnSwitch, m_flashZonesOnSwitch, flashZonesOnSwitchChanged)
SETTINGS_SETTER(bool, ShowOsdOnLayoutSwitch, m_showOsdOnLayoutSwitch, showOsdOnLayoutSwitchChanged)
SETTINGS_SETTER(bool, ShowNavigationOsd, m_showNavigationOsd, showNavigationOsdChanged)

void Settings::setOsdStyle(OsdStyle style)
{
    if (m_osdStyle != style) {
        m_osdStyle = style;
        Q_EMIT osdStyleChanged();
        Q_EMIT settingsChanged();
    }
}

SETTINGS_SETTER_ENUM_INT(OsdStyle, OsdStyle, 0, static_cast<int>(OsdStyle::Preview))

void Settings::setOverlayDisplayMode(OverlayDisplayMode mode)
{
    if (m_overlayDisplayMode != mode) {
        m_overlayDisplayMode = mode;
        Q_EMIT overlayDisplayModeChanged();
        Q_EMIT settingsChanged();
    }
}

SETTINGS_SETTER_ENUM_INT(OverlayDisplayMode, OverlayDisplayMode, 0, static_cast<int>(OverlayDisplayMode::LayoutPreview))

// ═══════════════════════════════════════════════════════════════════════════════
// Appearance setters
// ═══════════════════════════════════════════════════════════════════════════════

void Settings::setUseSystemColors(bool use)
{
    if (m_useSystemColors != use) {
        m_useSystemColors = use;
        if (use) {
            applySystemColorScheme();
        }
        Q_EMIT useSystemColorsChanged();
        Q_EMIT settingsChanged();
    }
}

SETTINGS_SETTER(const QColor&, HighlightColor, m_highlightColor, highlightColorChanged)
SETTINGS_SETTER(const QColor&, InactiveColor, m_inactiveColor, inactiveColorChanged)
SETTINGS_SETTER(const QColor&, BorderColor, m_borderColor, borderColorChanged)
SETTINGS_SETTER(const QColor&, LabelFontColor, m_labelFontColor, labelFontColorChanged)

SETTINGS_SETTER_CLAMPED_QREAL(ActiveOpacity, m_activeOpacity, activeOpacityChanged, 0.0, 1.0)
SETTINGS_SETTER_CLAMPED_QREAL(InactiveOpacity, m_inactiveOpacity, inactiveOpacityChanged, 0.0, 1.0)

SETTINGS_SETTER_CLAMPED(BorderWidth, m_borderWidth, borderWidthChanged, 0, 10)
SETTINGS_SETTER_CLAMPED(BorderRadius, m_borderRadius, borderRadiusChanged, 0, 50)

SETTINGS_SETTER(bool, EnableBlur, m_enableBlur, enableBlurChanged)
SETTINGS_SETTER(const QString&, LabelFontFamily, m_labelFontFamily, labelFontFamilyChanged)
SETTINGS_SETTER_CLAMPED(LabelFontWeight, m_labelFontWeight, labelFontWeightChanged, 100, 900)
SETTINGS_SETTER(bool, LabelFontItalic, m_labelFontItalic, labelFontItalicChanged)
SETTINGS_SETTER(bool, LabelFontUnderline, m_labelFontUnderline, labelFontUnderlineChanged)
SETTINGS_SETTER(bool, LabelFontStrikeout, m_labelFontStrikeout, labelFontStrikeoutChanged)

SETTINGS_SETTER_CLAMPED_QREAL(LabelFontSizeScale, m_labelFontSizeScale, labelFontSizeScaleChanged, 0.25, 3.0)

// ═══════════════════════════════════════════════════════════════════════════════
// Zone geometry setters
// ═══════════════════════════════════════════════════════════════════════════════

SETTINGS_SETTER_CLAMPED(ZonePadding, m_zonePadding, zonePaddingChanged, 0, Defaults::MaxGap)
SETTINGS_SETTER_CLAMPED(OuterGap, m_outerGap, outerGapChanged, 0, Defaults::MaxGap)
SETTINGS_SETTER(bool, UsePerSideOuterGap, m_usePerSideOuterGap, usePerSideOuterGapChanged)
SETTINGS_SETTER_CLAMPED(OuterGapTop, m_outerGapTop, outerGapTopChanged, 0, Defaults::MaxGap)
SETTINGS_SETTER_CLAMPED(OuterGapBottom, m_outerGapBottom, outerGapBottomChanged, 0, Defaults::MaxGap)
SETTINGS_SETTER_CLAMPED(OuterGapLeft, m_outerGapLeft, outerGapLeftChanged, 0, Defaults::MaxGap)
SETTINGS_SETTER_CLAMPED(OuterGapRight, m_outerGapRight, outerGapRightChanged, 0, Defaults::MaxGap)
SETTINGS_SETTER_CLAMPED(AdjacentThreshold, m_adjacentThreshold, adjacentThresholdChanged, 5, 100)
SETTINGS_SETTER_CLAMPED(PollIntervalMs, m_pollIntervalMs, pollIntervalMsChanged, 10, 1000)
SETTINGS_SETTER_CLAMPED(MinimumZoneSizePx, m_minimumZoneSizePx, minimumZoneSizePxChanged, 50, 500)
SETTINGS_SETTER_CLAMPED(MinimumZoneDisplaySizePx, m_minimumZoneDisplaySizePx, minimumZoneDisplaySizePxChanged, 1, 50)

// ═══════════════════════════════════════════════════════════════════════════════
// Behavior setters
// ═══════════════════════════════════════════════════════════════════════════════

SETTINGS_SETTER(bool, KeepWindowsInZonesOnResolutionChange, m_keepWindowsInZonesOnResolutionChange,
                keepWindowsInZonesOnResolutionChangeChanged)
SETTINGS_SETTER(bool, MoveNewWindowsToLastZone, m_moveNewWindowsToLastZone, moveNewWindowsToLastZoneChanged)
SETTINGS_SETTER(bool, RestoreOriginalSizeOnUnsnap, m_restoreOriginalSizeOnUnsnap, restoreOriginalSizeOnUnsnapChanged)
SETTINGS_SETTER(StickyWindowHandling, StickyWindowHandling, m_stickyWindowHandling, stickyWindowHandlingChanged)

SETTINGS_SETTER_ENUM_INT(StickyWindowHandling, StickyWindowHandling,
                         static_cast<int>(StickyWindowHandling::TreatAsNormal),
                         static_cast<int>(StickyWindowHandling::IgnoreAll))

SETTINGS_SETTER(bool, RestoreWindowsToZonesOnLogin, m_restoreWindowsToZonesOnLogin, restoreWindowsToZonesOnLoginChanged)
SETTINGS_SETTER(bool, SnapAssistFeatureEnabled, m_snapAssistFeatureEnabled, snapAssistFeatureEnabledChanged)
SETTINGS_SETTER(bool, SnapAssistEnabled, m_snapAssistEnabled, snapAssistEnabledChanged)

void Settings::setSnapAssistTriggers(const QVariantList& triggers)
{
    QVariantList capped = triggers.mid(0, MaxTriggersPerAction);
    if (m_snapAssistTriggers != capped) {
        m_snapAssistTriggers = capped;
        Q_EMIT snapAssistTriggersChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setDefaultLayoutId(const QString& layoutId)
{
    QString normalizedId = normalizeUuidString(layoutId);
    if (m_defaultLayoutId != normalizedId) {
        m_defaultLayoutId = normalizedId;
        Q_EMIT defaultLayoutIdChanged();
        Q_EMIT settingsChanged();
    }
}

SETTINGS_SETTER(const QStringList&, ExcludedApplications, m_excludedApplications, excludedApplicationsChanged)
SETTINGS_SETTER(const QStringList&, ExcludedWindowClasses, m_excludedWindowClasses, excludedWindowClassesChanged)
SETTINGS_SETTER(bool, ExcludeTransientWindows, m_excludeTransientWindows, excludeTransientWindowsChanged)
SETTINGS_SETTER_CLAMPED(MinimumWindowWidth, m_minimumWindowWidth, minimumWindowWidthChanged, 0, 2000)
SETTINGS_SETTER_CLAMPED(MinimumWindowHeight, m_minimumWindowHeight, minimumWindowHeightChanged, 0, 2000)

// ═══════════════════════════════════════════════════════════════════════════════
// Zone Selector setters
// ═══════════════════════════════════════════════════════════════════════════════

SETTINGS_SETTER(bool, ZoneSelectorEnabled, m_zoneSelectorEnabled, zoneSelectorEnabledChanged)
SETTINGS_SETTER_CLAMPED(ZoneSelectorTriggerDistance, m_zoneSelectorTriggerDistance, zoneSelectorTriggerDistanceChanged,
                        10, 200)
SETTINGS_SETTER(ZoneSelectorPosition, ZoneSelectorPosition, m_zoneSelectorPosition, zoneSelectorPositionChanged)

void Settings::setZoneSelectorPositionInt(int position)
{
    // Valid positions are 0-8 (3x3 grid)
    if (position >= 0 && position <= 8) {
        setZoneSelectorPosition(static_cast<ZoneSelectorPosition>(position));
    }
}

SETTINGS_SETTER(ZoneSelectorLayoutMode, ZoneSelectorLayoutMode, m_zoneSelectorLayoutMode, zoneSelectorLayoutModeChanged)

SETTINGS_SETTER_ENUM_INT(ZoneSelectorLayoutMode, ZoneSelectorLayoutMode, 0,
                         static_cast<int>(ZoneSelectorLayoutMode::Vertical))

SETTINGS_SETTER_CLAMPED(ZoneSelectorPreviewWidth, m_zoneSelectorPreviewWidth, zoneSelectorPreviewWidthChanged, 80, 400)
SETTINGS_SETTER_CLAMPED(ZoneSelectorPreviewHeight, m_zoneSelectorPreviewHeight, zoneSelectorPreviewHeightChanged, 60,
                        300)
SETTINGS_SETTER(bool, ZoneSelectorPreviewLockAspect, m_zoneSelectorPreviewLockAspect,
                zoneSelectorPreviewLockAspectChanged)
SETTINGS_SETTER_CLAMPED(ZoneSelectorGridColumns, m_zoneSelectorGridColumns, zoneSelectorGridColumnsChanged, 1, 10)
SETTINGS_SETTER(ZoneSelectorSizeMode, ZoneSelectorSizeMode, m_zoneSelectorSizeMode, zoneSelectorSizeModeChanged)

SETTINGS_SETTER_ENUM_INT(ZoneSelectorSizeMode, ZoneSelectorSizeMode, 0, static_cast<int>(ZoneSelectorSizeMode::Manual))

SETTINGS_SETTER_CLAMPED(ZoneSelectorMaxRows, m_zoneSelectorMaxRows, zoneSelectorMaxRowsChanged, 1, 10)

// ═══════════════════════════════════════════════════════════════════════════════
// Autotiling setters
// ═══════════════════════════════════════════════════════════════════════════════

SETTINGS_SETTER(bool, AutotileEnabled, m_autotileEnabled, autotileEnabledChanged)

void Settings::setAutotileAlgorithm(const QString& algorithm)
{
    // Validate algorithm ID against the algorithm registry (single source of truth)
    QString validatedAlgorithm = algorithm;
    if (!AlgorithmRegistry::instance()->algorithm(algorithm)) {
        qCWarning(lcConfig) << "Unknown autotile algorithm:" << algorithm << "- using default";
        validatedAlgorithm = AlgorithmRegistry::defaultAlgorithmId();
    }

    if (m_autotileAlgorithm != validatedAlgorithm) {
        m_autotileAlgorithm = validatedAlgorithm;
        Q_EMIT autotileAlgorithmChanged();
        Q_EMIT settingsChanged();
    }
}

SETTINGS_SETTER_CLAMPED_QREAL(AutotileSplitRatio, m_autotileSplitRatio, autotileSplitRatioChanged,
                              AutotileDefaults::MinSplitRatio, AutotileDefaults::MaxSplitRatio)
SETTINGS_SETTER_CLAMPED(AutotileMasterCount, m_autotileMasterCount, autotileMasterCountChanged,
                        AutotileDefaults::MinMasterCount, AutotileDefaults::MaxMasterCount)
SETTINGS_SETTER_CLAMPED_QREAL(AutotileCenteredMasterSplitRatio, m_autotileCenteredMasterSplitRatio,
                              autotileCenteredMasterSplitRatioChanged, AutotileDefaults::MinSplitRatio,
                              AutotileDefaults::MaxSplitRatio)
SETTINGS_SETTER_CLAMPED(AutotileCenteredMasterMasterCount, m_autotileCenteredMasterMasterCount,
                        autotileCenteredMasterMasterCountChanged, AutotileDefaults::MinMasterCount,
                        AutotileDefaults::MaxMasterCount)
SETTINGS_SETTER_CLAMPED(AutotileInnerGap, m_autotileInnerGap, autotileInnerGapChanged, AutotileDefaults::MinGap,
                        AutotileDefaults::MaxGap)
SETTINGS_SETTER_CLAMPED(AutotileOuterGap, m_autotileOuterGap, autotileOuterGapChanged, AutotileDefaults::MinGap,
                        AutotileDefaults::MaxGap)
SETTINGS_SETTER(bool, AutotileUsePerSideOuterGap, m_autotileUsePerSideOuterGap, autotileUsePerSideOuterGapChanged)
SETTINGS_SETTER_CLAMPED(AutotileOuterGapTop, m_autotileOuterGapTop, autotileOuterGapTopChanged,
                        AutotileDefaults::MinGap, AutotileDefaults::MaxGap)
SETTINGS_SETTER_CLAMPED(AutotileOuterGapBottom, m_autotileOuterGapBottom, autotileOuterGapBottomChanged,
                        AutotileDefaults::MinGap, AutotileDefaults::MaxGap)
SETTINGS_SETTER_CLAMPED(AutotileOuterGapLeft, m_autotileOuterGapLeft, autotileOuterGapLeftChanged,
                        AutotileDefaults::MinGap, AutotileDefaults::MaxGap)
SETTINGS_SETTER_CLAMPED(AutotileOuterGapRight, m_autotileOuterGapRight, autotileOuterGapRightChanged,
                        AutotileDefaults::MinGap, AutotileDefaults::MaxGap)

SETTINGS_SETTER(bool, AutotileFocusNewWindows, m_autotileFocusNewWindows, autotileFocusNewWindowsChanged)
SETTINGS_SETTER(bool, AutotileSmartGaps, m_autotileSmartGaps, autotileSmartGapsChanged)
SETTINGS_SETTER_CLAMPED(AutotileMaxWindows, m_autotileMaxWindows, autotileMaxWindowsChanged,
                        AutotileDefaults::MinMaxWindows, AutotileDefaults::MaxMaxWindows)
SETTINGS_SETTER(AutotileInsertPosition, AutotileInsertPosition, m_autotileInsertPosition, autotileInsertPositionChanged)

SETTINGS_SETTER_ENUM_INT(AutotileInsertPosition, AutotileInsertPosition, 0, 2)

SETTINGS_SETTER(const QString&, AutotileToggleShortcut, m_autotileToggleShortcut, autotileToggleShortcutChanged)
SETTINGS_SETTER(const QString&, AutotileFocusMasterShortcut, m_autotileFocusMasterShortcut,
                autotileFocusMasterShortcutChanged)
SETTINGS_SETTER(const QString&, AutotileSwapMasterShortcut, m_autotileSwapMasterShortcut,
                autotileSwapMasterShortcutChanged)
SETTINGS_SETTER(const QString&, AutotileIncMasterRatioShortcut, m_autotileIncMasterRatioShortcut,
                autotileIncMasterRatioShortcutChanged)
SETTINGS_SETTER(const QString&, AutotileDecMasterRatioShortcut, m_autotileDecMasterRatioShortcut,
                autotileDecMasterRatioShortcutChanged)
SETTINGS_SETTER(const QString&, AutotileIncMasterCountShortcut, m_autotileIncMasterCountShortcut,
                autotileIncMasterCountShortcutChanged)
SETTINGS_SETTER(const QString&, AutotileDecMasterCountShortcut, m_autotileDecMasterCountShortcut,
                autotileDecMasterCountShortcutChanged)
SETTINGS_SETTER(const QString&, AutotileRetileShortcut, m_autotileRetileShortcut, autotileRetileShortcutChanged)

// ═══════════════════════════════════════════════════════════════════════════════
// Animation setters
// ═══════════════════════════════════════════════════════════════════════════════

SETTINGS_SETTER(bool, AnimationsEnabled, m_animationsEnabled, animationsEnabledChanged)
SETTINGS_SETTER_CLAMPED(AnimationDuration, m_animationDuration, animationDurationChanged, 50, 500)
SETTINGS_SETTER(const QString&, AnimationEasingCurve, m_animationEasingCurve, animationEasingCurveChanged)

SETTINGS_SETTER_CLAMPED(AnimationMinDistance, m_animationMinDistance, animationMinDistanceChanged, 0, 200)
SETTINGS_SETTER_CLAMPED(AnimationSequenceMode, m_animationSequenceMode, animationSequenceModeChanged, 0, 1)
SETTINGS_SETTER_CLAMPED(AnimationStaggerInterval, m_animationStaggerInterval, animationStaggerIntervalChanged,
                        AutotileDefaults::MinAnimationStaggerIntervalMs,
                        AutotileDefaults::MaxAnimationStaggerIntervalMs)
SETTINGS_SETTER(bool, AutotileFocusFollowsMouse, m_autotileFocusFollowsMouse, autotileFocusFollowsMouseChanged)
SETTINGS_SETTER(bool, AutotileRespectMinimumSize, m_autotileRespectMinimumSize, autotileRespectMinimumSizeChanged)
SETTINGS_SETTER(bool, AutotileHideTitleBars, m_autotileHideTitleBars, autotileHideTitleBarsChanged)

SETTINGS_SETTER_CLAMPED(AutotileBorderWidth, m_autotileBorderWidth, autotileBorderWidthChanged, 0, 10)

SETTINGS_SETTER(const QColor&, AutotileBorderColor, m_autotileBorderColor, autotileBorderColorChanged)

void Settings::setAutotileUseSystemBorderColors(bool use)
{
    if (m_autotileUseSystemBorderColors != use) {
        m_autotileUseSystemBorderColors = use;
        if (use) {
            applyAutotileBorderSystemColor();
        }
        Q_EMIT autotileUseSystemBorderColorsChanged();
        Q_EMIT settingsChanged();
    }
}

// Shader Effects
SETTINGS_SETTER(bool, EnableShaderEffects, m_enableShaderEffects, enableShaderEffectsChanged)
SETTINGS_SETTER_CLAMPED(ShaderFrameRate, m_shaderFrameRate, shaderFrameRateChanged, 30, 144)
SETTINGS_SETTER(bool, EnableAudioVisualizer, m_enableAudioVisualizer, enableAudioVisualizerChanged)
SETTINGS_SETTER_CLAMPED(AudioSpectrumBarCount, m_audioSpectrumBarCount, audioSpectrumBarCountChanged, 16, 256)

// ═══════════════════════════════════════════════════════════════════════════════
// Shortcut setters
// ═══════════════════════════════════════════════════════════════════════════════

SETTINGS_SETTER(const QString&, OpenEditorShortcut, m_openEditorShortcut, openEditorShortcutChanged)
SETTINGS_SETTER(const QString&, PreviousLayoutShortcut, m_previousLayoutShortcut, previousLayoutShortcutChanged)
SETTINGS_SETTER(const QString&, NextLayoutShortcut, m_nextLayoutShortcut, nextLayoutShortcutChanged)

void Settings::setQuickLayout1Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(0, shortcut);
}
void Settings::setQuickLayout2Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(1, shortcut);
}
void Settings::setQuickLayout3Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(2, shortcut);
}
void Settings::setQuickLayout4Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(3, shortcut);
}
void Settings::setQuickLayout5Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(4, shortcut);
}
void Settings::setQuickLayout6Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(5, shortcut);
}
void Settings::setQuickLayout7Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(6, shortcut);
}
void Settings::setQuickLayout8Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(7, shortcut);
}
void Settings::setQuickLayout9Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(8, shortcut);
}

QString Settings::quickLayoutShortcut(int index) const
{
    if (index >= 0 && index < 9) {
        return m_quickLayoutShortcuts[index];
    }
    return QString();
}

void Settings::setIndexedShortcut(QString (&arr)[9], int index, const QString& shortcut,
                                  const ShortcutSignalFn (&signals)[9])
{
    if (index >= 0 && index < 9 && arr[index] != shortcut) {
        arr[index] = shortcut;
        Q_EMIT(this->*signals[index])();
        Q_EMIT settingsChanged();
    }
}

void Settings::setQuickLayoutShortcut(int index, const QString& shortcut)
{
    static const ShortcutSignalFn signals[9] = {
        &Settings::quickLayout1ShortcutChanged, &Settings::quickLayout2ShortcutChanged,
        &Settings::quickLayout3ShortcutChanged, &Settings::quickLayout4ShortcutChanged,
        &Settings::quickLayout5ShortcutChanged, &Settings::quickLayout6ShortcutChanged,
        &Settings::quickLayout7ShortcutChanged, &Settings::quickLayout8ShortcutChanged,
        &Settings::quickLayout9ShortcutChanged,
    };
    setIndexedShortcut(m_quickLayoutShortcuts, index, shortcut, signals);
}

SETTINGS_SETTER(const QString&, MoveWindowLeftShortcut, m_moveWindowLeftShortcut, moveWindowLeftShortcutChanged)
SETTINGS_SETTER(const QString&, MoveWindowRightShortcut, m_moveWindowRightShortcut, moveWindowRightShortcutChanged)
SETTINGS_SETTER(const QString&, MoveWindowUpShortcut, m_moveWindowUpShortcut, moveWindowUpShortcutChanged)
SETTINGS_SETTER(const QString&, MoveWindowDownShortcut, m_moveWindowDownShortcut, moveWindowDownShortcutChanged)
SETTINGS_SETTER(const QString&, FocusZoneLeftShortcut, m_focusZoneLeftShortcut, focusZoneLeftShortcutChanged)
SETTINGS_SETTER(const QString&, FocusZoneRightShortcut, m_focusZoneRightShortcut, focusZoneRightShortcutChanged)
SETTINGS_SETTER(const QString&, FocusZoneUpShortcut, m_focusZoneUpShortcut, focusZoneUpShortcutChanged)
SETTINGS_SETTER(const QString&, FocusZoneDownShortcut, m_focusZoneDownShortcut, focusZoneDownShortcutChanged)
SETTINGS_SETTER(const QString&, PushToEmptyZoneShortcut, m_pushToEmptyZoneShortcut, pushToEmptyZoneShortcutChanged)
SETTINGS_SETTER(const QString&, RestoreWindowSizeShortcut, m_restoreWindowSizeShortcut,
                restoreWindowSizeShortcutChanged)
SETTINGS_SETTER(const QString&, ToggleWindowFloatShortcut, m_toggleWindowFloatShortcut,
                toggleWindowFloatShortcutChanged)

SETTINGS_SETTER(const QString&, SwapWindowLeftShortcut, m_swapWindowLeftShortcut, swapWindowLeftShortcutChanged)
SETTINGS_SETTER(const QString&, SwapWindowRightShortcut, m_swapWindowRightShortcut, swapWindowRightShortcutChanged)
SETTINGS_SETTER(const QString&, SwapWindowUpShortcut, m_swapWindowUpShortcut, swapWindowUpShortcutChanged)
SETTINGS_SETTER(const QString&, SwapWindowDownShortcut, m_swapWindowDownShortcut, swapWindowDownShortcutChanged)

QString Settings::snapToZoneShortcut(int index) const
{
    if (index >= 0 && index < 9) {
        return m_snapToZoneShortcuts[index];
    }
    return QString();
}

void Settings::setSnapToZoneShortcut(int index, const QString& shortcut)
{
    static const ShortcutSignalFn signals[9] = {
        &Settings::snapToZone1ShortcutChanged, &Settings::snapToZone2ShortcutChanged,
        &Settings::snapToZone3ShortcutChanged, &Settings::snapToZone4ShortcutChanged,
        &Settings::snapToZone5ShortcutChanged, &Settings::snapToZone6ShortcutChanged,
        &Settings::snapToZone7ShortcutChanged, &Settings::snapToZone8ShortcutChanged,
        &Settings::snapToZone9ShortcutChanged,
    };
    setIndexedShortcut(m_snapToZoneShortcuts, index, shortcut, signals);
}

void Settings::setSnapToZone1Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(0, shortcut);
}

void Settings::setSnapToZone2Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(1, shortcut);
}

void Settings::setSnapToZone3Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(2, shortcut);
}

void Settings::setSnapToZone4Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(3, shortcut);
}

void Settings::setSnapToZone5Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(4, shortcut);
}

void Settings::setSnapToZone6Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(5, shortcut);
}

void Settings::setSnapToZone7Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(6, shortcut);
}

void Settings::setSnapToZone8Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(7, shortcut);
}

void Settings::setSnapToZone9Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(8, shortcut);
}

SETTINGS_SETTER(const QString&, RotateWindowsClockwiseShortcut, m_rotateWindowsClockwiseShortcut,
                rotateWindowsClockwiseShortcutChanged)
SETTINGS_SETTER(const QString&, RotateWindowsCounterclockwiseShortcut, m_rotateWindowsCounterclockwiseShortcut,
                rotateWindowsCounterclockwiseShortcutChanged)

SETTINGS_SETTER(const QString&, CycleWindowForwardShortcut, m_cycleWindowForwardShortcut,
                cycleWindowForwardShortcutChanged)
SETTINGS_SETTER(const QString&, CycleWindowBackwardShortcut, m_cycleWindowBackwardShortcut,
                cycleWindowBackwardShortcutChanged)
SETTINGS_SETTER(const QString&, ResnapToNewLayoutShortcut, m_resnapToNewLayoutShortcut,
                resnapToNewLayoutShortcutChanged)
SETTINGS_SETTER(const QString&, SnapAllWindowsShortcut, m_snapAllWindowsShortcut, snapAllWindowsShortcutChanged)
SETTINGS_SETTER(const QString&, LayoutPickerShortcut, m_layoutPickerShortcut, layoutPickerShortcutChanged)

} // namespace PlasmaZones
