// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/ShaderPresetRegistry.h>

#include <QLoggingCategory>
#include <QSet>
#include <QStringList>

#include <algorithm>

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
        // Numbers only. A colour, a path or a bool has no declared range, and
        // canConvert would happily turn "4" into 4 and write the number back.
        const QMetaType::Type type = static_cast<QMetaType::Type>(it.value().typeId());
        const bool numeric = type == QMetaType::Int || type == QMetaType::UInt || type == QMetaType::LongLong
            || type == QMetaType::ULongLong || type == QMetaType::Double || type == QMetaType::Float;
        if (!numeric) {
            continue;
        }
        double value = it.value().toDouble();
        if (range->first.isValid()) {
            value = std::max(value, range->first.toDouble());
        }
        if (range->second.isValid()) {
            value = std::min(value, range->second.toDouble());
        }
        // Preserve integrality: writing a double back into an int-typed
        // parameter would change the variant's type under consumers that branch
        // on it.
        if (type == QMetaType::Double || type == QMetaType::Float) {
            it.value() = value;
        } else {
            it.value() = static_cast<qlonglong>(qRound(value));
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
    // preset file, and the assignment's own deltas. A pack's declared min/max
    // was otherwise enforced only by the settings slider, and several bundled
    // overlay packs feed a parameter straight into a GLSL loop bound.
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
        preset.name = it.key();
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
    for (const ShaderPreset& preset : presets) {
        if (!preset.isValid()) {
            continue;
        }
        const auto clash = seenIdSource.constFind(preset.id);
        if (clash != seenIdSource.constEnd()) {
            qCWarning(lcPresetRegistry).noquote()
                << "Two" << shaderFamilyToken(family) << "presets share the id" << preset.id << "—" << *clash << "and"
                << preset.sourcePath << ". Editing either may write over the other; give one a different id.";
        } else {
            seenIdSource.insert(preset.id, preset.sourcePath);
        }
        next[scopeKey(family, preset.packId)].insert(preset.id, preset);
    }

    // Every pack that had user presets before OR has them now, so a pack whose
    // last preset was just deleted still gets told.
    QSet<QString> touched;
    const QString familyPrefix = QString(shaderFamilyToken(family)) + QLatin1Char('/');
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
