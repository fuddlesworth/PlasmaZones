// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../settings.h"
#include "../configbackends.h"
#include "../configdefaults.h"
#include "../../core/constants.h"
#include "../../core/logging.h"
#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleStore.h>
#include <QSet>
#include <QStringList>
#include <QUuid>
#include <algorithm>
#include <iterator>

namespace PlasmaZones {

// Settings::canonicalPerScreenKey (declared in settings.h, defined among the
// file-scope helpers further down) is the write-side key canonicalization that
// migrateConnectorNames (in the anonymous namespace below) reuses. Load and
// write MUST use the same transform: gating on isConnectorName(wholeKey) would
// skip a virtual-suffixed connector key like "DP-2/vs:0" (isConnectorName
// rejects it for the ':' in "vs:0") on load, while a later write canonicalizes
// it to EDID form — leaving a stale duplicate.

namespace {

// D-Bus boundary guards: per-screen values arrive as raw QVariants from the
// settings adaptor dispatch. QVariant::toInt()/toDouble() silently coerce a
// non-numeric payload to 0, which the validators would then accept or clamp
// as a real override (e.g. Position "garbage" -> 0 = TopLeft stored). These
// helpers reject non-convertible payloads with an invalid QVariant instead,
// matching the contract the enum-range validators already use.
QVariant boundedInt(const QVariant& value, int min, int max)
{
    bool ok = false;
    const int v = value.toInt(&ok);
    return ok ? QVariant(qBound(min, v, max)) : QVariant();
}

QVariant boundedDouble(const QVariant& value, double min, double max)
{
    bool ok = false;
    const double v = value.toDouble(&ok);
    return ok ? QVariant(qBound(min, v, max)) : QVariant();
}

/// Enum-range check over [0, max]; rejects non-numeric payloads.
QVariant enumInRange(const QVariant& value, int max)
{
    bool ok = false;
    const int v = value.toInt(&ok);
    return (ok && v >= 0 && v <= max) ? QVariant(v) : QVariant();
}

QVariant validatePerScreenValue(const QString& key, const QVariant& value)
{
    namespace K = ZoneSelectorConfigKey;
    if (key == QLatin1String(K::Position)) {
        return enumInRange(value, static_cast<int>(ZoneSelectorPosition::BottomRight));
    }
    if (key == QLatin1String(K::LayoutMode)) {
        return enumInRange(value, static_cast<int>(ZoneSelectorLayoutMode::Vertical));
    }
    if (key == QLatin1String(K::SizeMode)) {
        return enumInRange(value, static_cast<int>(ZoneSelectorSizeMode::Manual));
    }
    if (key == QLatin1String(K::MaxRows))
        return boundedInt(value, ConfigDefaults::maxRowsMin(), ConfigDefaults::maxRowsMax());
    if (key == QLatin1String(K::PreviewWidth))
        return boundedInt(value, ConfigDefaults::previewWidthMin(), ConfigDefaults::previewWidthMax());
    if (key == QLatin1String(K::PreviewHeight))
        return boundedInt(value, ConfigDefaults::previewHeightMin(), ConfigDefaults::previewHeightMax());
    if (key == QLatin1String(K::PreviewLockAspect))
        return QVariant(value.toBool());
    if (key == QLatin1String(K::GridColumns))
        return boundedInt(value, ConfigDefaults::gridColumnsMin(), ConfigDefaults::gridColumnsMax());
    if (key == QLatin1String(K::TriggerDistance))
        return boundedInt(value, ConfigDefaults::triggerDistanceMin(), ConfigDefaults::triggerDistanceMax());
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

// The per-screen autotile inner/outer gap keys are intentionally ABSENT: gaps
// are rule-backed (per-monitor gap Rules) and merged into the accessor's
// result by perScreenGapRuleOverrides, never stored in this per-screen map.
// SmartGaps stays — it is a tiling behaviour flag, not a gap dimension.
const QLatin1String kPerScreenAutotileKeys[] = {
    QLatin1String(PerScreenAutotileKey::Algorithm),
    QLatin1String(PerScreenAutotileKey::SplitRatio),
    QLatin1String(PerScreenAutotileKey::SplitRatioStep),
    QLatin1String(PerScreenAutotileKey::MasterCount),
    QLatin1String(PerScreenAutotileKey::FocusNewWindows),
    QLatin1String(PerScreenAutotileKey::SmartGaps),
    QLatin1String(PerScreenAutotileKey::MaxWindows),
    QLatin1String(PerScreenAutotileKey::InsertPosition),
    QLatin1String(PerScreenAutotileKey::FocusFollowsMouse),
    QLatin1String(PerScreenAutotileKey::RespectMinimumSize),
    QLatin1String(PerScreenAutotileKey::AnimationsEnabled),
    QLatin1String(PerScreenAutotileKey::AnimationDuration),
    QLatin1String(PerScreenAutotileKey::AnimationEasingCurve),
};

// Gaps sub-domain of the autotile per-screen keys. The inner/outer gap
// dimensions are rule-backed now, so SmartGaps is the only remaining
// per-screen gaps-card key. Everything else in kPerScreenAutotileKeys falls to
// the Tiling Algorithm card's sub-domain BY COMPLEMENT — including the
// Animation* keys, which currently have no settings card at all (only the
// D-Bus dispatch path can write them); they ride the Algorithm card's scope
// chip and reset until an animations card exists, at which point this binary
// set-and-complement classification needs a third explicit set. The cards live
// on disjoint key subsets so each card's scope chip reports its override dot
// and clears its reset against ONLY its own keys; a shared whole-domain clear
// would wipe the other card's per-monitor overrides (data loss on reset).
const QLatin1String kPerScreenAutotileGapsKeys[] = {
    QLatin1String(PerScreenAutotileKey::SmartGaps),
};

// The "Autotile" prefix distinguishing the prefixed disk form of autotile
// per-screen keys ("AutotileInnerGap") from the short in-memory form QML uses
// ("InnerGap"). Centralized so the strip/expand sites can't desync on the
// literal or its length.
constexpr QLatin1String kAutotilePrefix{"Autotile"};

// Strip the "Autotile" prefix to the short in-memory key form, returning the
// key unchanged when it carries no prefix (e.g. the unprefixed Animation keys).
QString stripAutotilePrefix(const QString& key)
{
    return key.startsWith(kAutotilePrefix) ? key.mid(kAutotilePrefix.size()) : key;
}

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
        for (const QLatin1String& k : kPerScreenAutotileGapsKeys)
            keys.insert(stripAutotilePrefix(QString(k)));
        return keys;
    }();
    return shortGapsKeys.contains(stripAutotilePrefix(key));
}

QVariant validatePerScreenAutotileValue(const QString& key, const QVariant& value)
{
    // Strip "Autotile" prefix so both "Algorithm" and "AutotileAlgorithm" match.
    // QML PerScreenOverrideHelper sends short keys; config storage uses prefixed keys.
    const QString k = stripAutotilePrefix(key);

    if (k == PerScreenKeys::SplitRatio) {
        return boundedDouble(value, ConfigDefaults::autotileSplitRatioMin(), ConfigDefaults::autotileSplitRatioMax());
    }
    if (k == PerScreenKeys::SplitRatioStep) {
        return boundedDouble(value, ConfigDefaults::autotileSplitRatioStepMin(),
                             ConfigDefaults::autotileSplitRatioStepMax());
    }
    if (k == PerScreenKeys::MasterCount)
        return boundedInt(value, ConfigDefaults::autotileMasterCountMin(), ConfigDefaults::autotileMasterCountMax());
    // Inner/outer gap dimensions are rule-backed (per-monitor gap Rules),
    // not per-screen Settings keys, so they are intentionally not validated here
    // — a write of one is rejected like any unknown key.
    if (k == PerScreenKeys::MaxWindows)
        return boundedInt(value, ConfigDefaults::autotileMaxWindowsMin(), ConfigDefaults::autotileMaxWindowsMax());
    if (k == PerScreenKeys::InsertPosition)
        return boundedInt(value, ConfigDefaults::autotileInsertPositionMin(),
                          ConfigDefaults::autotileInsertPositionMax());
    // Algorithm / easing-curve tokens are resolved (with a fallback) at the
    // daemon, so the registry isn't available here to validate them — but
    // reject an empty override outright, since a blank per-screen algorithm or
    // curve is never a meaningful override.
    if (k == PerScreenKeys::Algorithm || k == PerScreenKeys::AnimationEasingCurve)
        // Canonicalize to QString: the backend round-trips these via
        // writeString/readString, so a non-string payload accepted here
        // (e.g. an int over D-Bus) would change observable type across a
        // restart (int in the writing session, string after reload).
        return value.toString().isEmpty() ? QVariant() : QVariant(value.toString());
    if (k == PerScreenKeys::FocusNewWindows || k == PerScreenKeys::SmartGaps || k == PerScreenKeys::FocusFollowsMouse
        || k == PerScreenKeys::RespectMinimumSize || k == PerScreenKeys::AnimationsEnabled)
        return QVariant(value.toBool());
    if (k == PerScreenKeys::AnimationDuration)
        return boundedInt(value, ConfigDefaults::animationDurationMin(), ConfigDefaults::animationDurationMax());
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
    if (key == QLatin1String(PerScreenAutotileKey::FocusNewWindows))
        return QVariant(group.readBool(key, ConfigDefaults::autotileFocusNewWindows()));
    if (key == QLatin1String(PerScreenAutotileKey::SmartGaps))
        return QVariant(group.readBool(key, ConfigDefaults::autotileSmartGaps()));
    if (key == QLatin1String(PerScreenAutotileKey::FocusFollowsMouse))
        return QVariant(group.readBool(key, ConfigDefaults::autotileFocusFollowsMouse()));
    if (key == QLatin1String(PerScreenAutotileKey::RespectMinimumSize))
        return QVariant(group.readBool(key, ConfigDefaults::autotileRespectMinimumSize()));
    if (key == QLatin1String(PerScreenAutotileKey::AnimationsEnabled))
        return QVariant(group.readBool(key, ConfigDefaults::animationsEnabled()));
    return QVariant(group.readInt(key, 0));
}

// Per-screen snapping has no Settings-stored keys: snap-assist is global, the
// zone-selector keys live in their own per-screen map, and the gap dimensions
// (the only former per-screen snapping keys) are now rule-backed (per-monitor
// gap Rules), read by getPerScreenSnappingSettings via
// perScreenGapRuleOverrides. So there is no snapping load/save/validate path.

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
    // make the tie-break deterministic (the slot's FIRST writer wins: a
    // pre-existing canonical entry beats every connector, and among colliding
    // connectors the lexicographically-first migrates and later ones drop)
    // rather than dependent on QHash iteration order, and warn on every
    // collision so the dropped override is surfaced.
    QStringList connectorKeys;
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
        if (Settings::canonicalPerScreenKey(it.key()) != it.key())
            connectorKeys.append(it.key());
    }
    std::sort(connectorKeys.begin(), connectorKeys.end());

    for (const QString& key : connectorKeys) {
        const QString canonical = Settings::canonicalPerScreenKey(key);
        const QVariantMap value = settings.take(key);
        if (settings.contains(canonical)) {
            // The slot's existing entry wins — either a pre-existing
            // canonical-form entry (written by current-format code, the
            // fresher data) or an earlier-sorted connector's already-migrated
            // value. Drop this connector's value (already take()n above) and
            // surface the loss.
            qCWarning(lcConfig) << "EDID collision during per-screen migration:" << key << "resolves to" << canonical
                                << "which already exists - keeping the canonical entry, dropping the legacy override";
            continue;
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
        for (auto kit = it.value().constBegin(); kit != it.value().constEnd(); ++kit)
            normalized[stripAutotilePrefix(kit.key())] = kit.value();
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
    // No per-screen snapping group to load: snapping gaps are rule-backed.
    // Per-screen change signals are emitted by the caller (Settings::load()),
    // gated on a before/after comparison of each map — so they fire only when a
    // reload actually changed something, which the daemon relies on to avoid
    // resnapping on unrelated saves.
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
            if (!s.startsWith(kAutotilePrefix))
                keys.insert(s);
        }
        return keys;
    }();

    QHash<QString, QVariantMap> expanded;
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
        QVariantMap expandedMap;
        for (auto kit = it.value().constBegin(); kit != it.value().constEnd(); ++kit) {
            const QString& key = kit.key();
            if (key.startsWith(kAutotilePrefix) || unprefixedKeys.contains(key)) {
                expandedMap[key] = kit.value();
            } else {
                expandedMap[kAutotilePrefix + key] = kit.value();
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
    // No per-screen snapping group to save: snapping gaps are rule-backed.
}

// Canonical storage-key form of a screen identifier: resolve a connector name
// (e.g. "DP-2") to its stable EDID id so the key matches daemon lookups,
// preserving any virtual "/vs:N" suffix — only the physical parent is
// translated, e.g. "DP-2/vs:0" → "Dell:U2722D:115107/vs:0". Identifiers already
// in id form (physical or virtual) pass through unchanged, as do connectors
// that don't currently resolve to a connected screen.
QString Settings::canonicalPerScreenKey(const QString& screenIdOrName)
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
// the per-screen setter (applyPerScreenSetting) and the gaps/algorithm
// sub-domain partial-clears below.
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

// Store a single validated per-screen override under `key`. Matches any existing
// entry under either the connector or EDID id form and updates it in place, so a
// write never creates a duplicate under the alternate form and a no-op write
// never default-inserts an empty husk entry (which the hasPerScreen* readers
// would treat as a phantom override). New entries key by the canonical EDID
// form. Returns true if the map was mutated (caller emits the change signals).
static bool applyPerScreenSetting(QHash<QString, QVariantMap>& hash, const QString& screenIdOrName, const QString& key,
                                  const QVariant& validated)
{
    auto it = findPerScreenEntryMutable(hash, screenIdOrName);
    if (it != hash.end()) {
        if (it.value().value(key) == validated)
            return false;
        it.value()[key] = validated;
    } else {
        hash[Settings::canonicalPerScreenKey(screenIdOrName)][key] = validated;
    }
    return true;
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
    // Virtual-screen fallback: an override stored on the physical monitor must
    // still apply when the selector runs on one of its virtual sub-screens.
    // Resolve a physical-monitor override for a virtual sub-screen id, matching
    // the virtual->physical fallback the per-screen reads elsewhere perform.
    if (it == m_perScreenZoneSelectorSettings.constEnd()
        && PhosphorIdentity::VirtualScreenId::isVirtual(screenIdOrName)) {
        it = findPerScreenEntry(m_perScreenZoneSelectorSettings,
                                PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenIdOrName));
    }
    if (it == m_perScreenZoneSelectorSettings.constEnd()) {
        return config;
    }

    applyPerScreenOverrides(config, it.value());
    return config;
}

QVariantMap Settings::getPerScreenZoneSelectorSettings(const QString& screenIdOrName) const
{
    QVariantMap result;
    auto it = findPerScreenEntry(m_perScreenZoneSelectorSettings, screenIdOrName);
    if (it != m_perScreenZoneSelectorSettings.constEnd()) {
        result = it.value();
    }
    // Overlay the per-monitor zone-selector rule's values on top of the
    // config-store values; when no rule exists the keys stay absent and the
    // consumer falls back to the global zone-selector defaults.
    const QVariantMap ruleOverrides = perScreenZoneSelectorRuleOverrides(m_ruleStore, screenIdOrName);
    for (auto o = ruleOverrides.constBegin(); o != ruleOverrides.constEnd(); ++o) {
        result.insert(o.key(), o.value());
    }
    return result;
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

    if (applyPerScreenSetting(m_perScreenZoneSelectorSettings, screenIdOrName, key, validated)) {
        Q_EMIT perScreenZoneSelectorSettingsChanged();
        Q_EMIT settingsChanged();
    }
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

// ── Per-Screen Gap Rule Resolution (rule-backed gaps) ────────────────────────

namespace {
// Ordered, de-duplicated identifier forms the per-monitor gap rule could be
// keyed under, derived from an incoming screen identifier (connector name,
// stable id, or virtual id). Per-monitor gap rules are keyed by the stable EDID
// form: both the Appearance page (WindowAppearanceController::perScreenGapRuleId)
// and the v4→v5 migration derive the key via canonicalPerScreenKey, so a rule
// written by either lands under the same id. Resolve to that canonical form
// first, then fall back to the physical parent and the raw identifier so a rule
// written under an older/alternate form is still found.
QStringList perScreenGapKeyForms(const QString& screenIdOrName)
{
    namespace VS = PhosphorIdentity::VirtualScreenId;
    QStringList forms;
    if (screenIdOrName.isEmpty()) {
        return forms;
    }
    const auto add = [&forms](const QString& s) {
        if (!s.isEmpty() && !forms.contains(s)) {
            forms.append(s);
        }
    };
    // The canonical stable form is the keying convention; the raw identifier
    // covers a rule keyed exactly as queried, and the physical parent lets a
    // virtual sub-screen inherit the physical monitor's override.
    add(Settings::canonicalPerScreenKey(screenIdOrName));
    add(screenIdOrName);
    add(VS::extractPhysicalId(screenIdOrName));
    return forms;
}
} // namespace

// Gap overrides authored on the per-monitor gap Rule for @p screenIdOrName,
// keyed in the short engine form, or an empty map when no such rule exists.
QVariantMap Settings::perScreenGapRuleOverrides(const PhosphorRules::RuleStore* store, const QString& screenIdOrName)
{
    QVariantMap gaps;
    if (store == nullptr) {
        return gaps;
    }
    namespace AT = PhosphorRules::ActionType;
    namespace PSK = PhosphorEngine::PerScreenKeys;
    for (const QString& keyForm : perScreenGapKeyForms(screenIdOrName)) {
        const QUuid ruleId = QUuid::createUuidV5(ConfigDefaults::baselineGapRuleId(), keyForm.toUtf8());
        if (!store->ruleSet().ruleById(ruleId)) {
            continue;
        }
        // First matching rule wins. Read each gap action through the shared
        // gapValueFromRule helper, in the short key form the autotile / snapping
        // consumers expect.
        const auto readInt = [&](QLatin1StringView type, QLatin1String key) {
            if (const auto v = gapValueFromRule(store, ruleId, type)) {
                gaps.insert(QString(key), v->toInt());
            }
        };
        const auto readBool = [&](QLatin1StringView type, QLatin1String key) {
            if (const auto v = gapValueFromRule(store, ruleId, type)) {
                gaps.insert(QString(key), v->toBool());
            }
        };
        readInt(AT::SetInnerGap, PSK::InnerGap);
        readInt(AT::SetOuterGap, PSK::OuterGap);
        readBool(AT::SetUsePerSideOuterGap, PSK::UsePerSideOuterGap);
        readInt(AT::SetOuterGapTop, PSK::OuterGapTop);
        readInt(AT::SetOuterGapBottom, PSK::OuterGapBottom);
        readInt(AT::SetOuterGapLeft, PSK::OuterGapLeft);
        readInt(AT::SetOuterGapRight, PSK::OuterGapRight);
        return gaps;
    }
    return gaps;
}

// Tiling-geometry overrides authored on the per-monitor tiling Rule for
// @p screenIdOrName, keyed in the short engine form the autotile consumer
// expects (SplitRatio / MasterCount / MaxWindows), or an empty map when no
// such rule exists. Mirrors perScreenGapRuleOverrides: the per-screen rule is
// keyed by the screen's stable EDID id under createUuidV5(
// perScreenTilingRuleNamespaceId, <stable id>); the incoming identifier is
// resolved to that canonical form first, with the raw and physical-parent forms
// tried as fallbacks (perScreenGapKeyForms is screen-id-generic, not gap-specific).
QVariantMap Settings::perScreenTilingRuleOverrides(const PhosphorRules::RuleStore* store, const QString& screenIdOrName)
{
    QVariantMap tiling;
    if (store == nullptr) {
        return tiling;
    }
    namespace AT = PhosphorRules::ActionType;
    namespace PSK = PhosphorEngine::PerScreenKeys;
    for (const QString& keyForm : perScreenGapKeyForms(screenIdOrName)) {
        const QUuid ruleId = QUuid::createUuidV5(ConfigDefaults::perScreenTilingRuleNamespaceId(), keyForm.toUtf8());
        if (!store->ruleSet().ruleById(ruleId)) {
            continue;
        }
        // First matching rule wins. Split ratio is a [0.1, 0.9] fraction (double);
        // master count and max windows are integers. Read each action's value
        // through the shared gapValueFromRule helper (action-type-generic despite
        // its name — it returns any action's Value param).
        if (const auto v = gapValueFromRule(store, ruleId, AT::SetSplitRatio)) {
            tiling.insert(QString(PSK::SplitRatio), v->toDouble());
        }
        if (const auto v = gapValueFromRule(store, ruleId, AT::SetMasterCount)) {
            tiling.insert(QString(PSK::MasterCount), v->toInt());
        }
        if (const auto v = gapValueFromRule(store, ruleId, AT::SetMaxWindows)) {
            tiling.insert(QString(PSK::MaxWindows), v->toInt());
        }
        return tiling;
    }
    return tiling;
}

// Zone-selector overrides authored on the per-monitor zone-selector Rule for
// @p screenIdOrName, keyed by the ZoneSelectorConfigKey names the consumer
// expects. Mirrors perScreenTilingRuleOverrides, but the per-screen zone-selector
// rule carries the GENERIC SetZoneSelectorProperty action once per property, so
// the reader iterates the rule's actions rather than probing by action type. The
// property token IS the config-key name (1:1 by construction), used verbatim as
// the override-map key. PreviewLockAspect yields a bool, every other property an int.
QVariantMap Settings::perScreenZoneSelectorRuleOverrides(const PhosphorRules::RuleStore* store,
                                                         const QString& screenIdOrName)
{
    QVariantMap overrides;
    if (store == nullptr) {
        return overrides;
    }
    namespace AT = PhosphorRules::ActionType;
    namespace AP = PhosphorRules::ActionParam;
    for (const QString& keyForm : perScreenGapKeyForms(screenIdOrName)) {
        const QUuid ruleId =
            QUuid::createUuidV5(ConfigDefaults::perScreenZoneSelectorRuleNamespaceId(), keyForm.toUtf8());
        const auto rule = store->ruleSet().ruleById(ruleId);
        if (!rule) {
            continue;
        }
        // First matching rule wins.
        for (const PhosphorRules::RuleAction& a : rule->actions) {
            if (a.type != QString(AT::SetZoneSelectorProperty)) {
                continue;
            }
            const QString property = a.params.value(QString(AP::Property)).toString();
            if (property.isEmpty()) {
                continue;
            }
            const QJsonValue value = a.params.value(QString(AP::Value));
            if (property == QLatin1String(ZoneSelectorConfigKey::PreviewLockAspect)) {
                overrides.insert(property, value.toBool());
            } else {
                overrides.insert(property, value.toInt());
            }
        }
        return overrides;
    }
    return overrides;
}

// ── Per-Screen Autotile Config ───────────────────────────────────────────────

QVariantMap Settings::getPerScreenAutotileSettings(const QString& screenIdOrName) const
{
    QVariantMap result;
    auto it = findPerScreenEntry(m_perScreenAutotileSettings, screenIdOrName);
    if (it != m_perScreenAutotileSettings.constEnd()) {
        result = it.value();
    }
    // Override the gap keys with the per-monitor gap rule's values, if one
    // exists for this screen. When none exists the gap keys stay absent and the
    // consumer falls back to the global (baseline) gaps.
    const QVariantMap ruleGaps = perScreenGapRuleOverrides(m_ruleStore, screenIdOrName);
    for (auto g = ruleGaps.constBegin(); g != ruleGaps.constEnd(); ++g) {
        result.insert(g.key(), g.value());
    }
    // Override split ratio / master count / max windows with the per-monitor
    // tiling rule's values, if one exists. These moved from the per-screen config
    // store onto rules in v5 (the same treatment gaps received); when no rule
    // exists the keys stay absent and the consumer falls back to the global
    // autotile defaults.
    const QVariantMap ruleTiling = perScreenTilingRuleOverrides(m_ruleStore, screenIdOrName);
    for (auto t = ruleTiling.constBegin(); t != ruleTiling.constEnd(); ++t) {
        result.insert(t.key(), t.value());
    }
    return result;
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
    const QString normalizedKey = stripAutotilePrefix(key);

    if (applyPerScreenSetting(m_perScreenAutotileSettings, screenIdOrName, normalizedKey, validated)) {
        Q_EMIT perScreenAutotileSettingsChanged();
        Q_EMIT settingsChanged();
    }
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

// Snapping per-screen gaps are entirely rule-backed: there is no Settings
// storage, only this reader, which surfaces the per-monitor gap rule's values
// (or an empty map, in which case the snap geometry path falls back to the
// layout / global gaps). The set/clear/has triplet stays as ISettings no-op
// defaults — writes go through the window-rule store via the Appearance page.
QVariantMap Settings::getPerScreenSnappingSettings(const QString& screenIdOrName) const
{
    return perScreenGapRuleOverrides(m_ruleStore, screenIdOrName);
}

} // namespace PlasmaZones
