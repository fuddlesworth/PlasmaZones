// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config/settings.h"
#include "config/settings/perscreen_detail.h"
#include "config/settings/settings_detail.h"
#include "config/configbackends.h"
#include "config/configdefaults.h"
#include "core/types/constants.h"
#include "core/platform/logging.h"
#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/ScreenIdentity.h>
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
using PerScreenDetail::boundedInt;

QVariant boundedDouble(const QVariant& value, double min, double max)
{
    bool ok = false;
    const double v = value.toDouble(&ok);
    return ok ? QVariant(qBound(min, v, max)) : QVariant();
}

using PerScreenDetail::enumInRange;

/// Closed-set check for enums whose legal values are not a contiguous range
/// from 0 — the ConfigDefaults isValid* predicates. The ok-check matters as
/// much as the predicate: every one of those sets contains 0, so a bare
/// toInt() would turn a non-numeric payload into a legal-looking override.
template<typename Predicate>
QVariant closedSetInt(const QVariant& value, Predicate isValid)
{
    bool ok = false;
    const int v = value.toInt(&ok);
    return (ok && isValid(v)) ? QVariant(v) : QVariant();
}

// The two zone-selector families' key tables, validators and accessors live in
// perscreen_selector.cpp; perscreen_detail.h declares what the load/save
// passes below need from them.

// The per-screen inner/outer gap dimensions live in THIS store (config-backed,
// unified — one value per monitor drives both snap and tile). SmartGaps stays
// too, but it is a tiling behaviour flag, not a gap dimension, so it sits in its
// own sub-domain (kPerScreenAutotileGapsKeys) disjoint from the gap dimensions
// (kPerScreenGapDimensionKeys).
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
    QLatin1String(PerScreenAutotileKey::InnerGap),
    QLatin1String(PerScreenAutotileKey::OuterGap),
    QLatin1String(PerScreenAutotileKey::UsePerSideOuterGap),
    QLatin1String(PerScreenAutotileKey::OuterGapTop),
    QLatin1String(PerScreenAutotileKey::OuterGapBottom),
    QLatin1String(PerScreenAutotileKey::OuterGapLeft),
    QLatin1String(PerScreenAutotileKey::OuterGapRight),
};

// Three disjoint sub-domains split kPerScreenAutotileKeys so each settings card's
// scope chip reports its override dot and clears its reset against ONLY its own
// keys; a shared whole-domain clear would wipe another card's per-monitor
// overrides (data loss on reset):
//   1. kPerScreenAutotileGapsKeys (SmartGaps) — the Tiling → Window smart-gaps row.
//   2. kPerScreenGapDimensionKeys (inner/outer gap dimensions) — the Windows
//      appearance page's Gaps card, config-backed per monitor.
//   3. Everything else (algorithm + behaviour + Animation* keys) — the Tiling
//      Algorithm card, classified BY COMPLEMENT (isPerScreenAutotileAlgorithmKey).
//      The Animation* keys have no card of their own yet, so they ride the
//      Algorithm card's scope chip and reset until an animations card exists.
const QLatin1String kPerScreenAutotileGapsKeys[] = {
    QLatin1String(PerScreenAutotileKey::SmartGaps),
};

// The inner/outer gap dimensions sub-domain (the Windows appearance page Gaps
// card). Kept DISJOINT from kPerScreenAutotileGapsKeys (SmartGaps) so the two
// cards' scope chips never clear each other's per-monitor overrides.
const QLatin1String kPerScreenGapDimensionKeys[] = {
    QLatin1String(PerScreenAutotileKey::InnerGap),           QLatin1String(PerScreenAutotileKey::OuterGap),
    QLatin1String(PerScreenAutotileKey::UsePerSideOuterGap), QLatin1String(PerScreenAutotileKey::OuterGapTop),
    QLatin1String(PerScreenAutotileKey::OuterGapBottom),     QLatin1String(PerScreenAutotileKey::OuterGapLeft),
    QLatin1String(PerScreenAutotileKey::OuterGapRight),
};

// The "Autotile" prefix distinguishing the prefixed disk form of autotile
// per-screen keys ("AutotileInnerGap") from the short in-memory form QML uses
// ("InnerGap"). Centralized so the strip/expand sites can't desync on the
// literal or its length.
constexpr QLatin1String kAutotilePrefix{"Autotile"};

// Per-screen scrolling override keys. NO prefix asymmetry (the autotile
// "Autotile" prefix exists only for v4-migration history): disk, memory, QML,
// and the engine's ScrollPerScreenKeys settings channel all use one spelling,
// so the daemon merge is a plain copy.
//
// TWO disjoint sub-domains: the New-columns card's sizing defaults (the
// analogue of the tiling Algorithm card's per-monitor tuning) and the strip
// axis. They are split by isPerScreenScrollingSizingKey /
// isPerScreenScrollingAxisKey below so one card's scope chip cannot report or
// clear the other's override — the whole-domain accessors remain the D-Bus
// category surface, not a card's chip surface.
//
// Scrolling's other behavior/view settings stay app-wide like their
// tiling/snapping siblings; per-context variants of those are the rule
// actions' job (SetCenterFocusedColumn / SetScrollInsertPosition), which write
// the engine's RULE channel, not this store.
const QLatin1String kPerScreenScrollingKeys[] = {
    QLatin1String(PerScreenScrollingKey::DefaultColumnWidthKind),
    QLatin1String(PerScreenScrollingKey::DefaultColumnWidthValue),
    QLatin1String(PerScreenScrollingKey::DefaultColumnWidthPresetIndex),
    QLatin1String(PerScreenScrollingKey::DefaultColumnDisplay),
    QLatin1String(PerScreenScrollingKey::DefaultWindowHeightKind),
    QLatin1String(PerScreenScrollingKey::DefaultWindowHeightValue),
    QLatin1String(PerScreenScrollingKey::DefaultWindowHeightPresetIndex),
    QLatin1String(PerScreenScrollingKey::StripAxis),
};

static_assert(std::size(kPerScreenScrollingKeys) == 8,
              "kPerScreenScrollingKeys changed size — the validate ladder, the read ladder, and the engine's "
              "ScrollPerScreenKeys channel each carry one arm per key and have to grow with it");

// The strip axis is its own sub-domain: it is an orientation intent, not a
// sizing default, and it is surfaced by a different card on a different page.
bool isPerScreenScrollingAxisKey(const QString& key)
{
    return key == QLatin1String(PerScreenScrollingKey::StripAxis);
}

// The New-columns card's sizing keys, as the complement — the same shape as
// the autotile store's isPerScreenAutotileAlgorithmKey. Being the complement
// makes the two sub-domains exhaustive and disjoint by construction, but it
// also means a NEW key defaults to SIZING silently. The thing that actually
// catches a new key is the static_assert on kPerScreenScrollingKeys' size
// above: adding one fails the build until its sub-domain is decided here.
bool isPerScreenScrollingSizingKey(const QString& key)
{
    return !isPerScreenScrollingAxisKey(key);
}

// Repair an inconsistent per-screen {Kind, Value} width pair, the way
// Settings::normalizeScrollingColumnWidthValue repairs the global one.
//
// The value key's schema-level bound spans both kinds (one key, one
// validator), and validatePerScreenScrollingValue applies that same
// kind-spanning union clamp — so a pair like {Kind=Fixed, Value=0.5} passes
// validation on both the write and the load path and reaches the daemon's
// plain-copy merge intact, opening every column on that monitor one pixel
// wide. The inverse pair opens them at 100%.
//
// A VALUE-only override (no per-screen Kind key) is left untouched: the
// engine gates the whole per-screen width channel on the Kind key's presence
// (ScrollEngine::effectiveDefaultColumnWidth returns the global width
// wholesale when the per-screen Kind is absent), so such a value never
// applies — and repairing it against the GLOBAL kind would destroy the
// retained figure the engine would use the moment the user re-adds a
// per-screen kind.
//
// Returns whether @p overrides was modified.
bool repairPerScreenScrollingWidth(QVariantMap& overrides)
{
    namespace K = PerScreenScrollingKey;
    auto valueIt = overrides.find(QString(QLatin1String(K::DefaultColumnWidthValue)));
    if (valueIt == overrides.end()) {
        return false;
    }
    const auto kindIt = overrides.constFind(QString(QLatin1String(K::DefaultColumnWidthKind)));
    if (kindIt == overrides.constEnd()) {
        return false;
    }
    const int kind = kindIt->toInt();
    const qreal stored = valueIt->toDouble();
    // ClientDecides and Preset are returned untouched by the shared helper —
    // neither owns the value, so neither can make the pair inconsistent.
    const qreal coerced = settings_detail::reseedColumnWidthForKind(stored, kind);
    if (qFuzzyCompare(1.0 + stored, 1.0 + coerced)) {
        return false;
    }
    qCWarning(lcConfig) << "scrolling: per-screen column width" << stored << "is out of range for kind" << kind
                        << "— using" << coerced;
    *valueIt = coerced;
    return true;
}

QVariant validatePerScreenScrollingValue(const QString& key, const QVariant& value)
{
    namespace K = PerScreenScrollingKey;
    if (key == QLatin1String(K::DefaultColumnWidthKind)) {
        return closedSetInt(value, ConfigDefaults::isValidScrollingWidthKind);
    }
    if (key == QLatin1String(K::DefaultColumnWidthValue)) {
        // Kind-spanning clamp, mirroring the global schema entry: the shared
        // value key serves proportion and fixed, so bound by the union.
        return boundedDouble(value, ConfigDefaults::scrollingDefaultColumnWidthProportionMin(),
                             ConfigDefaults::scrollingDefaultColumnWidthFixedMax());
    }
    if (key == QLatin1String(K::DefaultColumnWidthPresetIndex)
        || key == QLatin1String(K::DefaultWindowHeightPresetIndex)) {
        return boundedInt(value, 0, ConfigDefaults::scrollingPresetIndexMax());
    }
    if (key == QLatin1String(K::DefaultColumnDisplay)) {
        return closedSetInt(value, ConfigDefaults::isValidScrollingColumnDisplay);
    }
    if (key == QLatin1String(K::DefaultWindowHeightKind)) {
        return closedSetInt(value, ConfigDefaults::isValidScrollingHeightKind);
    }
    if (key == QLatin1String(K::DefaultWindowHeightValue)) {
        return boundedDouble(value, ConfigDefaults::scrollingDefaultWindowHeightMin(),
                             ConfigDefaults::scrollingDefaultWindowHeightMax());
    }
    if (key == QLatin1String(K::StripAxis)) {
        return closedSetInt(value, ConfigDefaults::isValidScrollingStripAxis);
    }
    return QVariant();
}

QVariant readPerScreenScrollingEntry(PhosphorConfig::IGroup& group, const QString& key)
{
    namespace K = PerScreenScrollingKey;
    if (key == QLatin1String(K::DefaultColumnWidthKind))
        return QVariant(group.readInt(key, ConfigDefaults::scrollingDefaultColumnWidthKind()));
    if (key == QLatin1String(K::DefaultColumnWidthValue))
        return QVariant(group.readDouble(key, ConfigDefaults::scrollingDefaultColumnWidthValue()));
    if (key == QLatin1String(K::DefaultColumnWidthPresetIndex))
        return QVariant(group.readInt(key, ConfigDefaults::scrollingDefaultColumnWidthPresetIndex()));
    if (key == QLatin1String(K::DefaultColumnDisplay))
        return QVariant(group.readInt(key, ConfigDefaults::scrollingDefaultColumnDisplay()));
    if (key == QLatin1String(K::DefaultWindowHeightKind))
        return QVariant(group.readInt(key, ConfigDefaults::scrollingDefaultWindowHeightKind()));
    if (key == QLatin1String(K::DefaultWindowHeightValue))
        return QVariant(group.readDouble(key, ConfigDefaults::scrollingDefaultWindowHeightValue()));
    if (key == QLatin1String(K::DefaultWindowHeightPresetIndex))
        return QVariant(group.readInt(key, ConfigDefaults::scrollingDefaultWindowHeightPresetIndex()));
    if (key == QLatin1String(K::StripAxis))
        return QVariant(group.readInt(key, ConfigDefaults::scrollingStripAxis()));
    return QVariant();
}

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

// True for the inner/outer gap dimension keys (the Windows appearance Gaps
// card's sub-domain). Compares on the short in-memory form, mirroring
// isPerScreenAutotileGapsKey — kPerScreenGapDimensionKeys holds the prefixed disk
// form, so strip both sides.
bool isPerScreenGapDimensionKey(const QString& key)
{
    static const QSet<QString> shortGapDimensionKeys = []() {
        QSet<QString> keys;
        for (const QLatin1String& k : kPerScreenGapDimensionKeys)
            keys.insert(stripAutotilePrefix(QString(k)));
        return keys;
    }();
    return shortGapDimensionKeys.contains(stripAutotilePrefix(key));
}

// The Algorithm card's sub-domain is the COMPLEMENT of the two gap sub-domains:
// a per-screen autotile key that is neither a SmartGaps flag nor a gap
// dimension. Keeping this an explicit predicate (rather than "not gaps") lets the
// three cards stay disjoint as keys are added.
bool isPerScreenAutotileAlgorithmKey(const QString& key)
{
    return !isPerScreenAutotileGapsKey(key) && !isPerScreenGapDimensionKey(key);
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
    // Inner/outer gap dimensions are config-backed per monitor (unified — one
    // value drives both snap and tile), clamped to the shared gap ranges.
    if (k == PerScreenKeys::InnerGap)
        return boundedInt(value, ConfigDefaults::innerGapMin(), ConfigDefaults::innerGapMax());
    if (k == PerScreenKeys::OuterGap || k == PerScreenKeys::OuterGapTop || k == PerScreenKeys::OuterGapBottom
        || k == PerScreenKeys::OuterGapLeft || k == PerScreenKeys::OuterGapRight)
        return boundedInt(value, ConfigDefaults::outerGapMin(), ConfigDefaults::outerGapMax());
    if (k == PerScreenKeys::UsePerSideOuterGap)
        // Type-gated like the numeric arms (see the strip selector's
        // PreviewLockAspect for the rationale).
        return value.typeId() == QMetaType::Bool ? QVariant(value.toBool()) : QVariant();
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
        return value.typeId() == QMetaType::Bool ? QVariant(value.toBool()) : QVariant();
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
    if (key == QLatin1String(PerScreenAutotileKey::UsePerSideOuterGap))
        return QVariant(group.readBool(key, ConfigDefaults::usePerSideOuterGap()));
    if (key == QLatin1String(PerScreenAutotileKey::InnerGap))
        return QVariant(group.readInt(key, ConfigDefaults::innerGap()));
    if (key == QLatin1String(PerScreenAutotileKey::OuterGap))
        return QVariant(group.readInt(key, ConfigDefaults::outerGap()));
    if (key == QLatin1String(PerScreenAutotileKey::OuterGapTop))
        return QVariant(group.readInt(key, ConfigDefaults::outerGapTop()));
    if (key == QLatin1String(PerScreenAutotileKey::OuterGapBottom))
        return QVariant(group.readInt(key, ConfigDefaults::outerGapBottom()));
    if (key == QLatin1String(PerScreenAutotileKey::OuterGapLeft))
        return QVariant(group.readInt(key, ConfigDefaults::outerGapLeft()));
    if (key == QLatin1String(PerScreenAutotileKey::OuterGapRight))
        return QVariant(group.readInt(key, ConfigDefaults::outerGapRight()));
    return QVariant(group.readInt(key, 0));
}

// Per-screen snapping has no dedicated Settings-stored group: snap-assist is
// global, the zone-selector keys live in their own per-screen map, and the gap
// dimensions are stored ONCE in the per-screen autotile store (unified — one
// value per monitor drives both snap and tile). getPerScreenSnappingSettings
// surfaces that gap subset via perScreenGapOverrides. So there is no separate
// snapping load/save/validate path.

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
    loadPerScreenGroup(backend, allGroups, ConfigDefaults::zoneSelectorGroupPrefix(),
                       PerScreenDetail::kZoneSelectorKeys, std::size(PerScreenDetail::kZoneSelectorKeys),
                       PerScreenDetail::readZoneSelectorEntry, PerScreenDetail::validateZoneSelectorValue,
                       m_perScreenZoneSelectorSettings);
    loadPerScreenGroup(backend, allGroups, ConfigDefaults::scrollingZoneSelectorGroupPrefix(),
                       PerScreenDetail::kStripSelectorKeys, std::size(PerScreenDetail::kStripSelectorKeys),
                       PerScreenDetail::readZoneSelectorEntry, PerScreenDetail::validateStripSelectorValue,
                       m_perScreenScrollingZoneSelectorSettings);
    loadPerScreenGroup(backend, allGroups, ConfigDefaults::autotileScreenGroupPrefix(), kPerScreenAutotileKeys,
                       std::size(kPerScreenAutotileKeys), readPerScreenAutotileEntry, validatePerScreenAutotileValue,
                       m_perScreenAutotileSettings);
    // Normalize autotile keys from disk format ("AutotileAlgorithm") to short format
    // ("Algorithm") that QML uses for lookup via PerScreenOverrideHelper.settingValue().
    normalizeAutotileKeys(m_perScreenAutotileSettings);
    // Scrolling keys carry no prefix asymmetry: disk form IS the in-memory
    // form, so no normalize/expand pair.
    loadPerScreenGroup(backend, allGroups, ConfigDefaults::scrollingScreenGroupPrefix(), kPerScreenScrollingKeys,
                       std::size(kPerScreenScrollingKeys), readPerScreenScrollingEntry, validatePerScreenScrollingValue,
                       m_perScreenScrollingSettings);
    // The width pair's per-key validation cannot see the pair, so a
    // {Kind, Value} combination that is individually legal but jointly
    // impossible survives it — from a hand edit, a config import, or a staged
    // profile blob. Repair it here, the load-path twin of the kind-aware
    // re-seed setPerScreenScrollingSetting applies on a kind write. Screens
    // overriding the value WITHOUT a kind are skipped inside the helper (the
    // engine ignores such a value entirely).
    for (auto it = m_perScreenScrollingSettings.begin(); it != m_perScreenScrollingSettings.end(); ++it) {
        repairPerScreenScrollingWidth(it.value());
    }
    // No separate per-screen snapping group to load: per-monitor gaps are unified
    // and live in the per-screen autotile store loaded above.
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
    savePerScreenOverrides(backend, ConfigDefaults::scrollingZoneSelectorGroupPrefix(),
                           m_perScreenScrollingZoneSelectorSettings);
    // Expand short keys back to disk format before saving
    savePerScreenOverrides(backend, ConfigDefaults::autotileScreenGroupPrefix(),
                           expandAutotileKeys(m_perScreenAutotileSettings));
    savePerScreenOverrides(backend, ConfigDefaults::scrollingScreenGroupPrefix(), m_perScreenScrollingSettings);
    // No separate per-screen snapping group to save: per-monitor gaps are unified
    // and persisted in the per-screen autotile store above.
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

QString PerScreenDetail::canonicalWriteKey(const QString& screenIdOrName)
{
    return Settings::canonicalPerScreenKey(screenIdOrName);
}

// Ordered, de-duplicated storage-key forms an entry could be keyed under: the
// queried form first, then its connector ↔ EDID-id translation (with the
// virtual "/vs:N" suffix preserved). Lets a lookup find an entry stored under
// the alternate form regardless of which one was written. Declared in
// perscreen_detail.h and defined here, next to canonicalPerScreenKey, which it
// must stay in lockstep with.
QStringList PerScreenDetail::perScreenKeyVariants(const QString& screenIdOrName)
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

using PerScreenDetail::applyPerScreenSetting;
using PerScreenDetail::clearPerScreenKeySubset;
using PerScreenDetail::findPerScreenEntry;
using PerScreenDetail::findPerScreenEntryMutable;
using PerScreenDetail::hasPerScreenKeySubset;
using PerScreenDetail::removePerScreenEntry;

// ── Per-Screen Gap Overrides (config-backed) ─────────────────────────────────

// The inner/outer gap dimensions authored for @p screenIdOrName, keyed in the
// short engine form (InnerGap / OuterGap / UsePerSideOuterGap / OuterGap
// {Top,Bottom,Left,Right}) — the SAME key strings contextGapOverrideMap
// produces, so the cascade merge is a plain QVariantMap union. Empty when the
// screen has no gap override. Gaps live in the per-screen autotile store
// (unified — one value per monitor drives both snap and tile); this filters that
// store's entry down to the gap-dimension sub-domain.
QVariantMap Settings::perScreenGapOverrides(const QString& screenIdOrName) const
{
    QVariantMap gaps;
    auto it = findPerScreenEntry(m_perScreenAutotileSettings, screenIdOrName);
    // Virtual-screen fallback: a gap override stored on the physical monitor must
    // still apply when queried with one of its virtual sub-screen ids, matching
    // resolvedZoneSelectorConfig and the geometry consumers' explicit fallback.
    if (it == m_perScreenAutotileSettings.constEnd() && PhosphorIdentity::VirtualScreenId::isVirtual(screenIdOrName)) {
        it = findPerScreenEntry(m_perScreenAutotileSettings,
                                PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenIdOrName));
    }
    if (it == m_perScreenAutotileSettings.constEnd()) {
        return gaps;
    }
    for (auto e = it.value().constBegin(); e != it.value().constEnd(); ++e) {
        if (isPerScreenGapDimensionKey(e.key())) {
            gaps.insert(e.key(), e.value());
        }
    }
    return gaps;
}

bool Settings::hasPerScreenGapOverride(const QString& screenIdOrName) const
{
    return hasPerScreenKeySubset(m_perScreenAutotileSettings, screenIdOrName, isPerScreenGapDimensionKey,
                                 /*wantMatch=*/true);
}

bool Settings::perScreenGapDimensionsDiffer(const QHash<QString, QVariantMap>& before,
                                            const QHash<QString, QVariantMap>& after)
{
    const auto project = [](const QHash<QString, QVariantMap>& src) {
        QHash<QString, QVariantMap> out;
        for (auto s = src.constBegin(); s != src.constEnd(); ++s) {
            QVariantMap gaps;
            for (auto e = s.value().constBegin(); e != s.value().constEnd(); ++e) {
                if (isPerScreenGapDimensionKey(e.key())) {
                    gaps.insert(e.key(), e.value());
                }
            }
            if (!gaps.isEmpty()) {
                out.insert(s.key(), gaps);
            }
        }
        return out;
    };
    return project(before) != project(after);
}

void Settings::clearPerScreenGapOverride(const QString& screenIdOrName)
{
    if (clearPerScreenKeySubset(m_perScreenAutotileSettings, screenIdOrName, isPerScreenGapDimensionKey,
                                /*clearMatch=*/true)) {
        Q_EMIT perScreenAutotileSettingsChanged();
        // Clearing a per-monitor gap override is a gap-dimension change, so fire
        // the gap-resnap trigger too (see setPerScreenAutotileSetting).
        Q_EMIT perScreenSnappingSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

// ── Per-Screen Autotile Config ───────────────────────────────────────────────

QVariantMap Settings::getPerScreenAutotileSettings(const QString& screenIdOrName) const
{
    auto it = findPerScreenEntry(m_perScreenAutotileSettings, screenIdOrName);
    // The store now holds the gap dimensions too (unified per-monitor gaps), so
    // no rule overlay: the entry already carries every per-screen autotile key.
    return (it != m_perScreenAutotileSettings.constEnd()) ? it.value() : QVariantMap();
}

void Settings::setPerScreenAutotileSetting(const QString& screenIdOrName, const QString& key, const QVariant& value)
{
    if (screenIdOrName.isEmpty() || key.isEmpty()) {
        return;
    }

    QVariant validated = validatePerScreenAutotileValue(key, value);
    if (!validated.isValid()) {
        // DEBUG, and the value is not echoed. Both the key and the value arrive from
        // whoever called setPerScreenSetting on the session bus, so a warning here let any
        // process fill the daemon's log with content of its own choosing, unbounded in
        // length and rate. The adaptor's own logs were demoted for exactly this reason and
        // the attacker-controlled strings simply reached a warning one frame deeper.
        qCDebug(lcConfig) << "Rejected per-screen autotile setting" << key;
        return;
    }

    // Normalize to short form: strip "Autotile" prefix so the in-memory map
    // always uses short keys ("Algorithm", "SplitRatio") matching QML lookups.
    // Animation keys ("AnimationsEnabled", etc.) have no "Autotile" prefix.
    const QString normalizedKey = stripAutotilePrefix(key);

    if (applyPerScreenSetting(m_perScreenAutotileSettings, screenIdOrName, normalizedKey, validated)) {
        Q_EMIT perScreenAutotileSettingsChanged();
        // A per-monitor gap-dimension change must fire the gap-resnap trigger the
        // daemon binds (scheduleGapResnap), the same way the config-reload load()
        // path and the context-gap-rule path do. Gate on the gap-dimension subset
        // so an algorithm / split edit doesn't trigger a spurious gap resnap.
        if (isPerScreenGapDimensionKey(normalizedKey)) {
            Q_EMIT perScreenSnappingSettingsChanged();
        }
        Q_EMIT settingsChanged();
    }
}

void Settings::clearPerScreenAutotileSettings(const QString& screenIdOrName)
{
    // Removing the whole entry also drops any gap dimensions it held, so note that
    // before the erase to fire the gap-resnap trigger in parity with
    // clearPerScreenGapOverride / the gap-dimension write path.
    const bool hadGaps = hasPerScreenKeySubset(m_perScreenAutotileSettings, screenIdOrName, isPerScreenGapDimensionKey,
                                               /*wantMatch=*/true);
    if (removePerScreenEntry(m_perScreenAutotileSettings, screenIdOrName)) {
        Q_EMIT perScreenAutotileSettingsChanged();
        if (hadGaps) {
            Q_EMIT perScreenSnappingSettingsChanged();
        }
        Q_EMIT settingsChanged();
    }
}

bool Settings::hasPerScreenAutotileSettings(const QString& screenIdOrName) const
{
    return findPerScreenEntry(m_perScreenAutotileSettings, screenIdOrName) != m_perScreenAutotileSettings.constEnd();
}

// Algorithm sub-domain accessor. The per-screen autotile map holds three disjoint
// key groups: the gap dimensions (isPerScreenGapDimensionKey, handled by the
// perScreenGap* accessors above), SmartGaps (isPerScreenAutotileGapsKey, which
// rides the map but has no per-screen card of its own), and the Algorithm card's
// keys (the complement of both). The Algorithm card reports/resets only its subset.

bool Settings::hasPerScreenAutotileAlgorithmSettings(const QString& screenIdOrName) const
{
    return hasPerScreenKeySubset(m_perScreenAutotileSettings, screenIdOrName, isPerScreenAutotileAlgorithmKey,
                                 /*wantMatch=*/true);
}

void Settings::clearPerScreenAutotileAlgorithmSettings(const QString& screenIdOrName)
{
    if (clearPerScreenKeySubset(m_perScreenAutotileSettings, screenIdOrName, isPerScreenAutotileAlgorithmKey,
                                /*clearMatch=*/true)) {
        Q_EMIT perScreenAutotileSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

// ── Per-Screen Scrolling Config ──────────────────────────────────────────────

// Reads the stored overrides for exactly this screen. Deliberately NO
// virtual→physical fallback, unlike resolvedZoneSelectorConfig and
// perScreenGapOverrides: the daemon resolves the virtual-screen fallback in
// its own scrolling path, so doing it here too would resolve it twice. Same
// shape as the autotile twin above.
QVariantMap Settings::getPerScreenScrollingSettings(const QString& screenIdOrName) const
{
    auto it = findPerScreenEntry(m_perScreenScrollingSettings, screenIdOrName);
    return (it != m_perScreenScrollingSettings.constEnd()) ? it.value() : QVariantMap();
}

void Settings::setPerScreenScrollingSetting(const QString& screenIdOrName, const QString& key, const QVariant& value)
{
    if (screenIdOrName.isEmpty() || key.isEmpty()) {
        return;
    }
    QVariant validated = validatePerScreenScrollingValue(key, value);
    if (!validated.isValid()) {
        // DEBUG, value not echoed — same session-bus log-flood rationale as
        // the autotile twin.
        qCDebug(lcConfig) << "Rejected per-screen scrolling setting" << key;
        return;
    }
    if (!applyPerScreenSetting(m_perScreenScrollingSettings, screenIdOrName, key, validated)) {
        return;
    }
    // A kind write can strand the value key on the other kind's scale — the
    // per-key validators bound each half independently and neither can see the
    // pair. Re-seed it now, matching what the global kind setter does, so a
    // scoped monitor never runs with a proportion under Fixed (1px columns) or
    // pixels under Proportion (100% columns). The VALUE key deliberately
    // takes NO repair: a client writing Value before Kind (the natural batch
    // order) would have its value re-seeded to the OLD kind's default before
    // the kind write arrives — destroying data to fix a pair the very next
    // write was about to make consistent. A value-only cross-kind write does
    // mis-render until the pair is next repaired (the kind arm here, or the
    // load()-time sweep), which is the lesser harm.
    if (key == QLatin1String(PerScreenScrollingKey::DefaultColumnWidthKind)) {
        auto it = findPerScreenEntryMutable(m_perScreenScrollingSettings, screenIdOrName);
        if (it != m_perScreenScrollingSettings.end()) {
            repairPerScreenScrollingWidth(it.value());
        }
    }
    Q_EMIT perScreenScrollingSettingsChanged();
    Q_EMIT settingsChanged();
}

void Settings::clearPerScreenScrollingSettings(const QString& screenIdOrName)
{
    if (removePerScreenEntry(m_perScreenScrollingSettings, screenIdOrName)) {
        Q_EMIT perScreenScrollingSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

bool Settings::hasPerScreenScrollingSettings(const QString& screenIdOrName) const
{
    // WHOLE-DOMAIN: this is the D-Bus category surface (clear-the-category),
    // not a card's chip surface. The cards use the sizing/axis sub-domain
    // accessors below, the way the autotile store's Algorithm twins do.
    return findPerScreenEntry(m_perScreenScrollingSettings, screenIdOrName) != m_perScreenScrollingSettings.constEnd();
}

bool Settings::hasPerScreenScrollingSizingSettings(const QString& screenIdOrName) const
{
    return hasPerScreenKeySubset(m_perScreenScrollingSettings, screenIdOrName, isPerScreenScrollingSizingKey,
                                 /*wantMatch=*/true);
}

void Settings::clearPerScreenScrollingSizingSettings(const QString& screenIdOrName)
{
    if (clearPerScreenKeySubset(m_perScreenScrollingSettings, screenIdOrName, isPerScreenScrollingSizingKey,
                                /*clearMatch=*/true)) {
        Q_EMIT perScreenScrollingSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

bool Settings::hasPerScreenScrollingAxisSettings(const QString& screenIdOrName) const
{
    return hasPerScreenKeySubset(m_perScreenScrollingSettings, screenIdOrName, isPerScreenScrollingAxisKey,
                                 /*wantMatch=*/true);
}

void Settings::clearPerScreenScrollingAxisSettings(const QString& screenIdOrName)
{
    if (clearPerScreenKeySubset(m_perScreenScrollingSettings, screenIdOrName, isPerScreenScrollingAxisKey,
                                /*clearMatch=*/true)) {
        Q_EMIT perScreenScrollingSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

// ── Per-Screen Snapping Config ───────────────────────────────────────────────

// Snapping per-screen gaps share the unified per-screen gap store (one value per
// monitor drives both snap and tile). This reader surfaces that gap subset (or
// an empty map, in which case the snap geometry path falls back to the layout /
// global gaps). The set/clear/has triplet stays as ISettings no-op defaults —
// writes go through setPerScreenAutotileSetting / the perScreenGap* accessors.
QVariantMap Settings::getPerScreenSnappingSettings(const QString& screenIdOrName) const
{
    return perScreenGapOverrides(screenIdOrName);
}

} // namespace PlasmaZones
