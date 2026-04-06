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

void Settings::loadActivationConfig(IConfigBackend* backend)
{
    {
        auto snapping = backend->group(ConfigDefaults::snappingGroup());
        m_snappingEnabled = snapping->readBool(ConfigDefaults::enabledKey(), ConfigDefaults::snappingEnabled());
    }
    {
        auto behavior = backend->group(ConfigDefaults::snappingBehaviorGroup());
        m_dragActivationTriggers = parseTriggerListJson(behavior->readString(ConfigDefaults::triggersKey()))
                                       .value_or(ConfigDefaults::dragActivationTriggers());
        m_toggleActivation =
            behavior->readBool(ConfigDefaults::toggleActivationKey(), ConfigDefaults::toggleActivation());
    }
    {
        auto zoneSpan = backend->group(ConfigDefaults::snappingBehaviorZoneSpanGroup());
        m_zoneSpanEnabled = zoneSpan->readBool(ConfigDefaults::enabledKey(), ConfigDefaults::zoneSpanEnabled());

        int spanMod = zoneSpan->readInt(ConfigDefaults::modifierKey(), ConfigDefaults::zoneSpanModifier());
        if (spanMod < 0 || spanMod > static_cast<int>(DragModifier::CtrlAltMeta)) {
            qCWarning(lcConfig) << "Invalid ZoneSpanModifier value:" << spanMod << "- using default";
            spanMod = ConfigDefaults::zoneSpanModifier();
        }
        m_zoneSpanModifier = static_cast<DragModifier>(spanMod);

        auto parsedSpanTriggers = parseTriggerListJson(zoneSpan->readString(ConfigDefaults::triggersKey()));
        if (parsedSpanTriggers.has_value()) {
            m_zoneSpanTriggers = *parsedSpanTriggers;
        } else {
            // No valid JSON — build default trigger from the actual spanMod value read above
            QVariantMap trigger;
            trigger[ConfigDefaults::triggerModifierField()] = spanMod;
            trigger[ConfigDefaults::triggerMouseButtonField()] = 0;
            m_zoneSpanTriggers = {trigger};
        }
    }
}

void Settings::loadDisplayConfig(IConfigBackend* backend)
{
    {
        auto display = backend->group(ConfigDefaults::snappingBehaviorDisplayGroup());
        m_showZonesOnAllMonitors =
            display->readBool(ConfigDefaults::showOnAllMonitorsKey(), ConfigDefaults::showOnAllMonitors());
        // DisabledMonitors is a comma-separated string list
        QString disabledMonitorsStr = display->readString(ConfigDefaults::disabledMonitorsKey());
        m_disabledMonitors = disabledMonitorsStr.isEmpty()
            ? QStringList()
            : disabledMonitorsStr.split(QLatin1Char(','), Qt::SkipEmptyParts);
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
        QString disabledDesktopsStr = display->readString(ConfigDefaults::disabledDesktopsKey());
        m_disabledDesktops = disabledDesktopsStr.isEmpty()
            ? QStringList()
            : disabledDesktopsStr.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (int i = 0; i < m_disabledDesktops.size(); ++i) {
            m_disabledDesktops[i] = m_disabledDesktops[i].trimmed();
        }
        // DisabledActivities is a comma-separated list of "screenId/activityUuid" composite keys
        QString disabledActivitiesStr = display->readString(ConfigDefaults::disabledActivitiesKey());
        m_disabledActivities = disabledActivitiesStr.isEmpty()
            ? QStringList()
            : disabledActivitiesStr.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (int i = 0; i < m_disabledActivities.size(); ++i) {
            m_disabledActivities[i] = m_disabledActivities[i].trimmed();
        }
        m_filterLayoutsByAspectRatio =
            display->readBool(ConfigDefaults::filterByAspectRatioKey(), ConfigDefaults::filterLayoutsByAspectRatio());
    }
    {
        auto effects = backend->group(ConfigDefaults::snappingEffectsGroup());
        m_showZoneNumbers = effects->readBool(ConfigDefaults::showNumbersKey(), ConfigDefaults::showNumbers());
        m_flashZonesOnSwitch = effects->readBool(ConfigDefaults::flashOnSwitchKey(), ConfigDefaults::flashOnSwitch());
        m_showOsdOnLayoutSwitch =
            effects->readBool(ConfigDefaults::osdOnLayoutSwitchKey(), ConfigDefaults::showOsdOnLayoutSwitch());
        m_showNavigationOsd =
            effects->readBool(ConfigDefaults::navigationOsdKey(), ConfigDefaults::showNavigationOsd());
        m_osdStyle = static_cast<OsdStyle>(readValidatedInt(*effects, ConfigDefaults::osdStyleKey(),
                                                            ConfigDefaults::osdStyle(), ConfigDefaults::osdStyleMin(),
                                                            ConfigDefaults::osdStyleMax(), "OSD style"));
        m_overlayDisplayMode = static_cast<OverlayDisplayMode>(readValidatedInt(
            *effects, ConfigDefaults::overlayDisplayModeKey(), ConfigDefaults::overlayDisplayMode(),
            ConfigDefaults::overlayDisplayModeMin(), ConfigDefaults::overlayDisplayModeMax(), "overlay display mode"));
    }
}

void Settings::loadAppearanceConfig(IConfigBackend* backend)
{
    {
        auto colors = backend->group(ConfigDefaults::snappingAppearanceColorsGroup());
        m_useSystemColors = colors->readBool(ConfigDefaults::useSystemKey(), ConfigDefaults::useSystemColors());
        m_highlightColor =
            readValidatedColor(*colors, ConfigDefaults::highlightKey(), ConfigDefaults::highlightColor(), "highlight");
        m_inactiveColor =
            readValidatedColor(*colors, ConfigDefaults::inactiveKey(), ConfigDefaults::inactiveColor(), "inactive");
        m_borderColor =
            readValidatedColor(*colors, ConfigDefaults::borderKey(), ConfigDefaults::borderColor(), "border");
    }
    {
        auto labels = backend->group(ConfigDefaults::snappingAppearanceLabelsGroup());
        m_labelFontColor =
            readValidatedColor(*labels, ConfigDefaults::fontColorKey(), ConfigDefaults::labelFontColor(), "label font");
        m_labelFontFamily = labels->readString(ConfigDefaults::fontFamilyKey(), ConfigDefaults::labelFontFamily());
        qreal fontScale = labels->readDouble(ConfigDefaults::fontSizeScaleKey(), ConfigDefaults::labelFontSizeScale());
        m_labelFontSizeScale =
            qBound(ConfigDefaults::labelFontSizeScaleMin(), fontScale, ConfigDefaults::labelFontSizeScaleMax());
        m_labelFontWeight = readValidatedInt(*labels, ConfigDefaults::fontWeightKey(),
                                             ConfigDefaults::labelFontWeight(), ConfigDefaults::labelFontWeightMin(),
                                             ConfigDefaults::labelFontWeightMax(), "label font weight");
        m_labelFontItalic = labels->readBool(ConfigDefaults::fontItalicKey(), ConfigDefaults::labelFontItalic());
        m_labelFontUnderline =
            labels->readBool(ConfigDefaults::fontUnderlineKey(), ConfigDefaults::labelFontUnderline());
        m_labelFontStrikeout =
            labels->readBool(ConfigDefaults::fontStrikeoutKey(), ConfigDefaults::labelFontStrikeout());
    }
    {
        auto opacity = backend->group(ConfigDefaults::snappingAppearanceOpacityGroup());

        qreal activeOpacity = opacity->readDouble(ConfigDefaults::activeKey(), ConfigDefaults::activeOpacity());
        if (activeOpacity < ConfigDefaults::activeOpacityMin() || activeOpacity > ConfigDefaults::activeOpacityMax()) {
            qCWarning(lcConfig) << "Invalid active opacity:" << activeOpacity << "clamping to valid range";
            activeOpacity =
                qBound(ConfigDefaults::activeOpacityMin(), activeOpacity, ConfigDefaults::activeOpacityMax());
        }
        m_activeOpacity = activeOpacity;

        qreal inactiveOpacity = opacity->readDouble(ConfigDefaults::inactiveKey(), ConfigDefaults::inactiveOpacity());
        if (inactiveOpacity < ConfigDefaults::inactiveOpacityMin()
            || inactiveOpacity > ConfigDefaults::inactiveOpacityMax()) {
            qCWarning(lcConfig) << "Invalid inactive opacity:" << inactiveOpacity << "clamping to valid range";
            inactiveOpacity =
                qBound(ConfigDefaults::inactiveOpacityMin(), inactiveOpacity, ConfigDefaults::inactiveOpacityMax());
        }
        m_inactiveOpacity = inactiveOpacity;
    }
    {
        auto border = backend->group(ConfigDefaults::snappingAppearanceBorderGroup());
        m_borderWidth =
            readValidatedInt(*border, ConfigDefaults::widthKey(), ConfigDefaults::borderWidth(),
                             ConfigDefaults::borderWidthMin(), ConfigDefaults::borderWidthMax(), "border width");
        m_borderRadius =
            readValidatedInt(*border, ConfigDefaults::radiusKey(), ConfigDefaults::borderRadius(),
                             ConfigDefaults::borderRadiusMin(), ConfigDefaults::borderRadiusMax(), "border radius");
    }
    {
        auto effects = backend->group(ConfigDefaults::snappingEffectsGroup());
        m_enableBlur = effects->readBool(ConfigDefaults::blurKey(), ConfigDefaults::enableBlur());
    }
}

void Settings::loadZoneGeometryConfig(IConfigBackend* backend)
{
    {
        auto gaps = backend->group(ConfigDefaults::snappingGapsGroup());
        m_zonePadding =
            readValidatedInt(*gaps, ConfigDefaults::innerKey(), ConfigDefaults::zonePadding(),
                             ConfigDefaults::zonePaddingMin(), ConfigDefaults::zonePaddingMax(), "zone padding");
        m_outerGap = readValidatedInt(*gaps, ConfigDefaults::outerKey(), ConfigDefaults::outerGap(),
                                      ConfigDefaults::outerGapMin(), ConfigDefaults::outerGapMax(), "outer gap");
        m_usePerSideOuterGap = gaps->readBool(ConfigDefaults::usePerSideKey(), ConfigDefaults::usePerSideOuterGap());
        m_outerGapTop =
            readValidatedInt(*gaps, ConfigDefaults::topKey(), ConfigDefaults::outerGapTop(),
                             ConfigDefaults::outerGapTopMin(), ConfigDefaults::outerGapTopMax(), "outer gap top");
        m_outerGapBottom = readValidatedInt(*gaps, ConfigDefaults::bottomKey(), ConfigDefaults::outerGapBottom(),
                                            ConfigDefaults::outerGapBottomMin(), ConfigDefaults::outerGapBottomMax(),
                                            "outer gap bottom");
        m_outerGapLeft =
            readValidatedInt(*gaps, ConfigDefaults::leftKey(), ConfigDefaults::outerGapLeft(),
                             ConfigDefaults::outerGapLeftMin(), ConfigDefaults::outerGapLeftMax(), "outer gap left");
        m_outerGapRight =
            readValidatedInt(*gaps, ConfigDefaults::rightKey(), ConfigDefaults::outerGapRight(),
                             ConfigDefaults::outerGapRightMin(), ConfigDefaults::outerGapRightMax(), "outer gap right");
        m_adjacentThreshold = readValidatedInt(
            *gaps, ConfigDefaults::adjacentThresholdKey(), ConfigDefaults::adjacentThreshold(),
            ConfigDefaults::adjacentThresholdMin(), ConfigDefaults::adjacentThresholdMax(), "adjacent threshold");
    }
    {
        auto perf = backend->group(ConfigDefaults::performanceGroup());
        m_pollIntervalMs =
            readValidatedInt(*perf, ConfigDefaults::pollIntervalMsKey(), ConfigDefaults::pollIntervalMs(),
                             ConfigDefaults::pollIntervalMsMin(), ConfigDefaults::pollIntervalMsMax(), "poll interval");
        m_minimumZoneSizePx = readValidatedInt(
            *perf, ConfigDefaults::minimumZoneSizePxKey(), ConfigDefaults::minimumZoneSizePx(),
            ConfigDefaults::minimumZoneSizePxMin(), ConfigDefaults::minimumZoneSizePxMax(), "minimum zone size");
        m_minimumZoneDisplaySizePx =
            readValidatedInt(*perf, ConfigDefaults::minimumZoneDisplaySizePxKey(),
                             ConfigDefaults::minimumZoneDisplaySizePx(), ConfigDefaults::minimumZoneDisplaySizePxMin(),
                             ConfigDefaults::minimumZoneDisplaySizePxMax(), "minimum zone display size");
    }
}

void Settings::loadBehaviorConfig(IConfigBackend* backend)
{
    {
        auto windowHandling = backend->group(ConfigDefaults::snappingBehaviorWindowHandlingGroup());
        m_keepWindowsInZonesOnResolutionChange = windowHandling->readBool(
            ConfigDefaults::keepOnResolutionChangeKey(), ConfigDefaults::keepWindowsInZonesOnResolutionChange());
        m_moveNewWindowsToLastZone = windowHandling->readBool(ConfigDefaults::moveNewToLastZoneKey(),
                                                              ConfigDefaults::moveNewWindowsToLastZone());
        m_restoreOriginalSizeOnUnsnap = windowHandling->readBool(ConfigDefaults::restoreOnUnsnapKey(),
                                                                 ConfigDefaults::restoreOriginalSizeOnUnsnap());
        int stickyHandling =
            windowHandling->readInt(ConfigDefaults::stickyWindowHandlingKey(), ConfigDefaults::stickyWindowHandling());
        m_stickyWindowHandling = static_cast<StickyWindowHandling>(
            qBound(static_cast<int>(StickyWindowHandling::TreatAsNormal), stickyHandling,
                   static_cast<int>(StickyWindowHandling::IgnoreAll)));
        m_restoreWindowsToZonesOnLogin = windowHandling->readBool(ConfigDefaults::restoreOnLoginKey(),
                                                                  ConfigDefaults::restoreWindowsToZonesOnLogin());
        m_defaultLayoutId = normalizeUuidString(windowHandling->readString(ConfigDefaults::defaultLayoutIdKey()));
    }

    {
        auto snapAssist = backend->group(ConfigDefaults::snappingBehaviorSnapAssistGroup());
        m_snapAssistFeatureEnabled =
            snapAssist->readBool(ConfigDefaults::featureEnabledKey(), ConfigDefaults::snapAssistFeatureEnabled());
        m_snapAssistEnabled = snapAssist->readBool(ConfigDefaults::enabledKey(), ConfigDefaults::snapAssistEnabled());
        m_snapAssistTriggers = parseTriggerListJson(snapAssist->readString(ConfigDefaults::triggersKey()))
                                   .value_or(ConfigDefaults::snapAssistTriggers());
    }

    // Exclusions
    {
        auto exclusions = backend->group(ConfigDefaults::exclusionsGroup());
        QString excludedAppsStr = exclusions->readString(ConfigDefaults::applicationsKey());
        m_excludedApplications = excludedAppsStr.isEmpty() ? QStringList() : excludedAppsStr.split(QLatin1Char(','));
        for (auto& app : m_excludedApplications)
            app = app.trimmed();
        QString excludedClassesStr = exclusions->readString(ConfigDefaults::windowClassesKey());
        m_excludedWindowClasses =
            excludedClassesStr.isEmpty() ? QStringList() : excludedClassesStr.split(QLatin1Char(','));
        for (auto& cls : m_excludedWindowClasses)
            cls = cls.trimmed();
        m_excludeTransientWindows =
            exclusions->readBool(ConfigDefaults::transientWindowsKey(), ConfigDefaults::excludeTransientWindows());
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

void Settings::loadZoneSelectorConfig(IConfigBackend* backend)
{
    auto zoneSelector = backend->group(ConfigDefaults::snappingZoneSelectorGroup());
    m_zoneSelectorEnabled = zoneSelector->readBool(ConfigDefaults::enabledKey(), ConfigDefaults::zoneSelectorEnabled());
    m_zoneSelectorTriggerDistance = readValidatedInt(
        *zoneSelector, ConfigDefaults::triggerDistanceKey(), ConfigDefaults::triggerDistance(),
        ConfigDefaults::triggerDistanceMin(), ConfigDefaults::triggerDistanceMax(), "zone selector trigger distance");
    int selectorPos = zoneSelector->readInt(ConfigDefaults::positionKey(), ConfigDefaults::position());
    if (selectorPos >= 0 && selectorPos <= 8) {
        m_zoneSelectorPosition = static_cast<ZoneSelectorPosition>(selectorPos);
    } else {
        m_zoneSelectorPosition = static_cast<ZoneSelectorPosition>(ConfigDefaults::position());
    }
    int selectorMode = zoneSelector->readInt(ConfigDefaults::layoutModeKey(), ConfigDefaults::layoutMode());
    m_zoneSelectorLayoutMode = static_cast<ZoneSelectorLayoutMode>(
        qBound(0, selectorMode, static_cast<int>(ZoneSelectorLayoutMode::Vertical)));
    m_zoneSelectorPreviewWidth = readValidatedInt(*zoneSelector, ConfigDefaults::previewWidthKey(),
                                                  ConfigDefaults::previewWidth(), ConfigDefaults::previewWidthMin(),
                                                  ConfigDefaults::previewWidthMax(), "zone selector preview width");
    m_zoneSelectorPreviewHeight = readValidatedInt(*zoneSelector, ConfigDefaults::previewHeightKey(),
                                                   ConfigDefaults::previewHeight(), ConfigDefaults::previewHeightMin(),
                                                   ConfigDefaults::previewHeightMax(), "zone selector preview height");
    m_zoneSelectorPreviewLockAspect =
        zoneSelector->readBool(ConfigDefaults::previewLockAspectKey(), ConfigDefaults::previewLockAspect());
    m_zoneSelectorGridColumns = readValidatedInt(*zoneSelector, ConfigDefaults::gridColumnsKey(),
                                                 ConfigDefaults::gridColumns(), ConfigDefaults::gridColumnsMin(),
                                                 ConfigDefaults::gridColumnsMax(), "zone selector grid columns");
    int sizeMode = zoneSelector->readInt(ConfigDefaults::sizeModeKey(), ConfigDefaults::sizeMode());
    m_zoneSelectorSizeMode =
        static_cast<ZoneSelectorSizeMode>(qBound(0, sizeMode, static_cast<int>(ZoneSelectorSizeMode::Manual)));
    m_zoneSelectorMaxRows =
        readValidatedInt(*zoneSelector, ConfigDefaults::maxRowsKey(), ConfigDefaults::maxRows(),
                         ConfigDefaults::maxRowsMin(), ConfigDefaults::maxRowsMax(), "zone selector max rows");
}

void Settings::loadShortcutConfig(IConfigBackend* backend)
{
    auto shortcuts = backend->group(ConfigDefaults::shortcutsGlobalGroup());
    m_openEditorShortcut = shortcuts->readString(ConfigDefaults::openEditorKey(), ConfigDefaults::openEditorShortcut());
    m_openSettingsShortcut =
        shortcuts->readString(ConfigDefaults::openSettingsKey(), ConfigDefaults::openSettingsShortcut());
    m_previousLayoutShortcut =
        shortcuts->readString(ConfigDefaults::previousLayoutKey(), ConfigDefaults::previousLayoutShortcut());
    m_nextLayoutShortcut = shortcuts->readString(ConfigDefaults::nextLayoutKey(), ConfigDefaults::nextLayoutShortcut());
    const QString quickLayoutDefaults[9] = {
        ConfigDefaults::quickLayout1Shortcut(), ConfigDefaults::quickLayout2Shortcut(),
        ConfigDefaults::quickLayout3Shortcut(), ConfigDefaults::quickLayout4Shortcut(),
        ConfigDefaults::quickLayout5Shortcut(), ConfigDefaults::quickLayout6Shortcut(),
        ConfigDefaults::quickLayout7Shortcut(), ConfigDefaults::quickLayout8Shortcut(),
        ConfigDefaults::quickLayout9Shortcut()};
    loadIndexedShortcuts(*shortcuts, ConfigDefaults::quickLayoutKeyPattern(), m_quickLayoutShortcuts,
                         quickLayoutDefaults);
    m_moveWindowLeftShortcut =
        shortcuts->readString(ConfigDefaults::moveWindowLeftKey(), ConfigDefaults::moveWindowLeftShortcut());
    m_moveWindowRightShortcut =
        shortcuts->readString(ConfigDefaults::moveWindowRightKey(), ConfigDefaults::moveWindowRightShortcut());
    m_moveWindowUpShortcut =
        shortcuts->readString(ConfigDefaults::moveWindowUpKey(), ConfigDefaults::moveWindowUpShortcut());
    m_moveWindowDownShortcut =
        shortcuts->readString(ConfigDefaults::moveWindowDownKey(), ConfigDefaults::moveWindowDownShortcut());
    m_focusZoneLeftShortcut =
        shortcuts->readString(ConfigDefaults::focusZoneLeftKey(), ConfigDefaults::focusZoneLeftShortcut());
    m_focusZoneRightShortcut =
        shortcuts->readString(ConfigDefaults::focusZoneRightKey(), ConfigDefaults::focusZoneRightShortcut());
    m_focusZoneUpShortcut =
        shortcuts->readString(ConfigDefaults::focusZoneUpKey(), ConfigDefaults::focusZoneUpShortcut());
    m_focusZoneDownShortcut =
        shortcuts->readString(ConfigDefaults::focusZoneDownKey(), ConfigDefaults::focusZoneDownShortcut());
    m_pushToEmptyZoneShortcut =
        shortcuts->readString(ConfigDefaults::pushToEmptyZoneKey(), ConfigDefaults::pushToEmptyZoneShortcut());
    m_restoreWindowSizeShortcut =
        shortcuts->readString(ConfigDefaults::restoreWindowSizeKey(), ConfigDefaults::restoreWindowSizeShortcut());
    m_toggleWindowFloatShortcut =
        shortcuts->readString(ConfigDefaults::toggleWindowFloatKey(), ConfigDefaults::toggleWindowFloatShortcut());
    m_swapWindowLeftShortcut =
        shortcuts->readString(ConfigDefaults::swapWindowLeftKey(), ConfigDefaults::swapWindowLeftShortcut());
    m_swapWindowRightShortcut =
        shortcuts->readString(ConfigDefaults::swapWindowRightKey(), ConfigDefaults::swapWindowRightShortcut());
    m_swapWindowUpShortcut =
        shortcuts->readString(ConfigDefaults::swapWindowUpKey(), ConfigDefaults::swapWindowUpShortcut());
    m_swapWindowDownShortcut =
        shortcuts->readString(ConfigDefaults::swapWindowDownKey(), ConfigDefaults::swapWindowDownShortcut());
    const QString snapToZoneDefaults[9] = {ConfigDefaults::snapToZone1Shortcut(), ConfigDefaults::snapToZone2Shortcut(),
                                           ConfigDefaults::snapToZone3Shortcut(), ConfigDefaults::snapToZone4Shortcut(),
                                           ConfigDefaults::snapToZone5Shortcut(), ConfigDefaults::snapToZone6Shortcut(),
                                           ConfigDefaults::snapToZone7Shortcut(), ConfigDefaults::snapToZone8Shortcut(),
                                           ConfigDefaults::snapToZone9Shortcut()};
    loadIndexedShortcuts(*shortcuts, ConfigDefaults::snapToZoneKeyPattern(), m_snapToZoneShortcuts, snapToZoneDefaults);
    m_rotateWindowsClockwiseShortcut = shortcuts->readString(ConfigDefaults::rotateWindowsClockwiseKey(),
                                                             ConfigDefaults::rotateWindowsClockwiseShortcut());
    m_rotateWindowsCounterclockwiseShortcut = shortcuts->readString(
        ConfigDefaults::rotateWindowsCounterclockwiseKey(), ConfigDefaults::rotateWindowsCounterclockwiseShortcut());
    m_cycleWindowForwardShortcut =
        shortcuts->readString(ConfigDefaults::cycleWindowForwardKey(), ConfigDefaults::cycleWindowForwardShortcut());
    m_cycleWindowBackwardShortcut =
        shortcuts->readString(ConfigDefaults::cycleWindowBackwardKey(), ConfigDefaults::cycleWindowBackwardShortcut());
    m_resnapToNewLayoutShortcut =
        shortcuts->readString(ConfigDefaults::resnapToNewLayoutKey(), ConfigDefaults::resnapToNewLayoutShortcut());
    m_snapAllWindowsShortcut =
        shortcuts->readString(ConfigDefaults::snapAllWindowsKey(), ConfigDefaults::snapAllWindowsShortcut());
    m_layoutPickerShortcut =
        shortcuts->readString(ConfigDefaults::layoutPickerKey(), ConfigDefaults::layoutPickerShortcut());
    m_toggleLayoutLockShortcut =
        shortcuts->readString(ConfigDefaults::toggleLayoutLockKey(), ConfigDefaults::toggleLayoutLockShortcut());
}

void Settings::loadAutotilingConfig(IConfigBackend* backend)
{
    {
        auto tiling = backend->group(ConfigDefaults::tilingGroup());
        m_autotileEnabled = tiling->readBool(ConfigDefaults::enabledKey(), ConfigDefaults::autotileEnabled());
    }
    {
        auto algorithm = backend->group(ConfigDefaults::tilingAlgorithmGroup());
        m_defaultAutotileAlgorithm =
            algorithm->readString(ConfigDefaults::defaultKey(), ConfigDefaults::defaultAutotileAlgorithm());

        // Do NOT validate the saved algorithm ID here — scripted algorithms
        // (including those with @builtinId) are not registered until
        // ScriptedAlgorithmLoader::scanAndRegister() runs later in Daemon::init().
        // The engine's syncFromSettings() validates after all algorithms are loaded.

        qreal splitRatio = algorithm->readDouble(ConfigDefaults::splitRatioKey(), ConfigDefaults::autotileSplitRatio());
        if (splitRatio < ConfigDefaults::autotileSplitRatioMin()
            || splitRatio > ConfigDefaults::autotileSplitRatioMax()) {
            qCWarning(lcConfig) << "Invalid autotile split ratio:" << splitRatio << "clamping to valid range";
            splitRatio =
                qBound(ConfigDefaults::autotileSplitRatioMin(), splitRatio, ConfigDefaults::autotileSplitRatioMax());
        }
        m_autotileSplitRatio = splitRatio;

        qreal splitRatioStep =
            algorithm->readDouble(ConfigDefaults::splitRatioStepKey(), ConfigDefaults::autotileSplitRatioStep());
        m_autotileSplitRatioStep = qBound(ConfigDefaults::autotileSplitRatioStepMin(), splitRatioStep,
                                          ConfigDefaults::autotileSplitRatioStepMax());

        int masterCount = algorithm->readInt(ConfigDefaults::masterCountKey(), ConfigDefaults::autotileMasterCount());
        if (masterCount < ConfigDefaults::autotileMasterCountMin()
            || masterCount > ConfigDefaults::autotileMasterCountMax()) {
            qCWarning(lcConfig) << "Invalid autotile master count:" << masterCount << "clamping to valid range";
            masterCount =
                qBound(ConfigDefaults::autotileMasterCountMin(), masterCount, ConfigDefaults::autotileMasterCountMax());
        }
        m_autotileMasterCount = masterCount;

        m_autotileMaxWindows = readValidatedInt(
            *algorithm, ConfigDefaults::maxWindowsKey(), ConfigDefaults::autotileMaxWindows(),
            ConfigDefaults::autotileMaxWindowsMin(), ConfigDefaults::autotileMaxWindowsMax(), "autotile max windows");

        // Load per-algorithm settings map (clear first so reset-to-defaults works)
        m_autotilePerAlgorithmSettings.clear();
        const QString perAlgoStr = algorithm->readString(ConfigDefaults::perAlgorithmSettingsKey(), QString());
        if (!perAlgoStr.isEmpty()) {
            // Deserialize JSON → QVariantMap → QHash → QVariantMap: the round-trip
            // through perAlgoFromVariantMap sanitizes/clamps values before storing.
            const QJsonObject perAlgoJson = QJsonDocument::fromJson(perAlgoStr.toUtf8()).object();
            m_autotilePerAlgorithmSettings =
                AutotileConfig::perAlgoToVariantMap(AutotileConfig::perAlgoFromVariantMap(perAlgoJson.toVariantMap()));
        }
    }
    {
        auto tilingGaps = backend->group(ConfigDefaults::tilingGapsGroup());
        m_autotileInnerGap = readValidatedInt(*tilingGaps, ConfigDefaults::innerKey(),
                                              ConfigDefaults::autotileInnerGap(), ConfigDefaults::autotileInnerGapMin(),
                                              ConfigDefaults::autotileInnerGapMax(), "autotile inner gap");
        m_autotileOuterGap = readValidatedInt(*tilingGaps, ConfigDefaults::outerKey(),
                                              ConfigDefaults::autotileOuterGap(), ConfigDefaults::autotileOuterGapMin(),
                                              ConfigDefaults::autotileOuterGapMax(), "autotile outer gap");
        m_autotileUsePerSideOuterGap =
            tilingGaps->readBool(ConfigDefaults::usePerSideKey(), ConfigDefaults::autotileUsePerSideOuterGap());
        m_autotileOuterGapTop =
            readValidatedInt(*tilingGaps, ConfigDefaults::topKey(), ConfigDefaults::autotileOuterGapTop(),
                             ConfigDefaults::autotileOuterGapTopMin(), ConfigDefaults::autotileOuterGapTopMax(),
                             "autotile outer gap top");
        m_autotileOuterGapBottom =
            readValidatedInt(*tilingGaps, ConfigDefaults::bottomKey(), ConfigDefaults::autotileOuterGapBottom(),
                             ConfigDefaults::autotileOuterGapBottomMin(), ConfigDefaults::autotileOuterGapBottomMax(),
                             "autotile outer gap bottom");
        m_autotileOuterGapLeft =
            readValidatedInt(*tilingGaps, ConfigDefaults::leftKey(), ConfigDefaults::autotileOuterGapLeft(),
                             ConfigDefaults::autotileOuterGapLeftMin(), ConfigDefaults::autotileOuterGapLeftMax(),
                             "autotile outer gap left");
        m_autotileOuterGapRight =
            readValidatedInt(*tilingGaps, ConfigDefaults::rightKey(), ConfigDefaults::autotileOuterGapRight(),
                             ConfigDefaults::autotileOuterGapRightMin(), ConfigDefaults::autotileOuterGapRightMax(),
                             "autotile outer gap right");
        m_autotileSmartGaps = tilingGaps->readBool(ConfigDefaults::smartGapsKey(), ConfigDefaults::autotileSmartGaps());
    }
    {
        auto tilingBehavior = backend->group(ConfigDefaults::tilingBehaviorGroup());
        m_autotileInsertPosition = static_cast<AutotileInsertPosition>(
            readValidatedInt(*tilingBehavior, ConfigDefaults::insertPositionKey(),
                             ConfigDefaults::autotileInsertPosition(), ConfigDefaults::autotileInsertPositionMin(),
                             ConfigDefaults::autotileInsertPositionMax(), "autotile insert position"));
        m_autotileFocusNewWindows =
            tilingBehavior->readBool(ConfigDefaults::focusNewWindowsKey(), ConfigDefaults::autotileFocusNewWindows());
        m_autotileFocusFollowsMouse = tilingBehavior->readBool(ConfigDefaults::focusFollowsMouseKey(),
                                                               ConfigDefaults::autotileFocusFollowsMouse());
        m_autotileRespectMinimumSize = tilingBehavior->readBool(ConfigDefaults::respectMinimumSizeKey(),
                                                                ConfigDefaults::autotileRespectMinimumSize());
        int autotileStickyHandling = tilingBehavior->readInt(ConfigDefaults::stickyWindowHandlingKey(),
                                                             ConfigDefaults::autotileStickyWindowHandling());
        m_autotileStickyWindowHandling = static_cast<StickyWindowHandling>(
            qBound(static_cast<int>(StickyWindowHandling::TreatAsNormal), autotileStickyHandling,
                   static_cast<int>(StickyWindowHandling::IgnoreAll)));
        QString lockedScreensStr = tilingBehavior->readString(ConfigDefaults::lockedScreensKey());
        QStringList newLocked = lockedScreensStr.isEmpty() ? QStringList() : lockedScreensStr.split(QLatin1Char(','));
        for (auto& s : newLocked)
            s = s.trimmed();
        if (m_lockedScreens != newLocked) {
            m_lockedScreens = newLocked;
            Q_EMIT lockedScreensChanged();
        }
    }
    {
        auto tilingDecorations = backend->group(ConfigDefaults::tilingAppearanceDecorationsGroup());
        m_autotileHideTitleBars =
            tilingDecorations->readBool(ConfigDefaults::hideTitleBarsKey(), ConfigDefaults::autotileHideTitleBars());
    }
    {
        auto tilingBorders = backend->group(ConfigDefaults::tilingAppearanceBordersGroup());
        m_autotileShowBorder =
            tilingBorders->readBool(ConfigDefaults::showBorderKey(), ConfigDefaults::autotileShowBorder());
        m_autotileBorderWidth =
            readValidatedInt(*tilingBorders, ConfigDefaults::widthKey(), ConfigDefaults::autotileBorderWidth(),
                             ConfigDefaults::autotileBorderWidthMin(), ConfigDefaults::autotileBorderWidthMax(),
                             "autotile border width");
        m_autotileBorderRadius =
            readValidatedInt(*tilingBorders, ConfigDefaults::radiusKey(), ConfigDefaults::autotileBorderRadius(),
                             ConfigDefaults::autotileBorderRadiusMin(), ConfigDefaults::autotileBorderRadiusMax(),
                             "autotile border radius");
    }
    {
        auto tilingColors = backend->group(ConfigDefaults::tilingAppearanceColorsGroup());
        m_autotileBorderColor = readValidatedColor(*tilingColors, ConfigDefaults::activeKey(),
                                                   ConfigDefaults::autotileBorderColor(), "autotile border");
        m_autotileInactiveBorderColor =
            readValidatedColor(*tilingColors, ConfigDefaults::inactiveKey(),
                               ConfigDefaults::autotileInactiveBorderColor(), "autotile inactive border");
        m_autotileUseSystemBorderColors =
            tilingColors->readBool(ConfigDefaults::useSystemKey(), ConfigDefaults::autotileUseSystemBorderColors());
    }

    // Animation Settings
    {
        auto animations = backend->group(ConfigDefaults::animationsGroup());
        m_animationsEnabled = animations->readBool(ConfigDefaults::enabledKey(), ConfigDefaults::animationsEnabled());
        m_animationDuration = readValidatedInt(
            *animations, ConfigDefaults::durationKey(), ConfigDefaults::animationDuration(),
            ConfigDefaults::animationDurationMin(), ConfigDefaults::animationDurationMax(), "animation duration");
        m_animationEasingCurve =
            animations->readString(ConfigDefaults::easingCurveKey(), ConfigDefaults::animationEasingCurve());
        m_animationMinDistance =
            readValidatedInt(*animations, ConfigDefaults::minDistanceKey(), ConfigDefaults::animationMinDistance(),
                             ConfigDefaults::animationMinDistanceMin(), ConfigDefaults::animationMinDistanceMax(),
                             "animation min distance");
        m_animationSequenceMode =
            readValidatedInt(*animations, ConfigDefaults::sequenceModeKey(), ConfigDefaults::animationSequenceMode(),
                             ConfigDefaults::animationSequenceModeMin(), ConfigDefaults::animationSequenceModeMax(),
                             "animation sequence mode");
        m_animationStaggerInterval =
            readValidatedInt(*animations, ConfigDefaults::staggerIntervalKey(),
                             ConfigDefaults::animationStaggerInterval(), ConfigDefaults::animationStaggerIntervalMin(),
                             ConfigDefaults::animationStaggerIntervalMax(), "animation stagger interval");
    }

    // Tiling Shortcuts
    {
        auto tilingShortcuts = backend->group(ConfigDefaults::shortcutsTilingGroup());
        m_autotileToggleShortcut =
            tilingShortcuts->readString(ConfigDefaults::toggleKey(), ConfigDefaults::autotileToggleShortcut());
        m_autotileFocusMasterShortcut = tilingShortcuts->readString(ConfigDefaults::focusMasterKey(),
                                                                    ConfigDefaults::autotileFocusMasterShortcut());
        m_autotileSwapMasterShortcut =
            tilingShortcuts->readString(ConfigDefaults::swapMasterKey(), ConfigDefaults::autotileSwapMasterShortcut());
        m_autotileIncMasterRatioShortcut = tilingShortcuts->readString(
            ConfigDefaults::incMasterRatioKey(), ConfigDefaults::autotileIncMasterRatioShortcut());
        m_autotileDecMasterRatioShortcut = tilingShortcuts->readString(
            ConfigDefaults::decMasterRatioKey(), ConfigDefaults::autotileDecMasterRatioShortcut());
        m_autotileIncMasterCountShortcut = tilingShortcuts->readString(
            ConfigDefaults::incMasterCountKey(), ConfigDefaults::autotileIncMasterCountShortcut());
        m_autotileDecMasterCountShortcut = tilingShortcuts->readString(
            ConfigDefaults::decMasterCountKey(), ConfigDefaults::autotileDecMasterCountShortcut());
        m_autotileRetileShortcut =
            tilingShortcuts->readString(ConfigDefaults::retileKey(), ConfigDefaults::autotileRetileShortcut());
    }
}

void Settings::loadEditorConfig(IConfigBackend* backend)
{
    // Capture old values for post-load change-guarded signal emission
    const QString oldDuplicate = m_editorDuplicateShortcut;
    const QString oldSplitH = m_editorSplitHorizontalShortcut;
    const QString oldSplitV = m_editorSplitVerticalShortcut;
    const QString oldFill = m_editorFillShortcut;
    const bool oldGridEnabled = m_editorGridSnappingEnabled;
    const bool oldEdgeEnabled = m_editorEdgeSnappingEnabled;
    const qreal oldIntervalX = m_editorSnapIntervalX;
    const qreal oldIntervalY = m_editorSnapIntervalY;
    const int oldOverrideMod = m_editorSnapOverrideModifier;
    const bool oldFillOnDropEnabled = m_fillOnDropEnabled;
    const int oldFillOnDropMod = m_fillOnDropModifier;

    {
        auto shortcuts = backend->group(ConfigDefaults::editorShortcutsGroup());
        m_editorDuplicateShortcut =
            shortcuts->readString(ConfigDefaults::duplicateKey(), ConfigDefaults::editorDuplicateShortcut());
        m_editorSplitHorizontalShortcut = shortcuts->readString(ConfigDefaults::splitHorizontalKey(),
                                                                ConfigDefaults::editorSplitHorizontalShortcut());
        m_editorSplitVerticalShortcut =
            shortcuts->readString(ConfigDefaults::splitVerticalKey(), ConfigDefaults::editorSplitVerticalShortcut());
        m_editorFillShortcut = shortcuts->readString(ConfigDefaults::fillKey(), ConfigDefaults::editorFillShortcut());
    }
    {
        auto snapping = backend->group(ConfigDefaults::editorSnappingGroup());
        m_editorGridSnappingEnabled =
            snapping->readBool(ConfigDefaults::gridEnabledKey(), ConfigDefaults::editorGridSnappingEnabled());
        m_editorEdgeSnappingEnabled =
            snapping->readBool(ConfigDefaults::edgeEnabledKey(), ConfigDefaults::editorEdgeSnappingEnabled());

        m_editorSnapIntervalX =
            snapping->readDouble(ConfigDefaults::intervalXKey(), ConfigDefaults::editorSnapInterval());
        m_editorSnapIntervalY =
            snapping->readDouble(ConfigDefaults::intervalYKey(), ConfigDefaults::editorSnapInterval());

        m_editorSnapOverrideModifier =
            snapping->readInt(ConfigDefaults::overrideModifierKey(), ConfigDefaults::editorSnapOverrideModifier());
    }
    {
        auto fillOnDrop = backend->group(ConfigDefaults::editorFillOnDropGroup());
        m_fillOnDropEnabled = fillOnDrop->readBool(ConfigDefaults::enabledKey(), ConfigDefaults::fillOnDropEnabled());
        m_fillOnDropModifier = fillOnDrop->readInt(ConfigDefaults::modifierKey(), ConfigDefaults::fillOnDropModifier());
    }

    if (m_editorDuplicateShortcut != oldDuplicate)
        Q_EMIT editorDuplicateShortcutChanged();
    if (m_editorSplitHorizontalShortcut != oldSplitH)
        Q_EMIT editorSplitHorizontalShortcutChanged();
    if (m_editorSplitVerticalShortcut != oldSplitV)
        Q_EMIT editorSplitVerticalShortcutChanged();
    if (m_editorFillShortcut != oldFill)
        Q_EMIT editorFillShortcutChanged();
    if (m_editorGridSnappingEnabled != oldGridEnabled)
        Q_EMIT editorGridSnappingEnabledChanged();
    if (m_editorEdgeSnappingEnabled != oldEdgeEnabled)
        Q_EMIT editorEdgeSnappingEnabledChanged();
    if (!qFuzzyCompare(m_editorSnapIntervalX, oldIntervalX))
        Q_EMIT editorSnapIntervalXChanged();
    if (!qFuzzyCompare(m_editorSnapIntervalY, oldIntervalY))
        Q_EMIT editorSnapIntervalYChanged();
    if (m_editorSnapOverrideModifier != oldOverrideMod)
        Q_EMIT editorSnapOverrideModifierChanged();
    if (m_fillOnDropEnabled != oldFillOnDropEnabled)
        Q_EMIT fillOnDropEnabledChanged();
    if (m_fillOnDropModifier != oldFillOnDropMod)
        Q_EMIT fillOnDropModifierChanged();
}

// ── save() helpers ───────────────────────────────────────────────────────────

void Settings::saveEditorConfig(IConfigBackend* backend)
{
    {
        auto shortcuts = backend->group(ConfigDefaults::editorShortcutsGroup());
        shortcuts->writeString(ConfigDefaults::duplicateKey(), m_editorDuplicateShortcut);
        shortcuts->writeString(ConfigDefaults::splitHorizontalKey(), m_editorSplitHorizontalShortcut);
        shortcuts->writeString(ConfigDefaults::splitVerticalKey(), m_editorSplitVerticalShortcut);
        shortcuts->writeString(ConfigDefaults::fillKey(), m_editorFillShortcut);
    }
    {
        auto snapping = backend->group(ConfigDefaults::editorSnappingGroup());
        snapping->writeBool(ConfigDefaults::gridEnabledKey(), m_editorGridSnappingEnabled);
        snapping->writeBool(ConfigDefaults::edgeEnabledKey(), m_editorEdgeSnappingEnabled);
        snapping->writeDouble(ConfigDefaults::intervalXKey(), m_editorSnapIntervalX);
        snapping->writeDouble(ConfigDefaults::intervalYKey(), m_editorSnapIntervalY);
        snapping->writeInt(ConfigDefaults::overrideModifierKey(), m_editorSnapOverrideModifier);
    }
    {
        auto fillOnDrop = backend->group(ConfigDefaults::editorFillOnDropGroup());
        fillOnDrop->writeBool(ConfigDefaults::enabledKey(), m_fillOnDropEnabled);
        fillOnDrop->writeInt(ConfigDefaults::modifierKey(), m_fillOnDropModifier);
    }
}

void Settings::saveActivationConfig(IConfigBackend* backend)
{
    {
        auto snapping = backend->group(ConfigDefaults::snappingGroup());
        snapping->writeBool(ConfigDefaults::enabledKey(), m_snappingEnabled);
    }
    {
        auto behavior = backend->group(ConfigDefaults::snappingBehaviorGroup());
        saveTriggerList(*behavior, ConfigDefaults::triggersKey(), m_dragActivationTriggers);
        behavior->writeBool(ConfigDefaults::toggleActivationKey(), m_toggleActivation);
    }
    {
        auto zoneSpan = backend->group(ConfigDefaults::snappingBehaviorZoneSpanGroup());
        zoneSpan->writeBool(ConfigDefaults::enabledKey(), m_zoneSpanEnabled);
        zoneSpan->writeInt(ConfigDefaults::modifierKey(), static_cast<int>(m_zoneSpanModifier));
        saveTriggerList(*zoneSpan, ConfigDefaults::triggersKey(), m_zoneSpanTriggers);
    }
}

void Settings::saveDisplayConfig(IConfigBackend* backend)
{
    {
        auto display = backend->group(ConfigDefaults::snappingBehaviorDisplayGroup());
        display->writeBool(ConfigDefaults::showOnAllMonitorsKey(), m_showZonesOnAllMonitors);
        display->writeString(ConfigDefaults::disabledMonitorsKey(), m_disabledMonitors.join(QLatin1Char(',')));
        display->writeString(ConfigDefaults::disabledDesktopsKey(), m_disabledDesktops.join(QLatin1Char(',')));
        display->writeString(ConfigDefaults::disabledActivitiesKey(), m_disabledActivities.join(QLatin1Char(',')));
        display->writeBool(ConfigDefaults::filterByAspectRatioKey(), m_filterLayoutsByAspectRatio);
    }
    {
        auto effects = backend->group(ConfigDefaults::snappingEffectsGroup());
        effects->writeBool(ConfigDefaults::showNumbersKey(), m_showZoneNumbers);
        effects->writeBool(ConfigDefaults::flashOnSwitchKey(), m_flashZonesOnSwitch);
        effects->writeBool(ConfigDefaults::osdOnLayoutSwitchKey(), m_showOsdOnLayoutSwitch);
        effects->writeBool(ConfigDefaults::navigationOsdKey(), m_showNavigationOsd);
        effects->writeInt(ConfigDefaults::osdStyleKey(), static_cast<int>(m_osdStyle));
        effects->writeInt(ConfigDefaults::overlayDisplayModeKey(), static_cast<int>(m_overlayDisplayMode));
    }
}

void Settings::saveAppearanceConfig(IConfigBackend* backend)
{
    {
        auto colors = backend->group(ConfigDefaults::snappingAppearanceColorsGroup());
        colors->writeBool(ConfigDefaults::useSystemKey(), m_useSystemColors);
        colors->writeColor(ConfigDefaults::highlightKey(), m_highlightColor);
        colors->writeColor(ConfigDefaults::inactiveKey(), m_inactiveColor);
        colors->writeColor(ConfigDefaults::borderKey(), m_borderColor);
    }
    {
        auto labels = backend->group(ConfigDefaults::snappingAppearanceLabelsGroup());
        labels->writeColor(ConfigDefaults::fontColorKey(), m_labelFontColor);
        labels->writeString(ConfigDefaults::fontFamilyKey(), m_labelFontFamily);
        labels->writeDouble(ConfigDefaults::fontSizeScaleKey(), m_labelFontSizeScale);
        labels->writeInt(ConfigDefaults::fontWeightKey(), m_labelFontWeight);
        labels->writeBool(ConfigDefaults::fontItalicKey(), m_labelFontItalic);
        labels->writeBool(ConfigDefaults::fontUnderlineKey(), m_labelFontUnderline);
        labels->writeBool(ConfigDefaults::fontStrikeoutKey(), m_labelFontStrikeout);
    }
    {
        auto opacity = backend->group(ConfigDefaults::snappingAppearanceOpacityGroup());
        opacity->writeDouble(ConfigDefaults::activeKey(), m_activeOpacity);
        opacity->writeDouble(ConfigDefaults::inactiveKey(), m_inactiveOpacity);
    }
    {
        auto border = backend->group(ConfigDefaults::snappingAppearanceBorderGroup());
        border->writeInt(ConfigDefaults::widthKey(), m_borderWidth);
        border->writeInt(ConfigDefaults::radiusKey(), m_borderRadius);
    }
    {
        auto effects = backend->group(ConfigDefaults::snappingEffectsGroup());
        effects->writeBool(ConfigDefaults::blurKey(), m_enableBlur);
    }
}

void Settings::saveZoneGeometryConfig(IConfigBackend* backend)
{
    {
        auto gaps = backend->group(ConfigDefaults::snappingGapsGroup());
        gaps->writeInt(ConfigDefaults::innerKey(), m_zonePadding);
        gaps->writeInt(ConfigDefaults::outerKey(), m_outerGap);
        gaps->writeBool(ConfigDefaults::usePerSideKey(), m_usePerSideOuterGap);
        gaps->writeInt(ConfigDefaults::topKey(), m_outerGapTop);
        gaps->writeInt(ConfigDefaults::bottomKey(), m_outerGapBottom);
        gaps->writeInt(ConfigDefaults::leftKey(), m_outerGapLeft);
        gaps->writeInt(ConfigDefaults::rightKey(), m_outerGapRight);
        gaps->writeInt(ConfigDefaults::adjacentThresholdKey(), m_adjacentThreshold);
    }
    {
        auto perf = backend->group(ConfigDefaults::performanceGroup());
        perf->writeInt(ConfigDefaults::pollIntervalMsKey(), m_pollIntervalMs);
        perf->writeInt(ConfigDefaults::minimumZoneSizePxKey(), m_minimumZoneSizePx);
        perf->writeInt(ConfigDefaults::minimumZoneDisplaySizePxKey(), m_minimumZoneDisplaySizePx);
    }
}

void Settings::saveBehaviorConfig(IConfigBackend* backend)
{
    {
        auto windowHandling = backend->group(ConfigDefaults::snappingBehaviorWindowHandlingGroup());
        windowHandling->writeBool(ConfigDefaults::keepOnResolutionChangeKey(), m_keepWindowsInZonesOnResolutionChange);
        windowHandling->writeBool(ConfigDefaults::moveNewToLastZoneKey(), m_moveNewWindowsToLastZone);
        windowHandling->writeBool(ConfigDefaults::restoreOnUnsnapKey(), m_restoreOriginalSizeOnUnsnap);
        windowHandling->writeInt(ConfigDefaults::stickyWindowHandlingKey(), static_cast<int>(m_stickyWindowHandling));
        windowHandling->writeBool(ConfigDefaults::restoreOnLoginKey(), m_restoreWindowsToZonesOnLogin);
        windowHandling->writeString(ConfigDefaults::defaultLayoutIdKey(), m_defaultLayoutId);
    }
    {
        auto snapAssist = backend->group(ConfigDefaults::snappingBehaviorSnapAssistGroup());
        snapAssist->writeBool(ConfigDefaults::featureEnabledKey(), m_snapAssistFeatureEnabled);
        snapAssist->writeBool(ConfigDefaults::enabledKey(), m_snapAssistEnabled);
        saveTriggerList(*snapAssist, ConfigDefaults::triggersKey(), m_snapAssistTriggers);
    }
    {
        auto exclusions = backend->group(ConfigDefaults::exclusionsGroup());
        exclusions->writeString(ConfigDefaults::applicationsKey(), m_excludedApplications.join(QLatin1Char(',')));
        exclusions->writeString(ConfigDefaults::windowClassesKey(), m_excludedWindowClasses.join(QLatin1Char(',')));
        exclusions->writeBool(ConfigDefaults::transientWindowsKey(), m_excludeTransientWindows);
        exclusions->writeInt(ConfigDefaults::minimumWindowWidthKey(), m_minimumWindowWidth);
        exclusions->writeInt(ConfigDefaults::minimumWindowHeightKey(), m_minimumWindowHeight);
    }
}

void Settings::saveZoneSelectorConfig(IConfigBackend* backend)
{
    auto zoneSelector = backend->group(ConfigDefaults::snappingZoneSelectorGroup());
    zoneSelector->writeBool(ConfigDefaults::enabledKey(), m_zoneSelectorEnabled);
    zoneSelector->writeInt(ConfigDefaults::triggerDistanceKey(), m_zoneSelectorTriggerDistance);
    zoneSelector->writeInt(ConfigDefaults::positionKey(), static_cast<int>(m_zoneSelectorPosition));
    zoneSelector->writeInt(ConfigDefaults::layoutModeKey(), static_cast<int>(m_zoneSelectorLayoutMode));
    zoneSelector->writeInt(ConfigDefaults::previewWidthKey(), m_zoneSelectorPreviewWidth);
    zoneSelector->writeInt(ConfigDefaults::previewHeightKey(), m_zoneSelectorPreviewHeight);
    zoneSelector->writeBool(ConfigDefaults::previewLockAspectKey(), m_zoneSelectorPreviewLockAspect);
    zoneSelector->writeInt(ConfigDefaults::gridColumnsKey(), m_zoneSelectorGridColumns);
    zoneSelector->writeInt(ConfigDefaults::sizeModeKey(), static_cast<int>(m_zoneSelectorSizeMode));
    zoneSelector->writeInt(ConfigDefaults::maxRowsKey(), m_zoneSelectorMaxRows);
}

void Settings::saveShortcutConfig(IConfigBackend* backend)
{
    auto shortcuts = backend->group(ConfigDefaults::shortcutsGlobalGroup());
    shortcuts->writeString(ConfigDefaults::openEditorKey(), m_openEditorShortcut);
    shortcuts->writeString(ConfigDefaults::openSettingsKey(), m_openSettingsShortcut);
    shortcuts->writeString(ConfigDefaults::previousLayoutKey(), m_previousLayoutShortcut);
    shortcuts->writeString(ConfigDefaults::nextLayoutKey(), m_nextLayoutShortcut);
    for (int i = 0; i < 9; ++i) {
        shortcuts->writeString(ConfigDefaults::quickLayoutKey(i + 1), m_quickLayoutShortcuts[i]);
    }
    shortcuts->writeString(ConfigDefaults::moveWindowLeftKey(), m_moveWindowLeftShortcut);
    shortcuts->writeString(ConfigDefaults::moveWindowRightKey(), m_moveWindowRightShortcut);
    shortcuts->writeString(ConfigDefaults::moveWindowUpKey(), m_moveWindowUpShortcut);
    shortcuts->writeString(ConfigDefaults::moveWindowDownKey(), m_moveWindowDownShortcut);
    shortcuts->writeString(ConfigDefaults::focusZoneLeftKey(), m_focusZoneLeftShortcut);
    shortcuts->writeString(ConfigDefaults::focusZoneRightKey(), m_focusZoneRightShortcut);
    shortcuts->writeString(ConfigDefaults::focusZoneUpKey(), m_focusZoneUpShortcut);
    shortcuts->writeString(ConfigDefaults::focusZoneDownKey(), m_focusZoneDownShortcut);
    shortcuts->writeString(ConfigDefaults::pushToEmptyZoneKey(), m_pushToEmptyZoneShortcut);
    shortcuts->writeString(ConfigDefaults::restoreWindowSizeKey(), m_restoreWindowSizeShortcut);
    shortcuts->writeString(ConfigDefaults::toggleWindowFloatKey(), m_toggleWindowFloatShortcut);
    shortcuts->writeString(ConfigDefaults::swapWindowLeftKey(), m_swapWindowLeftShortcut);
    shortcuts->writeString(ConfigDefaults::swapWindowRightKey(), m_swapWindowRightShortcut);
    shortcuts->writeString(ConfigDefaults::swapWindowUpKey(), m_swapWindowUpShortcut);
    shortcuts->writeString(ConfigDefaults::swapWindowDownKey(), m_swapWindowDownShortcut);
    for (int i = 0; i < 9; ++i) {
        shortcuts->writeString(ConfigDefaults::snapToZoneKey(i + 1), m_snapToZoneShortcuts[i]);
    }
    shortcuts->writeString(ConfigDefaults::rotateWindowsClockwiseKey(), m_rotateWindowsClockwiseShortcut);
    shortcuts->writeString(ConfigDefaults::rotateWindowsCounterclockwiseKey(), m_rotateWindowsCounterclockwiseShortcut);
    shortcuts->writeString(ConfigDefaults::cycleWindowForwardKey(), m_cycleWindowForwardShortcut);
    shortcuts->writeString(ConfigDefaults::cycleWindowBackwardKey(), m_cycleWindowBackwardShortcut);
    shortcuts->writeString(ConfigDefaults::resnapToNewLayoutKey(), m_resnapToNewLayoutShortcut);
    shortcuts->writeString(ConfigDefaults::snapAllWindowsKey(), m_snapAllWindowsShortcut);
    shortcuts->writeString(ConfigDefaults::layoutPickerKey(), m_layoutPickerShortcut);
    shortcuts->writeString(ConfigDefaults::toggleLayoutLockKey(), m_toggleLayoutLockShortcut);
}

void Settings::saveAutotilingConfig(IConfigBackend* backend)
{
    {
        auto tiling = backend->group(ConfigDefaults::tilingGroup());
        tiling->writeBool(ConfigDefaults::enabledKey(), m_autotileEnabled);
    }
    {
        auto algorithm = backend->group(ConfigDefaults::tilingAlgorithmGroup());
        algorithm->writeString(ConfigDefaults::defaultKey(), m_defaultAutotileAlgorithm);
        algorithm->writeDouble(ConfigDefaults::splitRatioKey(), m_autotileSplitRatio);
        algorithm->writeDouble(ConfigDefaults::splitRatioStepKey(), m_autotileSplitRatioStep);
        algorithm->writeInt(ConfigDefaults::masterCountKey(), m_autotileMasterCount);
        algorithm->writeInt(ConfigDefaults::maxWindowsKey(), m_autotileMaxWindows);
        // Save per-algorithm settings map (reuse shared serialization helpers)
        if (!m_autotilePerAlgorithmSettings.isEmpty()) {
            algorithm->writeString(
                ConfigDefaults::perAlgorithmSettingsKey(),
                QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(m_autotilePerAlgorithmSettings))
                                      .toJson(QJsonDocument::Compact)));
        } else {
            algorithm->deleteKey(ConfigDefaults::perAlgorithmSettingsKey());
        }
    }
    {
        auto tilingGaps = backend->group(ConfigDefaults::tilingGapsGroup());
        tilingGaps->writeInt(ConfigDefaults::innerKey(), m_autotileInnerGap);
        tilingGaps->writeInt(ConfigDefaults::outerKey(), m_autotileOuterGap);
        tilingGaps->writeBool(ConfigDefaults::usePerSideKey(), m_autotileUsePerSideOuterGap);
        tilingGaps->writeInt(ConfigDefaults::topKey(), m_autotileOuterGapTop);
        tilingGaps->writeInt(ConfigDefaults::bottomKey(), m_autotileOuterGapBottom);
        tilingGaps->writeInt(ConfigDefaults::leftKey(), m_autotileOuterGapLeft);
        tilingGaps->writeInt(ConfigDefaults::rightKey(), m_autotileOuterGapRight);
        tilingGaps->writeBool(ConfigDefaults::smartGapsKey(), m_autotileSmartGaps);
    }
    {
        auto tilingBehavior = backend->group(ConfigDefaults::tilingBehaviorGroup());
        tilingBehavior->writeInt(ConfigDefaults::insertPositionKey(), static_cast<int>(m_autotileInsertPosition));
        tilingBehavior->writeBool(ConfigDefaults::focusNewWindowsKey(), m_autotileFocusNewWindows);
        tilingBehavior->writeBool(ConfigDefaults::focusFollowsMouseKey(), m_autotileFocusFollowsMouse);
        tilingBehavior->writeBool(ConfigDefaults::respectMinimumSizeKey(), m_autotileRespectMinimumSize);
        tilingBehavior->writeInt(ConfigDefaults::stickyWindowHandlingKey(),
                                 static_cast<int>(m_autotileStickyWindowHandling));
        tilingBehavior->writeString(ConfigDefaults::lockedScreensKey(), m_lockedScreens.join(QLatin1Char(',')));
    }
    {
        auto tilingDecorations = backend->group(ConfigDefaults::tilingAppearanceDecorationsGroup());
        tilingDecorations->writeBool(ConfigDefaults::hideTitleBarsKey(), m_autotileHideTitleBars);
    }
    {
        auto tilingBorders = backend->group(ConfigDefaults::tilingAppearanceBordersGroup());
        tilingBorders->writeBool(ConfigDefaults::showBorderKey(), m_autotileShowBorder);
        tilingBorders->writeInt(ConfigDefaults::widthKey(), m_autotileBorderWidth);
        tilingBorders->writeInt(ConfigDefaults::radiusKey(), m_autotileBorderRadius);
    }
    {
        auto tilingColors = backend->group(ConfigDefaults::tilingAppearanceColorsGroup());
        tilingColors->writeColor(ConfigDefaults::activeKey(), m_autotileBorderColor);
        tilingColors->writeColor(ConfigDefaults::inactiveKey(), m_autotileInactiveBorderColor);
        tilingColors->writeBool(ConfigDefaults::useSystemKey(), m_autotileUseSystemBorderColors);
    }

    {
        auto animations = backend->group(ConfigDefaults::animationsGroup());
        animations->writeBool(ConfigDefaults::enabledKey(), m_animationsEnabled);
        animations->writeInt(ConfigDefaults::durationKey(), m_animationDuration);
        animations->writeString(ConfigDefaults::easingCurveKey(), m_animationEasingCurve);
        animations->writeInt(ConfigDefaults::minDistanceKey(), m_animationMinDistance);
        animations->writeInt(ConfigDefaults::sequenceModeKey(), m_animationSequenceMode);
        animations->writeInt(ConfigDefaults::staggerIntervalKey(), m_animationStaggerInterval);
    }

    {
        auto tilingShortcuts = backend->group(ConfigDefaults::shortcutsTilingGroup());
        tilingShortcuts->writeString(ConfigDefaults::toggleKey(), m_autotileToggleShortcut);
        tilingShortcuts->writeString(ConfigDefaults::focusMasterKey(), m_autotileFocusMasterShortcut);
        tilingShortcuts->writeString(ConfigDefaults::swapMasterKey(), m_autotileSwapMasterShortcut);
        tilingShortcuts->writeString(ConfigDefaults::incMasterRatioKey(), m_autotileIncMasterRatioShortcut);
        tilingShortcuts->writeString(ConfigDefaults::decMasterRatioKey(), m_autotileDecMasterRatioShortcut);
        tilingShortcuts->writeString(ConfigDefaults::incMasterCountKey(), m_autotileIncMasterCountShortcut);
        tilingShortcuts->writeString(ConfigDefaults::decMasterCountKey(), m_autotileDecMasterCountShortcut);
        tilingShortcuts->writeString(ConfigDefaults::retileKey(), m_autotileRetileShortcut);
    }
}

} // namespace PlasmaZones
