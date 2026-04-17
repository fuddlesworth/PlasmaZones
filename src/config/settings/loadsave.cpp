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

void Settings::loadActivationConfig(PhosphorConfig::IBackend* backend)
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

void Settings::loadDisplayConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Display is backed by PhosphorConfig::Store. Disabled-monitor
    // connector-name resolution lives in the getter (see
    // Settings::disabledMonitors in settings.cpp) so stored connector names
    // get resolved to stable screen ids on every read.
}

// loadAppearanceConfig removed: Appearance group (Colors, Labels, Opacity,
// Border) plus Effects.Blur are now backed by PhosphorConfig::Store and
// loaded on-demand via the Settings getters. See settingsschema.cpp for
// the declarative schema.

void Settings::loadZoneGeometryConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Zone geometry + Performance are backed by m_store — clamp validators
    // in the schema handle what the old readValidatedInt call chain did.
}

void Settings::loadBehaviorConfig(PhosphorConfig::IBackend* backend)
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

    // Exclusions are backed by PhosphorConfig::Store.
}

void Settings::loadZoneSelectorConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Zone selector is backed by PhosphorConfig::Store.
}

void Settings::loadShortcutConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Global shortcuts are backed by PhosphorConfig::Store — getters read
    // through the store on demand.
}

void Settings::loadAutotilingConfig(PhosphorConfig::IBackend* backend)
{
    // Use v2 schema structure: Tiling.* sub-groups
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
        // Drag/Overflow behavior: snap unknown values to the safe default
        // (Float) instead of qBound-clamping to the nearest enum. Clamping
        // to nearest would silently misinterpret a future config value
        // (e.g. DragBehavior=2 for a hypothetical ReorderAcrossScreens) as
        // the highest known mode, exactly the failure pattern the effect-
        // side cache (plasmazoneseffect.cpp:loadCachedSettings) carefully
        // avoids. Both readers must agree.
        const int dragBehaviorRaw =
            tilingBehavior->readInt(ConfigDefaults::dragBehaviorKey(), ConfigDefaults::autotileDragBehavior());
        switch (dragBehaviorRaw) {
        case static_cast<int>(AutotileDragBehavior::Float):
            m_autotileDragBehavior = AutotileDragBehavior::Float;
            break;
        case static_cast<int>(AutotileDragBehavior::Reorder):
            m_autotileDragBehavior = AutotileDragBehavior::Reorder;
            break;
        default:
            m_autotileDragBehavior = AutotileDragBehavior::Float;
            break;
        }
        const int overflowBehaviorRaw =
            tilingBehavior->readInt(ConfigDefaults::overflowBehaviorKey(), ConfigDefaults::autotileOverflowBehavior());
        switch (overflowBehaviorRaw) {
        case static_cast<int>(AutotileOverflowBehavior::Float):
            m_autotileOverflowBehavior = AutotileOverflowBehavior::Float;
            break;
        case static_cast<int>(AutotileOverflowBehavior::Unlimited):
            m_autotileOverflowBehavior = AutotileOverflowBehavior::Unlimited;
            break;
        default:
            m_autotileOverflowBehavior = AutotileOverflowBehavior::Float;
            break;
        }
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
        auto tilingBehaviorTriggers = backend->group(ConfigDefaults::tilingBehaviorTriggersGroup());
        m_autotileDragInsertTriggers =
            parseTriggerListJson(tilingBehaviorTriggers->readString(ConfigDefaults::triggersKey()))
                .value_or(ConfigDefaults::autotileDragInsertTriggers());
        m_autotileDragInsertToggle = tilingBehaviorTriggers->readBool(ConfigDefaults::toggleActivationKey(),
                                                                      ConfigDefaults::autotileDragInsertToggle());
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

    // Animation settings are backed by PhosphorConfig::Store — nothing to
    // load here. Getters read through the store on demand with the schema's
    // clamp validators applied.

    // Tiling shortcuts are backed by PhosphorConfig::Store.
}

void Settings::loadEditorConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Editor settings (Shortcuts + Snapping + FillOnDrop) are backed by
    // PhosphorConfig::Store. Post-load NOTIFY fan-out is handled by the
    // generic Q_PROPERTY re-emit loop in load() — no group-specific logic
    // needed here.
}

// ── save() helpers ───────────────────────────────────────────────────────────

void Settings::saveEditorConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Editor settings are backed by PhosphorConfig::Store.
}

void Settings::saveActivationConfig(PhosphorConfig::IBackend* backend)
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

void Settings::saveDisplayConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Display is backed by PhosphorConfig::Store.
}

// saveAppearanceConfig removed: Appearance setters persist writes through
// PhosphorConfig::Store immediately; the top-level Settings::save() still
// calls m_configBackend->sync() to flush alongside other groups.

void Settings::saveZoneGeometryConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Zone geometry + Performance are backed by m_store — setters persist
    // immediately; save() flushes via m_configBackend->sync().
}

void Settings::saveBehaviorConfig(PhosphorConfig::IBackend* backend)
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
    // Exclusions are backed by PhosphorConfig::Store.
}

void Settings::saveZoneSelectorConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Zone selector is backed by PhosphorConfig::Store.
}

void Settings::saveShortcutConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Global shortcuts are backed by PhosphorConfig::Store.
}

void Settings::saveAutotilingConfig(PhosphorConfig::IBackend* backend)
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
        tilingBehavior->writeInt(ConfigDefaults::dragBehaviorKey(), static_cast<int>(m_autotileDragBehavior));
        tilingBehavior->writeInt(ConfigDefaults::overflowBehaviorKey(), static_cast<int>(m_autotileOverflowBehavior));
        tilingBehavior->writeString(ConfigDefaults::lockedScreensKey(), m_lockedScreens.join(QLatin1Char(',')));
    }
    {
        auto tilingBehaviorTriggers = backend->group(ConfigDefaults::tilingBehaviorTriggersGroup());
        saveTriggerList(*tilingBehaviorTriggers, ConfigDefaults::triggersKey(), m_autotileDragInsertTriggers);
        tilingBehaviorTriggers->writeBool(ConfigDefaults::toggleActivationKey(), m_autotileDragInsertToggle);
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

    // Animation settings are backed by PhosphorConfig::Store — persisted
    // by setters directly, no save pass needed here.

    // Tiling shortcuts are backed by PhosphorConfig::Store.
}

// ── Virtual screen config load/save ──────────────────────────────────────────

void Settings::loadVirtualScreenConfigs(PhosphorConfig::IBackend* backend)
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
            vs.displayName = group->readString(p + ConfigDefaults::virtualScreenNameKey(),
                                               ConfigDefaults::defaultVirtualScreenName(i));
            const QRectF defaultRegion = ConfigDefaults::defaultVirtualScreenRegion();
            qreal x = group->readDouble(p + ConfigDefaults::virtualScreenXKey(), defaultRegion.x());
            qreal y = group->readDouble(p + ConfigDefaults::virtualScreenYKey(), defaultRegion.y());
            qreal w = group->readDouble(p + ConfigDefaults::virtualScreenWidthKey(), defaultRegion.width());
            qreal h = group->readDouble(p + ConfigDefaults::virtualScreenHeightKey(), defaultRegion.height());
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
        // Renumber surviving entries with contiguous indices (0..N-1) so that
        // save round-trips don't cause ID drift when interior entries were invalid.
        for (int i = 0; i < validScreens.size(); ++i) {
            validScreens[i].index = i;
            validScreens[i].id = VirtualScreenId::make(physId, i);
        }
        config.screens = validScreens;

        // Need at least minVirtualScreensPerPhysical() screens for a meaningful subdivision
        if (config.screens.size() < ConfigDefaults::minVirtualScreensPerPhysical())
            continue;

        // Validate no overlapping regions (pairwise intersection, tolerance-aware)
        {
            bool hasOverlap = false;
            for (int i = 0; i < config.screens.size(); ++i) {
                for (int j = i + 1; j < config.screens.size(); ++j) {
                    QRectF intersection = config.screens[i].region.intersected(config.screens[j].region);
                    if (intersection.width() > VirtualScreenDef::Tolerance
                        && intersection.height() > VirtualScreenDef::Tolerance) {
                        qCWarning(lcConfig)
                            << "loadVirtualScreenConfigs: overlapping regions between" << config.screens[i].id << "and"
                            << config.screens[j].id << "for" << physId << "- skipping config";
                        hasOverlap = true;
                        break;
                    }
                }
                if (hasOverlap)
                    break;
            }
            if (hasOverlap)
                continue;
        }

        // Validate total area coverage is approximately 1.0
        {
            qreal totalArea = 0.0;
            for (const auto& vs : config.screens) {
                totalArea += vs.region.width() * vs.region.height();
            }
            constexpr qreal tol = ConfigDefaults::areaCoverageTolerance();
            if (totalArea < 1.0 - tol || totalArea > 1.0 + tol) {
                qCWarning(lcConfig) << "loadVirtualScreenConfigs: total area" << totalArea << "outside tolerance for"
                                    << physId << "- skipping config";
                continue;
            }
        }

        m_virtualScreenConfigs.insert(physId, config);
    }
}

void Settings::saveVirtualScreenConfigs(PhosphorConfig::IBackend* backend)
{
    // Remove old VirtualScreen: groups that are no longer in the config
    const QStringList allGroups = backend->groupList();
    const QString prefix = ConfigDefaults::virtualScreenGroupPrefix();
    for (const QString& groupName : allGroups) {
        if (groupName.startsWith(prefix)) {
            backend->deleteGroup(groupName);
        }
    }

    // Write current configs — normalize indices to be contiguous (0..N-1) so that
    // the load path (which reconstructs index and id from the loop counter) produces
    // identical IDs to what was saved.
    for (auto it = m_virtualScreenConfigs.constBegin(); it != m_virtualScreenConfigs.constEnd(); ++it) {
        const QString& physId = it.key();
        const VirtualScreenConfig& config = it.value();
        if (config.screens.isEmpty())
            continue;

        auto group = backend->group(prefix + physId);
        group->writeInt(ConfigDefaults::virtualScreenCountKey(), config.screens.size());

        for (int i = 0; i < config.screens.size(); ++i) {
            VirtualScreenDef vs = config.screens[i];
            // Normalize index and id to match the save position so round-trip is stable
            vs.index = i;
            vs.id = VirtualScreenId::make(physId, i);
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
