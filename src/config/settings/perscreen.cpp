// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../settings.h"
#include "../configbackends.h"
#include "../configdefaults.h"
#include "../../core/constants.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <QSet>
#include <QStringList>
#include <algorithm>

namespace PlasmaZones {

// Forward-declared so migrateConnectorNames (in the anonymous namespace below)
// can reuse the write-side key canonicalization, defined among the file-scope
// helpers further down. Load and write MUST use the same transform: gating on
// isConnectorName(wholeKey) would skip a virtual-suffixed connector key like
// "DP-2/vs:0" (isConnectorName rejects it for the ':' in "vs:0") on load, while
// a later write canonicalizes it to EDID form — leaving a stale duplicate.
static QString canonicalPerScreenKey(const QString& screenIdOrName);

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
    // Algorithm / easing-curve tokens are resolved (with a fallback) at the
    // daemon, so the registry isn't available here to validate them — but
    // reject an empty override outright, since a blank per-screen algorithm or
    // curve is never a meaningful override.
    if (k == PerScreenKeys::Algorithm || k == PerScreenKeys::AnimationEasingCurve)
        return value.toString().isEmpty() ? QVariant() : value;
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

// Gaps sub-domain of the per-screen snapping keys — the keys the Snapping →
// Window → Appearance "Gaps" card writes. The rest of kPerScreenSnappingKeys
// (SnapAssist / ZoneSelector) is a different concern; the card reports its
// override dot and clears its reset against ONLY these gap keys so a shared
// whole-domain clear can't wipe the map's other overrides (data loss on reset).
// Snapping per-screen keys are stored unprefixed, so this is a direct match.
bool isPerScreenSnappingGapsKey(const QString& key)
{
    namespace K = PerScreenSnappingKey;
    return key == K::ZonePadding || key == K::OuterGap || key == K::UsePerSideOuterGap || key == K::OuterGapTop
        || key == K::OuterGapBottom || key == K::OuterGapLeft || key == K::OuterGapRight;
}

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
    // Canonicalize connector-form keys to the same EDID form writes produce, so
    // there's no stale duplicate under the connector name. Process the affected
    // keys in SORTED order: when two connectors resolve to the same EDID (or a
    // connector resolves to a key already present in canonical form) the
    // collision is inherently lossy — only one override can keep the slot — so
    // make the tie-break deterministic (lexicographically-last source key wins)
    // rather than dependent on QHash iteration order, and warn on every
    // overwrite so the dropped override is surfaced.
    QStringList connectorKeys;
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
        if (canonicalPerScreenKey(it.key()) != it.key())
            connectorKeys.append(it.key());
    }
    std::sort(connectorKeys.begin(), connectorKeys.end());

    for (const QString& key : connectorKeys) {
        const QString canonical = canonicalPerScreenKey(key);
        const QVariantMap value = settings.take(key);
        if (settings.contains(canonical)) {
            qCWarning(lcConfig) << "EDID collision during per-screen migration:" << key << "resolves to" << canonical
                                << "which already exists - the later entry wins, the earlier override is dropped";
        }
        settings.insert(canonical, value);
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
    // Keys stored on disk WITHOUT the "Autotile" prefix (the animation keys).
    // Derived from the canonical key list rather than spelled out a second time,
    // so a new unprefixed key added to kPerScreenAutotileKeys can't desync this
    // expand step from normalizeAutotileKeys (which strips by prefix-presence).
    static const QSet<QString> unprefixedKeys = []() {
        QSet<QString> keys;
        for (const QLatin1String& k : kPerScreenAutotileKeys) {
            const QString s(k);
            if (!s.startsWith(QLatin1String("Autotile")))
                keys.insert(s);
        }
        return keys;
    }();

    QHash<QString, QVariantMap> expanded;
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
        QVariantMap expandedMap;
        for (auto kit = it.value().constBegin(); kit != it.value().constEnd(); ++kit) {
            const QString& key = kit.key();
            if (key.startsWith(QLatin1String("Autotile")) || unprefixedKeys.contains(key)) {
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

// Canonical storage-key form of a screen identifier: resolve a connector name
// (e.g. "DP-2") to its stable EDID id so the key matches daemon lookups,
// preserving any virtual "/vs:N" suffix — only the physical parent is
// translated, e.g. "DP-2/vs:0" → "Dell:U2722D:115107/vs:0". Identifiers already
// in id form (physical or virtual) pass through unchanged, as do connectors
// that don't currently resolve to a connected screen.
static QString canonicalPerScreenKey(const QString& screenIdOrName)
{
    namespace VS = PhosphorIdentity::VirtualScreenId;
    namespace SI = PhosphorScreens::ScreenIdentity;
    const QString physical = VS::extractPhysicalId(screenIdOrName);
    if (!SI::isConnectorName(physical))
        return screenIdOrName;
    const QString resolved = SI::idForName(physical);
    if (resolved == physical)
        return screenIdOrName;
    const int vsIndex = VS::extractIndex(screenIdOrName);
    return vsIndex >= 0 ? VS::make(resolved, vsIndex) : resolved;
}

// Ordered, de-duplicated storage-key forms an entry could be keyed under: the
// queried form first, then its connector ↔ EDID-id translation (with the
// virtual "/vs:N" suffix preserved). Lets a lookup find an entry stored under
// the alternate form regardless of which one was written.
static QStringList perScreenKeyVariants(const QString& screenIdOrName)
{
    namespace VS = PhosphorIdentity::VirtualScreenId;
    namespace SI = PhosphorScreens::ScreenIdentity;
    QStringList variants;
    if (screenIdOrName.isEmpty())
        return variants;
    variants.append(screenIdOrName);

    const QString physical = VS::extractPhysicalId(screenIdOrName);
    const int vsIndex = VS::extractIndex(screenIdOrName);
    // Translate the physical part to its alternate form and re-attach the
    // virtual suffix (connector→id when queried by connector, id→connector
    // otherwise) so reads match writes across both keying conventions.
    const QString altPhysical = SI::isConnectorName(physical) ? SI::idForName(physical) : SI::nameForId(physical);
    if (!altPhysical.isEmpty() && altPhysical != physical) {
        const QString form = vsIndex >= 0 ? VS::make(altPhysical, vsIndex) : altPhysical;
        if (!form.isEmpty() && !variants.contains(form))
            variants.append(form);
    }
    return variants;
}

template<typename T>
static typename QHash<QString, T>::const_iterator findPerScreenEntry(const QHash<QString, T>& hash,
                                                                     const QString& screenIdOrName)
{
    for (const QString& key : perScreenKeyVariants(screenIdOrName)) {
        auto it = hash.constFind(key);
        if (it != hash.constEnd())
            return it;
    }
    return hash.constEnd();
}

template<typename T>
static bool removePerScreenEntry(QHash<QString, T>& hash, const QString& screenIdOrName)
{
    for (const QString& key : perScreenKeyVariants(screenIdOrName)) {
        if (hash.remove(key))
            return true;
    }
    return false;
}

// Mutable sibling of findPerScreenEntry — same id/connector resolution, used by
// the autotile sub-domain partial-clear below.
template<typename T>
static typename QHash<QString, T>::iterator findPerScreenEntryMutable(QHash<QString, T>& hash,
                                                                      const QString& screenIdOrName)
{
    for (const QString& key : perScreenKeyVariants(screenIdOrName)) {
        auto it = hash.find(key);
        if (it != hash.end())
            return it;
    }
    return hash.end();
}

// True if the screen's override map holds any key in the requested sub-domain
// (gaps when wantGaps, otherwise the complement), classified by isGapsKey.
static bool hasPerScreenKeySubset(const QHash<QString, QVariantMap>& hash, const QString& screenIdOrName,
                                  bool (*isGapsKey)(const QString&), bool wantGaps)
{
    auto it = findPerScreenEntry(hash, screenIdOrName);
    if (it == hash.constEnd())
        return false;
    for (auto k = it.value().constBegin(); k != it.value().constEnd(); ++k) {
        if (isGapsKey(k.key()) == wantGaps)
            return true;
    }
    return false;
}

// Remove only the requested sub-domain's keys from the screen's override map
// (gaps when clearGaps, otherwise the complement), classified by isGapsKey,
// dropping the whole entry once empty. Returns true if anything changed.
static bool clearPerScreenKeySubset(QHash<QString, QVariantMap>& hash, const QString& screenIdOrName,
                                    bool (*isGapsKey)(const QString&), bool clearGaps)
{
    auto it = findPerScreenEntryMutable(hash, screenIdOrName);
    if (it == hash.end())
        return false;
    QVariantMap& overrides = it.value();
    bool changed = false;
    for (auto k = overrides.begin(); k != overrides.end();) {
        if (isGapsKey(k.key()) == clearGaps) {
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

    // Resolve to EDID-based screen ID so the key matches daemon lookups. Look
    // up before taking a mutable reference so a no-op write never default-
    // inserts an empty husk entry (which hasPerScreen*Settings would then read
    // as a phantom override).
    const QString resolved = canonicalPerScreenKey(screenIdOrName);
    const auto existing = m_perScreenZoneSelectorSettings.constFind(resolved);
    if (existing != m_perScreenZoneSelectorSettings.constEnd() && existing.value().value(key) == validated) {
        return;
    }
    m_perScreenZoneSelectorSettings[resolved][key] = validated;
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

    // Resolve to EDID-based screen ID so the key matches daemon lookups. Look
    // up before taking a mutable reference so a no-op write never default-
    // inserts an empty husk entry.
    const QString resolved = canonicalPerScreenKey(screenIdOrName);
    const auto existing = m_perScreenAutotileSettings.constFind(resolved);
    if (existing != m_perScreenAutotileSettings.constEnd() && existing.value().value(normalizedKey) == validated) {
        return;
    }
    m_perScreenAutotileSettings[resolved][normalizedKey] = validated;
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
    return hasPerScreenKeySubset(m_perScreenAutotileSettings, screenIdOrName, isPerScreenAutotileGapsKey,
                                 /*wantGaps=*/true);
}

bool Settings::hasPerScreenAutotileAlgorithmSettings(const QString& screenIdOrName) const
{
    return hasPerScreenKeySubset(m_perScreenAutotileSettings, screenIdOrName, isPerScreenAutotileGapsKey,
                                 /*wantGaps=*/false);
}

void Settings::clearPerScreenAutotileGapsSettings(const QString& screenIdOrName)
{
    if (clearPerScreenKeySubset(m_perScreenAutotileSettings, screenIdOrName, isPerScreenAutotileGapsKey,
                                /*clearGaps=*/true)) {
        Q_EMIT perScreenAutotileSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::clearPerScreenAutotileAlgorithmSettings(const QString& screenIdOrName)
{
    if (clearPerScreenKeySubset(m_perScreenAutotileSettings, screenIdOrName, isPerScreenAutotileGapsKey,
                                /*clearGaps=*/false)) {
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

    // Resolve to EDID-based screen ID so the key matches daemon lookups. Look
    // up before taking a mutable reference so a no-op write never default-
    // inserts an empty husk entry.
    const QString resolved = canonicalPerScreenKey(screenIdOrName);
    const auto existing = m_perScreenSnappingSettings.constFind(resolved);
    if (existing != m_perScreenSnappingSettings.constEnd() && existing.value().value(key) == validated) {
        return;
    }
    m_perScreenSnappingSettings[resolved][key] = validated;
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

// Gaps sub-domain accessors: the Gaps card reports/clears only the snapping
// gap keys, leaving the map's other (SnapAssist / ZoneSelector) overrides
// untouched — mirrors the autotile Gaps/Algorithm split.

bool Settings::hasPerScreenSnappingGapsSettings(const QString& screenIdOrName) const
{
    return hasPerScreenKeySubset(m_perScreenSnappingSettings, screenIdOrName, isPerScreenSnappingGapsKey,
                                 /*wantGaps=*/true);
}

void Settings::clearPerScreenSnappingGapsSettings(const QString& screenIdOrName)
{
    if (clearPerScreenKeySubset(m_perScreenSnappingSettings, screenIdOrName, isPerScreenSnappingGapsKey,
                                /*clearGaps=*/true)) {
        Q_EMIT perScreenSnappingSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

} // namespace PlasmaZones
