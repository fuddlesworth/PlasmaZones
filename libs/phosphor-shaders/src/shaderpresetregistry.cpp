// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/ShaderPresetRegistry.h>

#include <QLoggingCategory>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <optional>

namespace PhosphorShaders {

namespace {
Q_LOGGING_CATEGORY(lcPresetRegistry, "phosphorshaders.presetregistry")
} // namespace

ShaderPresetRegistry::ShaderPresetRegistry(QObject* parent)
    : QObject(parent)
{
}

ShaderPresetRegistry::~ShaderPresetRegistry() = default;

QString ShaderPresetRegistry::scopeKey(ShaderFamily family, const QString& packId)
{
    // Collision-free because the four family tokens contain no separator and
    // none is a prefix of another, so the first separator always ends the
    // family token and everything after it is the pack id — however many
    // separators that contains.
    //
    // Note it is the FAMILY token, not the pack id, that provides this. A
    // pack-declared preset's pack id really is a scanned directory name, but a
    // USER preset's comes from an unvalidated JSON field and may contain
    // anything; such a key is simply inert, matching no real pack.
    return QString(shaderFamilyToken(family)) + QLatin1Char('/') + packId;
}

QList<ShaderPreset> ShaderPresetRegistry::mergedFor(ShaderFamily family, const QString& packId) const
{
    const QString key = scopeKey(family, packId);

    const QHash<QString, ShaderPreset> user = m_userDefined.value(key);
    QList<ShaderPreset> userPresets = user.values();
    QList<ShaderPreset> packPresets;
    const QHash<QString, ShaderPreset> declared = m_packDeclared.value(key);
    // User-wins on an id collision, the same layering every Phosphor loader
    // applies to a user file shadowing a system one.
    for (auto it = declared.constBegin(); it != declared.constEnd(); ++it) {
        if (!user.contains(it.key())) {
            packPresets.append(it.value());
        }
    }

    // QHash iteration order is unspecified and varies between runs, so sort or
    // the picker reshuffles itself across restarts. Name first, id as the
    // tie-break, since a name is not an identity and two presets may share one.
    const auto byName = [](const ShaderPreset& a, const ShaderPreset& b) {
        const int cmp = a.name.localeAwareCompare(b.name);
        return cmp != 0 ? cmp < 0 : a.id < b.id;
    };
    std::sort(userPresets.begin(), userPresets.end(), byName);
    std::sort(packPresets.begin(), packPresets.end(), byName);

    // User presets first: the ones the user made are the ones they are looking
    // for, and the pack's own tunings read as the catalogue below them.
    userPresets.append(packPresets);
    return userPresets;
}

QList<ShaderPreset> ShaderPresetRegistry::presetsFor(ShaderFamily family, const QString& packId) const
{
    if (packId.isEmpty()) {
        return {};
    }
    return mergedFor(family, packId);
}

ShaderPreset ShaderPresetRegistry::preset(ShaderFamily family, const QString& packId, const QString& presetId) const
{
    if (packId.isEmpty() || presetId.isEmpty()) {
        return {};
    }
    const QString key = scopeKey(family, packId);
    const QHash<QString, ShaderPreset> user = m_userDefined.value(key);
    if (const auto it = user.constFind(presetId); it != user.constEnd()) {
        return *it;
    }
    return m_packDeclared.value(key).value(presetId);
}

ShaderPreset ShaderPresetRegistry::presetById(ShaderFamily family, const QString& presetId) const
{
    if (presetId.isEmpty()) {
        return {};
    }
    const QString familyPrefix = QString(shaderFamilyToken(family)) + QLatin1Char('/');

    // Sorted scope keys, not raw QHash order. Ids are supposed to be unique
    // within a family, but a user preset's id comes from a hand-editable file,
    // so two packs CAN carry the same one — and the write side resolves a
    // record through here before writing it back. An unspecified iteration
    // order would make that "whichever one this run happened to reach first".
    const auto scanSorted = [&](const QHash<QString, QHash<QString, ShaderPreset>>& buckets) -> ShaderPreset {
        QStringList keys;
        keys.reserve(buckets.size());
        for (auto it = buckets.constBegin(); it != buckets.constEnd(); ++it) {
            if (it.key().startsWith(familyPrefix)) {
                keys.append(it.key());
            }
        }
        std::sort(keys.begin(), keys.end());
        for (const QString& key : keys) {
            const QHash<QString, ShaderPreset>& bucket = *buckets.constFind(key);
            if (const auto found = bucket.constFind(presetId); found != bucket.constEnd()) {
                return *found;
            }
        }
        return {};
    };

    // User presets first, for the same reason the pack-scoped lookup prefers
    // them: a user preset shadowing a pack-declared id is the one that answers.
    if (const ShaderPreset user = scanSorted(m_userDefined); user.isValid()) {
        return user;
    }
    return scanSorted(m_packDeclared);
}

void ShaderPresetRegistry::clampToBounds(const QString& key, QVariantMap& values) const
{
    const auto boundsIt = m_packBounds.constFind(key);
    if (boundsIt == m_packBounds.constEnd()) {
        return;
    }
    for (auto it = values.begin(); it != values.end(); ++it) {
        const auto range = boundsIt->constFind(it.key());
        if (range == boundsIt->constEnd()) {
            continue;
        }
        // Numbers only, and a STRING that is a number counts. A colour or a path has no
        // declared range so it never reaches here, and a bool is handled by its own
        // branch; what is left is a value whose variant type is incidental. The schemas
        // allow a string preset value, and the uniform upload reads every custom param
        // through `toFloat(&ok)` and takes it when ok — so `"octaves": "9999"` on a
        // ranged parameter reached the shader unclamped while every numeric spelling of
        // the same value was bounded. A numeric string is clamped and written back AS A
        // NUMBER, which is what the consumer reads anyway; a genuinely non-numeric
        // string is left alone.
        const QMetaType::Type type = static_cast<QMetaType::Type>(it.value().typeId());
        bool numericString = false;
        if (type == QMetaType::QString) {
            it.value().toString().toDouble(&numericString);
        }
        const bool numeric = type == QMetaType::Int || type == QMetaType::UInt || type == QMetaType::LongLong
            || type == QMetaType::ULongLong || type == QMetaType::Double || type == QMetaType::Float || numericString;
        if (!numeric) {
            continue;
        }
        // A declared bound is only a bound when it is a NUMBER. `presetBoundsFrom`
        // inserts whatever the pack wrote and `isValid()` is true for a QString, so
        // `"min": "abc"` used to read as min 0.0 and silently clamp every value for that
        // parameter to >= 0 — and two bad sides pinned everything to exactly 0. Only the
        // animation and wallpaper registries schema-validate at load, so a user-installed
        // surface or pointer pack reaches this ungated. Same class as the inverted range
        // below, on the other axis: a bound that cannot be read is no bound.
        const auto numericBound = [&key, &it](const QVariant& side, const char* which) -> std::optional<double> {
            if (!side.isValid()) {
                return std::nullopt;
            }
            bool ok = false;
            const double value = side.toDouble(&ok);
            if (!ok || !std::isfinite(value)) {
                qCWarning(lcPresetRegistry) << "ShaderPresetRegistry: pack" << key << "declares a non-numeric" << which
                                            << "for" << it.key() << "(" << side << "); ignoring that bound";
                return std::nullopt;
            }
            return value;
        };
        const std::optional<double> minBound = numericBound(range->first, "min");
        const std::optional<double> maxBound = numericBound(range->second, "max");
        const bool hasMin = minBound.has_value();
        const bool hasMax = maxBound.has_value();
        const double min = minBound.value_or(0.0);
        const double max = maxBound.value_or(0.0);
        // An INVERTED range is refused rather than applied. Nothing validates
        // min <= max — `presetBoundsFrom` inserts whatever the pack declared —
        // and applying max() then min() to a declared min 5 / max 1 leaves the
        // value BELOW the minimum, so the clamp would move a legal value out of
        // both bounds and say nothing. A pack that declares the pair backwards
        // has no usable range, so there is nothing to enforce.
        if (hasMin && hasMax && min > max) {
            qCWarning(lcPresetRegistry) << "ShaderPresetRegistry: pack" << key << "declares an inverted range for"
                                        << it.key() << "(min" << min << "> max" << max << "); not clamping";
            continue;
        }

        double value = it.value().toDouble();
        // NON-FINITE first. std::max(NaN, min) returns NaN (the `a < b ? b : a` shape),
        // so a NaN or infinity walked through the clamp untouched and reached the uniform.
        // JSON cannot carry one — Qt's parser refuses both spellings — but a D-Bus or
        // QVariantMap write can, and this is the only place a declared range is enforced.
        // Treated as "no usable value": the parameter falls back to its declared default
        // by having its entry dropped, which is what an unparseable colour already does.
        if (!std::isfinite(value)) {
            qCWarning(lcPresetRegistry) << "ShaderPresetRegistry: pack" << key << "received a non-finite value for"
                                        << it.key() << "; dropping it so the declared default applies";
            it = values.erase(it);
            if (it == values.end()) {
                break;
            }
            --it;
            continue;
        }
        if (hasMin) {
            value = std::max(value, min);
        }
        if (hasMax) {
            value = std::min(value, max);
        }
        // A numeric STRING comes back as a number: the consumer reads it as one either
        // way, and leaving it a string would mean the next clamp has to re-parse it.
        if (type == QMetaType::Double || type == QMetaType::Float || numericString) {
            it.value() = value;
            continue;
        }

        // An INTEGRAL variant is kept integral where it can be, because writing a
        // double back would change the type under consumers that branch on it. But
        // integrality never outranks the bound, and rounding to NEAREST let it:
        // QJsonObject::toVariantMap returns qlonglong for every whole number, `2.0`
        // included, so a whole-number value on a FLOAT parameter arrives here as
        // LongLong and takes this branch. With a fractional declared range that
        // rounded the clamped value straight back out — `smoothness` (min 0.01, max
        // 0.6) clamped 2 to 0.6 and then qRound put it at 1, above the max, which
        // made the clamp a silent no-op for exactly the out-of-range case it exists
        // to catch.
        //
        // So round TOWARD the interval, and when no integer fits inside it (any
        // range narrower than 1, which is most float ranges) keep the clamped
        // double. The declared bound is the guarantee; the variant's incidental
        // integrality is not.
        // qRound64, not qRound: qRound returns INT and Qt's checked conversion asserts in
        // debug and is undefined in release for a value outside int's range — reachable
        // with a one-sided declared range (presetBoundsFrom inserts whichever side the
        // pack gave) and a hand-edited 1e12. The value is already clamped to whatever
        // bounds exist, so this only has to survive the unbounded side.
        double rounded = static_cast<double>(qRound64(value));
        if (hasMin && rounded < min) {
            rounded = std::ceil(min);
        }
        if (hasMax && rounded > max) {
            rounded = std::floor(max);
        }
        if ((hasMin && rounded < min) || (hasMax && rounded > max)) {
            it.value() = value;
        } else {
            it.value() = static_cast<qlonglong>(rounded);
        }
    }
}

QVariantMap ShaderPresetRegistry::resolveParams(ShaderFamily family, const QString& packId, const QString& presetId,
                                                const QVariantMap& deltas) const
{
    const QString key = scopeKey(family, packId);
    const ShaderPreset found = preset(family, packId, presetId);

    // Clamp on the way out, not on the way in, so this covers all three
    // provenances at one site: a pack-declared preset, a hand-written user
    // preset file, and the assignment's own deltas. A pack's declared min/max was
    // otherwise enforced only by the settings slider, so any value arriving by
    // another door (a hand-edited config.json, a D-Bus write, a config predating a
    // narrowed range) reached the uniform unbounded.
    //
    // Belt and braces rather than a stall fix, and worth stating because an earlier
    // review of this work claimed otherwise: no bundled pack derives a GLSL loop
    // bound from an unclamped parameter. Every consuming loop carries its own
    // in-shader cap (`i < octaves && i < 8` in data/overlays/shared/common.glsl, the
    // `&& li < 8` logo loops, neon-venom's `&& i < 20`, chrome-protocol's
    // clamp(...,16,96)), and the one uncapped fbm loop is only ever reached with a
    // literal octave count. What this prevents is an out-of-range value reaching a
    // shader that trusts its declared range, which is a correctness guarantee, not
    // a performance one.
    if (!found.isValid()) {
        // No preset, or one that has since been deleted or dropped by a pack
        // update. Either way the assignment's own values are the whole answer,
        // which is exactly the look it would have had with no preset at all.
        QVariantMap out = deltas;
        clampToBounds(key, out);
        return out;
    }
    QVariantMap out = overlayPresetDeltas(found.params, deltas);
    clampToBounds(key, out);
    return out;
}

bool ShaderPresetRegistry::applyPackBucket(const QString& key, const QString& packId, const PackPresets& presets,
                                           const PresetValueBounds& bounds)
{
    QHash<QString, ShaderPreset> next;
    next.reserve(presets.size());
    for (auto it = presets.constBegin(); it != presets.constEnd(); ++it) {
        ShaderPreset preset;
        // A pack-declared preset is keyed by its name in the metadata block, so
        // id and name are the same string. That is the pack author's identity
        // for it, and renaming one in a pack update is indistinguishable from
        // deleting one and adding another — which is the honest reading.
        preset.id = it.key();
        // The key is the id AND the display name, and only the id half is identity: a
        // 5000-character key from a hand-written metadata.json rendered unbounded in every
        // picker row. fromJson caps a user preset's name for exactly this reason, and the
        // offline validator's diagnostic is for the author, not a runtime refusal.
        preset.name = it.key();
        if (preset.name.size() > ShaderPreset::MaxNameChars) {
            preset.name.truncate(ShaderPreset::MaxNameChars);
            if (!preset.name.isEmpty() && preset.name.back().isHighSurrogate()) {
                preset.name.chop(1);
            }
        }
        preset.packId = packId;
        preset.params = it.value();
        preset.readOnly = true;
        next.insert(preset.id, preset);
    }

    // Bounds are refreshed even when the presets are unchanged: a pack update
    // can narrow a declared range without touching its presets, and the new
    // range has to apply to the values already held.
    const bool boundsChanged = m_packBounds.value(key) != bounds;
    if (bounds.isEmpty()) {
        m_packBounds.remove(key);
    } else {
        m_packBounds.insert(key, bounds);
    }

    if (m_packDeclared.value(key) == next) {
        // A pack reload that did not touch the presets must not re-emit: the
        // compositor drops every compiled pack on this signal. A changed range
        // does warrant one, because it changes what resolveParams returns.
        return boundsChanged;
    }
    if (next.isEmpty()) {
        m_packDeclared.remove(key);
    } else {
        m_packDeclared.insert(key, next);
    }
    return true;
}

void ShaderPresetRegistry::setPackPresets(ShaderFamily family, const QString& packId, const PackPresets& presets,
                                          const PresetValueBounds& bounds)
{
    if (packId.isEmpty()) {
        return;
    }
    if (applyPackBucket(scopeKey(family, packId), packId, presets, bounds)) {
        Q_EMIT presetsChanged(family, packId);
    }
}

void ShaderPresetRegistry::setPackPresetsForFamily(ShaderFamily family, const QHash<QString, PackPresets>& byPackId,
                                                   const QHash<QString, PresetValueBounds>& boundsByPackId)
{
    const QString familyPrefix = QString(shaderFamilyToken(family)) + QLatin1Char('/');

    // Every pack that HAD pack-declared presets or bounds, plus every pack that
    // has them now. The first half is what a per-pack loop driven by the
    // installed packs can never reach, and is why an uninstalled pack used to
    // keep its presets for the life of the process.
    QSet<QString> touched;
    for (auto it = m_packDeclared.constBegin(); it != m_packDeclared.constEnd(); ++it) {
        if (it.key().startsWith(familyPrefix)) {
            touched.insert(it.key());
        }
    }
    for (auto it = m_packBounds.constBegin(); it != m_packBounds.constEnd(); ++it) {
        if (it.key().startsWith(familyPrefix)) {
            touched.insert(it.key());
        }
    }
    for (auto it = byPackId.constBegin(); it != byPackId.constEnd(); ++it) {
        if (!it.key().isEmpty()) {
            touched.insert(scopeKey(family, it.key()));
        }
    }
    // Bounds-bearing packs too, and NOT only the ones that also declare presets.
    // Most packs declare a parameter range and no preset at all, so keying this
    // loop on `byPackId` alone silently discarded every one of their ranges: the
    // pack never entered `touched`, applyPackBucket never ran for it, and
    // resolveParams then had nothing to clamp a hand-edited value against.
    for (auto it = boundsByPackId.constBegin(); it != boundsByPackId.constEnd(); ++it) {
        if (!it.key().isEmpty()) {
            touched.insert(scopeKey(family, it.key()));
        }
    }

    // Sorted, so the order the signals fire in is the same on every run.
    QStringList keys(touched.constBegin(), touched.constEnd());
    std::sort(keys.begin(), keys.end());

    QStringList changedPacks;
    for (const QString& key : keys) {
        const QString packId = key.mid(familyPrefix.size());
        if (applyPackBucket(key, packId, byPackId.value(packId), boundsByPackId.value(packId))) {
            changedPacks.append(packId);
        }
    }

    for (const QString& packId : changedPacks) {
        Q_EMIT presetsChanged(family, packId);
    }
}

void ShaderPresetRegistry::setUserPresets(ShaderFamily family, const QList<ShaderPreset>& presets)
{
    // Regroup the flat batch the loader hands over into per-pack buckets.
    QHash<QString, QHash<QString, ShaderPreset>> next;
    // Ids are supposed to be unique within a family, and the write side relies
    // on it (`presetById` resolves a record to write back). Nothing enforces it
    // for a hand-written file, so say so rather than resolving it silently.
    QHash<QString, QString> seenIdSource;
    // Once per clash, not once per rescan. The watcher re-reads the whole family
    // on every save and on every external edit, so one hand-written duplicate used
    // to emit an identical line for the life of the process.
    // One spelling of the family prefix for the whole function: the clash bookkeeping
    // below and the touched-pack sweep further down both key on it.
    const QString familyPrefix = QString(shaderFamilyToken(family)) + QLatin1Char('/');
    QSet<QString> clashingNow;
    for (const ShaderPreset& preset : presets) {
        if (!preset.isValid()) {
            continue;
        }
        const auto clash = seenIdSource.constFind(preset.id);
        if (clash != seenIdSource.constEnd()) {
            const QString clashKey = familyPrefix + preset.id;
            clashingNow.insert(clashKey);
            if (!m_reportedIdClashes.contains(clashKey)) {
                m_reportedIdClashes.insert(clashKey);
                qCWarning(lcPresetRegistry).noquote()
                    << "Two" << shaderFamilyToken(family) << "presets share the id" << preset.id << "—" << *clash
                    << "and" << preset.sourcePath
                    << ". Editing either may write over the other; give one a different id.";
            }
        } else {
            seenIdSource.insert(preset.id, preset.sourcePath);
        }
        next[scopeKey(family, preset.packId)].insert(preset.id, preset);
    }
    // Forget this family's resolved clashes, so fixing one and then re-introducing
    // it warns again rather than staying silent for the rest of the process.
    for (auto it = m_reportedIdClashes.begin(); it != m_reportedIdClashes.end();) {
        it = (it->startsWith(familyPrefix) && !clashingNow.contains(*it)) ? m_reportedIdClashes.erase(it) : ++it;
    }

    // Every pack that had user presets before OR has them now, so a pack whose
    // last preset was just deleted still gets told.
    QSet<QString> touched;
    for (auto it = m_userDefined.constBegin(); it != m_userDefined.constEnd(); ++it) {
        if (it.key().startsWith(familyPrefix)) {
            touched.insert(it.key());
        }
    }
    for (auto it = next.constBegin(); it != next.constEnd(); ++it) {
        touched.insert(it.key());
    }

    // Sorted, so the order the per-pack signals fire in does not vary between
    // runs. Consumers only drop caches on these, but an unspecified order is
    // the kind of thing a later change comes to depend on by accident.
    QStringList touchedKeys(touched.constBegin(), touched.constEnd());
    std::sort(touchedKeys.begin(), touchedKeys.end());

    QList<QString> changedPacks;
    for (const QString& key : touchedKeys) {
        const QHash<QString, ShaderPreset> before = m_userDefined.value(key);
        const QHash<QString, ShaderPreset> after = next.value(key);
        // One rescan usually touches one file. Emitting per pack rather than per
        // family keeps a single edit from invalidating every pack's compiled
        // shaders.
        if (before == after) {
            continue;
        }
        if (after.isEmpty()) {
            m_userDefined.remove(key);
        } else {
            m_userDefined.insert(key, after);
        }
        // Recover the pack id from the scope key: everything after the first
        // separator, which the family token cannot contain.
        changedPacks.append(key.mid(familyPrefix.size()));
    }

    for (const QString& packId : changedPacks) {
        Q_EMIT presetsChanged(family, packId);
    }
}

} // namespace PhosphorShaders
