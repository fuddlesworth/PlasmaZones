// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../settings.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>

namespace PlasmaZones {

namespace {

QVariant validatePerScreenValue(const QString& key, const QVariant& value)
{
    namespace K = ZoneSelectorConfigKey;
    if (key == QLatin1String(K::Position)) {
        int v = value.toInt();
        return (v >= 0 && v <= 8) ? QVariant(v) : QVariant();
    }
    if (key == QLatin1String(K::LayoutMode)) {
        int v = value.toInt();
        return (v >= 0 && v <= static_cast<int>(ZoneSelectorLayoutMode::Vertical)) ? QVariant(v) : QVariant();
    }
    if (key == QLatin1String(K::SizeMode)) {
        int v = value.toInt();
        return (v >= 0 && v <= static_cast<int>(ZoneSelectorSizeMode::Manual)) ? QVariant(v) : QVariant();
    }
    if (key == QLatin1String(K::MaxRows))
        return QVariant(qBound(1, value.toInt(), 10));
    if (key == QLatin1String(K::PreviewWidth))
        return QVariant(qBound(80, value.toInt(), 400));
    if (key == QLatin1String(K::PreviewHeight))
        return QVariant(qBound(60, value.toInt(), 300));
    if (key == QLatin1String(K::PreviewLockAspect))
        return QVariant(value.toBool());
    if (key == QLatin1String(K::GridColumns))
        return QVariant(qBound(1, value.toInt(), 10));
    if (key == QLatin1String(K::TriggerDistance))
        return QVariant(qBound(10, value.toInt(), 200));
    return QVariant();
}

void applyPerScreenOverrides(ZoneSelectorConfig& config, const QVariantMap& overrides)
{
    namespace K = ZoneSelectorConfigKey;
    auto applyInt = [&](const char* key, int& field) {
        auto it = overrides.constFind(QLatin1String(key));
        if (it != overrides.constEnd()) {
            QVariant v = validatePerScreenValue(QLatin1String(key), it.value());
            if (v.isValid()) {
                field = v.toInt();
            }
        }
    };
    applyInt(K::Position, config.position);
    applyInt(K::LayoutMode, config.layoutMode);
    applyInt(K::SizeMode, config.sizeMode);
    applyInt(K::MaxRows, config.maxRows);
    applyInt(K::PreviewWidth, config.previewWidth);
    applyInt(K::PreviewHeight, config.previewHeight);
    applyInt(K::GridColumns, config.gridColumns);
    applyInt(K::TriggerDistance, config.triggerDistance);
    auto lockIt = overrides.constFind(QLatin1String(K::PreviewLockAspect));
    if (lockIt != overrides.constEnd()) {
        QVariant v = validatePerScreenValue(QLatin1String(K::PreviewLockAspect), lockIt.value());
        if (v.isValid()) {
            config.previewLockAspect = v.toBool();
        }
    }
}

constexpr const char* kPerScreenKeys[] = {
    ZoneSelectorConfigKey::Position,          ZoneSelectorConfigKey::LayoutMode,
    ZoneSelectorConfigKey::SizeMode,          ZoneSelectorConfigKey::MaxRows,
    ZoneSelectorConfigKey::PreviewWidth,      ZoneSelectorConfigKey::PreviewHeight,
    ZoneSelectorConfigKey::PreviewLockAspect, ZoneSelectorConfigKey::GridColumns,
    ZoneSelectorConfigKey::TriggerDistance,
};

constexpr const char* kPerScreenAutotileKeys[] = {
    "AutotileAlgorithm",     "AutotileSplitRatio",         "AutotileMasterCount",       "AutotileInnerGap",
    "AutotileOuterGap",      "AutotileUsePerSideOuterGap", "AutotileOuterGapTop",       "AutotileOuterGapBottom",
    "AutotileOuterGapLeft",  "AutotileOuterGapRight",      "AutotileFocusNewWindows",   "AutotileSmartGaps",
    "AutotileMaxWindows",    "AutotileInsertPosition",     "AutotileFocusFollowsMouse", "AutotileRespectMinimumSize",
    "AutotileHideTitleBars", "AnimationsEnabled",          "AnimationDuration",         "AnimationEasingCurve",
};

QVariant validatePerScreenAutotileValue(const QString& key, const QVariant& value)
{
    if (key == QLatin1String("AutotileSplitRatio")) {
        double v = value.toDouble();
        return QVariant(qBound(AutotileDefaults::MinSplitRatio, v, AutotileDefaults::MaxSplitRatio));
    }
    if (key == QLatin1String("AutotileMasterCount"))
        return QVariant(qBound(AutotileDefaults::MinMasterCount, value.toInt(), AutotileDefaults::MaxMasterCount));
    if (key == QLatin1String("AutotileInnerGap") || key == QLatin1String("AutotileOuterGap"))
        return QVariant(qBound(AutotileDefaults::MinGap, value.toInt(), AutotileDefaults::MaxGap));
    if (key.startsWith(QLatin1String("AutotileOuterGap")))
        return QVariant(qBound(AutotileDefaults::MinGap, value.toInt(), AutotileDefaults::MaxGap));
    if (key == QLatin1String("AutotileMaxWindows"))
        return QVariant(qBound(AutotileDefaults::MinMaxWindows, value.toInt(), AutotileDefaults::MaxMaxWindows));
    if (key == QLatin1String("AutotileInsertPosition"))
        return QVariant(qBound(0, value.toInt(), 2));
    if (key == QLatin1String("AutotileAlgorithm") || key == QLatin1String("AnimationEasingCurve"))
        return value;
    if (key == QLatin1String("AutotileUsePerSideOuterGap") || key == QLatin1String("AutotileFocusNewWindows")
        || key == QLatin1String("AutotileSmartGaps") || key == QLatin1String("AutotileFocusFollowsMouse")
        || key == QLatin1String("AutotileRespectMinimumSize") || key == QLatin1String("AutotileHideTitleBars")
        || key == QLatin1String("AnimationsEnabled"))
        return QVariant(value.toBool());
    if (key == QLatin1String("AnimationDuration"))
        return QVariant(qBound(50, value.toInt(), 500));
    return QVariant();
}

QVariant readPerScreenAutotileEntry(const KConfigGroup& group, const QLatin1String& key)
{
    if (key == QLatin1String("AutotileSplitRatio"))
        return QVariant(group.readEntry(key, AutotileDefaults::DefaultSplitRatio));
    if (key == QLatin1String("AutotileAlgorithm") || key == QLatin1String("AnimationEasingCurve"))
        return QVariant(group.readEntry(key, QString()));
    if (key == QLatin1String("AutotileUsePerSideOuterGap") || key == QLatin1String("AutotileFocusNewWindows")
        || key == QLatin1String("AutotileSmartGaps") || key == QLatin1String("AutotileFocusFollowsMouse")
        || key == QLatin1String("AutotileRespectMinimumSize") || key == QLatin1String("AutotileHideTitleBars")
        || key == QLatin1String("AnimationsEnabled"))
        return QVariant(group.readEntry(key, false));
    return QVariant(group.readEntry(key, 0));
}

constexpr const char* kPerScreenSnappingKeys[] = {
    "SnapAssistEnabled",    "ZoneSelectorEnabled",      "ZoneSelectorTriggerDistance",
    "ZoneSelectorPosition", "ZoneSelectorLayoutMode",   "ZoneSelectorSizeMode",
    "ZoneSelectorMaxRows",  "ZoneSelectorPreviewWidth", "ZoneSelectorPreviewHeight",
};

QVariant validatePerScreenSnappingValue(const QString& key, const QVariant& value)
{
    if (key == QLatin1String("SnapAssistEnabled") || key == QLatin1String("ZoneSelectorEnabled"))
        return QVariant(value.toBool());
    if (key == QLatin1String("ZoneSelectorTriggerDistance"))
        return QVariant(qBound(10, value.toInt(), 200));
    if (key == QLatin1String("ZoneSelectorPosition")) {
        int v = value.toInt();
        return (v >= 0 && v <= 8) ? QVariant(v) : QVariant();
    }
    if (key == QLatin1String("ZoneSelectorLayoutMode"))
        return QVariant(qBound(0, value.toInt(), static_cast<int>(ZoneSelectorLayoutMode::Vertical)));
    if (key == QLatin1String("ZoneSelectorSizeMode"))
        return QVariant(qBound(0, value.toInt(), static_cast<int>(ZoneSelectorSizeMode::Manual)));
    if (key == QLatin1String("ZoneSelectorMaxRows"))
        return QVariant(qBound(1, value.toInt(), 10));
    if (key == QLatin1String("ZoneSelectorPreviewWidth"))
        return QVariant(qBound(80, value.toInt(), 400));
    if (key == QLatin1String("ZoneSelectorPreviewHeight"))
        return QVariant(qBound(60, value.toInt(), 300));
    return QVariant();
}

QVariant readPerScreenSnappingEntry(const KConfigGroup& group, const QLatin1String& key)
{
    if (key == QLatin1String("SnapAssistEnabled") || key == QLatin1String("ZoneSelectorEnabled"))
        return QVariant(group.readEntry(key, false));
    return QVariant(group.readEntry(key, 0));
}

QVariant readPerScreenZoneSelectorEntry(const KConfigGroup& group, const QLatin1String& key)
{
    namespace K = ZoneSelectorConfigKey;
    if (key == QLatin1String(K::PreviewLockAspect))
        return QVariant(group.readEntry(key, true));
    return QVariant(group.readEntry(key, 0));
}

void savePerScreenOverrides(KSharedConfigPtr config, const QLatin1String& prefix,
                            const QHash<QString, QVariantMap>& source)
{
    const QStringList groups = config->groupList();
    for (const QString& groupName : groups) {
        if (groupName.startsWith(prefix)) {
            config->deleteGroup(groupName);
        }
    }
    for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
        const QVariantMap& overrides = it.value();
        if (overrides.isEmpty())
            continue;
        KConfigGroup screenGroup = config->group(prefix + it.key());
        for (auto oit = overrides.constBegin(); oit != overrides.constEnd(); ++oit) {
            screenGroup.writeEntry(oit.key(), oit.value());
        }
    }
}

void migrateConnectorNames(QHash<QString, QVariantMap>& settings)
{
    QHash<QString, QVariantMap> migrated;
    for (auto it = settings.begin(); it != settings.end();) {
        if (Utils::isConnectorName(it.key())) {
            QString resolved = Utils::screenIdForName(it.key());
            if (resolved != it.key()) {
                if (migrated.contains(resolved)) {
                    qCWarning(lcConfig) << "EDID collision during migration:" << it.key()
                                        << "and another connector both resolve to" << resolved << "- later entry wins";
                }
                migrated[resolved] = it.value();
                it = settings.erase(it);
                continue;
            }
        }
        ++it;
    }
    for (auto mit = migrated.constBegin(); mit != migrated.constEnd(); ++mit) {
        settings.insert(mit.key(), mit.value());
    }
}

using PerScreenReadFn = QVariant (*)(const KConfigGroup&, const QLatin1String&);
using PerScreenValidateFn = QVariant (*)(const QString&, const QVariant&);

void loadPerScreenGroup(KSharedConfigPtr config, const QStringList& allGroups, const QLatin1String& prefix,
                        const char* const* keys, size_t keyCount, PerScreenReadFn readEntry,
                        PerScreenValidateFn validate, QHash<QString, QVariantMap>& dest)
{
    dest.clear();
    for (const QString& groupName : allGroups) {
        if (!groupName.startsWith(prefix))
            continue;
        QString screenIdOrName = groupName.mid(prefix.size());
        if (screenIdOrName.isEmpty())
            continue;

        KConfigGroup screenGroup = config->group(groupName);
        QVariantMap overrides;
        for (size_t i = 0; i < keyCount; ++i) {
            const char* key = keys[i];
            QLatin1String keyStr(key);
            if (screenGroup.hasKey(keyStr)) {
                QVariant raw = readEntry(screenGroup, keyStr);
                QVariant validated = validate(keyStr, raw);
                if (validated.isValid()) {
                    overrides[QString::fromLatin1(key)] = validated;
                }
            }
        }
        if (!overrides.isEmpty()) {
            dest[screenIdOrName] = overrides;
        }
    }
    migrateConnectorNames(dest);
}

} // anonymous namespace

void Settings::loadPerScreenOverrides(KSharedConfigPtr config)
{
    const QStringList allGroups = config->groupList();
    loadPerScreenGroup(config, allGroups, QLatin1String("ZoneSelector:"), kPerScreenKeys, std::size(kPerScreenKeys),
                       readPerScreenZoneSelectorEntry, validatePerScreenValue, m_perScreenZoneSelectorSettings);
    loadPerScreenGroup(config, allGroups, QLatin1String("AutotileScreen:"), kPerScreenAutotileKeys,
                       std::size(kPerScreenAutotileKeys), readPerScreenAutotileEntry, validatePerScreenAutotileValue,
                       m_perScreenAutotileSettings);
    loadPerScreenGroup(config, allGroups, QLatin1String("SnappingScreen:"), kPerScreenSnappingKeys,
                       std::size(kPerScreenSnappingKeys), readPerScreenSnappingEntry, validatePerScreenSnappingValue,
                       m_perScreenSnappingSettings);
}

void Settings::saveAllPerScreenOverrides(KSharedConfigPtr config)
{
    savePerScreenOverrides(config, QLatin1String("ZoneSelector:"), m_perScreenZoneSelectorSettings);
    savePerScreenOverrides(config, QLatin1String("AutotileScreen:"), m_perScreenAutotileSettings);
    savePerScreenOverrides(config, QLatin1String("SnappingScreen:"), m_perScreenSnappingSettings);
}

template<typename T>
static typename QHash<QString, T>::const_iterator findPerScreenEntry(const QHash<QString, T>& hash,
                                                                     const QString& screenIdOrName)
{
    auto it = hash.constFind(screenIdOrName);
    if (it != hash.constEnd()) {
        return it;
    }
    if (Utils::isConnectorName(screenIdOrName)) {
        QString resolved = Utils::screenIdForName(screenIdOrName);
        if (resolved != screenIdOrName) {
            it = hash.constFind(resolved);
            if (it != hash.constEnd())
                return it;
        }
    }
    QString connector = Utils::screenNameForId(screenIdOrName);
    if (!connector.isEmpty() && connector != screenIdOrName) {
        it = hash.constFind(connector);
        if (it != hash.constEnd())
            return it;
    }
    return hash.constEnd();
}

template<typename T>
static bool removePerScreenEntry(QHash<QString, T>& hash, const QString& screenIdOrName)
{
    if (hash.remove(screenIdOrName)) {
        return true;
    }
    if (Utils::isConnectorName(screenIdOrName)) {
        QString resolved = Utils::screenIdForName(screenIdOrName);
        if (resolved != screenIdOrName && hash.remove(resolved))
            return true;
    }
    QString connector = Utils::screenNameForId(screenIdOrName);
    if (!connector.isEmpty() && connector != screenIdOrName && hash.remove(connector))
        return true;
    return false;
}

// ── Per-Screen Zone Selector Config ──────────────────────────────────────────

ZoneSelectorConfig Settings::resolvedZoneSelectorConfig(const QString& screenIdOrName) const
{
    ZoneSelectorConfig config = {static_cast<int>(m_zoneSelectorPosition),
                                 static_cast<int>(m_zoneSelectorLayoutMode),
                                 static_cast<int>(m_zoneSelectorSizeMode),
                                 m_zoneSelectorMaxRows,
                                 m_zoneSelectorPreviewWidth,
                                 m_zoneSelectorPreviewHeight,
                                 m_zoneSelectorPreviewLockAspect,
                                 m_zoneSelectorGridColumns,
                                 m_zoneSelectorTriggerDistance};

    auto it = findPerScreenEntry(m_perScreenZoneSelectorSettings, screenIdOrName);
    if (it == m_perScreenZoneSelectorSettings.constEnd()) {
        return config;
    }

    applyPerScreenOverrides(config, it.value());
    return config;
}

QVariantMap Settings::getPerScreenZoneSelectorSettings(const QString& screenIdOrName) const
{
    auto it = findPerScreenEntry(m_perScreenZoneSelectorSettings, screenIdOrName);
    return (it != m_perScreenZoneSelectorSettings.constEnd()) ? it.value() : QVariantMap();
}

void Settings::setPerScreenZoneSelectorSetting(const QString& screenIdOrName, const QString& key, const QVariant& value)
{
    if (screenIdOrName.isEmpty() || key.isEmpty()) {
        return;
    }

    QVariant validated = validatePerScreenValue(key, value);
    if (!validated.isValid()) {
        qCWarning(lcConfig) << "Unknown or invalid per-screen zone selector key:" << key;
        return;
    }

    QVariantMap& screenSettings = m_perScreenZoneSelectorSettings[screenIdOrName];
    if (screenSettings.value(key) == validated) {
        return;
    }
    screenSettings[key] = validated;
    Q_EMIT perScreenZoneSelectorSettingsChanged();
    Q_EMIT settingsChanged();
}

void Settings::clearPerScreenZoneSelectorSettings(const QString& screenIdOrName)
{
    if (removePerScreenEntry(m_perScreenZoneSelectorSettings, screenIdOrName)) {
        Q_EMIT perScreenZoneSelectorSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

bool Settings::hasPerScreenZoneSelectorSettings(const QString& screenIdOrName) const
{
    return findPerScreenEntry(m_perScreenZoneSelectorSettings, screenIdOrName)
        != m_perScreenZoneSelectorSettings.constEnd();
}

QStringList Settings::screensWithZoneSelectorOverrides() const
{
    return m_perScreenZoneSelectorSettings.keys();
}

// ── Per-Screen Autotile Config ───────────────────────────────────────────────

QVariantMap Settings::getPerScreenAutotileSettings(const QString& screenIdOrName) const
{
    auto it = findPerScreenEntry(m_perScreenAutotileSettings, screenIdOrName);
    return (it != m_perScreenAutotileSettings.constEnd()) ? it.value() : QVariantMap();
}

void Settings::setPerScreenAutotileSetting(const QString& screenIdOrName, const QString& key, const QVariant& value)
{
    if (screenIdOrName.isEmpty() || key.isEmpty()) {
        return;
    }

    QVariant validated = validatePerScreenAutotileValue(key, value);
    if (!validated.isValid()) {
        qCWarning(lcConfig) << "Rejected per-screen autotile setting:" << key + QLatin1String("=") << value;
        return;
    }

    QVariantMap& screenSettings = m_perScreenAutotileSettings[screenIdOrName];
    if (screenSettings.value(key) == validated) {
        return;
    }
    screenSettings[key] = validated;
    Q_EMIT perScreenAutotileSettingsChanged();
    Q_EMIT settingsChanged();
}

void Settings::clearPerScreenAutotileSettings(const QString& screenIdOrName)
{
    if (removePerScreenEntry(m_perScreenAutotileSettings, screenIdOrName)) {
        Q_EMIT perScreenAutotileSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

bool Settings::hasPerScreenAutotileSettings(const QString& screenIdOrName) const
{
    return findPerScreenEntry(m_perScreenAutotileSettings, screenIdOrName) != m_perScreenAutotileSettings.constEnd();
}

// ── Per-Screen Snapping Config ───────────────────────────────────────────────

QVariantMap Settings::getPerScreenSnappingSettings(const QString& screenIdOrName) const
{
    auto it = findPerScreenEntry(m_perScreenSnappingSettings, screenIdOrName);
    return (it != m_perScreenSnappingSettings.constEnd()) ? it.value() : QVariantMap();
}

void Settings::setPerScreenSnappingSetting(const QString& screenIdOrName, const QString& key, const QVariant& value)
{
    if (screenIdOrName.isEmpty() || key.isEmpty()) {
        return;
    }

    QVariant validated = validatePerScreenSnappingValue(key, value);
    if (!validated.isValid()) {
        qCWarning(lcConfig) << "Rejected per-screen snapping setting:" << key + QLatin1String("=") << value;
        return;
    }

    QVariantMap& screenSettings = m_perScreenSnappingSettings[screenIdOrName];
    if (screenSettings.value(key) == validated) {
        return;
    }
    screenSettings[key] = validated;
    Q_EMIT perScreenSnappingSettingsChanged();
    Q_EMIT settingsChanged();
}

void Settings::clearPerScreenSnappingSettings(const QString& screenIdOrName)
{
    if (removePerScreenEntry(m_perScreenSnappingSettings, screenIdOrName)) {
        Q_EMIT perScreenSnappingSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

bool Settings::hasPerScreenSnappingSettings(const QString& screenIdOrName) const
{
    return findPerScreenEntry(m_perScreenSnappingSettings, screenIdOrName) != m_perScreenSnappingSettings.constEnd();
}

} // namespace PlasmaZones
