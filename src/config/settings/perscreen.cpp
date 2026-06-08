// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../settings.h"
#include "../configbackends.h"
#include "../configdefaults.h"
#include "../../core/constants.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include <PhosphorScreens/ScreenIdentity.h>
#include <QSet>

namespace PlasmaZones {

namespace {

QVariant validatePerScreenValue(const QString& key, const QVariant& value)
{
    namespace K = ZoneSelectorConfigKey;
    if (key == QLatin1String(K::Position)) {
        int v = value.toInt();
        return (v >= 0 && v <= static_cast<int>(ZoneSelectorPosition::BottomRight)) ? QVariant(v) : QVariant();
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
        return QVariant(qBound(ConfigDefaults::maxRowsMin(), value.toInt(), ConfigDefaults::maxRowsMax()));
    if (key == QLatin1String(K::PreviewWidth))
        return QVariant(qBound(ConfigDefaults::previewWidthMin(), value.toInt(), ConfigDefaults::previewWidthMax()));
    if (key == QLatin1String(K::PreviewHeight))
        return QVariant(qBound(ConfigDefaults::previewHeightMin(), value.toInt(), ConfigDefaults::previewHeightMax()));
    if (key == QLatin1String(K::PreviewLockAspect))
        return QVariant(value.toBool());
    if (key == QLatin1String(K::GridColumns))
        return QVariant(qBound(ConfigDefaults::gridColumnsMin(), value.toInt(), ConfigDefaults::gridColumnsMax()));
    if (key == QLatin1String(K::TriggerDistance))
        return QVariant(
            qBound(ConfigDefaults::triggerDistanceMin(), value.toInt(), ConfigDefaults::triggerDistanceMax()));
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

const QLatin1String kPerScreenKeys[] = {
    QLatin1String(ZoneSelectorConfigKey::Position),          QLatin1String(ZoneSelectorConfigKey::LayoutMode),
    QLatin1String(ZoneSelectorConfigKey::SizeMode),          QLatin1String(ZoneSelectorConfigKey::MaxRows),
    QLatin1String(ZoneSelectorConfigKey::PreviewWidth),      QLatin1String(ZoneSelectorConfigKey::PreviewHeight),
    QLatin1String(ZoneSelectorConfigKey::PreviewLockAspect), QLatin1String(ZoneSelectorConfigKey::GridColumns),
    QLatin1String(ZoneSelectorConfigKey::TriggerDistance),
};

const QLatin1String kPerScreenAutotileKeys[] = {
    QLatin1String(PerScreenAutotileKey::Algorithm),
    QLatin1String(PerScreenAutotileKey::SplitRatio),
    QLatin1String(PerScreenAutotileKey::SplitRatioStep),
    QLatin1String(PerScreenAutotileKey::MasterCount),
    QLatin1String(PerScreenAutotileKey::InnerGap),
    QLatin1String(PerScreenAutotileKey::OuterGap),
    QLatin1String(PerScreenAutotileKey::UsePerSideOuterGap),
    QLatin1String(PerScreenAutotileKey::OuterGapTop),
    QLatin1String(PerScreenAutotileKey::OuterGapBottom),
    QLatin1String(PerScreenAutotileKey::OuterGapLeft),
    QLatin1String(PerScreenAutotileKey::OuterGapRight),
    QLatin1String(PerScreenAutotileKey::FocusNewWindows),
    QLatin1String(PerScreenAutotileKey::SmartGaps),
    QLatin1String(PerScreenAutotileKey::MaxWindows),
    QLatin1String(PerScreenAutotileKey::InsertPosition),
    QLatin1String(PerScreenAutotileKey::FocusFollowsMouse),
    QLatin1String(PerScreenAutotileKey::RespectMinimumSize),
    QLatin1String(PerScreenAutotileKey::HideTitleBars),
    QLatin1String(PerScreenAutotileKey::AnimationsEnabled),
    QLatin1String(PerScreenAutotileKey::AnimationDuration),
    QLatin1String(PerScreenAutotileKey::AnimationEasingCurve),
};

// Gaps sub-domain of the autotile per-screen keys — the keys the Tiling
// Appearance "Gaps" card writes. Everything else in kPerScreenAutotileKeys is
// the Tiling Algorithm card's sub-domain. The two cards live on disjoint key
// subsets so each card's scope chip reports its override dot and clears its
// reset against ONLY its own keys; a shared whole-domain clear would wipe the
// other card's per-monitor overrides (data loss on reset).
const QLatin1String kPerScreenAutotileGapsKeys[] = {
    QLatin1String(PerScreenAutotileKey::InnerGap),           QLatin1String(PerScreenAutotileKey::OuterGap),
    QLatin1String(PerScreenAutotileKey::UsePerSideOuterGap), QLatin1String(PerScreenAutotileKey::OuterGapTop),
    QLatin1String(PerScreenAutotileKey::OuterGapBottom),     QLatin1String(PerScreenAutotileKey::OuterGapLeft),
    QLatin1String(PerScreenAutotileKey::OuterGapRight),      QLatin1String(PerScreenAutotileKey::SmartGaps),
};

bool isPerScreenAutotileGapsKey(const QString& key)
{
    // In-memory per-screen autotile keys are short form (the setter and
    // normalizeAutotileKeys strip the "Autotile" prefix), while
    // kPerScreenAutotileGapsKeys holds the prefixed disk form — compare on the
    // short form so e.g. stored "InnerGap" matches "AutotileInnerGap". The short
    // gaps-key set is a compile-time constant, so strip it once into a static
    // set rather than re-allocating a QString per disk-form key on every call.
    static const QSet<QString> shortGapsKeys = []() {
        QSet<QString> keys;
        for (const QLatin1String& k : kPerScreenAutotileGapsKeys) {
            const QString s(k);
            keys.insert(s.startsWith(QLatin1String("Autotile")) ? s.mid(8) : s);
        }
        return keys;
    }();
    const QString shortKey = key.startsWith(QLatin1String("Autotile")) ? key.mid(8) : key;
    return shortGapsKeys.contains(shortKey);
}

QVariant validatePerScreenAutotileValue(const QString& key, const QVariant& value)
{
    // Strip "Autotile" prefix so both "Algorithm" and "AutotileAlgorithm" match.
    // QML PerScreenOverrideHelper sends short keys; config storage uses prefixed keys.
    const QString k = key.startsWith(QLatin1String("Autotile")) ? key.mid(8) : key;

    if (k == PerScreenKeys::SplitRatio) {
        double v = value.toDouble();
        return QVariant(qBound(ConfigDefaults::autotileSplitRatioMin(), v, ConfigDefaults::autotileSplitRatioMax()));
    }
    if (k == PerScreenKeys::SplitRatioStep) {
        double v = value.toDouble();
        return QVariant(
            qBound(ConfigDefaults::autotileSplitRatioStepMin(), v, ConfigDefaults::autotileSplitRatioStepMax()));
    }
    if (k == PerScreenKeys::MasterCount)
        return QVariant(
            qBound(ConfigDefaults::autotileMasterCountMin(), value.toInt(), ConfigDefaults::autotileMasterCountMax()));
    if (k == PerScreenKeys::InnerGap)
        return QVariant(
            qBound(ConfigDefaults::autotileInnerGapMin(), value.toInt(), ConfigDefaults::autotileInnerGapMax()));
    // Per-side gaps each have their own min/max — match exactly, not by prefix.
    // A single startsWith("OuterGap") would apply the uniform-gap bounds to
    // Top/Bottom/Left/Right, which silently clamps to the wrong range whenever
    // those bounds diverge from the uniform ones.
    if (k == PerScreenKeys::OuterGap)
        return QVariant(
            qBound(ConfigDefaults::autotileOuterGapMin(), value.toInt(), ConfigDefaults::autotileOuterGapMax()));
    // Per-side keys (OuterGapTop/Bottom/Left/Right) have no short-form named
    // constant — PerScreenAutotileKey holds only the prefixed disk form and the
    // value here is already prefix-stripped — so match the short literals.
    if (k == QLatin1String("OuterGapTop"))
        return QVariant(
            qBound(ConfigDefaults::autotileOuterGapTopMin(), value.toInt(), ConfigDefaults::autotileOuterGapTopMax()));
    if (k == QLatin1String("OuterGapBottom"))
        return QVariant(qBound(ConfigDefaults::autotileOuterGapBottomMin(), value.toInt(),
                               ConfigDefaults::autotileOuterGapBottomMax()));
    if (k == QLatin1String("OuterGapLeft"))
        return QVariant(qBound(ConfigDefaults::autotileOuterGapLeftMin(), value.toInt(),
                               ConfigDefaults::autotileOuterGapLeftMax()));
    if (k == QLatin1String("OuterGapRight"))
        return QVariant(qBound(ConfigDefaults::autotileOuterGapRightMin(), value.toInt(),
                               ConfigDefaults::autotileOuterGapRightMax()));
    if (k == PerScreenKeys::MaxWindows)
        return QVariant(
            qBound(ConfigDefaults::autotileMaxWindowsMin(), value.toInt(), ConfigDefaults::autotileMaxWindowsMax()));
    if (k == PerScreenKeys::InsertPosition)
        return QVariant(qBound(ConfigDefaults::autotileInsertPositionMin(), value.toInt(),
                               ConfigDefaults::autotileInsertPositionMax()));
    if (k == PerScreenKeys::Algorithm || k == PerScreenKeys::AnimationEasingCurve)
        return value;
    if (k == PerScreenKeys::UsePerSideOuterGap || k == PerScreenKeys::FocusNewWindows || k == PerScreenKeys::SmartGaps
        || k == PerScreenKeys::FocusFollowsMouse || k == PerScreenKeys::RespectMinimumSize
        || k == PerScreenKeys::HideTitleBars || k == PerScreenKeys::AnimationsEnabled)
        return QVariant(value.toBool());
    if (k == PerScreenKeys::AnimationDuration)
        return QVariant(
            qBound(ConfigDefaults::animationDurationMin(), value.toInt(), ConfigDefaults::animationDurationMax()));
    return QVariant();
}

QVariant readPerScreenAutotileEntry(PhosphorConfig::IGroup& group, const QString& key)
{
    if (key == QLatin1String(PerScreenAutotileKey::SplitRatio))
        return QVariant(group.readDouble(key, ConfigDefaults::autotileSplitRatio()));
    if (key == QLatin1String(PerScreenAutotileKey::SplitRatioStep))
        return QVariant(group.readDouble(key, ConfigDefaults::autotileSplitRatioStep()));
    if (key == QLatin1String(PerScreenAutotileKey::Algorithm)
        || key == QLatin1String(PerScreenAutotileKey::AnimationEasingCurve))
        return QVariant(group.readString(key));
    if (key == QLatin1String(PerScreenAutotileKey::UsePerSideOuterGap))
        return QVariant(group.readBool(key, ConfigDefaults::autotileUsePerSideOuterGap()));
    if (key == QLatin1String(PerScreenAutotileKey::FocusNewWindows))
        return QVariant(group.readBool(key, ConfigDefaults::autotileFocusNewWindows()));
    if (key == QLatin1String(PerScreenAutotileKey::SmartGaps))
        return QVariant(group.readBool(key, ConfigDefaults::autotileSmartGaps()));
    if (key == QLatin1String(PerScreenAutotileKey::FocusFollowsMouse))
        return QVariant(group.readBool(key, ConfigDefaults::autotileFocusFollowsMouse()));
    if (key == QLatin1String(PerScreenAutotileKey::RespectMinimumSize))
        return QVariant(group.readBool(key, ConfigDefaults::autotileRespectMinimumSize()));
    if (key == QLatin1String(PerScreenAutotileKey::HideTitleBars))
        return QVariant(group.readBool(key, ConfigDefaults::autotileHideTitleBars()));
    if (key == QLatin1String(PerScreenAutotileKey::AnimationsEnabled))
        return QVariant(group.readBool(key, ConfigDefaults::animationsEnabled()));
    return QVariant(group.readInt(key, 0));
}

const QLatin1String kPerScreenSnappingKeys[] = {
    PerScreenSnappingKey::SnapAssistEnabled,
    PerScreenSnappingKey::ZoneSelectorEnabled,
    PerScreenSnappingKey::ZoneSelectorTriggerDistance,
    PerScreenSnappingKey::ZoneSelectorPosition,
    PerScreenSnappingKey::ZoneSelectorLayoutMode,
    PerScreenSnappingKey::ZoneSelectorSizeMode,
    PerScreenSnappingKey::ZoneSelectorMaxRows,
    PerScreenSnappingKey::ZoneSelectorPreviewWidth,
    PerScreenSnappingKey::ZoneSelectorPreviewHeight,
    // Snapping gaps (per-screen) — without these the Gaps card's overrides would
    // not be re-loaded on next launch even after the validator accepts the write.
    PerScreenSnappingKey::ZonePadding,
    PerScreenSnappingKey::OuterGap,
    PerScreenSnappingKey::UsePerSideOuterGap,
    PerScreenSnappingKey::OuterGapTop,
    PerScreenSnappingKey::OuterGapBottom,
    PerScreenSnappingKey::OuterGapLeft,
    PerScreenSnappingKey::OuterGapRight,
};

QVariant validatePerScreenSnappingValue(const QString& key, const QVariant& value)
{
    namespace K = PerScreenSnappingKey;
    if (key == K::SnapAssistEnabled || key == K::ZoneSelectorEnabled)
        return QVariant(value.toBool());
    if (key == K::ZoneSelectorTriggerDistance)
        return QVariant(
            qBound(ConfigDefaults::triggerDistanceMin(), value.toInt(), ConfigDefaults::triggerDistanceMax()));
    if (key == K::ZoneSelectorPosition) {
        int v = value.toInt();
        return (v >= 0 && v <= static_cast<int>(ZoneSelectorPosition::BottomRight)) ? QVariant(v) : QVariant();
    }
    if (key == K::ZoneSelectorLayoutMode) {
        int v = value.toInt();
        return (v >= 0 && v <= static_cast<int>(ZoneSelectorLayoutMode::Vertical)) ? QVariant(v) : QVariant();
    }
    if (key == K::ZoneSelectorSizeMode) {
        int v = value.toInt();
        return (v >= 0 && v <= static_cast<int>(ZoneSelectorSizeMode::Manual)) ? QVariant(v) : QVariant();
    }
    if (key == K::ZoneSelectorMaxRows)
        return QVariant(qBound(ConfigDefaults::maxRowsMin(), value.toInt(), ConfigDefaults::maxRowsMax()));
    if (key == K::ZoneSelectorPreviewWidth)
        return QVariant(qBound(ConfigDefaults::previewWidthMin(), value.toInt(), ConfigDefaults::previewWidthMax()));
    if (key == K::ZoneSelectorPreviewHeight)
        return QVariant(qBound(ConfigDefaults::previewHeightMin(), value.toInt(), ConfigDefaults::previewHeightMax()));
    // Per-screen snapping gaps (the Gaps card on Snapping → Window → Appearance).
    // Each key clamps against its own ConfigDefaults bounds — mirroring the
    // per-side handling in the autotile validator above; a uniform startsWith
    // would clamp Top/Bottom/Left/Right to the wrong range when those bounds
    // diverge from the uniform OuterGap bounds.
    if (key == K::ZonePadding)
        return QVariant(qBound(ConfigDefaults::zonePaddingMin(), value.toInt(), ConfigDefaults::zonePaddingMax()));
    if (key == K::OuterGap)
        return QVariant(qBound(ConfigDefaults::outerGapMin(), value.toInt(), ConfigDefaults::outerGapMax()));
    if (key == K::OuterGapTop)
        return QVariant(qBound(ConfigDefaults::outerGapTopMin(), value.toInt(), ConfigDefaults::outerGapTopMax()));
    if (key == K::OuterGapBottom)
        return QVariant(
            qBound(ConfigDefaults::outerGapBottomMin(), value.toInt(), ConfigDefaults::outerGapBottomMax()));
    if (key == K::OuterGapLeft)
        return QVariant(qBound(ConfigDefaults::outerGapLeftMin(), value.toInt(), ConfigDefaults::outerGapLeftMax()));
    if (key == K::OuterGapRight)
        return QVariant(qBound(ConfigDefaults::outerGapRightMin(), value.toInt(), ConfigDefaults::outerGapRightMax()));
    if (key == K::UsePerSideOuterGap)
        return QVariant(value.toBool());
    return QVariant();
}

QVariant readPerScreenSnappingEntry(PhosphorConfig::IGroup& group, const QString& key)
{
    namespace K = PerScreenSnappingKey;
    if (key == K::SnapAssistEnabled)
        return QVariant(group.readBool(key, ConfigDefaults::snapAssistEnabled()));
    if (key == K::ZoneSelectorEnabled)
        return QVariant(group.readBool(key, ConfigDefaults::zoneSelectorEnabled()));
    // UsePerSideOuterGap is a bool; the int gap keys (ZonePadding/OuterGap/per-side)
    // fall through to readInt below, which is the correct type for them.
    if (key == K::UsePerSideOuterGap)
        return QVariant(group.readBool(key, ConfigDefaults::usePerSideOuterGap()));
    return QVariant(group.readInt(key, 0));
}

QVariant readPerScreenZoneSelectorEntry(PhosphorConfig::IGroup& group, const QString& key)
{
    namespace K = ZoneSelectorConfigKey;
    if (key == QLatin1String(K::PreviewLockAspect))
        return QVariant(group.readBool(key, ConfigDefaults::previewLockAspect()));
    return QVariant(group.readInt(key, 0));
}

void savePerScreenOverrides(PhosphorConfig::IBackend* backend, const QString& prefix,
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
        if (PhosphorScreens::ScreenIdentity::isConnectorName(it.key())) {
            QString resolved = PhosphorScreens::ScreenIdentity::idForName(it.key());
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

using PerScreenReadFn = QVariant (*)(PhosphorConfig::IGroup&, const QString&);
using PerScreenValidateFn = QVariant (*)(const QString&, const QVariant&);

void loadPerScreenGroup(PhosphorConfig::IBackend* backend, const QStringList& allGroups, const QString& prefix,
                        const QLatin1String* keys, size_t keyCount, PerScreenReadFn readEntry,
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
            const QString keyStr(keys[i]);
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

void Settings::loadPerScreenOverrides(PhosphorConfig::IBackend* backend)
{
    const QStringList allGroups = backend->groupList();
    loadPerScreenGroup(backend, allGroups, ConfigDefaults::zoneSelectorGroupPrefix(), kPerScreenKeys,
                       std::size(kPerScreenKeys), readPerScreenZoneSelectorEntry, validatePerScreenValue,
                       m_perScreenZoneSelectorSettings);
    loadPerScreenGroup(backend, allGroups, ConfigDefaults::autotileScreenGroupPrefix(), kPerScreenAutotileKeys,
                       std::size(kPerScreenAutotileKeys), readPerScreenAutotileEntry, validatePerScreenAutotileValue,
                       m_perScreenAutotileSettings);
    // Normalize autotile keys from disk format ("AutotileAlgorithm") to short format
    // ("Algorithm") that QML uses for lookup via PerScreenOverrideHelper.settingValue().
    normalizeAutotileKeys(m_perScreenAutotileSettings);
    loadPerScreenGroup(backend, allGroups, ConfigDefaults::snappingScreenGroupPrefix(), kPerScreenSnappingKeys,
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
        QLatin1String(PerScreenAutotileKey::AnimationsEnabled),
        QLatin1String(PerScreenAutotileKey::AnimationDuration),
        QLatin1String(PerScreenAutotileKey::AnimationEasingCurve),
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

void Settings::saveAllPerScreenOverrides(PhosphorConfig::IBackend* backend)
{
    savePerScreenOverrides(backend, ConfigDefaults::zoneSelectorGroupPrefix(), m_perScreenZoneSelectorSettings);
    // Expand short keys back to disk format before saving
    savePerScreenOverrides(backend, ConfigDefaults::autotileScreenGroupPrefix(),
                           expandAutotileKeys(m_perScreenAutotileSettings));
    savePerScreenOverrides(backend, ConfigDefaults::snappingScreenGroupPrefix(), m_perScreenSnappingSettings);
}

template<typename T>
static typename QHash<QString, T>::const_iterator findPerScreenEntry(const QHash<QString, T>& hash,
                                                                     const QString& screenIdOrName)
{
    auto it = hash.constFind(screenIdOrName);
    if (it != hash.constEnd()) {
        return it;
    }
    if (PhosphorScreens::ScreenIdentity::isConnectorName(screenIdOrName)) {
        QString resolved = PhosphorScreens::ScreenIdentity::idForName(screenIdOrName);
        if (resolved != screenIdOrName) {
            it = hash.constFind(resolved);
            if (it != hash.constEnd())
                return it;
        }
    }
    QString connector = PhosphorScreens::ScreenIdentity::nameForId(screenIdOrName);
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
    if (PhosphorScreens::ScreenIdentity::isConnectorName(screenIdOrName)) {
        QString resolved = PhosphorScreens::ScreenIdentity::idForName(screenIdOrName);
        if (resolved != screenIdOrName && hash.remove(resolved))
            return true;
    }
    QString connector = PhosphorScreens::ScreenIdentity::nameForId(screenIdOrName);
    if (!connector.isEmpty() && connector != screenIdOrName && hash.remove(connector))
        return true;
    return false;
}

// Mutable sibling of findPerScreenEntry — same id/connector resolution, used by
// the autotile sub-domain partial-clear below.
template<typename T>
static typename QHash<QString, T>::iterator findPerScreenEntryMutable(QHash<QString, T>& hash,
                                                                      const QString& screenIdOrName)
{
    auto it = hash.find(screenIdOrName);
    if (it != hash.end())
        return it;
    if (PhosphorScreens::ScreenIdentity::isConnectorName(screenIdOrName)) {
        QString resolved = PhosphorScreens::ScreenIdentity::idForName(screenIdOrName);
        if (resolved != screenIdOrName) {
            it = hash.find(resolved);
            if (it != hash.end())
                return it;
        }
    }
    QString connector = PhosphorScreens::ScreenIdentity::nameForId(screenIdOrName);
    if (!connector.isEmpty() && connector != screenIdOrName) {
        it = hash.find(connector);
        if (it != hash.end())
            return it;
    }
    return hash.end();
}

// True if the screen's autotile override map holds any key in the requested
// sub-domain (gaps when wantGaps, otherwise the algorithm complement).
static bool hasPerScreenAutotileSubset(const QHash<QString, QVariantMap>& hash, const QString& screenIdOrName,
                                       bool wantGaps)
{
    auto it = findPerScreenEntry(hash, screenIdOrName);
    if (it == hash.constEnd())
        return false;
    for (auto k = it.value().constBegin(); k != it.value().constEnd(); ++k) {
        if (isPerScreenAutotileGapsKey(k.key()) == wantGaps)
            return true;
    }
    return false;
}

// Remove only the requested sub-domain's keys from the screen's autotile
// override map (gaps when clearGaps, otherwise the algorithm complement),
// dropping the whole entry once empty. Returns true if anything changed.
static bool clearPerScreenAutotileSubset(QHash<QString, QVariantMap>& hash, const QString& screenIdOrName,
                                         bool clearGaps)
{
    auto it = findPerScreenEntryMutable(hash, screenIdOrName);
    if (it == hash.end())
        return false;
    QVariantMap& overrides = it.value();
    bool changed = false;
    for (auto k = overrides.begin(); k != overrides.end();) {
        if (isPerScreenAutotileGapsKey(k.key()) == clearGaps) {
            k = overrides.erase(k);
            changed = true;
        } else {
            ++k;
        }
    }
    if (overrides.isEmpty())
        hash.erase(it);
    return changed;
}

// ── Per-Screen PhosphorZones::Zone Selector Config ──────────────────────────────────────────

ZoneSelectorConfig Settings::resolvedZoneSelectorConfig(const QString& screenIdOrName) const
{
    ZoneSelectorConfig config = {static_cast<int>(zoneSelectorPosition()),
                                 static_cast<int>(zoneSelectorLayoutMode()),
                                 static_cast<int>(zoneSelectorSizeMode()),
                                 zoneSelectorMaxRows(),
                                 zoneSelectorPreviewWidth(),
                                 zoneSelectorPreviewHeight(),
                                 zoneSelectorPreviewLockAspect(),
                                 zoneSelectorGridColumns(),
                                 zoneSelectorTriggerDistance()};

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
    const QString resolved = PhosphorScreens::ScreenIdentity::isConnectorName(screenIdOrName)
        ? PhosphorScreens::ScreenIdentity::idForName(screenIdOrName)
        : screenIdOrName;
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
        qCWarning(lcConfig) << "Rejected per-screen autotile setting:" << (key + QLatin1Char('=') + value.toString());
        return;
    }

    // Normalize to short form: strip "Autotile" prefix so the in-memory map
    // always uses short keys ("Algorithm", "SplitRatio") matching QML lookups.
    // Animation keys ("AnimationsEnabled", etc.) have no "Autotile" prefix.
    const QString normalizedKey = key.startsWith(QLatin1String("Autotile")) ? key.mid(8) : key;

    // Resolve to EDID-based screen ID so the key matches daemon lookups
    const QString resolved = PhosphorScreens::ScreenIdentity::isConnectorName(screenIdOrName)
        ? PhosphorScreens::ScreenIdentity::idForName(screenIdOrName)
        : screenIdOrName;
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

// Sub-domain accessors: the Gaps card and the Algorithm card share one
// per-screen autotile map but must report/reset only their own keys.

bool Settings::hasPerScreenAutotileGapsSettings(const QString& screenIdOrName) const
{
    return hasPerScreenAutotileSubset(m_perScreenAutotileSettings, screenIdOrName, /*wantGaps=*/true);
}

bool Settings::hasPerScreenAutotileAlgorithmSettings(const QString& screenIdOrName) const
{
    return hasPerScreenAutotileSubset(m_perScreenAutotileSettings, screenIdOrName, /*wantGaps=*/false);
}

void Settings::clearPerScreenAutotileGapsSettings(const QString& screenIdOrName)
{
    if (clearPerScreenAutotileSubset(m_perScreenAutotileSettings, screenIdOrName, /*clearGaps=*/true)) {
        Q_EMIT perScreenAutotileSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::clearPerScreenAutotileAlgorithmSettings(const QString& screenIdOrName)
{
    if (clearPerScreenAutotileSubset(m_perScreenAutotileSettings, screenIdOrName, /*clearGaps=*/false)) {
        Q_EMIT perScreenAutotileSettingsChanged();
        Q_EMIT settingsChanged();
    }
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
        qCWarning(lcConfig) << "Rejected per-screen snapping setting:" << (key + QLatin1Char('=') + value.toString());
        return;
    }

    // Resolve to EDID-based screen ID so the key matches daemon lookups
    const QString resolved = PhosphorScreens::ScreenIdentity::isConnectorName(screenIdOrName)
        ? PhosphorScreens::ScreenIdentity::idForName(screenIdOrName)
        : screenIdOrName;
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
