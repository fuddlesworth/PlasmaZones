// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/ShaderPresetRegistry.h>

#include <QSet>

#include <algorithm>

namespace PhosphorShaders {

ShaderPresetRegistry::ShaderPresetRegistry(QObject* parent)
    : QObject(parent)
{
}

ShaderPresetRegistry::~ShaderPresetRegistry() = default;

QString ShaderPresetRegistry::scopeKey(ShaderFamily family, const QString& packId)
{
    // The separator is a character no pack id can contain: pack ids are
    // directory names, and the loaders reject a separator in one. Without that
    // guarantee "animation" + "a/b" and "animation/a" + "b" would collide.
    return QString(shaderFamilyToken(family)) + QLatin1Char('/') + packId;
}

QList<ShaderPreset> ShaderPresetRegistry::mergedFor(ShaderFamily family, const QString& packId) const
{
    const QString key = scopeKey(family, packId);

    QList<ShaderPreset> userPresets = m_userDefined.value(key).values();
    QList<ShaderPreset> packPresets;
    const QHash<QString, ShaderPreset> declared = m_packDeclared.value(key);
    // User-wins on an id collision, the same layering every Phosphor loader
    // applies to a user file shadowing a system one.
    for (auto it = declared.constBegin(); it != declared.constEnd(); ++it) {
        if (!m_userDefined.value(key).contains(it.key())) {
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
    // User presets first, for the same reason the pack-scoped lookup prefers
    // them: a user preset shadowing a pack-declared id is the one that answers.
    for (auto it = m_userDefined.constBegin(); it != m_userDefined.constEnd(); ++it) {
        if (!it.key().startsWith(familyPrefix)) {
            continue;
        }
        if (const auto found = it->constFind(presetId); found != it->constEnd()) {
            return *found;
        }
    }
    for (auto it = m_packDeclared.constBegin(); it != m_packDeclared.constEnd(); ++it) {
        if (!it.key().startsWith(familyPrefix)) {
            continue;
        }
        if (const auto found = it->constFind(presetId); found != it->constEnd()) {
            return *found;
        }
    }
    return {};
}

QVariantMap ShaderPresetRegistry::resolveParams(ShaderFamily family, const QString& packId, const QString& presetId,
                                                const QVariantMap& deltas) const
{
    const ShaderPreset found = preset(family, packId, presetId);
    if (!found.isValid()) {
        // No preset, or one that has since been deleted or dropped by a pack
        // update. Either way the assignment's own values are the whole answer,
        // which is exactly the look it would have had with no preset at all.
        return deltas;
    }
    return overlayPresetDeltas(found.params, deltas);
}

void ShaderPresetRegistry::setPackPresets(ShaderFamily family, const QString& packId, const PackPresets& presets)
{
    if (packId.isEmpty()) {
        return;
    }
    const QString key = scopeKey(family, packId);

    QHash<QString, ShaderPreset> next;
    next.reserve(static_cast<int>(presets.size()));
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

    if (m_packDeclared.value(key) == next) {
        // A pack reload that did not touch the presets must not re-emit: the
        // compositor drops every compiled pack on this signal.
        return;
    }
    if (next.isEmpty()) {
        m_packDeclared.remove(key);
    } else {
        m_packDeclared.insert(key, next);
    }
    Q_EMIT presetsChanged(family, packId);
}

void ShaderPresetRegistry::setUserPresets(ShaderFamily family, const QList<ShaderPreset>& presets)
{
    // Regroup the flat batch the loader hands over into per-pack buckets.
    QHash<QString, QHash<QString, ShaderPreset>> next;
    for (const ShaderPreset& preset : presets) {
        if (!preset.isValid()) {
            continue;
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

    QList<QString> changedPacks;
    for (const QString& key : touched) {
        const QHash<QString, ShaderPreset> before = m_userDefined.value(key);
        const QHash<QString, ShaderPreset> after = next.value(key);
        if (before == after) {
            // One rescan usually touches one file. Emitting per pack rather
            // than per family keeps a single edit from invalidating every
            // pack's compiled shaders.
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
