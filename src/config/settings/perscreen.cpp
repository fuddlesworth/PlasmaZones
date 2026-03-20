// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../settings.h"
#include "../configbackend.h"
#include "../configbackend_qsettings.h"
#include "../../core/logging.h"
#include "../../core/utils.h"

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

QVariant readPerScreenAutotileEntry(ConfigGroup& group, const QString& key)
{
    if (key == QLatin1String("AutotileSplitRatio"))
        return QVariant(group.readDouble(key, AutotileDefaults::DefaultSplitRatio));
    if (key == QLatin1String("AutotileAlgorithm") || key == QLatin1String("AnimationEasingCurve"))
        return QVariant(group.readString(key));
    if (key == QLatin1String("AutotileUsePerSideOuterGap") || key == QLatin1String("AutotileFocusNewWindows")
        || key == QLatin1String("AutotileSmartGaps") || key == QLatin1String("AutotileFocusFollowsMouse")
        || key == QLatin1String("AutotileRespectMinimumSize") || key == QLatin1String("AutotileHideTitleBars")
        || key == QLatin1String("AnimationsEnabled"))
        return QVariant(group.readBool(key, false));
    return QVariant(group.readInt(key, 0));
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

QVariant readPerScreenSnappingEntry(ConfigGroup& group, const QString& key)
{
    if (key == QLatin1String("SnapAssistEnabled") || key == QLatin1String("ZoneSelectorEnabled"))
        return QVariant(group.readBool(key, false));
    return QVariant(group.readInt(key, 0));
}

QVariant readPerScreenZoneSelectorEntry(ConfigGroup& group, const QString& key)
{
    namespace K = ZoneSelectorConfigKey;
    if (key == QLatin1String(K::PreviewLockAspect))
        return QVariant(group.readBool(key, true));
    return QVariant(group.readInt(key, 0));
}

void savePerScreenOverrides(IConfigBackend* backend, const QString& prefix, const QHash<QString, QVariantMap>& source)
{
    const QStringList groups = backend->groupList();
    for (const QString& groupName : groups) {
        if (groupName.startsWith(prefix)) {
            backend->deleteGroup(groupName);
        }
    }
    for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
        const QVariantMap& overrides = it.value();
        if (overrides.isEmpty())
            continue;
        auto screenGroup = backend->group(prefix + it.key());
        for (auto oit = overrides.constBegin(); oit != overrides.constEnd(); ++oit) {
            const QVariant& val = oit.value();
            switch (val.typeId()) {
            case QMetaType::Bool:
                screenGroup->writeBool(oit.key(), val.toBool());
                break;
            case QMetaType::Int:
                screenGroup->writeInt(oit.key(), val.toInt());
                break;
            case QMetaType::Double:
            case QMetaType::Float:
                screenGroup->writeDouble(oit.key(), val.toDouble());
                break;
            default:
                screenGroup->writeString(oit.key(), val.toString());
                break;
            }
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

using PerScreenReadFn = QVariant (*)(ConfigGroup&, const QString&);
using PerScreenValidateFn = QVariant (*)(const QString&, const QVariant&);

void loadPerScreenGroup(IConfigBackend* backend, const QStringList& allGroups, const QString& prefix,
                        const char* const* keys, size_t keyCount, PerScreenReadFn readEntry,
                        PerScreenValidateFn validate, QHash<QString, QVariantMap>& dest)
{
    dest.clear();
    for (const QString& groupName : allGroups) {
        if (!groupName.startsWith(prefix))
            continue;
        QString screenName = groupName.mid(prefix.size());
        if (screenName.isEmpty())
            continue;

        auto screenGroup = backend->group(groupName);
        QVariantMap overrides;
        for (size_t i = 0; i < keyCount; ++i) {
            const char* key = keys[i];
            QString keyStr = QString::fromLatin1(key);
            if (screenGroup->hasKey(keyStr)) {
                QVariant raw = readEntry(*screenGroup, keyStr);
                QVariant validated = validate(keyStr, raw);
                if (validated.isValid()) {
                    overrides[keyStr] = validated;
                }
            }
        }
        if (!overrides.isEmpty()) {
            dest[screenName] = overrides;
        }
    }
    migrateConnectorNames(dest);
}

} // anonymous namespace

void Settings::loadPerScreenOverrides(IConfigBackend* backend)
{
    const QStringList allGroups = backend->groupList();
    loadPerScreenGroup(backend, allGroups, QStringLiteral("ZoneSelector:"), kPerScreenKeys, std::size(kPerScreenKeys),
                       readPerScreenZoneSelectorEntry, validatePerScreenValue, m_perScreenZoneSelectorSettings);
    loadPerScreenGroup(backend, allGroups, QStringLiteral("AutotileScreen:"), kPerScreenAutotileKeys,
                       std::size(kPerScreenAutotileKeys), readPerScreenAutotileEntry, validatePerScreenAutotileValue,
                       m_perScreenAutotileSettings);
    loadPerScreenGroup(backend, allGroups, QStringLiteral("SnappingScreen:"), kPerScreenSnappingKeys,
                       std::size(kPerScreenSnappingKeys), readPerScreenSnappingEntry, validatePerScreenSnappingValue,
                       m_perScreenSnappingSettings);
}

void Settings::saveAllPerScreenOverrides(IConfigBackend* backend)
{
    savePerScreenOverrides(backend, QStringLiteral("ZoneSelector:"), m_perScreenZoneSelectorSettings);
    savePerScreenOverrides(backend, QStringLiteral("AutotileScreen:"), m_perScreenAutotileSettings);
    savePerScreenOverrides(backend, QStringLiteral("SnappingScreen:"), m_perScreenSnappingSettings);
}

template<typename T>
static typename QHash<QString, T>::const_iterator findPerScreenEntry(const QHash<QString, T>& hash,
                                                                     const QString& screenName)
{
    auto it = hash.constFind(screenName);
    if (it != hash.constEnd()) {
        return it;
    }
    if (Utils::isConnectorName(screenName)) {
        QString resolved = Utils::screenIdForName(screenName);
        if (resolved != screenName) {
            it = hash.constFind(resolved);
            if (it != hash.constEnd())
                return it;
        }
    }
    QString connector = Utils::screenNameForId(screenName);
    if (!connector.isEmpty() && connector != screenName) {
        it = hash.constFind(connector);
        if (it != hash.constEnd())
            return it;
    }
    return hash.constEnd();
}

template<typename T>
static bool removePerScreenEntry(QHash<QString, T>& hash, const QString& screenName)
{
    if (hash.remove(screenName)) {
        return true;
    }
    if (Utils::isConnectorName(screenName)) {
        QString resolved = Utils::screenIdForName(screenName);
        if (resolved != screenName && hash.remove(resolved))
            return true;
    }
    QString connector = Utils::screenNameForId(screenName);
    if (!connector.isEmpty() && connector != screenName && hash.remove(connector))
        return true;
    return false;
}

// ── Per-Screen Zone Selector Config ──────────────────────────────────────────

ZoneSelectorConfig Settings::resolvedZoneSelectorConfig(const QString& screenName) const
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

    auto it = findPerScreenEntry(m_perScreenZoneSelectorSettings, screenName);
    if (it == m_perScreenZoneSelectorSettings.constEnd()) {
        return config;
    }

    applyPerScreenOverrides(config, it.value());
    return config;
}

QVariantMap Settings::getPerScreenZoneSelectorSettings(const QString& screenName) const
{
    auto it = findPerScreenEntry(m_perScreenZoneSelectorSettings, screenName);
    return (it != m_perScreenZoneSelectorSettings.constEnd()) ? it.value() : QVariantMap();
}

void Settings::setPerScreenZoneSelectorSetting(const QString& screenName, const QString& key, const QVariant& value)
{
    if (screenName.isEmpty() || key.isEmpty()) {
        return;
    }

    QVariant validated = validatePerScreenValue(key, value);
    if (!validated.isValid()) {
        qCWarning(lcConfig) << "Unknown or invalid per-screen zone selector key:" << key;
        return;
    }

    QVariantMap& screenSettings = m_perScreenZoneSelectorSettings[screenName];
    if (screenSettings.value(key) == validated) {
        return;
    }
    screenSettings[key] = validated;
    Q_EMIT perScreenZoneSelectorSettingsChanged();
    Q_EMIT settingsChanged();
}

void Settings::clearPerScreenZoneSelectorSettings(const QString& screenName)
{
    if (removePerScreenEntry(m_perScreenZoneSelectorSettings, screenName)) {
        Q_EMIT perScreenZoneSelectorSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

bool Settings::hasPerScreenZoneSelectorSettings(const QString& screenName) const
{
    return findPerScreenEntry(m_perScreenZoneSelectorSettings, screenName)
        != m_perScreenZoneSelectorSettings.constEnd();
}

QStringList Settings::screensWithZoneSelectorOverrides() const
{
    return m_perScreenZoneSelectorSettings.keys();
}

// ── Per-Screen Autotile Config ───────────────────────────────────────────────

QVariantMap Settings::getPerScreenAutotileSettings(const QString& screenName) const
{
    auto it = findPerScreenEntry(m_perScreenAutotileSettings, screenName);
    return (it != m_perScreenAutotileSettings.constEnd()) ? it.value() : QVariantMap();
}

void Settings::setPerScreenAutotileSetting(const QString& screenName, const QString& key, const QVariant& value)
{
    if (screenName.isEmpty() || key.isEmpty()) {
        return;
    }

    QVariant validated = validatePerScreenAutotileValue(key, value);
    if (!validated.isValid()) {
        qCWarning(lcConfig) << "Rejected per-screen autotile setting:" << key + QLatin1String("=") << value;
        return;
    }

    QVariantMap& screenSettings = m_perScreenAutotileSettings[screenName];
    if (screenSettings.value(key) == validated) {
        return;
    }
    screenSettings[key] = validated;
    Q_EMIT perScreenAutotileSettingsChanged();
    Q_EMIT settingsChanged();
}

void Settings::clearPerScreenAutotileSettings(const QString& screenName)
{
    if (removePerScreenEntry(m_perScreenAutotileSettings, screenName)) {
        Q_EMIT perScreenAutotileSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

bool Settings::hasPerScreenAutotileSettings(const QString& screenName) const
{
    return findPerScreenEntry(m_perScreenAutotileSettings, screenName) != m_perScreenAutotileSettings.constEnd();
}

// ── Per-Screen Snapping Config ───────────────────────────────────────────────

QVariantMap Settings::getPerScreenSnappingSettings(const QString& screenName) const
{
    auto it = findPerScreenEntry(m_perScreenSnappingSettings, screenName);
    return (it != m_perScreenSnappingSettings.constEnd()) ? it.value() : QVariantMap();
}

void Settings::setPerScreenSnappingSetting(const QString& screenName, const QString& key, const QVariant& value)
{
    if (screenName.isEmpty() || key.isEmpty()) {
        return;
    }

    QVariant validated = validatePerScreenSnappingValue(key, value);
    if (!validated.isValid()) {
        qCWarning(lcConfig) << "Rejected per-screen snapping setting:" << key + QLatin1String("=") << value;
        return;
    }

    QVariantMap& screenSettings = m_perScreenSnappingSettings[screenName];
    if (screenSettings.value(key) == validated) {
        return;
    }
    screenSettings[key] = validated;
    Q_EMIT perScreenSnappingSettingsChanged();
    Q_EMIT settingsChanged();
}

void Settings::clearPerScreenSnappingSettings(const QString& screenName)
{
    if (removePerScreenEntry(m_perScreenSnappingSettings, screenName)) {
        Q_EMIT perScreenSnappingSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

bool Settings::hasPerScreenSnappingSettings(const QString& screenName) const
{
    return findPerScreenEntry(m_perScreenSnappingSettings, screenName) != m_perScreenSnappingSettings.constEnd();
}

} // namespace PlasmaZones
