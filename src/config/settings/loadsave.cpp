// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../settings.h"
#include "../configdefaults.h"
#include "../../core/constants.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../autotile/AlgorithmRegistry.h"

namespace PlasmaZones {

// ── load() helpers ───────────────────────────────────────────────────────────

void Settings::loadActivationConfig(ConfigGroup& activation)
{
    m_shiftDragToActivate = activation.readBool(QStringLiteral("ShiftDrag"), ConfigDefaults::shiftDrag());

    int legacyDragMod = activation.readInt(QStringLiteral("DragActivationModifier"), -1);
    if (legacyDragMod == -1) {
        if (activation.hasKey(QStringLiteral("ShiftDrag")) && m_shiftDragToActivate) {
            legacyDragMod = static_cast<int>(DragModifier::Shift);
        } else {
            legacyDragMod = ConfigDefaults::dragActivationModifier();
        }
    } else {
        legacyDragMod = qBound(0, legacyDragMod, static_cast<int>(DragModifier::CtrlAltMeta));
    }
    int legacyDragBtn = qBound(
        0, activation.readInt(QStringLiteral("DragActivationMouseButton"), ConfigDefaults::dragActivationMouseButton()),
        128);
    if (legacyDragBtn == 1)
        legacyDragBtn = 0;

    m_dragActivationTriggers =
        loadTriggerList(activation, QStringLiteral("DragActivationTriggers"), legacyDragMod, legacyDragBtn);

    m_zoneSpanEnabled = activation.readBool(QStringLiteral("ZoneSpanEnabled"), ConfigDefaults::zoneSpanEnabled());

    // Migration: honour old MiddleClickMultiZone disabled state
    if (activation.hasKey(QStringLiteral("MiddleClickMultiZone"))
        && !activation.hasKey(QStringLiteral("ZoneSpanModifier"))) {
        bool oldVal = activation.readBool(QStringLiteral("MiddleClickMultiZone"), true);
        if (!oldVal) {
            activation.writeInt(QStringLiteral("ZoneSpanModifier"), static_cast<int>(DragModifier::Disabled));
            qCInfo(lcConfig) << "Migrated MiddleClickMultiZone=false to ZoneSpanModifier=Disabled";
        }
        activation.deleteKey(QStringLiteral("MiddleClickMultiZone"));
    }
    int legacySpanMod = activation.readInt(QStringLiteral("ZoneSpanModifier"), ConfigDefaults::zoneSpanModifier());
    if (legacySpanMod < 0 || legacySpanMod > static_cast<int>(DragModifier::CtrlAltMeta)) {
        qCWarning(lcConfig) << "Invalid ZoneSpanModifier value:" << legacySpanMod << "- using default";
        legacySpanMod = ConfigDefaults::zoneSpanModifier();
    }
    m_zoneSpanModifier = static_cast<DragModifier>(legacySpanMod);

    m_zoneSpanTriggers = loadTriggerList(activation, QStringLiteral("ZoneSpanTriggers"), legacySpanMod, 0);

    m_toggleActivation = activation.readBool(QStringLiteral("ToggleActivation"), ConfigDefaults::toggleActivation());
    m_snappingEnabled = activation.readBool(QStringLiteral("SnappingEnabled"), ConfigDefaults::snappingEnabled());
}

void Settings::loadDisplayConfig(ConfigGroup& display)
{
    m_showZonesOnAllMonitors =
        display.readBool(QStringLiteral("ShowOnAllMonitors"), ConfigDefaults::showOnAllMonitors());
    // DisabledMonitors is a comma-separated string list
    QString disabledMonitorsStr = display.readString(QStringLiteral("DisabledMonitors"));
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
    m_showZoneNumbers = display.readBool(QStringLiteral("ShowNumbers"), ConfigDefaults::showNumbers());
    m_flashZonesOnSwitch = display.readBool(QStringLiteral("FlashOnSwitch"), ConfigDefaults::flashOnSwitch());
    m_showOsdOnLayoutSwitch =
        display.readBool(QStringLiteral("ShowOsdOnLayoutSwitch"), ConfigDefaults::showOsdOnLayoutSwitch());
    m_showNavigationOsd = display.readBool(QStringLiteral("ShowNavigationOsd"), ConfigDefaults::showNavigationOsd());
    m_osdStyle =
        static_cast<OsdStyle>(readValidatedInt(display, "OsdStyle", ConfigDefaults::osdStyle(), 0, 2, "OSD style"));
    m_overlayDisplayMode = static_cast<OverlayDisplayMode>(readValidatedInt(
        display, "OverlayDisplayMode", ConfigDefaults::overlayDisplayMode(), 0, 1, "overlay display mode"));
}

void Settings::loadAppearanceConfig(ConfigGroup& appearance)
{
    m_useSystemColors = appearance.readBool(QStringLiteral("UseSystemColors"), ConfigDefaults::useSystemColors());
    m_highlightColor = readValidatedColor(appearance, "HighlightColor", ConfigDefaults::highlightColor(), "highlight");
    m_inactiveColor = readValidatedColor(appearance, "InactiveColor", ConfigDefaults::inactiveColor(), "inactive");
    m_borderColor = readValidatedColor(appearance, "BorderColor", ConfigDefaults::borderColor(), "border");
    m_labelFontColor = readValidatedColor(appearance, "LabelFontColor", ConfigDefaults::labelFontColor(), "label font");

    qreal activeOpacity = appearance.readDouble(QStringLiteral("ActiveOpacity"), ConfigDefaults::activeOpacity());
    if (activeOpacity < 0.0 || activeOpacity > 1.0) {
        qCWarning(lcConfig) << "Invalid active opacity:" << activeOpacity << "clamping to valid range";
        activeOpacity = qBound(0.0, activeOpacity, 1.0);
    }
    m_activeOpacity = activeOpacity;

    qreal inactiveOpacity = appearance.readDouble(QStringLiteral("InactiveOpacity"), ConfigDefaults::inactiveOpacity());
    if (inactiveOpacity < 0.0 || inactiveOpacity > 1.0) {
        qCWarning(lcConfig) << "Invalid inactive opacity:" << inactiveOpacity << "clamping to valid range";
        inactiveOpacity = qBound(0.0, inactiveOpacity, 1.0);
    }
    m_inactiveOpacity = inactiveOpacity;

    m_borderWidth = readValidatedInt(appearance, "BorderWidth", ConfigDefaults::borderWidth(), 0, 10, "border width");
    m_borderRadius =
        readValidatedInt(appearance, "BorderRadius", ConfigDefaults::borderRadius(), 0, 50, "border radius");
    m_enableBlur = appearance.readBool(QStringLiteral("EnableBlur"), ConfigDefaults::enableBlur());
    m_labelFontFamily = appearance.readString(QStringLiteral("LabelFontFamily"), ConfigDefaults::labelFontFamily());
    qreal fontScale = appearance.readDouble(QStringLiteral("LabelFontSizeScale"), ConfigDefaults::labelFontSizeScale());
    m_labelFontSizeScale = qBound(0.25, fontScale, 3.0);
    m_labelFontWeight = readValidatedInt(appearance, "LabelFontWeight", ConfigDefaults::labelFontWeight(), 100, 900,
                                         "label font weight");
    m_labelFontItalic = appearance.readBool(QStringLiteral("LabelFontItalic"), ConfigDefaults::labelFontItalic());
    m_labelFontUnderline =
        appearance.readBool(QStringLiteral("LabelFontUnderline"), ConfigDefaults::labelFontUnderline());
    m_labelFontStrikeout =
        appearance.readBool(QStringLiteral("LabelFontStrikeout"), ConfigDefaults::labelFontStrikeout());
}

void Settings::loadZoneGeometryConfig(ConfigGroup& zones)
{
    m_zonePadding =
        readValidatedInt(zones, "Padding", ConfigDefaults::zonePadding(), 0, Defaults::MaxGap, "zone padding");
    m_outerGap = readValidatedInt(zones, "OuterGap", ConfigDefaults::outerGap(), 0, Defaults::MaxGap, "outer gap");
    m_usePerSideOuterGap = zones.readBool(QStringLiteral("UsePerSideOuterGap"), ConfigDefaults::usePerSideOuterGap());
    m_outerGapTop =
        readValidatedInt(zones, "OuterGapTop", ConfigDefaults::outerGapTop(), 0, Defaults::MaxGap, "outer gap top");
    m_outerGapBottom = readValidatedInt(zones, "OuterGapBottom", ConfigDefaults::outerGapBottom(), 0, Defaults::MaxGap,
                                        "outer gap bottom");
    m_outerGapLeft =
        readValidatedInt(zones, "OuterGapLeft", ConfigDefaults::outerGapLeft(), 0, Defaults::MaxGap, "outer gap left");
    m_outerGapRight = readValidatedInt(zones, "OuterGapRight", ConfigDefaults::outerGapRight(), 0, Defaults::MaxGap,
                                       "outer gap right");
    m_adjacentThreshold =
        readValidatedInt(zones, "AdjacentThreshold", ConfigDefaults::adjacentThreshold(), 5, 100, "adjacent threshold");
    m_pollIntervalMs =
        readValidatedInt(zones, "PollIntervalMs", ConfigDefaults::pollIntervalMs(), 10, 1000, "poll interval");
    m_minimumZoneSizePx =
        readValidatedInt(zones, "MinimumZoneSizePx", ConfigDefaults::minimumZoneSizePx(), 50, 500, "minimum zone size");
    m_minimumZoneDisplaySizePx =
        readValidatedInt(zones, "MinimumZoneDisplaySizePx", ConfigDefaults::minimumZoneDisplaySizePx(), 1, 50,
                         "minimum zone display size");
}

void Settings::loadBehaviorConfig(IConfigBackend* backend)
{
    {
        auto behavior = backend->group(QStringLiteral("Behavior"));
        m_keepWindowsInZonesOnResolutionChange = behavior->readBool(
            QStringLiteral("KeepOnResolutionChange"), ConfigDefaults::keepWindowsInZonesOnResolutionChange());
        m_moveNewWindowsToLastZone =
            behavior->readBool(QStringLiteral("MoveNewToLastZone"), ConfigDefaults::moveNewWindowsToLastZone());
        m_restoreOriginalSizeOnUnsnap =
            behavior->readBool(QStringLiteral("RestoreSizeOnUnsnap"), ConfigDefaults::restoreOriginalSizeOnUnsnap());
        int stickyHandling =
            behavior->readInt(QStringLiteral("StickyWindowHandling"), ConfigDefaults::stickyWindowHandling());
        m_stickyWindowHandling = static_cast<StickyWindowHandling>(
            qBound(static_cast<int>(StickyWindowHandling::TreatAsNormal), stickyHandling,
                   static_cast<int>(StickyWindowHandling::IgnoreAll)));
        m_restoreWindowsToZonesOnLogin = behavior->readBool(QStringLiteral("RestoreWindowsToZonesOnLogin"),
                                                            ConfigDefaults::restoreWindowsToZonesOnLogin());
        m_defaultLayoutId = normalizeUuidString(behavior->readString(QStringLiteral("DefaultLayoutId")));
    }

    {
        const QString snapAssistEnabledKey = QStringLiteral("SnapAssistEnabled");
        const QString snapAssistTriggersKey = QStringLiteral("SnapAssistTriggers");

        // Read from Activation group first
        bool hasSnapAssistInActivation = false;
        bool hasTriggersInActivation = false;
        QString snapAssistTriggersJson;
        {
            auto activation = backend->group(QStringLiteral("Activation"));
            m_snapAssistFeatureEnabled = activation->readBool(QStringLiteral("SnapAssistFeatureEnabled"),
                                                              ConfigDefaults::snapAssistFeatureEnabled());
            hasSnapAssistInActivation = activation->hasKey(snapAssistEnabledKey);
            if (hasSnapAssistInActivation) {
                m_snapAssistEnabled = activation->readBool(snapAssistEnabledKey, ConfigDefaults::snapAssistEnabled());
            }
            hasTriggersInActivation = activation->hasKey(snapAssistTriggersKey);
            if (hasTriggersInActivation) {
                snapAssistTriggersJson = activation->readString(snapAssistTriggersKey);
            }
        } // activation group closed

        // Fall back to Behavior group for migrated keys
        if (!hasSnapAssistInActivation || !hasTriggersInActivation) {
            auto behavior2 = backend->group(QStringLiteral("Behavior"));
            if (!hasSnapAssistInActivation) {
                m_snapAssistEnabled = behavior2->readBool(snapAssistEnabledKey, ConfigDefaults::snapAssistEnabled());
            }
            if (!hasTriggersInActivation) {
                snapAssistTriggersJson = behavior2->readString(snapAssistTriggersKey);
            }
        }
        m_snapAssistTriggers =
            parseTriggerListJson(snapAssistTriggersJson).value_or(ConfigDefaults::snapAssistTriggers());
    }

    // Exclusions
    {
        auto exclusions = backend->group(QStringLiteral("Exclusions"));
        QString excludedAppsStr = exclusions->readString(QStringLiteral("Applications"));
        m_excludedApplications = excludedAppsStr.isEmpty() ? QStringList() : excludedAppsStr.split(QLatin1Char(','));
        for (auto& app : m_excludedApplications)
            app = app.trimmed();
        QString excludedClassesStr = exclusions->readString(QStringLiteral("WindowClasses"));
        m_excludedWindowClasses =
            excludedClassesStr.isEmpty() ? QStringList() : excludedClassesStr.split(QLatin1Char(','));
        for (auto& cls : m_excludedWindowClasses)
            cls = cls.trimmed();
        m_excludeTransientWindows =
            exclusions->readBool(QStringLiteral("ExcludeTransientWindows"), ConfigDefaults::excludeTransientWindows());
        int minWidth = exclusions->readInt(QStringLiteral("MinimumWindowWidth"), ConfigDefaults::minimumWindowWidth());
        m_minimumWindowWidth = qBound(0, minWidth, 2000);
        int minHeight =
            exclusions->readInt(QStringLiteral("MinimumWindowHeight"), ConfigDefaults::minimumWindowHeight());
        m_minimumWindowHeight = qBound(0, minHeight, 2000);
    }
}

void Settings::loadZoneSelectorConfig(ConfigGroup& zoneSelector)
{
    m_zoneSelectorEnabled = zoneSelector.readBool(QStringLiteral("Enabled"), ConfigDefaults::zoneSelectorEnabled());
    m_zoneSelectorTriggerDistance = readValidatedInt(zoneSelector, "TriggerDistance", ConfigDefaults::triggerDistance(),
                                                     10, 200, "zone selector trigger distance");
    int selectorPos = zoneSelector.readInt(QStringLiteral("Position"), ConfigDefaults::position());
    if (selectorPos >= 0 && selectorPos <= 8) {
        m_zoneSelectorPosition = static_cast<ZoneSelectorPosition>(selectorPos);
    } else {
        m_zoneSelectorPosition = static_cast<ZoneSelectorPosition>(ConfigDefaults::position());
    }
    int selectorMode = zoneSelector.readInt(QStringLiteral("LayoutMode"), ConfigDefaults::layoutMode());
    m_zoneSelectorLayoutMode = static_cast<ZoneSelectorLayoutMode>(
        qBound(0, selectorMode, static_cast<int>(ZoneSelectorLayoutMode::Vertical)));
    m_zoneSelectorPreviewWidth = readValidatedInt(zoneSelector, "PreviewWidth", ConfigDefaults::previewWidth(), 80, 400,
                                                  "zone selector preview width");
    m_zoneSelectorPreviewHeight = readValidatedInt(zoneSelector, "PreviewHeight", ConfigDefaults::previewHeight(), 60,
                                                   300, "zone selector preview height");
    m_zoneSelectorPreviewLockAspect =
        zoneSelector.readBool(QStringLiteral("PreviewLockAspect"), ConfigDefaults::previewLockAspect());
    m_zoneSelectorGridColumns = readValidatedInt(zoneSelector, "GridColumns", ConfigDefaults::gridColumns(), 1, 10,
                                                 "zone selector grid columns");
    int sizeMode = zoneSelector.readInt(QStringLiteral("SizeMode"), ConfigDefaults::sizeMode());
    m_zoneSelectorSizeMode =
        static_cast<ZoneSelectorSizeMode>(qBound(0, sizeMode, static_cast<int>(ZoneSelectorSizeMode::Manual)));
    m_zoneSelectorMaxRows =
        readValidatedInt(zoneSelector, "MaxRows", ConfigDefaults::maxRows(), 1, 10, "zone selector max rows");
}

void Settings::loadShortcutConfig(ConfigGroup& globalShortcuts)
{
    m_openEditorShortcut =
        globalShortcuts.readString(QStringLiteral("OpenEditorShortcut"), ConfigDefaults::openEditorShortcut());
    m_previousLayoutShortcut =
        globalShortcuts.readString(QStringLiteral("PreviousLayoutShortcut"), ConfigDefaults::previousLayoutShortcut());
    m_nextLayoutShortcut =
        globalShortcuts.readString(QStringLiteral("NextLayoutShortcut"), ConfigDefaults::nextLayoutShortcut());
    const QString quickLayoutDefaults[9] = {
        ConfigDefaults::quickLayout1Shortcut(), ConfigDefaults::quickLayout2Shortcut(),
        ConfigDefaults::quickLayout3Shortcut(), ConfigDefaults::quickLayout4Shortcut(),
        ConfigDefaults::quickLayout5Shortcut(), ConfigDefaults::quickLayout6Shortcut(),
        ConfigDefaults::quickLayout7Shortcut(), ConfigDefaults::quickLayout8Shortcut(),
        ConfigDefaults::quickLayout9Shortcut()};
    loadIndexedShortcuts(globalShortcuts, QStringLiteral("QuickLayout%1Shortcut"), m_quickLayoutShortcuts,
                         quickLayoutDefaults);
    m_moveWindowLeftShortcut =
        globalShortcuts.readString(QStringLiteral("MoveWindowLeft"), ConfigDefaults::moveWindowLeftShortcut());
    m_moveWindowRightShortcut =
        globalShortcuts.readString(QStringLiteral("MoveWindowRight"), ConfigDefaults::moveWindowRightShortcut());
    m_moveWindowUpShortcut =
        globalShortcuts.readString(QStringLiteral("MoveWindowUp"), ConfigDefaults::moveWindowUpShortcut());
    m_moveWindowDownShortcut =
        globalShortcuts.readString(QStringLiteral("MoveWindowDown"), ConfigDefaults::moveWindowDownShortcut());
    m_focusZoneLeftShortcut =
        globalShortcuts.readString(QStringLiteral("FocusZoneLeft"), ConfigDefaults::focusZoneLeftShortcut());
    m_focusZoneRightShortcut =
        globalShortcuts.readString(QStringLiteral("FocusZoneRight"), ConfigDefaults::focusZoneRightShortcut());
    m_focusZoneUpShortcut =
        globalShortcuts.readString(QStringLiteral("FocusZoneUp"), ConfigDefaults::focusZoneUpShortcut());
    m_focusZoneDownShortcut =
        globalShortcuts.readString(QStringLiteral("FocusZoneDown"), ConfigDefaults::focusZoneDownShortcut());
    m_pushToEmptyZoneShortcut =
        globalShortcuts.readString(QStringLiteral("PushToEmptyZone"), ConfigDefaults::pushToEmptyZoneShortcut());
    m_restoreWindowSizeShortcut =
        globalShortcuts.readString(QStringLiteral("RestoreWindowSize"), ConfigDefaults::restoreWindowSizeShortcut());
    m_toggleWindowFloatShortcut =
        globalShortcuts.readString(QStringLiteral("ToggleWindowFloat"), ConfigDefaults::toggleWindowFloatShortcut());
    m_swapWindowLeftShortcut =
        globalShortcuts.readString(QStringLiteral("SwapWindowLeft"), ConfigDefaults::swapWindowLeftShortcut());
    m_swapWindowRightShortcut =
        globalShortcuts.readString(QStringLiteral("SwapWindowRight"), ConfigDefaults::swapWindowRightShortcut());
    m_swapWindowUpShortcut =
        globalShortcuts.readString(QStringLiteral("SwapWindowUp"), ConfigDefaults::swapWindowUpShortcut());
    m_swapWindowDownShortcut =
        globalShortcuts.readString(QStringLiteral("SwapWindowDown"), ConfigDefaults::swapWindowDownShortcut());
    const QString snapToZoneDefaults[9] = {ConfigDefaults::snapToZone1Shortcut(), ConfigDefaults::snapToZone2Shortcut(),
                                           ConfigDefaults::snapToZone3Shortcut(), ConfigDefaults::snapToZone4Shortcut(),
                                           ConfigDefaults::snapToZone5Shortcut(), ConfigDefaults::snapToZone6Shortcut(),
                                           ConfigDefaults::snapToZone7Shortcut(), ConfigDefaults::snapToZone8Shortcut(),
                                           ConfigDefaults::snapToZone9Shortcut()};
    loadIndexedShortcuts(globalShortcuts, QStringLiteral("SnapToZone%1"), m_snapToZoneShortcuts, snapToZoneDefaults);
    m_rotateWindowsClockwiseShortcut = globalShortcuts.readString(QStringLiteral("RotateWindowsClockwise"),
                                                                  ConfigDefaults::rotateWindowsClockwiseShortcut());
    m_rotateWindowsCounterclockwiseShortcut = globalShortcuts.readString(
        QStringLiteral("RotateWindowsCounterclockwise"), ConfigDefaults::rotateWindowsCounterclockwiseShortcut());
    m_cycleWindowForwardShortcut =
        globalShortcuts.readString(QStringLiteral("CycleWindowForward"), ConfigDefaults::cycleWindowForwardShortcut());
    m_cycleWindowBackwardShortcut = globalShortcuts.readString(QStringLiteral("CycleWindowBackward"),
                                                               ConfigDefaults::cycleWindowBackwardShortcut());
    m_resnapToNewLayoutShortcut = globalShortcuts.readString(QStringLiteral("ResnapToNewLayoutShortcut"),
                                                             ConfigDefaults::resnapToNewLayoutShortcut());
    m_snapAllWindowsShortcut =
        globalShortcuts.readString(QStringLiteral("SnapAllWindowsShortcut"), ConfigDefaults::snapAllWindowsShortcut());
    m_layoutPickerShortcut =
        globalShortcuts.readString(QStringLiteral("LayoutPickerShortcut"), ConfigDefaults::layoutPickerShortcut());
    m_toggleLayoutLockShortcut = globalShortcuts.readString(QStringLiteral("ToggleLayoutLockShortcut"),
                                                            ConfigDefaults::toggleLayoutLockShortcut());
}

void Settings::loadAutotilingConfig(IConfigBackend* backend)
{
    auto autotiling = backend->group(QStringLiteral("Autotiling"));
    m_autotileEnabled = autotiling->readBool(QStringLiteral("AutotileEnabled"), ConfigDefaults::autotileEnabled());
    m_autotileAlgorithm =
        autotiling->readString(QStringLiteral("AutotileAlgorithm"), ConfigDefaults::autotileAlgorithm());

    qreal splitRatio =
        autotiling->readDouble(QStringLiteral("AutotileSplitRatio"), ConfigDefaults::autotileSplitRatio());
    if (splitRatio < AutotileDefaults::MinSplitRatio || splitRatio > AutotileDefaults::MaxSplitRatio) {
        qCWarning(lcConfig) << "Invalid autotile split ratio:" << splitRatio << "clamping to valid range";
        splitRatio = qBound(AutotileDefaults::MinSplitRatio, splitRatio, AutotileDefaults::MaxSplitRatio);
    }
    m_autotileSplitRatio = splitRatio;

    int masterCount = autotiling->readInt(QStringLiteral("AutotileMasterCount"), ConfigDefaults::autotileMasterCount());
    if (masterCount < AutotileDefaults::MinMasterCount || masterCount > AutotileDefaults::MaxMasterCount) {
        qCWarning(lcConfig) << "Invalid autotile master count:" << masterCount << "clamping to valid range";
        masterCount = qBound(AutotileDefaults::MinMasterCount, masterCount, AutotileDefaults::MaxMasterCount);
    }
    m_autotileMasterCount = masterCount;

    qreal cmSplitRatio = autotiling->readDouble(QStringLiteral("AutotileCenteredMasterSplitRatio"),
                                                ConfigDefaults::autotileCenteredMasterSplitRatio());
    if (cmSplitRatio < AutotileDefaults::MinSplitRatio || cmSplitRatio > AutotileDefaults::MaxSplitRatio) {
        cmSplitRatio = qBound(AutotileDefaults::MinSplitRatio, cmSplitRatio, AutotileDefaults::MaxSplitRatio);
    }
    m_autotileCenteredMasterSplitRatio = cmSplitRatio;

    int cmMasterCount = autotiling->readInt(QStringLiteral("AutotileCenteredMasterMasterCount"),
                                            ConfigDefaults::autotileCenteredMasterMasterCount());
    if (cmMasterCount < AutotileDefaults::MinMasterCount || cmMasterCount > AutotileDefaults::MaxMasterCount) {
        cmMasterCount = qBound(AutotileDefaults::MinMasterCount, cmMasterCount, AutotileDefaults::MaxMasterCount);
    }
    m_autotileCenteredMasterMasterCount = cmMasterCount;

    m_autotileInnerGap = readValidatedInt(*autotiling, "AutotileInnerGap", ConfigDefaults::autotileInnerGap(),
                                          AutotileDefaults::MinGap, AutotileDefaults::MaxGap, "autotile inner gap");
    m_autotileOuterGap = readValidatedInt(*autotiling, "AutotileOuterGap", ConfigDefaults::autotileOuterGap(),
                                          AutotileDefaults::MinGap, AutotileDefaults::MaxGap, "autotile outer gap");
    m_autotileUsePerSideOuterGap = autotiling->readBool(QStringLiteral("AutotileUsePerSideOuterGap"),
                                                        ConfigDefaults::autotileUsePerSideOuterGap());
    m_autotileOuterGapTop =
        readValidatedInt(*autotiling, "AutotileOuterGapTop", ConfigDefaults::autotileOuterGapTop(),
                         AutotileDefaults::MinGap, AutotileDefaults::MaxGap, "autotile outer gap top");
    m_autotileOuterGapBottom =
        readValidatedInt(*autotiling, "AutotileOuterGapBottom", ConfigDefaults::autotileOuterGapBottom(),
                         AutotileDefaults::MinGap, AutotileDefaults::MaxGap, "autotile outer gap bottom");
    m_autotileOuterGapLeft =
        readValidatedInt(*autotiling, "AutotileOuterGapLeft", ConfigDefaults::autotileOuterGapLeft(),
                         AutotileDefaults::MinGap, AutotileDefaults::MaxGap, "autotile outer gap left");
    m_autotileOuterGapRight =
        readValidatedInt(*autotiling, "AutotileOuterGapRight", ConfigDefaults::autotileOuterGapRight(),
                         AutotileDefaults::MinGap, AutotileDefaults::MaxGap, "autotile outer gap right");
    m_autotileFocusNewWindows =
        autotiling->readBool(QStringLiteral("AutotileFocusNewWindows"), ConfigDefaults::autotileFocusNewWindows());
    m_autotileSmartGaps =
        autotiling->readBool(QStringLiteral("AutotileSmartGaps"), ConfigDefaults::autotileSmartGaps());
    m_autotileMaxWindows =
        readValidatedInt(*autotiling, "AutotileMaxWindows", ConfigDefaults::autotileMaxWindows(),
                         AutotileDefaults::MinMaxWindows, AutotileDefaults::MaxMaxWindows, "autotile max windows");
    m_autotileInsertPosition = static_cast<AutotileInsertPosition>(
        readValidatedInt(*autotiling, "AutotileInsertPosition", ConfigDefaults::autotileInsertPosition(), 0, 2,
                         "autotile insert position"));

    // Release autotiling group before accessing animations group
    autotiling.reset();

    // Animation Settings
    {
        auto animations = backend->group(QStringLiteral("Animations"));
        m_animationsEnabled =
            animations->readBool(QStringLiteral("AnimationsEnabled"), ConfigDefaults::animationsEnabled());
        m_animationDuration = readValidatedInt(*animations, "AnimationDuration", ConfigDefaults::animationDuration(),
                                               50, 500, "animation duration");
        m_animationEasingCurve =
            animations->readString(QStringLiteral("AnimationEasingCurve"), ConfigDefaults::animationEasingCurve());
        m_animationMinDistance =
            readValidatedInt(*animations, "AnimationMinDistance", ConfigDefaults::animationMinDistance(), 0, 200,
                             "animation min distance");
        m_animationSequenceMode =
            readValidatedInt(*animations, "AnimationSequenceMode", ConfigDefaults::animationSequenceMode(), 0, 1,
                             "animation sequence mode");
        m_animationStaggerInterval =
            readValidatedInt(*animations, "AnimationStaggerInterval", ConfigDefaults::animationStaggerInterval(),
                             AutotileDefaults::MinAnimationStaggerIntervalMs,
                             AutotileDefaults::MaxAnimationStaggerIntervalMs, "animation stagger interval");
    }

    // Re-open autotiling group for remaining fields
    {
        auto autotiling2 = backend->group(QStringLiteral("Autotiling"));
        m_autotileFocusFollowsMouse = autotiling2->readBool(QStringLiteral("AutotileFocusFollowsMouse"),
                                                            ConfigDefaults::autotileFocusFollowsMouse());
        m_autotileRespectMinimumSize = autotiling2->readBool(QStringLiteral("AutotileRespectMinimumSize"),
                                                             ConfigDefaults::autotileRespectMinimumSize());
        m_autotileHideTitleBars =
            autotiling2->readBool(QStringLiteral("AutotileHideTitleBars"), ConfigDefaults::autotileHideTitleBars());
        m_autotileShowBorder =
            autotiling2->readBool(QStringLiteral("AutotileShowBorder"), ConfigDefaults::autotileShowBorder());
        m_autotileBorderWidth = readValidatedInt(*autotiling2, "AutotileBorderWidth",
                                                 ConfigDefaults::autotileBorderWidth(), 0, 10, "autotile border width");
        m_autotileBorderRadius =
            readValidatedInt(*autotiling2, "AutotileBorderRadius", ConfigDefaults::autotileBorderRadius(), 0, 20,
                             "autotile border radius");
        m_autotileBorderColor = readValidatedColor(*autotiling2, "AutotileBorderColor",
                                                   ConfigDefaults::autotileBorderColor(), "autotile border");
        m_autotileInactiveBorderColor =
            readValidatedColor(*autotiling2, "AutotileInactiveBorderColor",
                               ConfigDefaults::autotileInactiveBorderColor(), "autotile inactive border");
        m_autotileUseSystemBorderColors = autotiling2->readBool(QStringLiteral("AutotileUseSystemBorderColors"),
                                                                ConfigDefaults::autotileUseSystemBorderColors());
        QString lockedScreensStr = autotiling2->readString(QStringLiteral("LockedScreens"));
        m_lockedScreens = lockedScreensStr.isEmpty() ? QStringList() : lockedScreensStr.split(QLatin1Char(','));
        for (auto& s : m_lockedScreens)
            s = s.trimmed();
    }

    // Autotile Shortcuts
    {
        auto autotileShortcuts = backend->group(QStringLiteral("AutotileShortcuts"));
        m_autotileToggleShortcut =
            autotileShortcuts->readString(QStringLiteral("ToggleShortcut"), ConfigDefaults::autotileToggleShortcut());
        m_autotileFocusMasterShortcut = autotileShortcuts->readString(QStringLiteral("FocusMasterShortcut"),
                                                                      ConfigDefaults::autotileFocusMasterShortcut());
        m_autotileSwapMasterShortcut = autotileShortcuts->readString(QStringLiteral("SwapMasterShortcut"),
                                                                     ConfigDefaults::autotileSwapMasterShortcut());
        m_autotileIncMasterRatioShortcut = autotileShortcuts->readString(
            QStringLiteral("IncMasterRatioShortcut"), ConfigDefaults::autotileIncMasterRatioShortcut());
        m_autotileDecMasterRatioShortcut = autotileShortcuts->readString(
            QStringLiteral("DecMasterRatioShortcut"), ConfigDefaults::autotileDecMasterRatioShortcut());
        m_autotileIncMasterCountShortcut = autotileShortcuts->readString(
            QStringLiteral("IncMasterCountShortcut"), ConfigDefaults::autotileIncMasterCountShortcut());
        m_autotileDecMasterCountShortcut = autotileShortcuts->readString(
            QStringLiteral("DecMasterCountShortcut"), ConfigDefaults::autotileDecMasterCountShortcut());
        m_autotileRetileShortcut =
            autotileShortcuts->readString(QStringLiteral("RetileShortcut"), ConfigDefaults::autotileRetileShortcut());
    }
}

// ── save() helpers ───────────────────────────────────────────────────────────

void Settings::saveActivationConfig(ConfigGroup& activation)
{
    activation.writeBool(QStringLiteral("ShiftDrag"), m_shiftDragToActivate); // Deprecated, kept for compatibility
    saveTriggerList(activation, QStringLiteral("DragActivationTriggers"), m_dragActivationTriggers);
    // Note: cannot delete individual entries with ConfigGroup interface.
    // Write empty strings to clear obsolete keys.
    activation.writeString(QStringLiteral("DragActivationModifier"), QString());
    activation.writeString(QStringLiteral("DragActivationMouseButton"), QString());
    activation.writeString(QStringLiteral("MultiZoneModifier"), QString());
    activation.writeString(QStringLiteral("MultiZoneTriggers"), QString());
    activation.writeString(QStringLiteral("MultiZoneMouseButton"), QString());
    activation.writeBool(QStringLiteral("ZoneSpanEnabled"), m_zoneSpanEnabled);
    activation.writeInt(QStringLiteral("ZoneSpanModifier"), static_cast<int>(m_zoneSpanModifier));
    saveTriggerList(activation, QStringLiteral("ZoneSpanTriggers"), m_zoneSpanTriggers);
    activation.writeString(QStringLiteral("ZoneSpanMouseButton"), QString());
    activation.writeBool(QStringLiteral("ToggleActivation"), m_toggleActivation);
    activation.writeBool(QStringLiteral("SnappingEnabled"), m_snappingEnabled);
}

void Settings::saveDisplayConfig(ConfigGroup& display)
{
    display.writeBool(QStringLiteral("ShowOnAllMonitors"), m_showZonesOnAllMonitors);
    display.writeString(QStringLiteral("DisabledMonitors"), m_disabledMonitors.join(QLatin1Char(',')));
    display.writeBool(QStringLiteral("ShowNumbers"), m_showZoneNumbers);
    display.writeBool(QStringLiteral("FlashOnSwitch"), m_flashZonesOnSwitch);
    display.writeBool(QStringLiteral("ShowOsdOnLayoutSwitch"), m_showOsdOnLayoutSwitch);
    display.writeBool(QStringLiteral("ShowNavigationOsd"), m_showNavigationOsd);
    display.writeInt(QStringLiteral("OsdStyle"), static_cast<int>(m_osdStyle));
    display.writeInt(QStringLiteral("OverlayDisplayMode"), static_cast<int>(m_overlayDisplayMode));
}

void Settings::saveAppearanceConfig(ConfigGroup& appearance)
{
    appearance.writeBool(QStringLiteral("UseSystemColors"), m_useSystemColors);
    appearance.writeColor(QStringLiteral("HighlightColor"), m_highlightColor);
    appearance.writeColor(QStringLiteral("InactiveColor"), m_inactiveColor);
    appearance.writeColor(QStringLiteral("BorderColor"), m_borderColor);
    appearance.writeColor(QStringLiteral("LabelFontColor"), m_labelFontColor);
    appearance.writeDouble(QStringLiteral("ActiveOpacity"), m_activeOpacity);
    appearance.writeDouble(QStringLiteral("InactiveOpacity"), m_inactiveOpacity);
    appearance.writeInt(QStringLiteral("BorderWidth"), m_borderWidth);
    appearance.writeInt(QStringLiteral("BorderRadius"), m_borderRadius);
    appearance.writeBool(QStringLiteral("EnableBlur"), m_enableBlur);
    appearance.writeString(QStringLiteral("LabelFontFamily"), m_labelFontFamily);
    appearance.writeDouble(QStringLiteral("LabelFontSizeScale"), m_labelFontSizeScale);
    appearance.writeInt(QStringLiteral("LabelFontWeight"), m_labelFontWeight);
    appearance.writeBool(QStringLiteral("LabelFontItalic"), m_labelFontItalic);
    appearance.writeBool(QStringLiteral("LabelFontUnderline"), m_labelFontUnderline);
    appearance.writeBool(QStringLiteral("LabelFontStrikeout"), m_labelFontStrikeout);
}

void Settings::saveZoneGeometryConfig(ConfigGroup& zones)
{
    zones.writeInt(QStringLiteral("Padding"), m_zonePadding);
    zones.writeInt(QStringLiteral("OuterGap"), m_outerGap);
    zones.writeBool(QStringLiteral("UsePerSideOuterGap"), m_usePerSideOuterGap);
    zones.writeInt(QStringLiteral("OuterGapTop"), m_outerGapTop);
    zones.writeInt(QStringLiteral("OuterGapBottom"), m_outerGapBottom);
    zones.writeInt(QStringLiteral("OuterGapLeft"), m_outerGapLeft);
    zones.writeInt(QStringLiteral("OuterGapRight"), m_outerGapRight);
    zones.writeInt(QStringLiteral("AdjacentThreshold"), m_adjacentThreshold);
    zones.writeInt(QStringLiteral("PollIntervalMs"), m_pollIntervalMs);
    zones.writeInt(QStringLiteral("MinimumZoneSizePx"), m_minimumZoneSizePx);
    zones.writeInt(QStringLiteral("MinimumZoneDisplaySizePx"), m_minimumZoneDisplaySizePx);
}

void Settings::saveBehaviorConfig(IConfigBackend* backend)
{
    {
        auto behavior = backend->group(QStringLiteral("Behavior"));
        behavior->writeBool(QStringLiteral("KeepOnResolutionChange"), m_keepWindowsInZonesOnResolutionChange);
        behavior->writeBool(QStringLiteral("MoveNewToLastZone"), m_moveNewWindowsToLastZone);
        behavior->writeBool(QStringLiteral("RestoreSizeOnUnsnap"), m_restoreOriginalSizeOnUnsnap);
        behavior->writeInt(QStringLiteral("StickyWindowHandling"), static_cast<int>(m_stickyWindowHandling));
        behavior->writeBool(QStringLiteral("RestoreWindowsToZonesOnLogin"), m_restoreWindowsToZonesOnLogin);
        behavior->writeString(QStringLiteral("SnapAssistEnabled"), QString()); // Clear obsolete location
        behavior->writeString(QStringLiteral("SnapAssistTriggers"), QString()); // Clear obsolete location
        behavior->writeString(QStringLiteral("DefaultLayoutId"), m_defaultLayoutId);
    }
    {
        auto activation = backend->group(QStringLiteral("Activation"));
        activation->writeBool(QStringLiteral("SnapAssistFeatureEnabled"), m_snapAssistFeatureEnabled);
        activation->writeBool(QStringLiteral("SnapAssistEnabled"), m_snapAssistEnabled);
        saveTriggerList(*activation, QStringLiteral("SnapAssistTriggers"), m_snapAssistTriggers);
    }
    {
        auto exclusions = backend->group(QStringLiteral("Exclusions"));
        exclusions->writeString(QStringLiteral("Applications"), m_excludedApplications.join(QLatin1Char(',')));
        exclusions->writeString(QStringLiteral("WindowClasses"), m_excludedWindowClasses.join(QLatin1Char(',')));
        exclusions->writeBool(QStringLiteral("ExcludeTransientWindows"), m_excludeTransientWindows);
        exclusions->writeInt(QStringLiteral("MinimumWindowWidth"), m_minimumWindowWidth);
        exclusions->writeInt(QStringLiteral("MinimumWindowHeight"), m_minimumWindowHeight);
    }
}

void Settings::saveZoneSelectorConfig(ConfigGroup& zoneSelector)
{
    zoneSelector.writeBool(QStringLiteral("Enabled"), m_zoneSelectorEnabled);
    zoneSelector.writeInt(QStringLiteral("TriggerDistance"), m_zoneSelectorTriggerDistance);
    zoneSelector.writeInt(QStringLiteral("Position"), static_cast<int>(m_zoneSelectorPosition));
    zoneSelector.writeInt(QStringLiteral("LayoutMode"), static_cast<int>(m_zoneSelectorLayoutMode));
    zoneSelector.writeInt(QStringLiteral("PreviewWidth"), m_zoneSelectorPreviewWidth);
    zoneSelector.writeInt(QStringLiteral("PreviewHeight"), m_zoneSelectorPreviewHeight);
    zoneSelector.writeBool(QStringLiteral("PreviewLockAspect"), m_zoneSelectorPreviewLockAspect);
    zoneSelector.writeInt(QStringLiteral("GridColumns"), m_zoneSelectorGridColumns);
    zoneSelector.writeInt(QStringLiteral("SizeMode"), static_cast<int>(m_zoneSelectorSizeMode));
    zoneSelector.writeInt(QStringLiteral("MaxRows"), m_zoneSelectorMaxRows);
}

void Settings::saveShortcutConfig(ConfigGroup& globalShortcuts)
{
    globalShortcuts.writeString(QStringLiteral("OpenEditorShortcut"), m_openEditorShortcut);
    globalShortcuts.writeString(QStringLiteral("PreviousLayoutShortcut"), m_previousLayoutShortcut);
    globalShortcuts.writeString(QStringLiteral("NextLayoutShortcut"), m_nextLayoutShortcut);
    for (int i = 0; i < 9; ++i) {
        globalShortcuts.writeString(QStringLiteral("QuickLayout%1Shortcut").arg(i + 1), m_quickLayoutShortcuts[i]);
    }
    globalShortcuts.writeString(QStringLiteral("MoveWindowLeft"), m_moveWindowLeftShortcut);
    globalShortcuts.writeString(QStringLiteral("MoveWindowRight"), m_moveWindowRightShortcut);
    globalShortcuts.writeString(QStringLiteral("MoveWindowUp"), m_moveWindowUpShortcut);
    globalShortcuts.writeString(QStringLiteral("MoveWindowDown"), m_moveWindowDownShortcut);
    globalShortcuts.writeString(QStringLiteral("FocusZoneLeft"), m_focusZoneLeftShortcut);
    globalShortcuts.writeString(QStringLiteral("FocusZoneRight"), m_focusZoneRightShortcut);
    globalShortcuts.writeString(QStringLiteral("FocusZoneUp"), m_focusZoneUpShortcut);
    globalShortcuts.writeString(QStringLiteral("FocusZoneDown"), m_focusZoneDownShortcut);
    globalShortcuts.writeString(QStringLiteral("PushToEmptyZone"), m_pushToEmptyZoneShortcut);
    globalShortcuts.writeString(QStringLiteral("RestoreWindowSize"), m_restoreWindowSizeShortcut);
    globalShortcuts.writeString(QStringLiteral("ToggleWindowFloat"), m_toggleWindowFloatShortcut);
    globalShortcuts.writeString(QStringLiteral("SwapWindowLeft"), m_swapWindowLeftShortcut);
    globalShortcuts.writeString(QStringLiteral("SwapWindowRight"), m_swapWindowRightShortcut);
    globalShortcuts.writeString(QStringLiteral("SwapWindowUp"), m_swapWindowUpShortcut);
    globalShortcuts.writeString(QStringLiteral("SwapWindowDown"), m_swapWindowDownShortcut);
    for (int i = 0; i < 9; ++i) {
        globalShortcuts.writeString(QStringLiteral("SnapToZone%1").arg(i + 1), m_snapToZoneShortcuts[i]);
    }
    globalShortcuts.writeString(QStringLiteral("RotateWindowsClockwise"), m_rotateWindowsClockwiseShortcut);
    globalShortcuts.writeString(QStringLiteral("RotateWindowsCounterclockwise"),
                                m_rotateWindowsCounterclockwiseShortcut);
    globalShortcuts.writeString(QStringLiteral("CycleWindowForward"), m_cycleWindowForwardShortcut);
    globalShortcuts.writeString(QStringLiteral("CycleWindowBackward"), m_cycleWindowBackwardShortcut);
    globalShortcuts.writeString(QStringLiteral("ResnapToNewLayoutShortcut"), m_resnapToNewLayoutShortcut);
    globalShortcuts.writeString(QStringLiteral("SnapAllWindowsShortcut"), m_snapAllWindowsShortcut);
    globalShortcuts.writeString(QStringLiteral("LayoutPickerShortcut"), m_layoutPickerShortcut);
    globalShortcuts.writeString(QStringLiteral("ToggleLayoutLockShortcut"), m_toggleLayoutLockShortcut);
}

void Settings::saveAutotilingConfig(IConfigBackend* backend)
{
    {
        auto autotiling = backend->group(QStringLiteral("Autotiling"));
        autotiling->writeBool(QStringLiteral("AutotileEnabled"), m_autotileEnabled);
        autotiling->writeString(QStringLiteral("AutotileAlgorithm"), m_autotileAlgorithm);
        autotiling->writeDouble(QStringLiteral("AutotileSplitRatio"), m_autotileSplitRatio);
        autotiling->writeInt(QStringLiteral("AutotileMasterCount"), m_autotileMasterCount);
        autotiling->writeDouble(QStringLiteral("AutotileCenteredMasterSplitRatio"), m_autotileCenteredMasterSplitRatio);
        autotiling->writeInt(QStringLiteral("AutotileCenteredMasterMasterCount"), m_autotileCenteredMasterMasterCount);
        autotiling->writeInt(QStringLiteral("AutotileInnerGap"), m_autotileInnerGap);
        autotiling->writeInt(QStringLiteral("AutotileOuterGap"), m_autotileOuterGap);
        autotiling->writeBool(QStringLiteral("AutotileUsePerSideOuterGap"), m_autotileUsePerSideOuterGap);
        autotiling->writeInt(QStringLiteral("AutotileOuterGapTop"), m_autotileOuterGapTop);
        autotiling->writeInt(QStringLiteral("AutotileOuterGapBottom"), m_autotileOuterGapBottom);
        autotiling->writeInt(QStringLiteral("AutotileOuterGapLeft"), m_autotileOuterGapLeft);
        autotiling->writeInt(QStringLiteral("AutotileOuterGapRight"), m_autotileOuterGapRight);
        autotiling->writeBool(QStringLiteral("AutotileFocusNewWindows"), m_autotileFocusNewWindows);
        autotiling->writeBool(QStringLiteral("AutotileSmartGaps"), m_autotileSmartGaps);
        autotiling->writeInt(QStringLiteral("AutotileMaxWindows"), m_autotileMaxWindows);
        autotiling->writeInt(QStringLiteral("AutotileInsertPosition"), static_cast<int>(m_autotileInsertPosition));
        autotiling->writeBool(QStringLiteral("AutotileFocusFollowsMouse"), m_autotileFocusFollowsMouse);
        autotiling->writeBool(QStringLiteral("AutotileRespectMinimumSize"), m_autotileRespectMinimumSize);
        autotiling->writeBool(QStringLiteral("AutotileHideTitleBars"), m_autotileHideTitleBars);
        autotiling->writeBool(QStringLiteral("AutotileShowBorder"), m_autotileShowBorder);
        autotiling->writeInt(QStringLiteral("AutotileBorderWidth"), m_autotileBorderWidth);
        autotiling->writeInt(QStringLiteral("AutotileBorderRadius"), m_autotileBorderRadius);
        autotiling->writeColor(QStringLiteral("AutotileBorderColor"), m_autotileBorderColor);
        autotiling->writeColor(QStringLiteral("AutotileInactiveBorderColor"), m_autotileInactiveBorderColor);
        autotiling->writeBool(QStringLiteral("AutotileUseSystemBorderColors"), m_autotileUseSystemBorderColors);
        autotiling->writeString(QStringLiteral("LockedScreens"), m_lockedScreens.join(QLatin1Char(',')));
    }

    {
        auto animations = backend->group(QStringLiteral("Animations"));
        animations->writeBool(QStringLiteral("AnimationsEnabled"), m_animationsEnabled);
        animations->writeInt(QStringLiteral("AnimationDuration"), m_animationDuration);
        animations->writeString(QStringLiteral("AnimationEasingCurve"), m_animationEasingCurve);
        animations->writeInt(QStringLiteral("AnimationMinDistance"), m_animationMinDistance);
        animations->writeInt(QStringLiteral("AnimationSequenceMode"), m_animationSequenceMode);
        animations->writeInt(QStringLiteral("AnimationStaggerInterval"), m_animationStaggerInterval);
    }

    {
        auto autotileShortcuts = backend->group(QStringLiteral("AutotileShortcuts"));
        autotileShortcuts->writeString(QStringLiteral("ToggleShortcut"), m_autotileToggleShortcut);
        autotileShortcuts->writeString(QStringLiteral("FocusMasterShortcut"), m_autotileFocusMasterShortcut);
        autotileShortcuts->writeString(QStringLiteral("SwapMasterShortcut"), m_autotileSwapMasterShortcut);
        autotileShortcuts->writeString(QStringLiteral("IncMasterRatioShortcut"), m_autotileIncMasterRatioShortcut);
        autotileShortcuts->writeString(QStringLiteral("DecMasterRatioShortcut"), m_autotileDecMasterRatioShortcut);
        autotileShortcuts->writeString(QStringLiteral("IncMasterCountShortcut"), m_autotileIncMasterCountShortcut);
        autotileShortcuts->writeString(QStringLiteral("DecMasterCountShortcut"), m_autotileDecMasterCountShortcut);
        autotileShortcuts->writeString(QStringLiteral("RetileShortcut"), m_autotileRetileShortcut);
    }
}

} // namespace PlasmaZones
