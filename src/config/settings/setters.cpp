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
// Activation setters moved to settings.cpp (PhosphorConfig::Store-backed).
// autotileDragInsertTriggers/Toggle stay inline below until the Autotiling
// group migration lands (those keys live in Tiling.Behavior).
// ═══════════════════════════════════════════════════════════════════════════════

void Settings::setAutotileDragInsertTriggers(const QVariantList& triggers)
{
    QVariantList capped = triggers.mid(0, MaxTriggersPerAction);
    if (m_autotileDragInsertTriggers != capped) {
        m_autotileDragInsertTriggers = capped;
        Q_EMIT autotileDragInsertTriggersChanged();
        Q_EMIT settingsChanged();
    }
}

SETTINGS_SETTER(bool, AutotileDragInsertToggle, m_autotileDragInsertToggle, autotileDragInsertToggleChanged)

// ═══════════════════════════════════════════════════════════════════════════════
// Display setters
// ═══════════════════════════════════════════════════════════════════════════════

// Display setters moved to settings.cpp (PhosphorConfig::Store-backed).
// isMonitorDisabled/isDesktopDisabled/isActivityDisabled helpers also live
// in settings.cpp so they can read the Store-backed disabled-* list getters.

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

// Effects toggles + OSD enum setters moved to settings.cpp (Store-backed).

// ═══════════════════════════════════════════════════════════════════════════════
// Appearance setters — PhosphorConfig::Store-backed, live in settings.cpp.
// See settingsschema.cpp for the schema and settings.cpp for the setter
// implementations.
// ═══════════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════════
// Zone geometry + Performance setters moved to settings.cpp
// (PhosphorConfig::Store-backed).
// ═══════════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════════
// Behavior setters moved to settings.cpp (PhosphorConfig::Store-backed).
// Exclusions + filterLayoutsByAspectRatio also Store-backed, see settings.cpp.
// ═══════════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════════
// Zone Selector setters moved to settings.cpp (PhosphorConfig::Store-backed).
// ═══════════════════════════════════════════════════════════════════════════════

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
SETTINGS_SETTER_CLAMPED_QREAL(AutotileSplitRatioStep, m_autotileSplitRatioStep, autotileSplitRatioStepChanged,
                              ConfigDefaults::autotileSplitRatioStepMin(), ConfigDefaults::autotileSplitRatioStepMax())
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

// Autotile shortcut setters moved to settings.cpp (Store-backed).

// ═══════════════════════════════════════════════════════════════════════════════
// Animation setters
// ═══════════════════════════════════════════════════════════════════════════════

// Animation setters moved to settings.cpp (PhosphorConfig::Store-backed).
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

SETTINGS_SETTER(StickyWindowHandling, AutotileStickyWindowHandling, m_autotileStickyWindowHandling,
                autotileStickyWindowHandlingChanged)

SETTINGS_SETTER_ENUM_INT(AutotileStickyWindowHandling, StickyWindowHandling,
                         static_cast<int>(StickyWindowHandling::TreatAsNormal),
                         static_cast<int>(StickyWindowHandling::IgnoreAll))

SETTINGS_SETTER(AutotileDragBehavior, AutotileDragBehavior, m_autotileDragBehavior, autotileDragBehaviorChanged)

SETTINGS_SETTER_ENUM_INT(AutotileDragBehavior, AutotileDragBehavior, static_cast<int>(AutotileDragBehavior::Float),
                         static_cast<int>(AutotileDragBehavior::Reorder))

SETTINGS_SETTER(AutotileOverflowBehavior, AutotileOverflowBehavior, m_autotileOverflowBehavior,
                autotileOverflowBehaviorChanged)

SETTINGS_SETTER_ENUM_INT(AutotileOverflowBehavior, AutotileOverflowBehavior,
                         static_cast<int>(AutotileOverflowBehavior::Float),
                         static_cast<int>(AutotileOverflowBehavior::Unlimited))

void Settings::setLockedScreens(const QStringList& screens)
{
    if (m_lockedScreens != screens) {
        m_lockedScreens = screens;
        Q_EMIT lockedScreensChanged();
        Q_EMIT settingsChanged();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Rendering — setter moved to settings.cpp (PhosphorConfig::Store-backed).
// ═══════════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════════
// Shader Effects — setters live in settings.cpp and route through
// PhosphorConfig::Store (m_store). See settingsschema.cpp for the schema
// and settings.cpp for the setter implementations.

// ═══════════════════════════════════════════════════════════════════════════════
// Shortcut setters
// ═══════════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════════
// Virtual screen config setters
// ═══════════════════════════════════════════════════════════════════════════════

QHash<QString, VirtualScreenConfig> Settings::virtualScreenConfigs() const
{
    return m_virtualScreenConfigs;
}

void Settings::setVirtualScreenConfigs(const QHash<QString, VirtualScreenConfig>& configs)
{
    // Filter out 1-screen configs: hasSubdivisions() returns false for size==1,
    // so effectiveScreenIds() would not emit virtual IDs for them, but storing them
    // causes inconsistency (settings says VS exists, ScreenManager disagrees).
    // Also reject individually-invalid entries via VirtualScreenConfig::isValid
    // — Settings is the source of truth, so it must apply the same admission
    // rules as the singular setVirtualScreenConfig path.
    QHash<QString, VirtualScreenConfig> filtered;
    for (auto it = configs.constBegin(); it != configs.constEnd(); ++it) {
        if (!it.value().hasSubdivisions()) {
            continue;
        }
        QString error;
        if (!VirtualScreenConfig::isValid(it.value(), it.key(), ConfigDefaults::maxVirtualScreensPerPhysical(),
                                          &error)) {
            qCWarning(lcConfig) << "setVirtualScreenConfigs: dropping invalid entry for" << it.key() << "—" << error;
            continue;
        }
        filtered.insert(it.key(), it.value());
    }

    // Check exact equality to avoid dropping tiny geometry adjustments
    if (m_virtualScreenConfigs.size() != filtered.size()) {
        m_virtualScreenConfigs = filtered;
        Q_EMIT virtualScreenConfigsChanged();
        Q_EMIT settingsChanged();
        return;
    }
    for (auto it = filtered.constBegin(); it != filtered.constEnd(); ++it) {
        auto existing = m_virtualScreenConfigs.constFind(it.key());
        if (existing == m_virtualScreenConfigs.constEnd() || !(existing.value() == it.value())) {
            m_virtualScreenConfigs = filtered;
            Q_EMIT virtualScreenConfigsChanged();
            Q_EMIT settingsChanged();
            return;
        }
    }
}

bool Settings::setVirtualScreenConfig(const QString& physicalScreenId, const VirtualScreenConfig& config)
{
    if (physicalScreenId.isEmpty()) {
        qCWarning(lcConfig) << "setVirtualScreenConfig: empty physicalScreenId";
        return false;
    }

    if (config.screens.isEmpty() || !config.hasSubdivisions()) {
        if (!m_virtualScreenConfigs.contains(physicalScreenId))
            return true; // already-empty removal is a successful no-op
        m_virtualScreenConfigs.remove(physicalScreenId);
    } else {
        // Validate before storing — Settings is the source of truth for VS
        // configs, so it must reject inputs that would later be refused by
        // ScreenManager. Otherwise Settings and ScreenManager diverge in
        // memory and the disk save persists garbage that next-load drops.
        QString error;
        if (!VirtualScreenConfig::isValid(config, physicalScreenId, ConfigDefaults::maxVirtualScreensPerPhysical(),
                                          &error)) {
            qCWarning(lcConfig) << "setVirtualScreenConfig: rejected invalid config for" << physicalScreenId << "—"
                                << error;
            return false;
        }
        if (m_virtualScreenConfigs.value(physicalScreenId) == config)
            return true; // unchanged is a successful no-op
        m_virtualScreenConfigs.insert(physicalScreenId, config);
    }
    Q_EMIT virtualScreenConfigsChanged();
    Q_EMIT settingsChanged();
    return true;
}

VirtualScreenConfig Settings::virtualScreenConfig(const QString& physicalScreenId) const
{
    return m_virtualScreenConfigs.value(physicalScreenId);
}

} // namespace PlasmaZones
