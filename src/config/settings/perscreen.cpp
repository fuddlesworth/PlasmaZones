// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../settings.h"
#include "../configbackend_qsettings.h"
#include "../configdefaults.h"
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
    // Strip "Autotile" prefix so both "Algorithm" and "AutotileAlgorithm" match.
    // QML PerScreenOverrideHelper sends short keys; config storage uses prefixed keys.
    const QString k = key.startsWith(QLatin1String("Autotile")) ? key.mid(8) : key;

    if (k == QLatin1String("SplitRatio")) {
        double v = value.toDouble();
        return QVariant(qBound(ConfigDefaults::autotileSplitRatioMin(), v, ConfigDefaults::autotileSplitRatioMax()));
    }
    if (k == QLatin1String("MasterCount"))
        return QVariant(
            qBound(ConfigDefaults::autotileMasterCountMin(), value.toInt(), ConfigDefaults::autotileMasterCountMax()));
    if (k == QLatin1String("InnerGap"))
        return QVariant(
            qBound(ConfigDefaults::autotileInnerGapMin(), value.toInt(), ConfigDefaults::autotileInnerGapMax()));
    if (k == QLatin1String("OuterGap") || k.startsWith(QLatin1String("OuterGap")))
        return QVariant(
            qBound(ConfigDefaults::autotileOuterGapMin(), value.toInt(), ConfigDefaults::autotileOuterGapMax()));
    if (k == QLatin1String("MaxWindows"))
        return QVariant(
            qBound(ConfigDefaults::autotileMaxWindowsMin(), value.toInt(), ConfigDefaults::autotileMaxWindowsMax()));
    if (k == QLatin1String("InsertPosition"))
        return QVariant(qBound(ConfigDefaults::autotileInsertPositionMin(), value.toInt(),
                               ConfigDefaults::autotileInsertPositionMax()));
    if (k == QLatin1String("Algorithm") || key == QLatin1String("AnimationEasingCurve"))
        return value;
    if (k == QLatin1String("UsePerSideOuterGap") || k == QLatin1String("FocusNewWindows")
        || k == QLatin1String("SmartGaps") || k == QLatin1String("FocusFollowsMouse")
        || k == QLatin1String("RespectMinimumSize") || k == QLatin1String("HideTitleBars")
        || key == QLatin1String("AnimationsEnabled"))
        return QVariant(value.toBool());
    if (key == QLatin1String("AnimationDuration"))
        return QVariant(
            qBound(ConfigDefaults::animationDurationMin(), value.toInt(), ConfigDefaults::animationDurationMax()));
    return QVariant();
}

QVariant readPerScreenAutotileEntry(QSettingsConfigGroup& group, const QString& key)
{
    if (key == QLatin1String("AutotileSplitRatio"))
        return QVariant(group.readDouble(key, ConfigDefaults::autotileSplitRatio()));
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

QVariant readPerScreenSnappingEntry(QSettingsConfigGroup& group, const QString& key)
{
    if (key == QLatin1String("SnapAssistEnabled") || key == QLatin1String("ZoneSelectorEnabled"))
        return QVariant(group.readBool(key, false));
    return QVariant(group.readInt(key, 0));
}

QVariant readPerScreenZoneSelectorEntry(QSettingsConfigGroup& group, const QString& key)
{
    namespace K = ZoneSelectorConfigKey;
    if (key == QLatin1String(K::PreviewLockAspect))
        return QVariant(group.readBool(key, true));
    return QVariant(group.readInt(key, 0));
}

void savePerScreenOverrides(QSettingsConfigBackend* backend, const QString& prefix,
                            const QHash<QString, QVariantMap>& source)
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

using PerScreenReadFn = QVariant (*)(QSettingsConfigGroup&, const QString&);
using PerScreenValidateFn = QVariant (*)(const QString&, const QVariant&);

void loadPerScreenGroup(QSettingsConfigBackend* backend, const QStringList& allGroups, const QString& prefix,
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
            dest[screenIdOrName] = overrides;
        }
    }
    migrateConnectorNames(dest);
}

} // anonymous namespace

/**
 * Normalize autotile per-screen override keys from disk-format (prefixed: "AutotileAlgorithm")
 * to short-format ("Algorithm") that QML uses for lookup via settingValue().
 *
 * On disk, keys use the "Autotile" prefix (e.g. "AutotileAlgorithm", "AutotileSplitRatio").
 * QML's PerScreenOverrideHelper calls settingValue("Algorithm", ...) with the short form.
 * Without normalization, keys loaded from disk can't be found by QML lookups.
 *
 * The "Animations*" keys have no "Autotile" prefix on disk, so they pass through unchanged.
 */
static void normalizeAutotileKeys(QHash<QString, QVariantMap>& settings)
{
    for (auto it = settings.begin(); it != settings.end(); ++it) {
        QVariantMap normalized;
        for (auto kit = it.value().constBegin(); kit != it.value().constEnd(); ++kit) {
            const QString& key = kit.key();
            if (key.startsWith(QLatin1String("Autotile"))) {
                normalized[key.mid(8)] = kit.value(); // Strip "Autotile" prefix
            } else {
                normalized[key] = kit.value();
            }
        }
        it.value() = normalized;
    }
}

void Settings::loadPerScreenOverrides(QSettingsConfigBackend* backend)
{
    const QStringList allGroups = backend->groupList();
    loadPerScreenGroup(backend, allGroups, QStringLiteral("ZoneSelector:"), kPerScreenKeys, std::size(kPerScreenKeys),
                       readPerScreenZoneSelectorEntry, validatePerScreenValue, m_perScreenZoneSelectorSettings);
    loadPerScreenGroup(backend, allGroups, QStringLiteral("AutotileScreen:"), kPerScreenAutotileKeys,
                       std::size(kPerScreenAutotileKeys), readPerScreenAutotileEntry, validatePerScreenAutotileValue,
                       m_perScreenAutotileSettings);
    // Normalize autotile keys from disk format ("AutotileAlgorithm") to short format
    // ("Algorithm") that QML uses for lookup via PerScreenOverrideHelper.settingValue().
    normalizeAutotileKeys(m_perScreenAutotileSettings);
    loadPerScreenGroup(backend, allGroups, QStringLiteral("SnappingScreen:"), kPerScreenSnappingKeys,
                       std::size(kPerScreenSnappingKeys), readPerScreenSnappingEntry, validatePerScreenSnappingValue,
                       m_perScreenSnappingSettings);
}

/**
 * Expand autotile per-screen override keys from short-format ("Algorithm") back to
 * disk-format ("AutotileAlgorithm") for saving. This is the inverse of normalizeAutotileKeys().
 *
 * Keys that already have the "Autotile" prefix or are "Animations*" keys pass through unchanged.
 */
static QHash<QString, QVariantMap> expandAutotileKeys(const QHash<QString, QVariantMap>& settings)
{
    // Short keys that should NOT get the "Autotile" prefix
    static const QStringList animationKeys = {
        QStringLiteral("AnimationsEnabled"),
        QStringLiteral("AnimationDuration"),
        QStringLiteral("AnimationEasingCurve"),
    };

    QHash<QString, QVariantMap> expanded;
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
        QVariantMap expandedMap;
        for (auto kit = it.value().constBegin(); kit != it.value().constEnd(); ++kit) {
            const QString& key = kit.key();
            if (key.startsWith(QLatin1String("Autotile")) || animationKeys.contains(key)) {
                expandedMap[key] = kit.value();
            } else {
                expandedMap[QStringLiteral("Autotile") + key] = kit.value();
            }
        }
        expanded[it.key()] = expandedMap;
    }
    return expanded;
}

void Settings::saveAllPerScreenOverrides(QSettingsConfigBackend* backend)
{
    savePerScreenOverrides(backend, QStringLiteral("ZoneSelector:"), m_perScreenZoneSelectorSettings);
    // Expand short keys back to disk format before saving
    savePerScreenOverrides(backend, QStringLiteral("AutotileScreen:"), expandAutotileKeys(m_perScreenAutotileSettings));
    savePerScreenOverrides(backend, QStringLiteral("SnappingScreen:"), m_perScreenSnappingSettings);
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

    // Resolve to EDID-based screen ID so the key matches daemon lookups
    const QString resolved =
        Utils::isConnectorName(screenIdOrName) ? Utils::screenIdForName(screenIdOrName) : screenIdOrName;
    QVariantMap& screenSettings = m_perScreenZoneSelectorSettings[resolved];
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

    // Normalize to short form: strip "Autotile" prefix so the in-memory map
    // always uses short keys ("Algorithm", "SplitRatio") matching QML lookups.
    // Animation keys ("AnimationsEnabled", etc.) have no "Autotile" prefix.
    const QString normalizedKey = key.startsWith(QLatin1String("Autotile")) ? key.mid(8) : key;

    // Resolve to EDID-based screen ID so the key matches daemon lookups
    const QString resolved =
        Utils::isConnectorName(screenIdOrName) ? Utils::screenIdForName(screenIdOrName) : screenIdOrName;
    QVariantMap& screenSettings = m_perScreenAutotileSettings[resolved];
    if (screenSettings.value(normalizedKey) == validated) {
        return;
    }
    screenSettings[normalizedKey] = validated;
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

    // Resolve to EDID-based screen ID so the key matches daemon lookups
    const QString resolved =
        Utils::isConnectorName(screenIdOrName) ? Utils::screenIdForName(screenIdOrName) : screenIdOrName;
    QVariantMap& screenSettings = m_perScreenSnappingSettings[resolved];
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
