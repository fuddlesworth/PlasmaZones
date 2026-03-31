// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../settings.h"
#include "../configdefaults.h"
#include "../../core/constants.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../autotile/AutotileConfig.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace PlasmaZones {

// ── load() helpers ───────────────────────────────────────────────────────────

void Settings::loadActivationConfig(QSettingsConfigGroup& activation)
{
    m_dragActivationTriggers = parseTriggerListJson(activation.readString(ConfigDefaults::dragActivationTriggersKey()))
                                   .value_or(ConfigDefaults::dragActivationTriggers());

    m_zoneSpanEnabled = activation.readBool(ConfigDefaults::zoneSpanEnabledKey(), ConfigDefaults::zoneSpanEnabled());

    int spanMod = activation.readInt(ConfigDefaults::zoneSpanModifierKey(), ConfigDefaults::zoneSpanModifier());
    if (spanMod < 0 || spanMod > static_cast<int>(DragModifier::CtrlAltMeta)) {
        qCWarning(lcConfig) << "Invalid ZoneSpanModifier value:" << spanMod << "- using default";
        spanMod = ConfigDefaults::zoneSpanModifier();
    }
    m_zoneSpanModifier = static_cast<DragModifier>(spanMod);

    auto parsedSpanTriggers = parseTriggerListJson(activation.readString(ConfigDefaults::zoneSpanTriggersKey()));
    if (parsedSpanTriggers.has_value()) {
        m_zoneSpanTriggers = *parsedSpanTriggers;
    } else {
        // No valid JSON — build default trigger from the actual spanMod value read above
        QVariantMap trigger;
        trigger[ConfigDefaults::triggerModifierField()] = spanMod;
        trigger[ConfigDefaults::triggerMouseButtonField()] = 0;
        m_zoneSpanTriggers = {trigger};
    }

    m_toggleActivation = activation.readBool(ConfigDefaults::toggleActivationKey(), ConfigDefaults::toggleActivation());
    m_snappingEnabled = activation.readBool(ConfigDefaults::snappingEnabledKey(), ConfigDefaults::snappingEnabled());
}

void Settings::loadDisplayConfig(QSettingsConfigGroup& display)
{
    m_showZonesOnAllMonitors =
        display.readBool(ConfigDefaults::showOnAllMonitorsKey(), ConfigDefaults::showOnAllMonitors());
    // DisabledMonitors is a comma-separated string list
    QString disabledMonitorsStr = display.readString(ConfigDefaults::disabledMonitorsKey());
    m_disabledMonitors =
        disabledMonitorsStr.isEmpty() ? QStringList() : disabledMonitorsStr.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (int i = 0; i < m_disabledMonitors.size(); ++i) {
        m_disabledMonitors[i] = m_disabledMonitors[i].trimmed();
        const QString& name = m_disabledMonitors[i];
        if (Utils::isConnectorName(name)) {
            QString resolved = Utils::screenIdForName(name);
            if (resolved != name) {
                m_disabledMonitors[i] = resolved;
            }
        }
    }
    // DisabledDesktops is a comma-separated list of "screenId/desktopNumber" composite keys
    QString disabledDesktopsStr = display.readString(ConfigDefaults::disabledDesktopsKey());
    m_disabledDesktops =
        disabledDesktopsStr.isEmpty() ? QStringList() : disabledDesktopsStr.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (int i = 0; i < m_disabledDesktops.size(); ++i) {
        m_disabledDesktops[i] = m_disabledDesktops[i].trimmed();
    }
    // DisabledActivities is a comma-separated list of "screenId/activityUuid" composite keys
    QString disabledActivitiesStr = display.readString(ConfigDefaults::disabledActivitiesKey());
    m_disabledActivities = disabledActivitiesStr.isEmpty()
        ? QStringList()
        : disabledActivitiesStr.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (int i = 0; i < m_disabledActivities.size(); ++i) {
        m_disabledActivities[i] = m_disabledActivities[i].trimmed();
    }
    m_showZoneNumbers = display.readBool(ConfigDefaults::showNumbersKey(), ConfigDefaults::showNumbers());
    m_flashZonesOnSwitch = display.readBool(ConfigDefaults::flashOnSwitchKey(), ConfigDefaults::flashOnSwitch());
    m_showOsdOnLayoutSwitch =
        display.readBool(ConfigDefaults::showOsdOnLayoutSwitchKey(), ConfigDefaults::showOsdOnLayoutSwitch());
    m_showNavigationOsd = display.readBool(ConfigDefaults::showNavigationOsdKey(), ConfigDefaults::showNavigationOsd());
    m_osdStyle = static_cast<OsdStyle>(readValidatedInt(display, ConfigDefaults::osdStyleKey(),
                                                        ConfigDefaults::osdStyle(), ConfigDefaults::osdStyleMin(),
                                                        ConfigDefaults::osdStyleMax(), "OSD style"));
    m_overlayDisplayMode = static_cast<OverlayDisplayMode>(readValidatedInt(
        display, ConfigDefaults::overlayDisplayModeKey(), ConfigDefaults::overlayDisplayMode(),
        ConfigDefaults::overlayDisplayModeMin(), ConfigDefaults::overlayDisplayModeMax(), "overlay display mode"));
}

void Settings::loadAppearanceConfig(QSettingsConfigGroup& appearance)
{
    m_useSystemColors = appearance.readBool(ConfigDefaults::useSystemColorsKey(), ConfigDefaults::useSystemColors());
    m_highlightColor = readValidatedColor(appearance, ConfigDefaults::highlightColorKey(),
                                          ConfigDefaults::highlightColor(), "highlight");
    m_inactiveColor =
        readValidatedColor(appearance, ConfigDefaults::inactiveColorKey(), ConfigDefaults::inactiveColor(), "inactive");
    m_borderColor =
        readValidatedColor(appearance, ConfigDefaults::borderColorKey(), ConfigDefaults::borderColor(), "border");
    m_labelFontColor = readValidatedColor(appearance, ConfigDefaults::labelFontColorKey(),
                                          ConfigDefaults::labelFontColor(), "label font");

    qreal activeOpacity = appearance.readDouble(ConfigDefaults::activeOpacityKey(), ConfigDefaults::activeOpacity());
    if (activeOpacity < ConfigDefaults::activeOpacityMin() || activeOpacity > ConfigDefaults::activeOpacityMax()) {
        qCWarning(lcConfig) << "Invalid active opacity:" << activeOpacity << "clamping to valid range";
        activeOpacity = qBound(ConfigDefaults::activeOpacityMin(), activeOpacity, ConfigDefaults::activeOpacityMax());
    }
    m_activeOpacity = activeOpacity;

    qreal inactiveOpacity =
        appearance.readDouble(ConfigDefaults::inactiveOpacityKey(), ConfigDefaults::inactiveOpacity());
    if (inactiveOpacity < ConfigDefaults::inactiveOpacityMin()
        || inactiveOpacity > ConfigDefaults::inactiveOpacityMax()) {
        qCWarning(lcConfig) << "Invalid inactive opacity:" << inactiveOpacity << "clamping to valid range";
        inactiveOpacity =
            qBound(ConfigDefaults::inactiveOpacityMin(), inactiveOpacity, ConfigDefaults::inactiveOpacityMax());
    }
    m_inactiveOpacity = inactiveOpacity;

    m_borderWidth =
        readValidatedInt(appearance, ConfigDefaults::borderWidthKey(), ConfigDefaults::borderWidth(),
                         ConfigDefaults::borderWidthMin(), ConfigDefaults::borderWidthMax(), "border width");
    m_borderRadius =
        readValidatedInt(appearance, ConfigDefaults::borderRadiusKey(), ConfigDefaults::borderRadius(),
                         ConfigDefaults::borderRadiusMin(), ConfigDefaults::borderRadiusMax(), "border radius");
    m_enableBlur = appearance.readBool(ConfigDefaults::enableBlurKey(), ConfigDefaults::enableBlur());
    m_labelFontFamily = appearance.readString(ConfigDefaults::labelFontFamilyKey(), ConfigDefaults::labelFontFamily());
    qreal fontScale =
        appearance.readDouble(ConfigDefaults::labelFontSizeScaleKey(), ConfigDefaults::labelFontSizeScale());
    m_labelFontSizeScale =
        qBound(ConfigDefaults::labelFontSizeScaleMin(), fontScale, ConfigDefaults::labelFontSizeScaleMax());
    m_labelFontWeight = readValidatedInt(appearance, ConfigDefaults::labelFontWeightKey(),
                                         ConfigDefaults::labelFontWeight(), ConfigDefaults::labelFontWeightMin(),
                                         ConfigDefaults::labelFontWeightMax(), "label font weight");
    m_labelFontItalic = appearance.readBool(ConfigDefaults::labelFontItalicKey(), ConfigDefaults::labelFontItalic());
    m_labelFontUnderline =
        appearance.readBool(ConfigDefaults::labelFontUnderlineKey(), ConfigDefaults::labelFontUnderline());
    m_labelFontStrikeout =
        appearance.readBool(ConfigDefaults::labelFontStrikeoutKey(), ConfigDefaults::labelFontStrikeout());
}

void Settings::loadZoneGeometryConfig(QSettingsConfigGroup& zones)
{
    m_zonePadding =
        readValidatedInt(zones, ConfigDefaults::zonePaddingKey(), ConfigDefaults::zonePadding(),
                         ConfigDefaults::zonePaddingMin(), ConfigDefaults::zonePaddingMax(), "zone padding");
    m_outerGap = readValidatedInt(zones, ConfigDefaults::outerGapKey(), ConfigDefaults::outerGap(),
                                  ConfigDefaults::outerGapMin(), ConfigDefaults::outerGapMax(), "outer gap");
    m_usePerSideOuterGap =
        zones.readBool(ConfigDefaults::usePerSideOuterGapKey(), ConfigDefaults::usePerSideOuterGap());
    m_outerGapTop =
        readValidatedInt(zones, ConfigDefaults::outerGapTopKey(), ConfigDefaults::outerGapTop(),
                         ConfigDefaults::outerGapTopMin(), ConfigDefaults::outerGapTopMax(), "outer gap top");
    m_outerGapBottom =
        readValidatedInt(zones, ConfigDefaults::outerGapBottomKey(), ConfigDefaults::outerGapBottom(),
                         ConfigDefaults::outerGapBottomMin(), ConfigDefaults::outerGapBottomMax(), "outer gap bottom");
    m_outerGapLeft =
        readValidatedInt(zones, ConfigDefaults::outerGapLeftKey(), ConfigDefaults::outerGapLeft(),
                         ConfigDefaults::outerGapLeftMin(), ConfigDefaults::outerGapLeftMax(), "outer gap left");
    m_outerGapRight =
        readValidatedInt(zones, ConfigDefaults::outerGapRightKey(), ConfigDefaults::outerGapRight(),
                         ConfigDefaults::outerGapRightMin(), ConfigDefaults::outerGapRightMax(), "outer gap right");
    m_adjacentThreshold = readValidatedInt(zones, ConfigDefaults::adjacentThresholdKey(),
                                           ConfigDefaults::adjacentThreshold(), ConfigDefaults::adjacentThresholdMin(),
                                           ConfigDefaults::adjacentThresholdMax(), "adjacent threshold");
    m_pollIntervalMs =
        readValidatedInt(zones, ConfigDefaults::pollIntervalMsKey(), ConfigDefaults::pollIntervalMs(),
                         ConfigDefaults::pollIntervalMsMin(), ConfigDefaults::pollIntervalMsMax(), "poll interval");
    m_minimumZoneSizePx = readValidatedInt(zones, ConfigDefaults::minimumZoneSizePxKey(),
                                           ConfigDefaults::minimumZoneSizePx(), ConfigDefaults::minimumZoneSizePxMin(),
                                           ConfigDefaults::minimumZoneSizePxMax(), "minimum zone size");
    m_minimumZoneDisplaySizePx =
        readValidatedInt(zones, ConfigDefaults::minimumZoneDisplaySizePxKey(),
                         ConfigDefaults::minimumZoneDisplaySizePx(), ConfigDefaults::minimumZoneDisplaySizePxMin(),
                         ConfigDefaults::minimumZoneDisplaySizePxMax(), "minimum zone display size");
}

void Settings::loadBehaviorConfig(QSettingsConfigBackend* backend)
{
    {
        auto behavior = backend->group(ConfigDefaults::behaviorGroup());
        m_keepWindowsInZonesOnResolutionChange = behavior->readBool(
            ConfigDefaults::keepOnResolutionChangeKey(), ConfigDefaults::keepWindowsInZonesOnResolutionChange());
        m_moveNewWindowsToLastZone =
            behavior->readBool(ConfigDefaults::moveNewToLastZoneKey(), ConfigDefaults::moveNewWindowsToLastZone());
        m_restoreOriginalSizeOnUnsnap =
            behavior->readBool(ConfigDefaults::restoreSizeOnUnsnapKey(), ConfigDefaults::restoreOriginalSizeOnUnsnap());
        int stickyHandling =
            behavior->readInt(ConfigDefaults::stickyWindowHandlingKey(), ConfigDefaults::stickyWindowHandling());
        m_stickyWindowHandling = static_cast<StickyWindowHandling>(
            qBound(static_cast<int>(StickyWindowHandling::TreatAsNormal), stickyHandling,
                   static_cast<int>(StickyWindowHandling::IgnoreAll)));
        m_restoreWindowsToZonesOnLogin = behavior->readBool(ConfigDefaults::restoreWindowsToZonesOnLoginKey(),
                                                            ConfigDefaults::restoreWindowsToZonesOnLogin());
        m_defaultLayoutId = normalizeUuidString(behavior->readString(ConfigDefaults::defaultLayoutIdKey()));
        m_filterLayoutsByAspectRatio = behavior->readBool(ConfigDefaults::filterLayoutsByAspectRatioKey(),
                                                          ConfigDefaults::filterLayoutsByAspectRatio());
    }

    {
        auto activation = backend->group(ConfigDefaults::activationGroup());
        m_snapAssistFeatureEnabled = activation->readBool(ConfigDefaults::snapAssistFeatureEnabledKey(),
                                                          ConfigDefaults::snapAssistFeatureEnabled());
        m_snapAssistEnabled =
            activation->readBool(ConfigDefaults::snapAssistEnabledKey(), ConfigDefaults::snapAssistEnabled());
        m_snapAssistTriggers = parseTriggerListJson(activation->readString(ConfigDefaults::snapAssistTriggersKey()))
                                   .value_or(ConfigDefaults::snapAssistTriggers());
    }

    // Exclusions
    {
        auto exclusions = backend->group(ConfigDefaults::exclusionsGroup());
        QString excludedAppsStr = exclusions->readString(ConfigDefaults::excludedApplicationsKey());
        m_excludedApplications = excludedAppsStr.isEmpty() ? QStringList() : excludedAppsStr.split(QLatin1Char(','));
        for (auto& app : m_excludedApplications)
            app = app.trimmed();
        QString excludedClassesStr = exclusions->readString(ConfigDefaults::excludedWindowClassesKey());
        m_excludedWindowClasses =
            excludedClassesStr.isEmpty() ? QStringList() : excludedClassesStr.split(QLatin1Char(','));
        for (auto& cls : m_excludedWindowClasses)
            cls = cls.trimmed();
        m_excludeTransientWindows = exclusions->readBool(ConfigDefaults::excludeTransientWindowsKey(),
                                                         ConfigDefaults::excludeTransientWindows());
        int minWidth =
            exclusions->readInt(ConfigDefaults::minimumWindowWidthKey(), ConfigDefaults::minimumWindowWidth());
        m_minimumWindowWidth =
            qBound(ConfigDefaults::minimumWindowWidthMin(), minWidth, ConfigDefaults::minimumWindowWidthMax());
        int minHeight =
            exclusions->readInt(ConfigDefaults::minimumWindowHeightKey(), ConfigDefaults::minimumWindowHeight());
        m_minimumWindowHeight =
            qBound(ConfigDefaults::minimumWindowHeightMin(), minHeight, ConfigDefaults::minimumWindowHeightMax());
    }
}

void Settings::loadZoneSelectorConfig(QSettingsConfigGroup& zoneSelector)
{
    m_zoneSelectorEnabled =
        zoneSelector.readBool(ConfigDefaults::zoneSelectorEnabledKey(), ConfigDefaults::zoneSelectorEnabled());
    m_zoneSelectorTriggerDistance = readValidatedInt(
        zoneSelector, ConfigDefaults::zoneSelectorTriggerDistanceKey(), ConfigDefaults::triggerDistance(),
        ConfigDefaults::triggerDistanceMin(), ConfigDefaults::triggerDistanceMax(), "zone selector trigger distance");
    int selectorPos = zoneSelector.readInt(ConfigDefaults::zoneSelectorPositionKey(), ConfigDefaults::position());
    if (selectorPos >= 0 && selectorPos <= 8) {
        m_zoneSelectorPosition = static_cast<ZoneSelectorPosition>(selectorPos);
    } else {
        m_zoneSelectorPosition = static_cast<ZoneSelectorPosition>(ConfigDefaults::position());
    }
    int selectorMode = zoneSelector.readInt(ConfigDefaults::zoneSelectorLayoutModeKey(), ConfigDefaults::layoutMode());
    m_zoneSelectorLayoutMode = static_cast<ZoneSelectorLayoutMode>(
        qBound(0, selectorMode, static_cast<int>(ZoneSelectorLayoutMode::Vertical)));
    m_zoneSelectorPreviewWidth = readValidatedInt(zoneSelector, ConfigDefaults::zoneSelectorPreviewWidthKey(),
                                                  ConfigDefaults::previewWidth(), ConfigDefaults::previewWidthMin(),
                                                  ConfigDefaults::previewWidthMax(), "zone selector preview width");
    m_zoneSelectorPreviewHeight = readValidatedInt(zoneSelector, ConfigDefaults::zoneSelectorPreviewHeightKey(),
                                                   ConfigDefaults::previewHeight(), ConfigDefaults::previewHeightMin(),
                                                   ConfigDefaults::previewHeightMax(), "zone selector preview height");
    m_zoneSelectorPreviewLockAspect =
        zoneSelector.readBool(ConfigDefaults::zoneSelectorPreviewLockAspectKey(), ConfigDefaults::previewLockAspect());
    m_zoneSelectorGridColumns = readValidatedInt(zoneSelector, ConfigDefaults::zoneSelectorGridColumnsKey(),
                                                 ConfigDefaults::gridColumns(), ConfigDefaults::gridColumnsMin(),
                                                 ConfigDefaults::gridColumnsMax(), "zone selector grid columns");
    int sizeMode = zoneSelector.readInt(ConfigDefaults::zoneSelectorSizeModeKey(), ConfigDefaults::sizeMode());
    m_zoneSelectorSizeMode =
        static_cast<ZoneSelectorSizeMode>(qBound(0, sizeMode, static_cast<int>(ZoneSelectorSizeMode::Manual)));
    m_zoneSelectorMaxRows =
        readValidatedInt(zoneSelector, ConfigDefaults::zoneSelectorMaxRowsKey(), ConfigDefaults::maxRows(),
                         ConfigDefaults::maxRowsMin(), ConfigDefaults::maxRowsMax(), "zone selector max rows");
}

void Settings::loadShortcutConfig(QSettingsConfigGroup& globalShortcuts)
{
    m_openEditorShortcut =
        globalShortcuts.readString(ConfigDefaults::openEditorShortcutKey(), ConfigDefaults::openEditorShortcut());
    m_openSettingsShortcut =
        globalShortcuts.readString(ConfigDefaults::openSettingsShortcutKey(), ConfigDefaults::openSettingsShortcut());
    m_previousLayoutShortcut = globalShortcuts.readString(ConfigDefaults::previousLayoutShortcutKey(),
                                                          ConfigDefaults::previousLayoutShortcut());
    m_nextLayoutShortcut =
        globalShortcuts.readString(ConfigDefaults::nextLayoutShortcutKey(), ConfigDefaults::nextLayoutShortcut());
    const QString quickLayoutDefaults[9] = {
        ConfigDefaults::quickLayout1Shortcut(), ConfigDefaults::quickLayout2Shortcut(),
        ConfigDefaults::quickLayout3Shortcut(), ConfigDefaults::quickLayout4Shortcut(),
        ConfigDefaults::quickLayout5Shortcut(), ConfigDefaults::quickLayout6Shortcut(),
        ConfigDefaults::quickLayout7Shortcut(), ConfigDefaults::quickLayout8Shortcut(),
        ConfigDefaults::quickLayout9Shortcut()};
    loadIndexedShortcuts(globalShortcuts, ConfigDefaults::quickLayoutShortcutKeyPattern(), m_quickLayoutShortcuts,
                         quickLayoutDefaults);
    m_moveWindowLeftShortcut = globalShortcuts.readString(ConfigDefaults::moveWindowLeftShortcutKey(),
                                                          ConfigDefaults::moveWindowLeftShortcut());
    m_moveWindowRightShortcut = globalShortcuts.readString(ConfigDefaults::moveWindowRightShortcutKey(),
                                                           ConfigDefaults::moveWindowRightShortcut());
    m_moveWindowUpShortcut =
        globalShortcuts.readString(ConfigDefaults::moveWindowUpShortcutKey(), ConfigDefaults::moveWindowUpShortcut());
    m_moveWindowDownShortcut = globalShortcuts.readString(ConfigDefaults::moveWindowDownShortcutKey(),
                                                          ConfigDefaults::moveWindowDownShortcut());
    m_focusZoneLeftShortcut =
        globalShortcuts.readString(ConfigDefaults::focusZoneLeftShortcutKey(), ConfigDefaults::focusZoneLeftShortcut());
    m_focusZoneRightShortcut = globalShortcuts.readString(ConfigDefaults::focusZoneRightShortcutKey(),
                                                          ConfigDefaults::focusZoneRightShortcut());
    m_focusZoneUpShortcut =
        globalShortcuts.readString(ConfigDefaults::focusZoneUpShortcutKey(), ConfigDefaults::focusZoneUpShortcut());
    m_focusZoneDownShortcut =
        globalShortcuts.readString(ConfigDefaults::focusZoneDownShortcutKey(), ConfigDefaults::focusZoneDownShortcut());
    m_pushToEmptyZoneShortcut = globalShortcuts.readString(ConfigDefaults::pushToEmptyZoneShortcutKey(),
                                                           ConfigDefaults::pushToEmptyZoneShortcut());
    m_restoreWindowSizeShortcut = globalShortcuts.readString(ConfigDefaults::restoreWindowSizeShortcutKey(),
                                                             ConfigDefaults::restoreWindowSizeShortcut());
    m_toggleWindowFloatShortcut = globalShortcuts.readString(ConfigDefaults::toggleWindowFloatShortcutKey(),
                                                             ConfigDefaults::toggleWindowFloatShortcut());
    m_swapWindowLeftShortcut = globalShortcuts.readString(ConfigDefaults::swapWindowLeftShortcutKey(),
                                                          ConfigDefaults::swapWindowLeftShortcut());
    m_swapWindowRightShortcut = globalShortcuts.readString(ConfigDefaults::swapWindowRightShortcutKey(),
                                                           ConfigDefaults::swapWindowRightShortcut());
    m_swapWindowUpShortcut =
        globalShortcuts.readString(ConfigDefaults::swapWindowUpShortcutKey(), ConfigDefaults::swapWindowUpShortcut());
    m_swapWindowDownShortcut = globalShortcuts.readString(ConfigDefaults::swapWindowDownShortcutKey(),
                                                          ConfigDefaults::swapWindowDownShortcut());
    const QString snapToZoneDefaults[9] = {ConfigDefaults::snapToZone1Shortcut(), ConfigDefaults::snapToZone2Shortcut(),
                                           ConfigDefaults::snapToZone3Shortcut(), ConfigDefaults::snapToZone4Shortcut(),
                                           ConfigDefaults::snapToZone5Shortcut(), ConfigDefaults::snapToZone6Shortcut(),
                                           ConfigDefaults::snapToZone7Shortcut(), ConfigDefaults::snapToZone8Shortcut(),
                                           ConfigDefaults::snapToZone9Shortcut()};
    loadIndexedShortcuts(globalShortcuts, ConfigDefaults::snapToZoneShortcutKeyPattern(), m_snapToZoneShortcuts,
                         snapToZoneDefaults);
    m_rotateWindowsClockwiseShortcut = globalShortcuts.readString(ConfigDefaults::rotateWindowsClockwiseShortcutKey(),
                                                                  ConfigDefaults::rotateWindowsClockwiseShortcut());
    m_rotateWindowsCounterclockwiseShortcut =
        globalShortcuts.readString(ConfigDefaults::rotateWindowsCounterclockwiseShortcutKey(),
                                   ConfigDefaults::rotateWindowsCounterclockwiseShortcut());
    m_cycleWindowForwardShortcut = globalShortcuts.readString(ConfigDefaults::cycleWindowForwardShortcutKey(),
                                                              ConfigDefaults::cycleWindowForwardShortcut());
    m_cycleWindowBackwardShortcut = globalShortcuts.readString(ConfigDefaults::cycleWindowBackwardShortcutKey(),
                                                               ConfigDefaults::cycleWindowBackwardShortcut());
    m_resnapToNewLayoutShortcut = globalShortcuts.readString(ConfigDefaults::resnapToNewLayoutShortcutKey(),
                                                             ConfigDefaults::resnapToNewLayoutShortcut());
    m_snapAllWindowsShortcut = globalShortcuts.readString(ConfigDefaults::snapAllWindowsShortcutKey(),
                                                          ConfigDefaults::snapAllWindowsShortcut());
    m_layoutPickerShortcut =
        globalShortcuts.readString(ConfigDefaults::layoutPickerShortcutKey(), ConfigDefaults::layoutPickerShortcut());
    m_toggleLayoutLockShortcut = globalShortcuts.readString(ConfigDefaults::toggleLayoutLockShortcutKey(),
                                                            ConfigDefaults::toggleLayoutLockShortcut());
}

void Settings::loadAutotilingConfig(QSettingsConfigBackend* backend)
{
    auto autotiling = backend->group(ConfigDefaults::autotilingGroup());
    m_autotileEnabled = autotiling->readBool(ConfigDefaults::autotileEnabledKey(), ConfigDefaults::autotileEnabled());
    m_defaultAutotileAlgorithm = autotiling->readString(ConfigDefaults::defaultAutotileAlgorithmKey(),
                                                        ConfigDefaults::defaultAutotileAlgorithm());

    // Do NOT validate the saved algorithm ID here — scripted algorithms
    // (including those with @builtinId) are not registered until
    // ScriptedAlgorithmLoader::scanAndRegister() runs later in Daemon::init().
    // The engine's syncFromSettings() validates after all algorithms are loaded.

    qreal splitRatio =
        autotiling->readDouble(ConfigDefaults::autotileSplitRatioKey(), ConfigDefaults::autotileSplitRatio());
    if (splitRatio < ConfigDefaults::autotileSplitRatioMin() || splitRatio > ConfigDefaults::autotileSplitRatioMax()) {
        qCWarning(lcConfig) << "Invalid autotile split ratio:" << splitRatio << "clamping to valid range";
        splitRatio =
            qBound(ConfigDefaults::autotileSplitRatioMin(), splitRatio, ConfigDefaults::autotileSplitRatioMax());
    }
    m_autotileSplitRatio = splitRatio;

    int masterCount =
        autotiling->readInt(ConfigDefaults::autotileMasterCountKey(), ConfigDefaults::autotileMasterCount());
    if (masterCount < ConfigDefaults::autotileMasterCountMin()
        || masterCount > ConfigDefaults::autotileMasterCountMax()) {
        qCWarning(lcConfig) << "Invalid autotile master count:" << masterCount << "clamping to valid range";
        masterCount =
            qBound(ConfigDefaults::autotileMasterCountMin(), masterCount, ConfigDefaults::autotileMasterCountMax());
    }
    m_autotileMasterCount = masterCount;

    // Load per-algorithm settings map
    const QString perAlgoStr = autotiling->readString(ConfigDefaults::autotilePerAlgorithmSettingsKey(), QString());
    if (!perAlgoStr.isEmpty()) {
        // Deserialize JSON -> QVariantMap -> QHash -> QVariantMap: the round-trip
        // through perAlgoFromVariantMap sanitizes/clamps values before storing.
        const QJsonObject perAlgoJson = QJsonDocument::fromJson(perAlgoStr.toUtf8()).object();
        m_autotilePerAlgorithmSettings =
            AutotileConfig::perAlgoToVariantMap(AutotileConfig::perAlgoFromVariantMap(perAlgoJson.toVariantMap()));
    }

    m_autotileInnerGap = readValidatedInt(*autotiling, ConfigDefaults::autotileInnerGapKey(),
                                          ConfigDefaults::autotileInnerGap(), ConfigDefaults::autotileInnerGapMin(),
                                          ConfigDefaults::autotileInnerGapMax(), "autotile inner gap");
    m_autotileOuterGap = readValidatedInt(*autotiling, ConfigDefaults::autotileOuterGapKey(),
                                          ConfigDefaults::autotileOuterGap(), ConfigDefaults::autotileOuterGapMin(),
                                          ConfigDefaults::autotileOuterGapMax(), "autotile outer gap");
    m_autotileUsePerSideOuterGap = autotiling->readBool(ConfigDefaults::autotileUsePerSideOuterGapKey(),
                                                        ConfigDefaults::autotileUsePerSideOuterGap());
    m_autotileOuterGapTop = readValidatedInt(
        *autotiling, ConfigDefaults::autotileOuterGapTopKey(), ConfigDefaults::autotileOuterGapTop(),
        ConfigDefaults::autotileOuterGapTopMin(), ConfigDefaults::autotileOuterGapTopMax(), "autotile outer gap top");
    m_autotileOuterGapBottom =
        readValidatedInt(*autotiling, ConfigDefaults::autotileOuterGapBottomKey(),
                         ConfigDefaults::autotileOuterGapBottom(), ConfigDefaults::autotileOuterGapBottomMin(),
                         ConfigDefaults::autotileOuterGapBottomMax(), "autotile outer gap bottom");
    m_autotileOuterGapLeft =
        readValidatedInt(*autotiling, ConfigDefaults::autotileOuterGapLeftKey(), ConfigDefaults::autotileOuterGapLeft(),
                         ConfigDefaults::autotileOuterGapLeftMin(), ConfigDefaults::autotileOuterGapLeftMax(),
                         "autotile outer gap left");
    m_autotileOuterGapRight =
        readValidatedInt(*autotiling, ConfigDefaults::autotileOuterGapRightKey(),
                         ConfigDefaults::autotileOuterGapRight(), ConfigDefaults::autotileOuterGapRightMin(),
                         ConfigDefaults::autotileOuterGapRightMax(), "autotile outer gap right");
    m_autotileFocusNewWindows =
        autotiling->readBool(ConfigDefaults::autotileFocusNewWindowsKey(), ConfigDefaults::autotileFocusNewWindows());
    m_autotileSmartGaps =
        autotiling->readBool(ConfigDefaults::autotileSmartGapsKey(), ConfigDefaults::autotileSmartGaps());
    m_autotileMaxWindows = readValidatedInt(
        *autotiling, ConfigDefaults::autotileMaxWindowsKey(), ConfigDefaults::autotileMaxWindows(),
        ConfigDefaults::autotileMaxWindowsMin(), ConfigDefaults::autotileMaxWindowsMax(), "autotile max windows");
    m_autotileInsertPosition = static_cast<AutotileInsertPosition>(
        readValidatedInt(*autotiling, ConfigDefaults::autotileInsertPositionKey(),
                         ConfigDefaults::autotileInsertPosition(), ConfigDefaults::autotileInsertPositionMin(),
                         ConfigDefaults::autotileInsertPositionMax(), "autotile insert position"));

    // Release autotiling group before accessing animations group
    autotiling.reset();

    // Animation Settings
    {
        auto animations = backend->group(ConfigDefaults::animationsGroup());
        m_animationsEnabled =
            animations->readBool(ConfigDefaults::animationsEnabledKey(), ConfigDefaults::animationsEnabled());
        m_animationDuration = readValidatedInt(
            *animations, ConfigDefaults::animationDurationKey(), ConfigDefaults::animationDuration(),
            ConfigDefaults::animationDurationMin(), ConfigDefaults::animationDurationMax(), "animation duration");
        m_animationEasingCurve =
            animations->readString(ConfigDefaults::animationEasingCurveKey(), ConfigDefaults::animationEasingCurve());
        m_animationMinDistance =
            readValidatedInt(*animations, ConfigDefaults::animationMinDistanceKey(),
                             ConfigDefaults::animationMinDistance(), ConfigDefaults::animationMinDistanceMin(),
                             ConfigDefaults::animationMinDistanceMax(), "animation min distance");
        m_animationSequenceMode =
            readValidatedInt(*animations, ConfigDefaults::animationSequenceModeKey(),
                             ConfigDefaults::animationSequenceMode(), ConfigDefaults::animationSequenceModeMin(),
                             ConfigDefaults::animationSequenceModeMax(), "animation sequence mode");
        m_animationStaggerInterval =
            readValidatedInt(*animations, ConfigDefaults::animationStaggerIntervalKey(),
                             ConfigDefaults::animationStaggerInterval(), ConfigDefaults::animationStaggerIntervalMin(),
                             ConfigDefaults::animationStaggerIntervalMax(), "animation stagger interval");
    }

    // Re-open autotiling group for remaining fields
    {
        auto autotiling2 = backend->group(ConfigDefaults::autotilingGroup());
        m_autotileFocusFollowsMouse = autotiling2->readBool(ConfigDefaults::autotileFocusFollowsMouseKey(),
                                                            ConfigDefaults::autotileFocusFollowsMouse());
        m_autotileRespectMinimumSize = autotiling2->readBool(ConfigDefaults::autotileRespectMinimumSizeKey(),
                                                             ConfigDefaults::autotileRespectMinimumSize());
        m_autotileHideTitleBars =
            autotiling2->readBool(ConfigDefaults::autotileHideTitleBarsKey(), ConfigDefaults::autotileHideTitleBars());
        m_autotileShowBorder =
            autotiling2->readBool(ConfigDefaults::autotileShowBorderKey(), ConfigDefaults::autotileShowBorder());
        m_autotileBorderWidth =
            readValidatedInt(*autotiling2, ConfigDefaults::autotileBorderWidthKey(),
                             ConfigDefaults::autotileBorderWidth(), ConfigDefaults::autotileBorderWidthMin(),
                             ConfigDefaults::autotileBorderWidthMax(), "autotile border width");
        m_autotileBorderRadius =
            readValidatedInt(*autotiling2, ConfigDefaults::autotileBorderRadiusKey(),
                             ConfigDefaults::autotileBorderRadius(), ConfigDefaults::autotileBorderRadiusMin(),
                             ConfigDefaults::autotileBorderRadiusMax(), "autotile border radius");
        m_autotileBorderColor = readValidatedColor(*autotiling2, ConfigDefaults::autotileBorderColorKey(),
                                                   ConfigDefaults::autotileBorderColor(), "autotile border");
        m_autotileInactiveBorderColor =
            readValidatedColor(*autotiling2, ConfigDefaults::autotileInactiveBorderColorKey(),
                               ConfigDefaults::autotileInactiveBorderColor(), "autotile inactive border");
        m_autotileUseSystemBorderColors = autotiling2->readBool(ConfigDefaults::autotileUseSystemBorderColorsKey(),
                                                                ConfigDefaults::autotileUseSystemBorderColors());
        QString lockedScreensStr = autotiling2->readString(ConfigDefaults::lockedScreensKey());
        QStringList newLocked = lockedScreensStr.isEmpty() ? QStringList() : lockedScreensStr.split(QLatin1Char(','));
        for (auto& s : newLocked)
            s = s.trimmed();
        if (m_lockedScreens != newLocked) {
            m_lockedScreens = newLocked;
            Q_EMIT lockedScreensChanged();
        }
    }

    // Autotile Shortcuts
    {
        auto autotileShortcuts = backend->group(ConfigDefaults::autotileShortcutsGroup());
        m_autotileToggleShortcut = autotileShortcuts->readString(ConfigDefaults::autotileToggleShortcutKey(),
                                                                 ConfigDefaults::autotileToggleShortcut());
        m_autotileFocusMasterShortcut = autotileShortcuts->readString(ConfigDefaults::autotileFocusMasterShortcutKey(),
                                                                      ConfigDefaults::autotileFocusMasterShortcut());
        m_autotileSwapMasterShortcut = autotileShortcuts->readString(ConfigDefaults::autotileSwapMasterShortcutKey(),
                                                                     ConfigDefaults::autotileSwapMasterShortcut());
        m_autotileIncMasterRatioShortcut = autotileShortcuts->readString(
            ConfigDefaults::autotileIncMasterRatioShortcutKey(), ConfigDefaults::autotileIncMasterRatioShortcut());
        m_autotileDecMasterRatioShortcut = autotileShortcuts->readString(
            ConfigDefaults::autotileDecMasterRatioShortcutKey(), ConfigDefaults::autotileDecMasterRatioShortcut());
        m_autotileIncMasterCountShortcut = autotileShortcuts->readString(
            ConfigDefaults::autotileIncMasterCountShortcutKey(), ConfigDefaults::autotileIncMasterCountShortcut());
        m_autotileDecMasterCountShortcut = autotileShortcuts->readString(
            ConfigDefaults::autotileDecMasterCountShortcutKey(), ConfigDefaults::autotileDecMasterCountShortcut());
        m_autotileRetileShortcut = autotileShortcuts->readString(ConfigDefaults::autotileRetileShortcutKey(),
                                                                 ConfigDefaults::autotileRetileShortcut());
    }
}

void Settings::loadEditorConfig(QSettingsConfigGroup& editor)
{
    m_editorDuplicateShortcut =
        editor.readString(ConfigDefaults::editorDuplicateShortcutKey(), ConfigDefaults::editorDuplicateShortcut());
    m_editorSplitHorizontalShortcut = editor.readString(ConfigDefaults::editorSplitHorizontalShortcutKey(),
                                                        ConfigDefaults::editorSplitHorizontalShortcut());
    m_editorSplitVerticalShortcut = editor.readString(ConfigDefaults::editorSplitVerticalShortcutKey(),
                                                      ConfigDefaults::editorSplitVerticalShortcut());
    m_editorFillShortcut =
        editor.readString(ConfigDefaults::editorFillShortcutKey(), ConfigDefaults::editorFillShortcut());
    m_editorGridSnappingEnabled =
        editor.readBool(ConfigDefaults::editorGridSnappingEnabledKey(), ConfigDefaults::editorGridSnappingEnabled());
    m_editorEdgeSnappingEnabled =
        editor.readBool(ConfigDefaults::editorEdgeSnappingEnabledKey(), ConfigDefaults::editorEdgeSnappingEnabled());

    double intervalX = editor.readDouble(ConfigDefaults::editorSnapIntervalXKey(), -1.0);
    if (intervalX < 0.0)
        intervalX = editor.readDouble(ConfigDefaults::editorSnapIntervalKey(), ConfigDefaults::editorSnapInterval());
    m_editorSnapIntervalX = intervalX;

    double intervalY = editor.readDouble(ConfigDefaults::editorSnapIntervalYKey(), -1.0);
    if (intervalY < 0.0)
        intervalY = editor.readDouble(ConfigDefaults::editorSnapIntervalKey(), ConfigDefaults::editorSnapInterval());
    m_editorSnapIntervalY = intervalY;

    m_editorSnapOverrideModifier =
        editor.readInt(ConfigDefaults::editorSnapOverrideModifierKey(), ConfigDefaults::editorSnapOverrideModifier());
    m_fillOnDropEnabled = editor.readBool(ConfigDefaults::fillOnDropEnabledKey(), ConfigDefaults::fillOnDropEnabled());
    m_fillOnDropModifier =
        editor.readInt(ConfigDefaults::fillOnDropModifierKey(), ConfigDefaults::fillOnDropModifier());

    Q_EMIT editorDuplicateShortcutChanged();
    Q_EMIT editorSplitHorizontalShortcutChanged();
    Q_EMIT editorSplitVerticalShortcutChanged();
    Q_EMIT editorFillShortcutChanged();
    Q_EMIT editorGridSnappingEnabledChanged();
    Q_EMIT editorEdgeSnappingEnabledChanged();
    Q_EMIT editorSnapIntervalXChanged();
    Q_EMIT editorSnapIntervalYChanged();
    Q_EMIT editorSnapOverrideModifierChanged();
    Q_EMIT fillOnDropEnabledChanged();
    Q_EMIT fillOnDropModifierChanged();
}

// ── save() helpers ───────────────────────────────────────────────────────────

void Settings::saveEditorConfig(QSettingsConfigGroup& editor)
{
    editor.writeString(ConfigDefaults::editorDuplicateShortcutKey(), m_editorDuplicateShortcut);
    editor.writeString(ConfigDefaults::editorSplitHorizontalShortcutKey(), m_editorSplitHorizontalShortcut);
    editor.writeString(ConfigDefaults::editorSplitVerticalShortcutKey(), m_editorSplitVerticalShortcut);
    editor.writeString(ConfigDefaults::editorFillShortcutKey(), m_editorFillShortcut);
    editor.writeBool(ConfigDefaults::editorGridSnappingEnabledKey(), m_editorGridSnappingEnabled);
    editor.writeBool(ConfigDefaults::editorEdgeSnappingEnabledKey(), m_editorEdgeSnappingEnabled);
    editor.writeDouble(ConfigDefaults::editorSnapIntervalXKey(), m_editorSnapIntervalX);
    editor.writeDouble(ConfigDefaults::editorSnapIntervalYKey(), m_editorSnapIntervalY);
    editor.writeInt(ConfigDefaults::editorSnapOverrideModifierKey(), m_editorSnapOverrideModifier);
    editor.writeBool(ConfigDefaults::fillOnDropEnabledKey(), m_fillOnDropEnabled);
    editor.writeInt(ConfigDefaults::fillOnDropModifierKey(), m_fillOnDropModifier);
}

void Settings::saveActivationConfig(QSettingsConfigGroup& activation)
{
    saveTriggerList(activation, ConfigDefaults::dragActivationTriggersKey(), m_dragActivationTriggers);
    activation.writeBool(ConfigDefaults::zoneSpanEnabledKey(), m_zoneSpanEnabled);
    activation.writeInt(ConfigDefaults::zoneSpanModifierKey(), static_cast<int>(m_zoneSpanModifier));
    saveTriggerList(activation, ConfigDefaults::zoneSpanTriggersKey(), m_zoneSpanTriggers);
    activation.writeBool(ConfigDefaults::toggleActivationKey(), m_toggleActivation);
    activation.writeBool(ConfigDefaults::snappingEnabledKey(), m_snappingEnabled);
}

void Settings::saveDisplayConfig(QSettingsConfigGroup& display)
{
    display.writeBool(ConfigDefaults::showOnAllMonitorsKey(), m_showZonesOnAllMonitors);
    display.writeString(ConfigDefaults::disabledMonitorsKey(), m_disabledMonitors.join(QLatin1Char(',')));
    display.writeString(ConfigDefaults::disabledDesktopsKey(), m_disabledDesktops.join(QLatin1Char(',')));
    display.writeString(ConfigDefaults::disabledActivitiesKey(), m_disabledActivities.join(QLatin1Char(',')));
    display.writeBool(ConfigDefaults::showNumbersKey(), m_showZoneNumbers);
    display.writeBool(ConfigDefaults::flashOnSwitchKey(), m_flashZonesOnSwitch);
    display.writeBool(ConfigDefaults::showOsdOnLayoutSwitchKey(), m_showOsdOnLayoutSwitch);
    display.writeBool(ConfigDefaults::showNavigationOsdKey(), m_showNavigationOsd);
    display.writeInt(ConfigDefaults::osdStyleKey(), static_cast<int>(m_osdStyle));
    display.writeInt(ConfigDefaults::overlayDisplayModeKey(), static_cast<int>(m_overlayDisplayMode));
}

void Settings::saveAppearanceConfig(QSettingsConfigGroup& appearance)
{
    appearance.writeBool(ConfigDefaults::useSystemColorsKey(), m_useSystemColors);
    appearance.writeColor(ConfigDefaults::highlightColorKey(), m_highlightColor);
    appearance.writeColor(ConfigDefaults::inactiveColorKey(), m_inactiveColor);
    appearance.writeColor(ConfigDefaults::borderColorKey(), m_borderColor);
    appearance.writeColor(ConfigDefaults::labelFontColorKey(), m_labelFontColor);
    appearance.writeDouble(ConfigDefaults::activeOpacityKey(), m_activeOpacity);
    appearance.writeDouble(ConfigDefaults::inactiveOpacityKey(), m_inactiveOpacity);
    appearance.writeInt(ConfigDefaults::borderWidthKey(), m_borderWidth);
    appearance.writeInt(ConfigDefaults::borderRadiusKey(), m_borderRadius);
    appearance.writeBool(ConfigDefaults::enableBlurKey(), m_enableBlur);
    appearance.writeString(ConfigDefaults::labelFontFamilyKey(), m_labelFontFamily);
    appearance.writeDouble(ConfigDefaults::labelFontSizeScaleKey(), m_labelFontSizeScale);
    appearance.writeInt(ConfigDefaults::labelFontWeightKey(), m_labelFontWeight);
    appearance.writeBool(ConfigDefaults::labelFontItalicKey(), m_labelFontItalic);
    appearance.writeBool(ConfigDefaults::labelFontUnderlineKey(), m_labelFontUnderline);
    appearance.writeBool(ConfigDefaults::labelFontStrikeoutKey(), m_labelFontStrikeout);
}

void Settings::saveZoneGeometryConfig(QSettingsConfigGroup& zones)
{
    zones.writeInt(ConfigDefaults::zonePaddingKey(), m_zonePadding);
    zones.writeInt(ConfigDefaults::outerGapKey(), m_outerGap);
    zones.writeBool(ConfigDefaults::usePerSideOuterGapKey(), m_usePerSideOuterGap);
    zones.writeInt(ConfigDefaults::outerGapTopKey(), m_outerGapTop);
    zones.writeInt(ConfigDefaults::outerGapBottomKey(), m_outerGapBottom);
    zones.writeInt(ConfigDefaults::outerGapLeftKey(), m_outerGapLeft);
    zones.writeInt(ConfigDefaults::outerGapRightKey(), m_outerGapRight);
    zones.writeInt(ConfigDefaults::adjacentThresholdKey(), m_adjacentThreshold);
    zones.writeInt(ConfigDefaults::pollIntervalMsKey(), m_pollIntervalMs);
    zones.writeInt(ConfigDefaults::minimumZoneSizePxKey(), m_minimumZoneSizePx);
    zones.writeInt(ConfigDefaults::minimumZoneDisplaySizePxKey(), m_minimumZoneDisplaySizePx);
}

void Settings::saveBehaviorConfig(QSettingsConfigBackend* backend)
{
    {
        auto behavior = backend->group(ConfigDefaults::behaviorGroup());
        behavior->writeBool(ConfigDefaults::keepOnResolutionChangeKey(), m_keepWindowsInZonesOnResolutionChange);
        behavior->writeBool(ConfigDefaults::moveNewToLastZoneKey(), m_moveNewWindowsToLastZone);
        behavior->writeBool(ConfigDefaults::restoreSizeOnUnsnapKey(), m_restoreOriginalSizeOnUnsnap);
        behavior->writeInt(ConfigDefaults::stickyWindowHandlingKey(), static_cast<int>(m_stickyWindowHandling));
        behavior->writeBool(ConfigDefaults::restoreWindowsToZonesOnLoginKey(), m_restoreWindowsToZonesOnLogin);
        behavior->writeString(ConfigDefaults::defaultLayoutIdKey(), m_defaultLayoutId);
        behavior->writeBool(ConfigDefaults::filterLayoutsByAspectRatioKey(), m_filterLayoutsByAspectRatio);
    }
    {
        auto activation = backend->group(ConfigDefaults::activationGroup());
        activation->writeBool(ConfigDefaults::snapAssistFeatureEnabledKey(), m_snapAssistFeatureEnabled);
        activation->writeBool(ConfigDefaults::snapAssistEnabledKey(), m_snapAssistEnabled);
        saveTriggerList(*activation, ConfigDefaults::snapAssistTriggersKey(), m_snapAssistTriggers);
    }
    {
        auto exclusions = backend->group(ConfigDefaults::exclusionsGroup());
        exclusions->writeString(ConfigDefaults::excludedApplicationsKey(),
                                m_excludedApplications.join(QLatin1Char(',')));
        exclusions->writeString(ConfigDefaults::excludedWindowClassesKey(),
                                m_excludedWindowClasses.join(QLatin1Char(',')));
        exclusions->writeBool(ConfigDefaults::excludeTransientWindowsKey(), m_excludeTransientWindows);
        exclusions->writeInt(ConfigDefaults::minimumWindowWidthKey(), m_minimumWindowWidth);
        exclusions->writeInt(ConfigDefaults::minimumWindowHeightKey(), m_minimumWindowHeight);
    }
}

void Settings::saveZoneSelectorConfig(QSettingsConfigGroup& zoneSelector)
{
    zoneSelector.writeBool(ConfigDefaults::zoneSelectorEnabledKey(), m_zoneSelectorEnabled);
    zoneSelector.writeInt(ConfigDefaults::zoneSelectorTriggerDistanceKey(), m_zoneSelectorTriggerDistance);
    zoneSelector.writeInt(ConfigDefaults::zoneSelectorPositionKey(), static_cast<int>(m_zoneSelectorPosition));
    zoneSelector.writeInt(ConfigDefaults::zoneSelectorLayoutModeKey(), static_cast<int>(m_zoneSelectorLayoutMode));
    zoneSelector.writeInt(ConfigDefaults::zoneSelectorPreviewWidthKey(), m_zoneSelectorPreviewWidth);
    zoneSelector.writeInt(ConfigDefaults::zoneSelectorPreviewHeightKey(), m_zoneSelectorPreviewHeight);
    zoneSelector.writeBool(ConfigDefaults::zoneSelectorPreviewLockAspectKey(), m_zoneSelectorPreviewLockAspect);
    zoneSelector.writeInt(ConfigDefaults::zoneSelectorGridColumnsKey(), m_zoneSelectorGridColumns);
    zoneSelector.writeInt(ConfigDefaults::zoneSelectorSizeModeKey(), static_cast<int>(m_zoneSelectorSizeMode));
    zoneSelector.writeInt(ConfigDefaults::zoneSelectorMaxRowsKey(), m_zoneSelectorMaxRows);
}

void Settings::saveShortcutConfig(QSettingsConfigGroup& globalShortcuts)
{
    globalShortcuts.writeString(ConfigDefaults::openEditorShortcutKey(), m_openEditorShortcut);
    globalShortcuts.writeString(ConfigDefaults::openSettingsShortcutKey(), m_openSettingsShortcut);
    globalShortcuts.writeString(ConfigDefaults::previousLayoutShortcutKey(), m_previousLayoutShortcut);
    globalShortcuts.writeString(ConfigDefaults::nextLayoutShortcutKey(), m_nextLayoutShortcut);
    for (int i = 0; i < 9; ++i) {
        globalShortcuts.writeString(ConfigDefaults::quickLayoutShortcutKey(i + 1), m_quickLayoutShortcuts[i]);
    }
    globalShortcuts.writeString(ConfigDefaults::moveWindowLeftShortcutKey(), m_moveWindowLeftShortcut);
    globalShortcuts.writeString(ConfigDefaults::moveWindowRightShortcutKey(), m_moveWindowRightShortcut);
    globalShortcuts.writeString(ConfigDefaults::moveWindowUpShortcutKey(), m_moveWindowUpShortcut);
    globalShortcuts.writeString(ConfigDefaults::moveWindowDownShortcutKey(), m_moveWindowDownShortcut);
    globalShortcuts.writeString(ConfigDefaults::focusZoneLeftShortcutKey(), m_focusZoneLeftShortcut);
    globalShortcuts.writeString(ConfigDefaults::focusZoneRightShortcutKey(), m_focusZoneRightShortcut);
    globalShortcuts.writeString(ConfigDefaults::focusZoneUpShortcutKey(), m_focusZoneUpShortcut);
    globalShortcuts.writeString(ConfigDefaults::focusZoneDownShortcutKey(), m_focusZoneDownShortcut);
    globalShortcuts.writeString(ConfigDefaults::pushToEmptyZoneShortcutKey(), m_pushToEmptyZoneShortcut);
    globalShortcuts.writeString(ConfigDefaults::restoreWindowSizeShortcutKey(), m_restoreWindowSizeShortcut);
    globalShortcuts.writeString(ConfigDefaults::toggleWindowFloatShortcutKey(), m_toggleWindowFloatShortcut);
    globalShortcuts.writeString(ConfigDefaults::swapWindowLeftShortcutKey(), m_swapWindowLeftShortcut);
    globalShortcuts.writeString(ConfigDefaults::swapWindowRightShortcutKey(), m_swapWindowRightShortcut);
    globalShortcuts.writeString(ConfigDefaults::swapWindowUpShortcutKey(), m_swapWindowUpShortcut);
    globalShortcuts.writeString(ConfigDefaults::swapWindowDownShortcutKey(), m_swapWindowDownShortcut);
    for (int i = 0; i < 9; ++i) {
        globalShortcuts.writeString(ConfigDefaults::snapToZoneShortcutKey(i + 1), m_snapToZoneShortcuts[i]);
    }
    globalShortcuts.writeString(ConfigDefaults::rotateWindowsClockwiseShortcutKey(), m_rotateWindowsClockwiseShortcut);
    globalShortcuts.writeString(ConfigDefaults::rotateWindowsCounterclockwiseShortcutKey(),
                                m_rotateWindowsCounterclockwiseShortcut);
    globalShortcuts.writeString(ConfigDefaults::cycleWindowForwardShortcutKey(), m_cycleWindowForwardShortcut);
    globalShortcuts.writeString(ConfigDefaults::cycleWindowBackwardShortcutKey(), m_cycleWindowBackwardShortcut);
    globalShortcuts.writeString(ConfigDefaults::resnapToNewLayoutShortcutKey(), m_resnapToNewLayoutShortcut);
    globalShortcuts.writeString(ConfigDefaults::snapAllWindowsShortcutKey(), m_snapAllWindowsShortcut);
    globalShortcuts.writeString(ConfigDefaults::layoutPickerShortcutKey(), m_layoutPickerShortcut);
    globalShortcuts.writeString(ConfigDefaults::toggleLayoutLockShortcutKey(), m_toggleLayoutLockShortcut);
}

void Settings::saveAutotilingConfig(QSettingsConfigBackend* backend)
{
    {
        auto autotiling = backend->group(ConfigDefaults::autotilingGroup());
        autotiling->writeBool(ConfigDefaults::autotileEnabledKey(), m_autotileEnabled);
        autotiling->writeString(ConfigDefaults::defaultAutotileAlgorithmKey(), m_defaultAutotileAlgorithm);
        autotiling->writeDouble(ConfigDefaults::autotileSplitRatioKey(), m_autotileSplitRatio);
        autotiling->writeInt(ConfigDefaults::autotileMasterCountKey(), m_autotileMasterCount);
        // Save per-algorithm settings map (reuse shared serialization helpers)
        if (!m_autotilePerAlgorithmSettings.isEmpty()) {
            autotiling->writeString(
                ConfigDefaults::autotilePerAlgorithmSettingsKey(),
                QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(m_autotilePerAlgorithmSettings))
                                      .toJson(QJsonDocument::Compact)));
        } else {
            autotiling->deleteKey(ConfigDefaults::autotilePerAlgorithmSettingsKey());
        }
        autotiling->writeInt(ConfigDefaults::autotileInnerGapKey(), m_autotileInnerGap);
        autotiling->writeInt(ConfigDefaults::autotileOuterGapKey(), m_autotileOuterGap);
        autotiling->writeBool(ConfigDefaults::autotileUsePerSideOuterGapKey(), m_autotileUsePerSideOuterGap);
        autotiling->writeInt(ConfigDefaults::autotileOuterGapTopKey(), m_autotileOuterGapTop);
        autotiling->writeInt(ConfigDefaults::autotileOuterGapBottomKey(), m_autotileOuterGapBottom);
        autotiling->writeInt(ConfigDefaults::autotileOuterGapLeftKey(), m_autotileOuterGapLeft);
        autotiling->writeInt(ConfigDefaults::autotileOuterGapRightKey(), m_autotileOuterGapRight);
        autotiling->writeBool(ConfigDefaults::autotileFocusNewWindowsKey(), m_autotileFocusNewWindows);
        autotiling->writeBool(ConfigDefaults::autotileSmartGapsKey(), m_autotileSmartGaps);
        autotiling->writeInt(ConfigDefaults::autotileMaxWindowsKey(), m_autotileMaxWindows);
        autotiling->writeInt(ConfigDefaults::autotileInsertPositionKey(), static_cast<int>(m_autotileInsertPosition));
        autotiling->writeBool(ConfigDefaults::autotileFocusFollowsMouseKey(), m_autotileFocusFollowsMouse);
        autotiling->writeBool(ConfigDefaults::autotileRespectMinimumSizeKey(), m_autotileRespectMinimumSize);
        autotiling->writeBool(ConfigDefaults::autotileHideTitleBarsKey(), m_autotileHideTitleBars);
        autotiling->writeBool(ConfigDefaults::autotileShowBorderKey(), m_autotileShowBorder);
        autotiling->writeInt(ConfigDefaults::autotileBorderWidthKey(), m_autotileBorderWidth);
        autotiling->writeInt(ConfigDefaults::autotileBorderRadiusKey(), m_autotileBorderRadius);
        autotiling->writeColor(ConfigDefaults::autotileBorderColorKey(), m_autotileBorderColor);
        autotiling->writeColor(ConfigDefaults::autotileInactiveBorderColorKey(), m_autotileInactiveBorderColor);
        autotiling->writeBool(ConfigDefaults::autotileUseSystemBorderColorsKey(), m_autotileUseSystemBorderColors);
        autotiling->writeString(ConfigDefaults::lockedScreensKey(), m_lockedScreens.join(QLatin1Char(',')));
    }

    {
        auto animations = backend->group(ConfigDefaults::animationsGroup());
        animations->writeBool(ConfigDefaults::animationsEnabledKey(), m_animationsEnabled);
        animations->writeInt(ConfigDefaults::animationDurationKey(), m_animationDuration);
        animations->writeString(ConfigDefaults::animationEasingCurveKey(), m_animationEasingCurve);
        animations->writeInt(ConfigDefaults::animationMinDistanceKey(), m_animationMinDistance);
        animations->writeInt(ConfigDefaults::animationSequenceModeKey(), m_animationSequenceMode);
        animations->writeInt(ConfigDefaults::animationStaggerIntervalKey(), m_animationStaggerInterval);
    }

    {
        auto autotileShortcuts = backend->group(ConfigDefaults::autotileShortcutsGroup());
        autotileShortcuts->writeString(ConfigDefaults::autotileToggleShortcutKey(), m_autotileToggleShortcut);
        autotileShortcuts->writeString(ConfigDefaults::autotileFocusMasterShortcutKey(), m_autotileFocusMasterShortcut);
        autotileShortcuts->writeString(ConfigDefaults::autotileSwapMasterShortcutKey(), m_autotileSwapMasterShortcut);
        autotileShortcuts->writeString(ConfigDefaults::autotileIncMasterRatioShortcutKey(),
                                       m_autotileIncMasterRatioShortcut);
        autotileShortcuts->writeString(ConfigDefaults::autotileDecMasterRatioShortcutKey(),
                                       m_autotileDecMasterRatioShortcut);
        autotileShortcuts->writeString(ConfigDefaults::autotileIncMasterCountShortcutKey(),
                                       m_autotileIncMasterCountShortcut);
        autotileShortcuts->writeString(ConfigDefaults::autotileDecMasterCountShortcutKey(),
                                       m_autotileDecMasterCountShortcut);
        autotileShortcuts->writeString(ConfigDefaults::autotileRetileShortcutKey(), m_autotileRetileShortcut);
    }
}

// ── Virtual screen config load/save ──────────────────────────────────────────

void Settings::loadVirtualScreenConfigs(QSettingsConfigBackend* backend)
{
    m_virtualScreenConfigs.clear();
    const QStringList allGroups = backend->groupList();
    const QString prefix = ConfigDefaults::virtualScreenGroupPrefix();

    for (const QString& groupName : allGroups) {
        if (!groupName.startsWith(prefix))
            continue;

        const QString physId = groupName.mid(prefix.size());
        if (physId.isEmpty())
            continue;

        auto group = backend->group(groupName);
        int count = group->readInt(ConfigDefaults::virtualScreenCountKey(), 0);
        count = qBound(0, count, ConfigDefaults::maxVirtualScreensPerPhysical());
        if (count <= 0) {
            qCWarning(lcConfig) << "VirtualScreen config for" << physId << "has invalid count:" << count;
            continue;
        }

        VirtualScreenConfig config;
        config.physicalScreenId = physId;

        for (int i = 0; i < count; ++i) {
            const QString p = QString::number(i) + QLatin1Char('/');
            VirtualScreenDef vs;
            vs.physicalScreenId = physId;
            vs.index = i;
            vs.id = VirtualScreenId::make(physId, i);
            vs.displayName =
                group->readString(p + ConfigDefaults::virtualScreenNameKey(), QStringLiteral("Screen %1").arg(i + 1));
            qreal x = group->readDouble(p + ConfigDefaults::virtualScreenXKey(), 0.0);
            qreal y = group->readDouble(p + ConfigDefaults::virtualScreenYKey(), 0.0);
            qreal w = group->readDouble(p + ConfigDefaults::virtualScreenWidthKey(), 1.0);
            qreal h = group->readDouble(p + ConfigDefaults::virtualScreenHeightKey(), 1.0);
            vs.region = QRectF(x, y, w, h);
            config.screens.append(vs);
        }

        // Validate loaded regions — skip invalid entries instead of discarding entire config
        QVector<VirtualScreenDef> validScreens;
        for (const auto& vs : config.screens) {
            if (!vs.isValid()) {
                qCWarning(lcConfig) << "Skipping VirtualScreen" << vs.id << "with invalid region:" << vs.region;
                continue;
            }
            validScreens.append(vs);
        }
        config.screens = validScreens;

        // Need at least 2 screens for a meaningful subdivision
        if (config.screens.size() >= 2) {
            m_virtualScreenConfigs.insert(physId, config);
        }
    }
}

void Settings::saveVirtualScreenConfigs(QSettingsConfigBackend* backend)
{
    // Remove old VirtualScreen: groups that are no longer in the config
    const QStringList allGroups = backend->groupList();
    const QString prefix = ConfigDefaults::virtualScreenGroupPrefix();
    for (const QString& groupName : allGroups) {
        if (groupName.startsWith(prefix)) {
            backend->deleteGroup(groupName);
        }
    }

    // Write current configs
    for (auto it = m_virtualScreenConfigs.constBegin(); it != m_virtualScreenConfigs.constEnd(); ++it) {
        const QString& physId = it.key();
        const VirtualScreenConfig& config = it.value();
        if (config.screens.isEmpty())
            continue;

        auto group = backend->group(prefix + physId);
        group->writeInt(ConfigDefaults::virtualScreenCountKey(), config.screens.size());

        for (int i = 0; i < config.screens.size(); ++i) {
            const VirtualScreenDef& vs = config.screens[i];
            const QString p = QString::number(i) + QLatin1Char('/');
            group->writeString(p + ConfigDefaults::virtualScreenNameKey(), vs.displayName);
            group->writeDouble(p + ConfigDefaults::virtualScreenXKey(), vs.region.x());
            group->writeDouble(p + ConfigDefaults::virtualScreenYKey(), vs.region.y());
            group->writeDouble(p + ConfigDefaults::virtualScreenWidthKey(), vs.region.width());
            group->writeDouble(p + ConfigDefaults::virtualScreenHeightKey(), vs.region.height());
        }
    }
}

} // namespace PlasmaZones
