// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../settings.h"
#include "../configdefaults.h"
#include "macros.h"
#include "../../core/constants.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../autotile/AlgorithmRegistry.h"
#include "../../autotile/AutotileConfig.h"

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Activation setters
// ═══════════════════════════════════════════════════════════════════════════════

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
            first[ConfigDefaults::triggerModifierField()] = static_cast<int>(modifier);
            m_zoneSpanTriggers[0] = first;
        } else {
            QVariantMap trigger;
            trigger[ConfigDefaults::triggerModifierField()] = static_cast<int>(modifier);
            trigger[ConfigDefaults::triggerMouseButtonField()] = 0;
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
            int mod = t.toMap().value(ConfigDefaults::triggerModifierField(), 0).toInt();
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

bool Settings::isMonitorDisabled(const QString& screenIdOrName) const
{
    if (m_disabledMonitors.contains(screenIdOrName)) {
        return true;
    }
    // Backward compat: if screenIdOrName looks like a connector name (no colons),
    // resolve to stable EDID-based screen ID and check again
    if (Utils::isConnectorName(screenIdOrName)) {
        QString resolved = Utils::screenIdForName(screenIdOrName);
        if (resolved != screenIdOrName && m_disabledMonitors.contains(resolved)) {
            return true;
        }
    } else {
        // screenIdOrName is a screen ID — try reverse lookup to connector name
        // (covers unmigrated entries from screens disconnected during load)
        QString connector = Utils::screenNameForId(screenIdOrName);
        if (!connector.isEmpty() && m_disabledMonitors.contains(connector)) {
            return true;
        }
    }
    return false;
}

SETTINGS_SETTER(const QStringList&, DisabledDesktops, m_disabledDesktops, disabledDesktopsChanged)

bool Settings::isDesktopDisabled(const QString& screenIdOrName, int desktop) const
{
    if (desktop <= 0)
        return false;

    // Resolve screen identifier bidirectionally (same pattern as isMonitorDisabled)
    QStringList namesToCheck = {screenIdOrName};
    if (Utils::isConnectorName(screenIdOrName)) {
        QString resolved = Utils::screenIdForName(screenIdOrName);
        if (resolved != screenIdOrName)
            namesToCheck.append(resolved);
    } else {
        QString connector = Utils::screenNameForId(screenIdOrName);
        if (!connector.isEmpty() && connector != screenIdOrName)
            namesToCheck.append(connector);
    }

    const QString desktopStr = QString::number(desktop);
    for (const QString& name : std::as_const(namesToCheck)) {
        if (m_disabledDesktops.contains(name + QLatin1Char('/') + desktopStr))
            return true;
    }
    return false;
}

SETTINGS_SETTER(const QStringList&, DisabledActivities, m_disabledActivities, disabledActivitiesChanged)

bool Settings::isActivityDisabled(const QString& screenIdOrName, const QString& activityId) const
{
    if (activityId.isEmpty())
        return false;

    QStringList namesToCheck = {screenIdOrName};
    if (Utils::isConnectorName(screenIdOrName)) {
        QString resolved = Utils::screenIdForName(screenIdOrName);
        if (resolved != screenIdOrName)
            namesToCheck.append(resolved);
    } else {
        QString connector = Utils::screenNameForId(screenIdOrName);
        if (!connector.isEmpty() && connector != screenIdOrName)
            namesToCheck.append(connector);
    }

    for (const QString& name : std::as_const(namesToCheck)) {
        if (m_disabledActivities.contains(name + QLatin1Char('/') + activityId))
            return true;
    }
    return false;
}

bool Settings::isScreenLocked(const QString& screenIdOrName) const
{
    return isContextLocked(screenIdOrName, 0, QString());
}

void Settings::setScreenLocked(const QString& screenIdOrName, bool locked)
{
    setContextLocked(screenIdOrName, 0, QString(), locked);
}

bool Settings::isContextLocked(const QString& screenIdOrName, int virtualDesktop, const QString& activity) const
{
    // Resolve both connector name and screen ID so locks match regardless
    // of which format was used to store vs query (same approach as isMonitorDisabled)
    QStringList namesToCheck = {screenIdOrName};
    if (Utils::isConnectorName(screenIdOrName)) {
        QString resolved = Utils::screenIdForName(screenIdOrName);
        if (resolved != screenIdOrName)
            namesToCheck.append(resolved);
    } else {
        QString connector = Utils::screenNameForId(screenIdOrName);
        if (!connector.isEmpty() && connector != screenIdOrName)
            namesToCheck.append(connector);
    }

    for (const QString& name : std::as_const(namesToCheck)) {
        // Check exact context first, then fall back to broader contexts
        // Most specific: screen+desktop+activity
        if (virtualDesktop > 0 && !activity.isEmpty()) {
            QString key = name + QStringLiteral(":") + QString::number(virtualDesktop) + QStringLiteral(":") + activity;
            if (m_lockedScreens.contains(key))
                return true;
        }
        // Screen+desktop
        if (virtualDesktop > 0) {
            QString key = name + QStringLiteral(":") + QString::number(virtualDesktop);
            if (m_lockedScreens.contains(key))
                return true;
        }
        // Screen-level (broadest lock)
        if (m_lockedScreens.contains(name))
            return true;
    }
    return false;
}

void Settings::setContextLocked(const QString& screenIdOrName, int virtualDesktop, const QString& activity, bool locked)
{
    QString key = screenIdOrName;
    if (virtualDesktop > 0) {
        key += QStringLiteral(":") + QString::number(virtualDesktop);
        if (!activity.isEmpty())
            key += QStringLiteral(":") + activity;
    }

    if (locked && !m_lockedScreens.contains(key)) {
        m_lockedScreens.append(key);
        Q_EMIT lockedScreensChanged();
        Q_EMIT settingsChanged();
    } else if (!locked && m_lockedScreens.removeAll(key) > 0) {
        Q_EMIT lockedScreensChanged();
        Q_EMIT settingsChanged();
    }
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

SETTINGS_SETTER_CLAMPED_QREAL(ActiveOpacity, m_activeOpacity, activeOpacityChanged, ConfigDefaults::activeOpacityMin(),
                              ConfigDefaults::activeOpacityMax())
SETTINGS_SETTER_CLAMPED_QREAL(InactiveOpacity, m_inactiveOpacity, inactiveOpacityChanged,
                              ConfigDefaults::inactiveOpacityMin(), ConfigDefaults::inactiveOpacityMax())

SETTINGS_SETTER_CLAMPED(BorderWidth, m_borderWidth, borderWidthChanged, ConfigDefaults::borderWidthMin(),
                        ConfigDefaults::borderWidthMax())
SETTINGS_SETTER_CLAMPED(BorderRadius, m_borderRadius, borderRadiusChanged, ConfigDefaults::borderRadiusMin(),
                        ConfigDefaults::borderRadiusMax())

SETTINGS_SETTER(bool, EnableBlur, m_enableBlur, enableBlurChanged)
SETTINGS_SETTER(const QString&, LabelFontFamily, m_labelFontFamily, labelFontFamilyChanged)
SETTINGS_SETTER_CLAMPED(LabelFontWeight, m_labelFontWeight, labelFontWeightChanged,
                        ConfigDefaults::labelFontWeightMin(), ConfigDefaults::labelFontWeightMax())
SETTINGS_SETTER(bool, LabelFontItalic, m_labelFontItalic, labelFontItalicChanged)
SETTINGS_SETTER(bool, LabelFontUnderline, m_labelFontUnderline, labelFontUnderlineChanged)
SETTINGS_SETTER(bool, LabelFontStrikeout, m_labelFontStrikeout, labelFontStrikeoutChanged)

SETTINGS_SETTER_CLAMPED_QREAL(LabelFontSizeScale, m_labelFontSizeScale, labelFontSizeScaleChanged,
                              ConfigDefaults::labelFontSizeScaleMin(), ConfigDefaults::labelFontSizeScaleMax())

// ═══════════════════════════════════════════════════════════════════════════════
// Zone geometry setters
// ═══════════════════════════════════════════════════════════════════════════════

SETTINGS_SETTER_CLAMPED(ZonePadding, m_zonePadding, zonePaddingChanged, ConfigDefaults::zonePaddingMin(),
                        ConfigDefaults::zonePaddingMax())
SETTINGS_SETTER_CLAMPED(OuterGap, m_outerGap, outerGapChanged, ConfigDefaults::outerGapMin(),
                        ConfigDefaults::outerGapMax())
SETTINGS_SETTER(bool, UsePerSideOuterGap, m_usePerSideOuterGap, usePerSideOuterGapChanged)
SETTINGS_SETTER_CLAMPED(OuterGapTop, m_outerGapTop, outerGapTopChanged, ConfigDefaults::outerGapTopMin(),
                        ConfigDefaults::outerGapTopMax())
SETTINGS_SETTER_CLAMPED(OuterGapBottom, m_outerGapBottom, outerGapBottomChanged, ConfigDefaults::outerGapBottomMin(),
                        ConfigDefaults::outerGapBottomMax())
SETTINGS_SETTER_CLAMPED(OuterGapLeft, m_outerGapLeft, outerGapLeftChanged, ConfigDefaults::outerGapLeftMin(),
                        ConfigDefaults::outerGapLeftMax())
SETTINGS_SETTER_CLAMPED(OuterGapRight, m_outerGapRight, outerGapRightChanged, ConfigDefaults::outerGapRightMin(),
                        ConfigDefaults::outerGapRightMax())
SETTINGS_SETTER_CLAMPED(AdjacentThreshold, m_adjacentThreshold, adjacentThresholdChanged,
                        ConfigDefaults::adjacentThresholdMin(), ConfigDefaults::adjacentThresholdMax())
SETTINGS_SETTER_CLAMPED(PollIntervalMs, m_pollIntervalMs, pollIntervalMsChanged, ConfigDefaults::pollIntervalMsMin(),
                        ConfigDefaults::pollIntervalMsMax())
SETTINGS_SETTER_CLAMPED(MinimumZoneSizePx, m_minimumZoneSizePx, minimumZoneSizePxChanged,
                        ConfigDefaults::minimumZoneSizePxMin(), ConfigDefaults::minimumZoneSizePxMax())
SETTINGS_SETTER_CLAMPED(MinimumZoneDisplaySizePx, m_minimumZoneDisplaySizePx, minimumZoneDisplaySizePxChanged,
                        ConfigDefaults::minimumZoneDisplaySizePxMin(), ConfigDefaults::minimumZoneDisplaySizePxMax())

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

void Settings::setFilterLayoutsByAspectRatio(bool filter)
{
    if (m_filterLayoutsByAspectRatio != filter) {
        m_filterLayoutsByAspectRatio = filter;
        Q_EMIT filterLayoutsByAspectRatioChanged();
        Q_EMIT settingsChanged();
    }
}

SETTINGS_SETTER(const QStringList&, ExcludedApplications, m_excludedApplications, excludedApplicationsChanged)

void Settings::addExcludedApplication(const QString& app)
{
    const QString trimmed = app.trimmed();
    if (trimmed.isEmpty() || m_excludedApplications.contains(trimmed)) {
        return;
    }
    m_excludedApplications.append(trimmed);
    Q_EMIT excludedApplicationsChanged();
    Q_EMIT settingsChanged();
}

void Settings::removeExcludedApplicationAt(int index)
{
    if (index < 0 || index >= m_excludedApplications.size()) {
        return;
    }
    m_excludedApplications.removeAt(index);
    Q_EMIT excludedApplicationsChanged();
    Q_EMIT settingsChanged();
}

SETTINGS_SETTER(const QStringList&, ExcludedWindowClasses, m_excludedWindowClasses, excludedWindowClassesChanged)

void Settings::addExcludedWindowClass(const QString& cls)
{
    const QString trimmed = cls.trimmed();
    if (trimmed.isEmpty() || m_excludedWindowClasses.contains(trimmed)) {
        return;
    }
    m_excludedWindowClasses.append(trimmed);
    Q_EMIT excludedWindowClassesChanged();
    Q_EMIT settingsChanged();
}

void Settings::removeExcludedWindowClassAt(int index)
{
    if (index < 0 || index >= m_excludedWindowClasses.size()) {
        return;
    }
    m_excludedWindowClasses.removeAt(index);
    Q_EMIT excludedWindowClassesChanged();
    Q_EMIT settingsChanged();
}

SETTINGS_SETTER(bool, ExcludeTransientWindows, m_excludeTransientWindows, excludeTransientWindowsChanged)
SETTINGS_SETTER_CLAMPED(MinimumWindowWidth, m_minimumWindowWidth, minimumWindowWidthChanged,
                        ConfigDefaults::minimumWindowWidthMin(), ConfigDefaults::minimumWindowWidthMax())
SETTINGS_SETTER_CLAMPED(MinimumWindowHeight, m_minimumWindowHeight, minimumWindowHeightChanged,
                        ConfigDefaults::minimumWindowHeightMin(), ConfigDefaults::minimumWindowHeightMax())

// ═══════════════════════════════════════════════════════════════════════════════
// Zone Selector setters
// ═══════════════════════════════════════════════════════════════════════════════

SETTINGS_SETTER(bool, ZoneSelectorEnabled, m_zoneSelectorEnabled, zoneSelectorEnabledChanged)
SETTINGS_SETTER_CLAMPED(ZoneSelectorTriggerDistance, m_zoneSelectorTriggerDistance, zoneSelectorTriggerDistanceChanged,
                        ConfigDefaults::triggerDistanceMin(), ConfigDefaults::triggerDistanceMax())
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

SETTINGS_SETTER_CLAMPED(ZoneSelectorPreviewWidth, m_zoneSelectorPreviewWidth, zoneSelectorPreviewWidthChanged,
                        ConfigDefaults::previewWidthMin(), ConfigDefaults::previewWidthMax())
SETTINGS_SETTER_CLAMPED(ZoneSelectorPreviewHeight, m_zoneSelectorPreviewHeight, zoneSelectorPreviewHeightChanged,
                        ConfigDefaults::previewHeightMin(), ConfigDefaults::previewHeightMax())
SETTINGS_SETTER(bool, ZoneSelectorPreviewLockAspect, m_zoneSelectorPreviewLockAspect,
                zoneSelectorPreviewLockAspectChanged)
SETTINGS_SETTER_CLAMPED(ZoneSelectorGridColumns, m_zoneSelectorGridColumns, zoneSelectorGridColumnsChanged,
                        ConfigDefaults::gridColumnsMin(), ConfigDefaults::gridColumnsMax())
SETTINGS_SETTER(ZoneSelectorSizeMode, ZoneSelectorSizeMode, m_zoneSelectorSizeMode, zoneSelectorSizeModeChanged)

SETTINGS_SETTER_ENUM_INT(ZoneSelectorSizeMode, ZoneSelectorSizeMode, 0, static_cast<int>(ZoneSelectorSizeMode::Manual))

SETTINGS_SETTER_CLAMPED(ZoneSelectorMaxRows, m_zoneSelectorMaxRows, zoneSelectorMaxRowsChanged,
                        ConfigDefaults::maxRowsMin(), ConfigDefaults::maxRowsMax())

// ═══════════════════════════════════════════════════════════════════════════════
// Autotiling setters
// ═══════════════════════════════════════════════════════════════════════════════

SETTINGS_SETTER(bool, AutotileEnabled, m_autotileEnabled, autotileEnabledChanged)

void Settings::setDefaultAutotileAlgorithm(const QString& algorithm)
{
    // Validate algorithm ID against the algorithm registry (single source of truth)
    QString validatedAlgorithm = algorithm;
    if (!algorithm.startsWith(QLatin1String("script:")) && !AlgorithmRegistry::instance()->algorithm(algorithm)) {
        qCWarning(lcConfig) << "Unknown autotile algorithm:" << algorithm << "- using default";
        validatedAlgorithm = AlgorithmRegistry::defaultAlgorithmId();
    }

    if (m_defaultAutotileAlgorithm != validatedAlgorithm) {
        m_defaultAutotileAlgorithm = validatedAlgorithm;
        Q_EMIT defaultAutotileAlgorithmChanged();
        Q_EMIT settingsChanged();
    }
}

SETTINGS_SETTER_CLAMPED_QREAL(AutotileSplitRatio, m_autotileSplitRatio, autotileSplitRatioChanged,
                              ConfigDefaults::autotileSplitRatioMin(), ConfigDefaults::autotileSplitRatioMax())
SETTINGS_SETTER_CLAMPED(AutotileMasterCount, m_autotileMasterCount, autotileMasterCountChanged,
                        ConfigDefaults::autotileMasterCountMin(), ConfigDefaults::autotileMasterCountMax())
void Settings::setAutotilePerAlgorithmSettings(const QVariantMap& value)
{
    // Round-trip sanitize: same validation the load() path uses
    auto sanitized = AutotileConfig::perAlgoToVariantMap(AutotileConfig::perAlgoFromVariantMap(value));
    if (m_autotilePerAlgorithmSettings != sanitized) {
        m_autotilePerAlgorithmSettings = sanitized;
        Q_EMIT autotilePerAlgorithmSettingsChanged();
        Q_EMIT settingsChanged();
    }
}
SETTINGS_SETTER_CLAMPED(AutotileInnerGap, m_autotileInnerGap, autotileInnerGapChanged,
                        ConfigDefaults::autotileInnerGapMin(), ConfigDefaults::autotileInnerGapMax())
SETTINGS_SETTER_CLAMPED(AutotileOuterGap, m_autotileOuterGap, autotileOuterGapChanged,
                        ConfigDefaults::autotileOuterGapMin(), ConfigDefaults::autotileOuterGapMax())
SETTINGS_SETTER(bool, AutotileUsePerSideOuterGap, m_autotileUsePerSideOuterGap, autotileUsePerSideOuterGapChanged)
SETTINGS_SETTER_CLAMPED(AutotileOuterGapTop, m_autotileOuterGapTop, autotileOuterGapTopChanged,
                        ConfigDefaults::autotileOuterGapTopMin(), ConfigDefaults::autotileOuterGapTopMax())
SETTINGS_SETTER_CLAMPED(AutotileOuterGapBottom, m_autotileOuterGapBottom, autotileOuterGapBottomChanged,
                        ConfigDefaults::autotileOuterGapBottomMin(), ConfigDefaults::autotileOuterGapBottomMax())
SETTINGS_SETTER_CLAMPED(AutotileOuterGapLeft, m_autotileOuterGapLeft, autotileOuterGapLeftChanged,
                        ConfigDefaults::autotileOuterGapLeftMin(), ConfigDefaults::autotileOuterGapLeftMax())
SETTINGS_SETTER_CLAMPED(AutotileOuterGapRight, m_autotileOuterGapRight, autotileOuterGapRightChanged,
                        ConfigDefaults::autotileOuterGapRightMin(), ConfigDefaults::autotileOuterGapRightMax())

SETTINGS_SETTER(bool, AutotileFocusNewWindows, m_autotileFocusNewWindows, autotileFocusNewWindowsChanged)
SETTINGS_SETTER(bool, AutotileSmartGaps, m_autotileSmartGaps, autotileSmartGapsChanged)
SETTINGS_SETTER_CLAMPED(AutotileMaxWindows, m_autotileMaxWindows, autotileMaxWindowsChanged,
                        ConfigDefaults::autotileMaxWindowsMin(), ConfigDefaults::autotileMaxWindowsMax())
SETTINGS_SETTER(AutotileInsertPosition, AutotileInsertPosition, m_autotileInsertPosition, autotileInsertPositionChanged)

SETTINGS_SETTER_ENUM_INT(AutotileInsertPosition, AutotileInsertPosition, ConfigDefaults::autotileInsertPositionMin(),
                         ConfigDefaults::autotileInsertPositionMax())

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
SETTINGS_SETTER_CLAMPED(AnimationDuration, m_animationDuration, animationDurationChanged,
                        ConfigDefaults::animationDurationMin(), ConfigDefaults::animationDurationMax())
SETTINGS_SETTER(const QString&, AnimationEasingCurve, m_animationEasingCurve, animationEasingCurveChanged)

SETTINGS_SETTER_CLAMPED(AnimationMinDistance, m_animationMinDistance, animationMinDistanceChanged,
                        ConfigDefaults::animationMinDistanceMin(), ConfigDefaults::animationMinDistanceMax())
SETTINGS_SETTER_CLAMPED(AnimationSequenceMode, m_animationSequenceMode, animationSequenceModeChanged,
                        ConfigDefaults::animationSequenceModeMin(), ConfigDefaults::animationSequenceModeMax())
SETTINGS_SETTER_CLAMPED(AnimationStaggerInterval, m_animationStaggerInterval, animationStaggerIntervalChanged,
                        ConfigDefaults::animationStaggerIntervalMin(), ConfigDefaults::animationStaggerIntervalMax())
SETTINGS_SETTER(bool, AutotileFocusFollowsMouse, m_autotileFocusFollowsMouse, autotileFocusFollowsMouseChanged)
SETTINGS_SETTER(bool, AutotileRespectMinimumSize, m_autotileRespectMinimumSize, autotileRespectMinimumSizeChanged)
SETTINGS_SETTER(bool, AutotileHideTitleBars, m_autotileHideTitleBars, autotileHideTitleBarsChanged)
SETTINGS_SETTER(bool, AutotileShowBorder, m_autotileShowBorder, autotileShowBorderChanged)

SETTINGS_SETTER_CLAMPED(AutotileBorderWidth, m_autotileBorderWidth, autotileBorderWidthChanged,
                        ConfigDefaults::autotileBorderWidthMin(), ConfigDefaults::autotileBorderWidthMax())
SETTINGS_SETTER_CLAMPED(AutotileBorderRadius, m_autotileBorderRadius, autotileBorderRadiusChanged,
                        ConfigDefaults::autotileBorderRadiusMin(), ConfigDefaults::autotileBorderRadiusMax())

SETTINGS_SETTER(const QColor&, AutotileBorderColor, m_autotileBorderColor, autotileBorderColorChanged)
SETTINGS_SETTER(const QColor&, AutotileInactiveBorderColor, m_autotileInactiveBorderColor,
                autotileInactiveBorderColorChanged)

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

void Settings::setLockedScreens(const QStringList& screens)
{
    if (m_lockedScreens != screens) {
        m_lockedScreens = screens;
        Q_EMIT lockedScreensChanged();
        Q_EMIT settingsChanged();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Rendering
// ═══════════════════════════════════════════════════════════════════════════════

void Settings::setRenderingBackend(const QString& backend)
{
    const QString value = ConfigDefaults::normalizeRenderingBackend(backend);
    if (m_renderingBackend != value) {
        m_renderingBackend = value;
        Q_EMIT renderingBackendChanged();
        Q_EMIT settingsChanged();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Shader Effects
// ═══════════════════════════════════════════════════════════════════════════════

SETTINGS_SETTER(bool, EnableShaderEffects, m_enableShaderEffects, enableShaderEffectsChanged)
SETTINGS_SETTER_CLAMPED(ShaderFrameRate, m_shaderFrameRate, shaderFrameRateChanged,
                        ConfigDefaults::shaderFrameRateMin(), ConfigDefaults::shaderFrameRateMax())
SETTINGS_SETTER(bool, EnableAudioVisualizer, m_enableAudioVisualizer, enableAudioVisualizerChanged)
SETTINGS_SETTER_CLAMPED(AudioSpectrumBarCount, m_audioSpectrumBarCount, audioSpectrumBarCountChanged,
                        ConfigDefaults::audioSpectrumBarCountMin(), ConfigDefaults::audioSpectrumBarCountMax())

// ═══════════════════════════════════════════════════════════════════════════════
// Shortcut setters
// ═══════════════════════════════════════════════════════════════════════════════

SETTINGS_SETTER(const QString&, OpenEditorShortcut, m_openEditorShortcut, openEditorShortcutChanged)
SETTINGS_SETTER(const QString&, OpenSettingsShortcut, m_openSettingsShortcut, openSettingsShortcutChanged)
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
SETTINGS_SETTER(const QString&, ToggleLayoutLockShortcut, m_toggleLayoutLockShortcut, toggleLayoutLockShortcutChanged)

// ═══════════════════════════════════════════════════════════════════════════════
// Virtual screen config setters
// ═══════════════════════════════════════════════════════════════════════════════

QHash<QString, VirtualScreenConfig> Settings::virtualScreenConfigs() const
{
    return m_virtualScreenConfigs;
}

void Settings::setVirtualScreenConfigs(const QHash<QString, VirtualScreenConfig>& configs)
{
    if (m_virtualScreenConfigs != configs) {
        m_virtualScreenConfigs = configs;
        save();
        Q_EMIT virtualScreenConfigsChanged();
    }
}

void Settings::setVirtualScreenConfig(const QString& physicalScreenId, const VirtualScreenConfig& config)
{
    if (config.screens.isEmpty()) {
        if (!m_virtualScreenConfigs.contains(physicalScreenId))
            return;
        m_virtualScreenConfigs.remove(physicalScreenId);
    } else {
        if (m_virtualScreenConfigs.value(physicalScreenId) == config)
            return;
        m_virtualScreenConfigs.insert(physicalScreenId, config);
    }
    save();
    Q_EMIT virtualScreenConfigsChanged();
}

VirtualScreenConfig Settings::virtualScreenConfig(const QString& physicalScreenId) const
{
    return m_virtualScreenConfigs.value(physicalScreenId);
}

} // namespace PlasmaZones
