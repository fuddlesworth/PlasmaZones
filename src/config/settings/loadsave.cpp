// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../settings.h"
#include "../configdefaults.h"
#include "../../core/constants.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include <KConfigGroup>
#include "../../autotile/AlgorithmRegistry.h"

namespace PlasmaZones {

// ── load() helpers ───────────────────────────────────────────────────────────

void Settings::loadActivationConfig(KConfigGroup& activation)
{
    m_shiftDragToActivate = activation.readEntry(QLatin1String("ShiftDrag"), ConfigDefaults::shiftDrag());

    int legacyDragMod = activation.readEntry(QLatin1String("DragActivationModifier"), -1);
    if (legacyDragMod == -1) {
        if (activation.hasKey(QLatin1String("ShiftDrag")) && m_shiftDragToActivate) {
            legacyDragMod = static_cast<int>(DragModifier::Shift);
        } else {
            legacyDragMod = ConfigDefaults::dragActivationModifier();
        }
    } else {
        legacyDragMod = qBound(0, legacyDragMod, static_cast<int>(DragModifier::CtrlAltMeta));
    }
    int legacyDragBtn = qBound(0, activation.readEntry(QLatin1String("DragActivationMouseButton"),
                                                        ConfigDefaults::dragActivationMouseButton()), 128);
    if (legacyDragBtn == 1) legacyDragBtn = 0;

    m_dragActivationTriggers = loadTriggerList(activation, QLatin1String("DragActivationTriggers"),
                                               legacyDragMod, legacyDragBtn);

    m_zoneSpanEnabled = activation.readEntry(QLatin1String("ZoneSpanEnabled"), ConfigDefaults::zoneSpanEnabled());

    // Migration: honour old MiddleClickMultiZone disabled state
    if (activation.hasKey(QLatin1String("MiddleClickMultiZone")) && !activation.hasKey(QLatin1String("ZoneSpanModifier"))) {
        bool oldVal = activation.readEntry(QLatin1String("MiddleClickMultiZone"), true);
        if (!oldVal) {
            activation.writeEntry(QLatin1String("ZoneSpanModifier"), static_cast<int>(DragModifier::Disabled));
            qCInfo(lcConfig) << "Migrated MiddleClickMultiZone=false to ZoneSpanModifier=Disabled";
        }
        activation.deleteEntry(QLatin1String("MiddleClickMultiZone"));
        activation.sync();
    }
    int legacySpanMod = activation.readEntry(QLatin1String("ZoneSpanModifier"), ConfigDefaults::zoneSpanModifier());
    if (legacySpanMod < 0 || legacySpanMod > static_cast<int>(DragModifier::CtrlAltMeta)) {
        qCWarning(lcConfig) << "Invalid ZoneSpanModifier value:" << legacySpanMod << "using default";
        legacySpanMod = ConfigDefaults::zoneSpanModifier();
    }
    m_zoneSpanModifier = static_cast<DragModifier>(legacySpanMod);

    m_zoneSpanTriggers = loadTriggerList(activation, QLatin1String("ZoneSpanTriggers"),
                                          legacySpanMod, 0);

    m_toggleActivation = activation.readEntry(QLatin1String("ToggleActivation"), ConfigDefaults::toggleActivation());
    m_snappingEnabled = activation.readEntry(QLatin1String("SnappingEnabled"), ConfigDefaults::snappingEnabled());
}

void Settings::loadDisplayConfig(const KConfigGroup& display)
{
    m_showZonesOnAllMonitors = display.readEntry(QLatin1String("ShowOnAllMonitors"), ConfigDefaults::showOnAllMonitors());
    m_disabledMonitors = display.readEntry(QLatin1String("DisabledMonitors"), QStringList());
    for (int i = 0; i < m_disabledMonitors.size(); ++i) {
        const QString& name = m_disabledMonitors[i];
        if (Utils::isConnectorName(name)) {
            QString resolved = Utils::screenIdForName(name);
            if (resolved != name) {
                m_disabledMonitors[i] = resolved;
            }
        }
    }
    m_showZoneNumbers = display.readEntry(QLatin1String("ShowNumbers"), ConfigDefaults::showNumbers());
    m_flashZonesOnSwitch = display.readEntry(QLatin1String("FlashOnSwitch"), ConfigDefaults::flashOnSwitch());
    m_showOsdOnLayoutSwitch = display.readEntry(QLatin1String("ShowOsdOnLayoutSwitch"), ConfigDefaults::showOsdOnLayoutSwitch());
    m_showNavigationOsd = display.readEntry(QLatin1String("ShowNavigationOsd"), ConfigDefaults::showNavigationOsd());
    m_osdStyle = static_cast<OsdStyle>(readValidatedInt(display, "OsdStyle", ConfigDefaults::osdStyle(), 0, 2, "OSD style"));
}

void Settings::loadAppearanceConfig(const KConfigGroup& appearance)
{
    m_useSystemColors = appearance.readEntry(QLatin1String("UseSystemColors"), ConfigDefaults::useSystemColors());
    m_highlightColor = readValidatedColor(appearance, "HighlightColor", ConfigDefaults::highlightColor(), "highlight");
    m_inactiveColor = readValidatedColor(appearance, "InactiveColor", ConfigDefaults::inactiveColor(), "inactive");
    m_borderColor = readValidatedColor(appearance, "BorderColor", ConfigDefaults::borderColor(), "border");
    m_labelFontColor = readValidatedColor(appearance, "LabelFontColor", ConfigDefaults::labelFontColor(), "label font");

    qreal activeOpacity = appearance.readEntry(QLatin1String("ActiveOpacity"), ConfigDefaults::activeOpacity());
    if (activeOpacity < 0.0 || activeOpacity > 1.0) {
        qCWarning(lcConfig) << "Invalid active opacity:" << activeOpacity << "clamping to valid range";
        activeOpacity = qBound(0.0, activeOpacity, 1.0);
    }
    m_activeOpacity = activeOpacity;

    qreal inactiveOpacity = appearance.readEntry(QLatin1String("InactiveOpacity"), ConfigDefaults::inactiveOpacity());
    if (inactiveOpacity < 0.0 || inactiveOpacity > 1.0) {
        qCWarning(lcConfig) << "Invalid inactive opacity:" << inactiveOpacity << "clamping to valid range";
        inactiveOpacity = qBound(0.0, inactiveOpacity, 1.0);
    }
    m_inactiveOpacity = inactiveOpacity;

    m_borderWidth = readValidatedInt(appearance, "BorderWidth", ConfigDefaults::borderWidth(), 0, 10, "border width");
    m_borderRadius = readValidatedInt(appearance, "BorderRadius", ConfigDefaults::borderRadius(), 0, 50, "border radius");
    m_enableBlur = appearance.readEntry(QLatin1String("EnableBlur"), ConfigDefaults::enableBlur());
    m_labelFontFamily = appearance.readEntry(QLatin1String("LabelFontFamily"), ConfigDefaults::labelFontFamily());
    qreal fontScale = appearance.readEntry(QLatin1String("LabelFontSizeScale"), ConfigDefaults::labelFontSizeScale());
    m_labelFontSizeScale = qBound(0.25, fontScale, 3.0);
    m_labelFontWeight = readValidatedInt(appearance, "LabelFontWeight", ConfigDefaults::labelFontWeight(), 100, 900, "label font weight");
    m_labelFontItalic = appearance.readEntry(QLatin1String("LabelFontItalic"), ConfigDefaults::labelFontItalic());
    m_labelFontUnderline = appearance.readEntry(QLatin1String("LabelFontUnderline"), ConfigDefaults::labelFontUnderline());
    m_labelFontStrikeout = appearance.readEntry(QLatin1String("LabelFontStrikeout"), ConfigDefaults::labelFontStrikeout());
}

void Settings::loadZoneGeometryConfig(const KConfigGroup& zones)
{
    m_zonePadding = readValidatedInt(zones, "Padding", ConfigDefaults::zonePadding(), 0, Defaults::MaxGap, "zone padding");
    m_outerGap = readValidatedInt(zones, "OuterGap", ConfigDefaults::outerGap(), 0, Defaults::MaxGap, "outer gap");
    m_usePerSideOuterGap = zones.readEntry(QLatin1String("UsePerSideOuterGap"), ConfigDefaults::usePerSideOuterGap());
    m_outerGapTop = readValidatedInt(zones, "OuterGapTop", ConfigDefaults::outerGapTop(), 0, Defaults::MaxGap, "outer gap top");
    m_outerGapBottom = readValidatedInt(zones, "OuterGapBottom", ConfigDefaults::outerGapBottom(), 0, Defaults::MaxGap, "outer gap bottom");
    m_outerGapLeft = readValidatedInt(zones, "OuterGapLeft", ConfigDefaults::outerGapLeft(), 0, Defaults::MaxGap, "outer gap left");
    m_outerGapRight = readValidatedInt(zones, "OuterGapRight", ConfigDefaults::outerGapRight(), 0, Defaults::MaxGap, "outer gap right");
    m_adjacentThreshold = readValidatedInt(zones, "AdjacentThreshold", ConfigDefaults::adjacentThreshold(), 5, 100, "adjacent threshold");
    m_pollIntervalMs = readValidatedInt(zones, "PollIntervalMs", ConfigDefaults::pollIntervalMs(), 10, 1000, "poll interval");
    m_minimumZoneSizePx = readValidatedInt(zones, "MinimumZoneSizePx", ConfigDefaults::minimumZoneSizePx(), 50, 500, "minimum zone size");
    m_minimumZoneDisplaySizePx = readValidatedInt(zones, "MinimumZoneDisplaySizePx", ConfigDefaults::minimumZoneDisplaySizePx(), 1, 50, "minimum zone display size");
}

void Settings::loadBehaviorConfig(const KConfigGroup& behavior, const KConfigGroup& exclusions,
                                  const KConfigGroup& activation)
{
    m_keepWindowsInZonesOnResolutionChange = behavior.readEntry(QLatin1String("KeepOnResolutionChange"), ConfigDefaults::keepWindowsInZonesOnResolutionChange());
    m_moveNewWindowsToLastZone = behavior.readEntry(QLatin1String("MoveNewToLastZone"), ConfigDefaults::moveNewWindowsToLastZone());
    m_restoreOriginalSizeOnUnsnap = behavior.readEntry(QLatin1String("RestoreSizeOnUnsnap"), ConfigDefaults::restoreOriginalSizeOnUnsnap());
    int stickyHandling = behavior.readEntry(QLatin1String("StickyWindowHandling"), ConfigDefaults::stickyWindowHandling());
    m_stickyWindowHandling = static_cast<StickyWindowHandling>(
        qBound(static_cast<int>(StickyWindowHandling::TreatAsNormal), stickyHandling,
               static_cast<int>(StickyWindowHandling::IgnoreAll)));
    m_restoreWindowsToZonesOnLogin = behavior.readEntry(QLatin1String("RestoreWindowsToZonesOnLogin"), ConfigDefaults::restoreWindowsToZonesOnLogin());
    m_snapAssistFeatureEnabled = activation.readEntry(QLatin1String("SnapAssistFeatureEnabled"), ConfigDefaults::snapAssistFeatureEnabled());
    const QString snapAssistEnabledKey = QLatin1String("SnapAssistEnabled");
    const QString snapAssistTriggersKey = QLatin1String("SnapAssistTriggers");
    m_snapAssistEnabled = activation.hasKey(snapAssistEnabledKey)
        ? activation.readEntry(snapAssistEnabledKey, ConfigDefaults::snapAssistEnabled())
        : behavior.readEntry(snapAssistEnabledKey, ConfigDefaults::snapAssistEnabled());
    QString snapAssistTriggersJson = activation.hasKey(snapAssistTriggersKey)
        ? activation.readEntry(snapAssistTriggersKey, QString())
        : behavior.readEntry(snapAssistTriggersKey, QString());
    m_snapAssistTriggers = parseTriggerListJson(snapAssistTriggersJson).value_or(ConfigDefaults::snapAssistTriggers());
    m_defaultLayoutId = normalizeUuidString(behavior.readEntry(QLatin1String("DefaultLayoutId"), QString()));

    // Exclusions
    m_excludedApplications = exclusions.readEntry(QLatin1String("Applications"), QStringList());
    m_excludedWindowClasses = exclusions.readEntry(QLatin1String("WindowClasses"), QStringList());
    m_excludeTransientWindows = exclusions.readEntry(QLatin1String("ExcludeTransientWindows"), ConfigDefaults::excludeTransientWindows());
    int minWidth = exclusions.readEntry(QLatin1String("MinimumWindowWidth"), ConfigDefaults::minimumWindowWidth());
    m_minimumWindowWidth = qBound(0, minWidth, 2000);
    int minHeight = exclusions.readEntry(QLatin1String("MinimumWindowHeight"), ConfigDefaults::minimumWindowHeight());
    m_minimumWindowHeight = qBound(0, minHeight, 2000);
}

void Settings::loadZoneSelectorConfig(const KConfigGroup& zoneSelector)
{
    m_zoneSelectorEnabled = zoneSelector.readEntry(QLatin1String("Enabled"), ConfigDefaults::zoneSelectorEnabled());
    m_zoneSelectorTriggerDistance = readValidatedInt(zoneSelector, "TriggerDistance", ConfigDefaults::triggerDistance(), 10, 200, "zone selector trigger distance");
    int selectorPos = zoneSelector.readEntry(QLatin1String("Position"), ConfigDefaults::position());
    if (selectorPos >= 0 && selectorPos <= 8 && selectorPos != 4) {
        m_zoneSelectorPosition = static_cast<ZoneSelectorPosition>(selectorPos);
    } else {
        m_zoneSelectorPosition = static_cast<ZoneSelectorPosition>(ConfigDefaults::position());
    }
    int selectorMode = zoneSelector.readEntry(QLatin1String("LayoutMode"), ConfigDefaults::layoutMode());
    m_zoneSelectorLayoutMode = static_cast<ZoneSelectorLayoutMode>(
        qBound(0, selectorMode, static_cast<int>(ZoneSelectorLayoutMode::Vertical)));
    m_zoneSelectorPreviewWidth = readValidatedInt(zoneSelector, "PreviewWidth", ConfigDefaults::previewWidth(), 80, 400, "zone selector preview width");
    m_zoneSelectorPreviewHeight = readValidatedInt(zoneSelector, "PreviewHeight", ConfigDefaults::previewHeight(), 60, 300, "zone selector preview height");
    m_zoneSelectorPreviewLockAspect = zoneSelector.readEntry(QLatin1String("PreviewLockAspect"), ConfigDefaults::previewLockAspect());
    m_zoneSelectorGridColumns = readValidatedInt(zoneSelector, "GridColumns", ConfigDefaults::gridColumns(), 1, 10, "zone selector grid columns");
    int sizeMode = zoneSelector.readEntry(QLatin1String("SizeMode"), ConfigDefaults::sizeMode());
    m_zoneSelectorSizeMode = static_cast<ZoneSelectorSizeMode>(
        qBound(0, sizeMode, static_cast<int>(ZoneSelectorSizeMode::Manual)));
    m_zoneSelectorMaxRows = readValidatedInt(zoneSelector, "MaxRows", ConfigDefaults::maxRows(), 1, 10, "zone selector max rows");
}

void Settings::loadShortcutConfig(const KConfigGroup& globalShortcuts)
{
    m_openEditorShortcut = globalShortcuts.readEntry(QLatin1String("OpenEditorShortcut"), ConfigDefaults::openEditorShortcut());
    m_previousLayoutShortcut = globalShortcuts.readEntry(QLatin1String("PreviousLayoutShortcut"), ConfigDefaults::previousLayoutShortcut());
    m_nextLayoutShortcut = globalShortcuts.readEntry(QLatin1String("NextLayoutShortcut"), ConfigDefaults::nextLayoutShortcut());
    const QString quickLayoutDefaults[9] = {
        ConfigDefaults::quickLayout1Shortcut(), ConfigDefaults::quickLayout2Shortcut(),
        ConfigDefaults::quickLayout3Shortcut(), ConfigDefaults::quickLayout4Shortcut(),
        ConfigDefaults::quickLayout5Shortcut(), ConfigDefaults::quickLayout6Shortcut(),
        ConfigDefaults::quickLayout7Shortcut(), ConfigDefaults::quickLayout8Shortcut(),
        ConfigDefaults::quickLayout9Shortcut()
    };
    loadIndexedShortcuts(globalShortcuts, QStringLiteral("QuickLayout%1Shortcut"), m_quickLayoutShortcuts, quickLayoutDefaults);
    m_moveWindowLeftShortcut = globalShortcuts.readEntry(QLatin1String("MoveWindowLeft"), ConfigDefaults::moveWindowLeftShortcut());
    m_moveWindowRightShortcut = globalShortcuts.readEntry(QLatin1String("MoveWindowRight"), ConfigDefaults::moveWindowRightShortcut());
    m_moveWindowUpShortcut = globalShortcuts.readEntry(QLatin1String("MoveWindowUp"), ConfigDefaults::moveWindowUpShortcut());
    m_moveWindowDownShortcut = globalShortcuts.readEntry(QLatin1String("MoveWindowDown"), ConfigDefaults::moveWindowDownShortcut());
    m_focusZoneLeftShortcut = globalShortcuts.readEntry(QLatin1String("FocusZoneLeft"), ConfigDefaults::focusZoneLeftShortcut());
    m_focusZoneRightShortcut = globalShortcuts.readEntry(QLatin1String("FocusZoneRight"), ConfigDefaults::focusZoneRightShortcut());
    m_focusZoneUpShortcut = globalShortcuts.readEntry(QLatin1String("FocusZoneUp"), ConfigDefaults::focusZoneUpShortcut());
    m_focusZoneDownShortcut = globalShortcuts.readEntry(QLatin1String("FocusZoneDown"), ConfigDefaults::focusZoneDownShortcut());
    m_pushToEmptyZoneShortcut = globalShortcuts.readEntry(QLatin1String("PushToEmptyZone"), ConfigDefaults::pushToEmptyZoneShortcut());
    m_restoreWindowSizeShortcut = globalShortcuts.readEntry(QLatin1String("RestoreWindowSize"), ConfigDefaults::restoreWindowSizeShortcut());
    m_toggleWindowFloatShortcut = globalShortcuts.readEntry(QLatin1String("ToggleWindowFloat"), ConfigDefaults::toggleWindowFloatShortcut());
    m_swapWindowLeftShortcut = globalShortcuts.readEntry(QLatin1String("SwapWindowLeft"), ConfigDefaults::swapWindowLeftShortcut());
    m_swapWindowRightShortcut = globalShortcuts.readEntry(QLatin1String("SwapWindowRight"), ConfigDefaults::swapWindowRightShortcut());
    m_swapWindowUpShortcut = globalShortcuts.readEntry(QLatin1String("SwapWindowUp"), ConfigDefaults::swapWindowUpShortcut());
    m_swapWindowDownShortcut = globalShortcuts.readEntry(QLatin1String("SwapWindowDown"), ConfigDefaults::swapWindowDownShortcut());
    const QString snapToZoneDefaults[9] = {
        ConfigDefaults::snapToZone1Shortcut(), ConfigDefaults::snapToZone2Shortcut(),
        ConfigDefaults::snapToZone3Shortcut(), ConfigDefaults::snapToZone4Shortcut(),
        ConfigDefaults::snapToZone5Shortcut(), ConfigDefaults::snapToZone6Shortcut(),
        ConfigDefaults::snapToZone7Shortcut(), ConfigDefaults::snapToZone8Shortcut(),
        ConfigDefaults::snapToZone9Shortcut()
    };
    loadIndexedShortcuts(globalShortcuts, QStringLiteral("SnapToZone%1"), m_snapToZoneShortcuts, snapToZoneDefaults);
    m_rotateWindowsClockwiseShortcut = globalShortcuts.readEntry(QLatin1String("RotateWindowsClockwise"), ConfigDefaults::rotateWindowsClockwiseShortcut());
    m_rotateWindowsCounterclockwiseShortcut = globalShortcuts.readEntry(QLatin1String("RotateWindowsCounterclockwise"), ConfigDefaults::rotateWindowsCounterclockwiseShortcut());
    m_cycleWindowForwardShortcut = globalShortcuts.readEntry(QLatin1String("CycleWindowForward"), ConfigDefaults::cycleWindowForwardShortcut());
    m_cycleWindowBackwardShortcut = globalShortcuts.readEntry(QLatin1String("CycleWindowBackward"), ConfigDefaults::cycleWindowBackwardShortcut());
    m_resnapToNewLayoutShortcut = globalShortcuts.readEntry(QLatin1String("ResnapToNewLayoutShortcut"), ConfigDefaults::resnapToNewLayoutShortcut());
    m_snapAllWindowsShortcut = globalShortcuts.readEntry(QLatin1String("SnapAllWindowsShortcut"), ConfigDefaults::snapAllWindowsShortcut());
    m_layoutPickerShortcut = globalShortcuts.readEntry(QLatin1String("LayoutPickerShortcut"), ConfigDefaults::layoutPickerShortcut());
}

void Settings::loadAutotilingConfig(const KConfigGroup& autotiling, const KConfigGroup& animations,
                                     const KConfigGroup& autotileShortcuts)
{
    m_autotileEnabled = autotiling.readEntry(QLatin1String("AutotileEnabled"), ConfigDefaults::autotileEnabled());
    m_autotileAlgorithm = autotiling.readEntry(QLatin1String("AutotileAlgorithm"), ConfigDefaults::autotileAlgorithm());

    qreal splitRatio = autotiling.readEntry(QLatin1String("AutotileSplitRatio"), ConfigDefaults::autotileSplitRatio());
    if (splitRatio < AutotileDefaults::MinSplitRatio || splitRatio > AutotileDefaults::MaxSplitRatio) {
        qCWarning(lcConfig) << "Invalid autotile split ratio:" << splitRatio << "clamping to valid range";
        splitRatio = qBound(AutotileDefaults::MinSplitRatio, splitRatio, AutotileDefaults::MaxSplitRatio);
    }
    m_autotileSplitRatio = splitRatio;

    int masterCount = autotiling.readEntry(QLatin1String("AutotileMasterCount"), ConfigDefaults::autotileMasterCount());
    if (masterCount < AutotileDefaults::MinMasterCount || masterCount > AutotileDefaults::MaxMasterCount) {
        qCWarning(lcConfig) << "Invalid autotile master count:" << masterCount << "clamping to valid range";
        masterCount = qBound(AutotileDefaults::MinMasterCount, masterCount, AutotileDefaults::MaxMasterCount);
    }
    m_autotileMasterCount = masterCount;

    m_autotileInnerGap = readValidatedInt(autotiling, "AutotileInnerGap", ConfigDefaults::autotileInnerGap(),
                                          AutotileDefaults::MinGap, AutotileDefaults::MaxGap, "autotile inner gap");
    m_autotileOuterGap = readValidatedInt(autotiling, "AutotileOuterGap", ConfigDefaults::autotileOuterGap(),
                                          AutotileDefaults::MinGap, AutotileDefaults::MaxGap, "autotile outer gap");
    m_autotileUsePerSideOuterGap = autotiling.readEntry(QLatin1String("AutotileUsePerSideOuterGap"), ConfigDefaults::autotileUsePerSideOuterGap());
    m_autotileOuterGapTop = readValidatedInt(autotiling, "AutotileOuterGapTop", ConfigDefaults::autotileOuterGapTop(),
                                              AutotileDefaults::MinGap, AutotileDefaults::MaxGap, "autotile outer gap top");
    m_autotileOuterGapBottom = readValidatedInt(autotiling, "AutotileOuterGapBottom", ConfigDefaults::autotileOuterGapBottom(),
                                                 AutotileDefaults::MinGap, AutotileDefaults::MaxGap, "autotile outer gap bottom");
    m_autotileOuterGapLeft = readValidatedInt(autotiling, "AutotileOuterGapLeft", ConfigDefaults::autotileOuterGapLeft(),
                                               AutotileDefaults::MinGap, AutotileDefaults::MaxGap, "autotile outer gap left");
    m_autotileOuterGapRight = readValidatedInt(autotiling, "AutotileOuterGapRight", ConfigDefaults::autotileOuterGapRight(),
                                                AutotileDefaults::MinGap, AutotileDefaults::MaxGap, "autotile outer gap right");
    m_autotileFocusNewWindows = autotiling.readEntry(QLatin1String("AutotileFocusNewWindows"), ConfigDefaults::autotileFocusNewWindows());
    m_autotileSmartGaps = autotiling.readEntry(QLatin1String("AutotileSmartGaps"), ConfigDefaults::autotileSmartGaps());
    m_autotileMaxWindows = readValidatedInt(autotiling, "AutotileMaxWindows", ConfigDefaults::autotileMaxWindows(),
                                            AutotileDefaults::MinMaxWindows, AutotileDefaults::MaxMaxWindows, "autotile max windows");
    m_autotileInsertPosition = static_cast<AutotileInsertPosition>(
        readValidatedInt(autotiling, "AutotileInsertPosition", ConfigDefaults::autotileInsertPosition(), 0, 2, "autotile insert position"));

    // Animation Settings
    m_animationsEnabled = animations.readEntry(QLatin1String("AnimationsEnabled"), ConfigDefaults::animationsEnabled());
    m_animationDuration = readValidatedInt(animations, "AnimationDuration", ConfigDefaults::animationDuration(), 50, 500, "animation duration");
    m_animationEasingCurve = animations.readEntry(QLatin1String("AnimationEasingCurve"), ConfigDefaults::animationEasingCurve());
    m_animationMinDistance = readValidatedInt(animations, "AnimationMinDistance", ConfigDefaults::animationMinDistance(), 0, 200, "animation min distance");
    m_animationSequenceMode = readValidatedInt(animations, "AnimationSequenceMode", ConfigDefaults::animationSequenceMode(), 0, 1, "animation sequence mode");
    m_animationStaggerInterval = readValidatedInt(animations, "AnimationStaggerInterval", ConfigDefaults::animationStaggerInterval(), AutotileDefaults::MinAnimationStaggerIntervalMs, AutotileDefaults::MaxAnimationStaggerIntervalMs, "animation stagger interval");

    m_autotileFocusFollowsMouse = autotiling.readEntry(QLatin1String("AutotileFocusFollowsMouse"), ConfigDefaults::autotileFocusFollowsMouse());
    m_autotileRespectMinimumSize = autotiling.readEntry(QLatin1String("AutotileRespectMinimumSize"), ConfigDefaults::autotileRespectMinimumSize());
    m_autotileHideTitleBars = autotiling.readEntry(QLatin1String("AutotileHideTitleBars"), ConfigDefaults::autotileHideTitleBars());
    m_autotileBorderWidth = readValidatedInt(autotiling, "AutotileBorderWidth", ConfigDefaults::autotileBorderWidth(), 0, 10, "autotile border width");
    m_autotileBorderColor = readValidatedColor(autotiling, "AutotileBorderColor", ConfigDefaults::autotileBorderColor(), "autotile border");
    m_autotileUseSystemBorderColors = autotiling.readEntry(QLatin1String("AutotileUseSystemBorderColors"), ConfigDefaults::autotileUseSystemBorderColors());

    // Autotile Shortcuts
    m_autotileToggleShortcut = autotileShortcuts.readEntry(QLatin1String("ToggleShortcut"), ConfigDefaults::autotileToggleShortcut());
    m_autotileFocusMasterShortcut = autotileShortcuts.readEntry(QLatin1String("FocusMasterShortcut"), ConfigDefaults::autotileFocusMasterShortcut());
    m_autotileSwapMasterShortcut = autotileShortcuts.readEntry(QLatin1String("SwapMasterShortcut"), ConfigDefaults::autotileSwapMasterShortcut());
    m_autotileIncMasterRatioShortcut = autotileShortcuts.readEntry(QLatin1String("IncMasterRatioShortcut"), ConfigDefaults::autotileIncMasterRatioShortcut());
    m_autotileDecMasterRatioShortcut = autotileShortcuts.readEntry(QLatin1String("DecMasterRatioShortcut"), ConfigDefaults::autotileDecMasterRatioShortcut());
    m_autotileIncMasterCountShortcut = autotileShortcuts.readEntry(QLatin1String("IncMasterCountShortcut"), ConfigDefaults::autotileIncMasterCountShortcut());
    m_autotileDecMasterCountShortcut = autotileShortcuts.readEntry(QLatin1String("DecMasterCountShortcut"), ConfigDefaults::autotileDecMasterCountShortcut());
    m_autotileRetileShortcut = autotileShortcuts.readEntry(QLatin1String("RetileShortcut"), ConfigDefaults::autotileRetileShortcut());
}

// ── save() helpers ───────────────────────────────────────────────────────────

void Settings::saveActivationConfig(KConfigGroup& activation)
{
    activation.writeEntry(QLatin1String("ShiftDrag"), m_shiftDragToActivate); // Deprecated, kept for compatibility
    saveTriggerList(activation, QLatin1String("DragActivationTriggers"), m_dragActivationTriggers);
    activation.deleteEntry(QLatin1String("DragActivationModifier"));
    activation.deleteEntry(QLatin1String("DragActivationMouseButton"));
    activation.deleteEntry(QLatin1String("MultiZoneModifier"));
    activation.deleteEntry(QLatin1String("MultiZoneTriggers"));
    activation.deleteEntry(QLatin1String("MultiZoneMouseButton"));
    activation.writeEntry(QLatin1String("ZoneSpanEnabled"), m_zoneSpanEnabled);
    activation.writeEntry(QLatin1String("ZoneSpanModifier"), static_cast<int>(m_zoneSpanModifier));
    saveTriggerList(activation, QLatin1String("ZoneSpanTriggers"), m_zoneSpanTriggers);
    activation.deleteEntry(QLatin1String("ZoneSpanMouseButton"));
    activation.writeEntry(QLatin1String("ToggleActivation"), m_toggleActivation);
    activation.writeEntry(QLatin1String("SnappingEnabled"), m_snappingEnabled);
}

void Settings::saveDisplayConfig(KConfigGroup& display)
{
    display.writeEntry(QLatin1String("ShowOnAllMonitors"), m_showZonesOnAllMonitors);
    display.writeEntry(QLatin1String("DisabledMonitors"), m_disabledMonitors);
    display.writeEntry(QLatin1String("ShowNumbers"), m_showZoneNumbers);
    display.writeEntry(QLatin1String("FlashOnSwitch"), m_flashZonesOnSwitch);
    display.writeEntry(QLatin1String("ShowOsdOnLayoutSwitch"), m_showOsdOnLayoutSwitch);
    display.writeEntry(QLatin1String("ShowNavigationOsd"), m_showNavigationOsd);
    display.writeEntry(QLatin1String("OsdStyle"), static_cast<int>(m_osdStyle));
}

void Settings::saveAppearanceConfig(KConfigGroup& appearance)
{
    appearance.writeEntry(QLatin1String("UseSystemColors"), m_useSystemColors);
    appearance.writeEntry(QLatin1String("HighlightColor"), m_highlightColor);
    appearance.writeEntry(QLatin1String("InactiveColor"), m_inactiveColor);
    appearance.writeEntry(QLatin1String("BorderColor"), m_borderColor);
    appearance.writeEntry(QLatin1String("LabelFontColor"), m_labelFontColor);
    appearance.writeEntry(QLatin1String("ActiveOpacity"), m_activeOpacity);
    appearance.writeEntry(QLatin1String("InactiveOpacity"), m_inactiveOpacity);
    appearance.writeEntry(QLatin1String("BorderWidth"), m_borderWidth);
    appearance.writeEntry(QLatin1String("BorderRadius"), m_borderRadius);
    appearance.writeEntry(QLatin1String("EnableBlur"), m_enableBlur);
    appearance.writeEntry(QLatin1String("LabelFontFamily"), m_labelFontFamily);
    appearance.writeEntry(QLatin1String("LabelFontSizeScale"), m_labelFontSizeScale);
    appearance.writeEntry(QLatin1String("LabelFontWeight"), m_labelFontWeight);
    appearance.writeEntry(QLatin1String("LabelFontItalic"), m_labelFontItalic);
    appearance.writeEntry(QLatin1String("LabelFontUnderline"), m_labelFontUnderline);
    appearance.writeEntry(QLatin1String("LabelFontStrikeout"), m_labelFontStrikeout);
}

void Settings::saveZoneGeometryConfig(KConfigGroup& zones)
{
    zones.writeEntry(QLatin1String("Padding"), m_zonePadding);
    zones.writeEntry(QLatin1String("OuterGap"), m_outerGap);
    zones.writeEntry(QLatin1String("UsePerSideOuterGap"), m_usePerSideOuterGap);
    zones.writeEntry(QLatin1String("OuterGapTop"), m_outerGapTop);
    zones.writeEntry(QLatin1String("OuterGapBottom"), m_outerGapBottom);
    zones.writeEntry(QLatin1String("OuterGapLeft"), m_outerGapLeft);
    zones.writeEntry(QLatin1String("OuterGapRight"), m_outerGapRight);
    zones.writeEntry(QLatin1String("AdjacentThreshold"), m_adjacentThreshold);
    zones.writeEntry(QLatin1String("PollIntervalMs"), m_pollIntervalMs);
    zones.writeEntry(QLatin1String("MinimumZoneSizePx"), m_minimumZoneSizePx);
    zones.writeEntry(QLatin1String("MinimumZoneDisplaySizePx"), m_minimumZoneDisplaySizePx);
}

void Settings::saveBehaviorConfig(KConfigGroup& behavior, KConfigGroup& exclusions, KConfigGroup& activation)
{
    behavior.writeEntry(QLatin1String("KeepOnResolutionChange"), m_keepWindowsInZonesOnResolutionChange);
    behavior.writeEntry(QLatin1String("MoveNewToLastZone"), m_moveNewWindowsToLastZone);
    behavior.writeEntry(QLatin1String("RestoreSizeOnUnsnap"), m_restoreOriginalSizeOnUnsnap);
    behavior.writeEntry(QLatin1String("StickyWindowHandling"), static_cast<int>(m_stickyWindowHandling));
    behavior.writeEntry(QLatin1String("RestoreWindowsToZonesOnLogin"), m_restoreWindowsToZonesOnLogin);
    activation.writeEntry(QLatin1String("SnapAssistFeatureEnabled"), m_snapAssistFeatureEnabled);
    activation.writeEntry(QLatin1String("SnapAssistEnabled"), m_snapAssistEnabled);
    saveTriggerList(activation, QLatin1String("SnapAssistTriggers"), m_snapAssistTriggers);
    behavior.deleteEntry(QLatin1String("SnapAssistEnabled"));
    behavior.deleteEntry(QLatin1String("SnapAssistTriggers"));
    behavior.writeEntry(QLatin1String("DefaultLayoutId"), m_defaultLayoutId);

    exclusions.writeEntry(QLatin1String("Applications"), m_excludedApplications);
    exclusions.writeEntry(QLatin1String("WindowClasses"), m_excludedWindowClasses);
    exclusions.writeEntry(QLatin1String("ExcludeTransientWindows"), m_excludeTransientWindows);
    exclusions.writeEntry(QLatin1String("MinimumWindowWidth"), m_minimumWindowWidth);
    exclusions.writeEntry(QLatin1String("MinimumWindowHeight"), m_minimumWindowHeight);
}

void Settings::saveZoneSelectorConfig(KConfigGroup& zoneSelector)
{
    zoneSelector.writeEntry(QLatin1String("Enabled"), m_zoneSelectorEnabled);
    zoneSelector.writeEntry(QLatin1String("TriggerDistance"), m_zoneSelectorTriggerDistance);
    zoneSelector.writeEntry(QLatin1String("Position"), static_cast<int>(m_zoneSelectorPosition));
    zoneSelector.writeEntry(QLatin1String("LayoutMode"), static_cast<int>(m_zoneSelectorLayoutMode));
    zoneSelector.writeEntry(QLatin1String("PreviewWidth"), m_zoneSelectorPreviewWidth);
    zoneSelector.writeEntry(QLatin1String("PreviewHeight"), m_zoneSelectorPreviewHeight);
    zoneSelector.writeEntry(QLatin1String("PreviewLockAspect"), m_zoneSelectorPreviewLockAspect);
    zoneSelector.writeEntry(QLatin1String("GridColumns"), m_zoneSelectorGridColumns);
    zoneSelector.writeEntry(QLatin1String("SizeMode"), static_cast<int>(m_zoneSelectorSizeMode));
    zoneSelector.writeEntry(QLatin1String("MaxRows"), m_zoneSelectorMaxRows);
}

void Settings::saveShortcutConfig(KConfigGroup& globalShortcuts)
{
    globalShortcuts.writeEntry(QLatin1String("OpenEditorShortcut"), m_openEditorShortcut);
    globalShortcuts.writeEntry(QLatin1String("PreviousLayoutShortcut"), m_previousLayoutShortcut);
    globalShortcuts.writeEntry(QLatin1String("NextLayoutShortcut"), m_nextLayoutShortcut);
    for (int i = 0; i < 9; ++i) {
        globalShortcuts.writeEntry(QStringLiteral("QuickLayout%1Shortcut").arg(i + 1), m_quickLayoutShortcuts[i]);
    }
    globalShortcuts.writeEntry(QLatin1String("MoveWindowLeft"), m_moveWindowLeftShortcut);
    globalShortcuts.writeEntry(QLatin1String("MoveWindowRight"), m_moveWindowRightShortcut);
    globalShortcuts.writeEntry(QLatin1String("MoveWindowUp"), m_moveWindowUpShortcut);
    globalShortcuts.writeEntry(QLatin1String("MoveWindowDown"), m_moveWindowDownShortcut);
    globalShortcuts.writeEntry(QLatin1String("FocusZoneLeft"), m_focusZoneLeftShortcut);
    globalShortcuts.writeEntry(QLatin1String("FocusZoneRight"), m_focusZoneRightShortcut);
    globalShortcuts.writeEntry(QLatin1String("FocusZoneUp"), m_focusZoneUpShortcut);
    globalShortcuts.writeEntry(QLatin1String("FocusZoneDown"), m_focusZoneDownShortcut);
    globalShortcuts.writeEntry(QLatin1String("PushToEmptyZone"), m_pushToEmptyZoneShortcut);
    globalShortcuts.writeEntry(QLatin1String("RestoreWindowSize"), m_restoreWindowSizeShortcut);
    globalShortcuts.writeEntry(QLatin1String("ToggleWindowFloat"), m_toggleWindowFloatShortcut);
    globalShortcuts.writeEntry(QLatin1String("SwapWindowLeft"), m_swapWindowLeftShortcut);
    globalShortcuts.writeEntry(QLatin1String("SwapWindowRight"), m_swapWindowRightShortcut);
    globalShortcuts.writeEntry(QLatin1String("SwapWindowUp"), m_swapWindowUpShortcut);
    globalShortcuts.writeEntry(QLatin1String("SwapWindowDown"), m_swapWindowDownShortcut);
    for (int i = 0; i < 9; ++i) {
        globalShortcuts.writeEntry(QStringLiteral("SnapToZone%1").arg(i + 1), m_snapToZoneShortcuts[i]);
    }
    globalShortcuts.writeEntry(QLatin1String("RotateWindowsClockwise"), m_rotateWindowsClockwiseShortcut);
    globalShortcuts.writeEntry(QLatin1String("RotateWindowsCounterclockwise"), m_rotateWindowsCounterclockwiseShortcut);
    globalShortcuts.writeEntry(QLatin1String("CycleWindowForward"), m_cycleWindowForwardShortcut);
    globalShortcuts.writeEntry(QLatin1String("CycleWindowBackward"), m_cycleWindowBackwardShortcut);
    globalShortcuts.writeEntry(QLatin1String("ResnapToNewLayoutShortcut"), m_resnapToNewLayoutShortcut);
    globalShortcuts.writeEntry(QLatin1String("SnapAllWindowsShortcut"), m_snapAllWindowsShortcut);
    globalShortcuts.writeEntry(QLatin1String("LayoutPickerShortcut"), m_layoutPickerShortcut);
}

void Settings::saveAutotilingConfig(KConfigGroup& autotiling, KConfigGroup& animations,
                                    KConfigGroup& autotileShortcuts)
{
    autotiling.writeEntry(QLatin1String("AutotileEnabled"), m_autotileEnabled);
    autotiling.writeEntry(QLatin1String("AutotileAlgorithm"), m_autotileAlgorithm);
    autotiling.writeEntry(QLatin1String("AutotileSplitRatio"), m_autotileSplitRatio);
    autotiling.writeEntry(QLatin1String("AutotileMasterCount"), m_autotileMasterCount);
    autotiling.writeEntry(QLatin1String("AutotileInnerGap"), m_autotileInnerGap);
    autotiling.writeEntry(QLatin1String("AutotileOuterGap"), m_autotileOuterGap);
    autotiling.writeEntry(QLatin1String("AutotileUsePerSideOuterGap"), m_autotileUsePerSideOuterGap);
    autotiling.writeEntry(QLatin1String("AutotileOuterGapTop"), m_autotileOuterGapTop);
    autotiling.writeEntry(QLatin1String("AutotileOuterGapBottom"), m_autotileOuterGapBottom);
    autotiling.writeEntry(QLatin1String("AutotileOuterGapLeft"), m_autotileOuterGapLeft);
    autotiling.writeEntry(QLatin1String("AutotileOuterGapRight"), m_autotileOuterGapRight);
    autotiling.writeEntry(QLatin1String("AutotileFocusNewWindows"), m_autotileFocusNewWindows);
    autotiling.writeEntry(QLatin1String("AutotileSmartGaps"), m_autotileSmartGaps);
    autotiling.writeEntry(QLatin1String("AutotileMaxWindows"), m_autotileMaxWindows);
    autotiling.writeEntry(QLatin1String("AutotileInsertPosition"), static_cast<int>(m_autotileInsertPosition));
    autotiling.writeEntry(QLatin1String("AutotileFocusFollowsMouse"), m_autotileFocusFollowsMouse);
    autotiling.writeEntry(QLatin1String("AutotileRespectMinimumSize"), m_autotileRespectMinimumSize);
    autotiling.writeEntry(QLatin1String("AutotileHideTitleBars"), m_autotileHideTitleBars);
    autotiling.writeEntry(QLatin1String("AutotileBorderWidth"), m_autotileBorderWidth);
    autotiling.writeEntry(QLatin1String("AutotileBorderColor"), m_autotileBorderColor);
    autotiling.writeEntry(QLatin1String("AutotileUseSystemBorderColors"), m_autotileUseSystemBorderColors);

    animations.writeEntry(QLatin1String("AnimationsEnabled"), m_animationsEnabled);
    animations.writeEntry(QLatin1String("AnimationDuration"), m_animationDuration);
    animations.writeEntry(QLatin1String("AnimationEasingCurve"), m_animationEasingCurve);
    animations.writeEntry(QLatin1String("AnimationMinDistance"), m_animationMinDistance);
    animations.writeEntry(QLatin1String("AnimationSequenceMode"), m_animationSequenceMode);
    animations.writeEntry(QLatin1String("AnimationStaggerInterval"), m_animationStaggerInterval);

    autotileShortcuts.writeEntry(QLatin1String("ToggleShortcut"), m_autotileToggleShortcut);
    autotileShortcuts.writeEntry(QLatin1String("FocusMasterShortcut"), m_autotileFocusMasterShortcut);
    autotileShortcuts.writeEntry(QLatin1String("SwapMasterShortcut"), m_autotileSwapMasterShortcut);
    autotileShortcuts.writeEntry(QLatin1String("IncMasterRatioShortcut"), m_autotileIncMasterRatioShortcut);
    autotileShortcuts.writeEntry(QLatin1String("DecMasterRatioShortcut"), m_autotileDecMasterRatioShortcut);
    autotileShortcuts.writeEntry(QLatin1String("IncMasterCountShortcut"), m_autotileIncMasterCountShortcut);
    autotileShortcuts.writeEntry(QLatin1String("DecMasterCountShortcut"), m_autotileDecMasterCountShortcut);
    autotileShortcuts.writeEntry(QLatin1String("RetileShortcut"), m_autotileRetileShortcut);
}

} // namespace PlasmaZones
