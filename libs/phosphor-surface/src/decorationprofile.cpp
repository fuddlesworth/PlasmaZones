// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSurface/DecorationProfile.h>

#include <QJsonArray>
#include <QJsonValue>

namespace PhosphorSurfaceShaders {

DecorationProfile DecorationProfile::withDefaults() const
{
    DecorationProfile out = *this;
    if (!out.chain)
        out.chain = QStringList();
    if (!out.parameters)
        out.parameters = QVariantMap();
    if (!out.disabledPacks)
        out.disabledPacks = QStringList();
    if (!out.presetIds)
        out.presetIds = QVariantMap();
    return out;
}

QJsonObject DecorationProfile::toJson() const
{
    QJsonObject obj;
    if (chain) {
        QJsonArray chainArr;
        for (const QString& packId : *chain)
            chainArr.append(packId);
        obj.insert(QLatin1String(JsonFieldChain), chainArr);
    }
    if (parameters) {
        QJsonObject paramsObj;
        for (auto it = parameters->constBegin(); it != parameters->constEnd(); ++it)
            paramsObj.insert(it.key(), QJsonValue::fromVariant(it.value()));
        obj.insert(QLatin1String(JsonFieldParameters), paramsObj);
    }
    if (disabledPacks) {
        QJsonArray disabledArr;
        for (const QString& packId : *disabledPacks)
            disabledArr.append(packId);
        obj.insert(QLatin1String(JsonFieldDisabledPacks), disabledArr);
    }
    if (presetIds) {
        QJsonObject presetsObj;
        for (auto it = presetIds->constBegin(); it != presetIds->constEnd(); ++it)
            presetsObj.insert(it.key(), QJsonValue::fromVariant(it.value()));
        obj.insert(QLatin1String(JsonFieldPresetIds), presetsObj);
    }
    return obj;
}

DecorationProfile DecorationProfile::fromJson(const QJsonObject& obj)
{
    DecorationProfile p;

    // An ENGAGED chain, even an empty one, is a statement: "this surface
    // runs exactly these packs", which stops the seed defaults from being
    // injected and overrides whatever an ancestor set (see the class doc).
    // So a non-string entry must not coerce to "" and quietly engage that
    // statement on the author's behalf: it is skipped, and an array holding
    // only non-strings is treated as if the field were absent, which leaves
    // the optional disengaged and the seeds in force.
    if (obj.contains(QLatin1String(JsonFieldChain))) {
        const QJsonValue v = obj.value(QLatin1String(JsonFieldChain));
        if (v.isArray()) {
            QStringList chain;
            bool anyString = false;
            const QJsonArray arr = v.toArray();
            for (const QJsonValue& entry : arr) {
                if (!entry.isString())
                    continue;
                anyString = true;
                chain.append(entry.toString());
            }
            if (arr.isEmpty() || anyString)
                p.chain = std::move(chain);
        }
    }

    if (obj.contains(QLatin1String(JsonFieldParameters))) {
        const QJsonValue v = obj.value(QLatin1String(JsonFieldParameters));
        if (v.isObject()) {
            QVariantMap params;
            const QJsonObject paramsObj = v.toObject();
            for (auto it = paramsObj.constBegin(); it != paramsObj.constEnd(); ++it) {
                // A JSON null is NOT "leave this at its default". It converts to an
                // INVALID QVariant, every numeric consumer reads that as 0, and
                // clampToBounds skips it as non-numeric — so it survives the flatten
                // and PINS the parameter to zero, overriding the pack's declared
                // default. Dropping the key is what actually means "say nothing about
                // this one". `parsePackPresets` drops nulls for exactly this reason;
                // these two parsers were the remaining door.
                if (it.value().isNull()) {
                    continue;
                }
                params.insert(it.key(), it.value().toVariant());
            }
            p.parameters = std::move(params);
        }
    }

    // Absent field = nullopt = "inherit / nothing disabled", so a config
    // written before the per-layer toggle existed loads with every pack on.
    if (obj.contains(QLatin1String(JsonFieldDisabledPacks))) {
        const QJsonValue v = obj.value(QLatin1String(JsonFieldDisabledPacks));
        // Same rule as the chain: non-string entries are skipped, and an
        // array of nothing but non-strings leaves the field absent.
        if (v.isArray()) {
            QStringList disabled;
            bool anyString = false;
            const QJsonArray arr = v.toArray();
            for (const QJsonValue& entry : arr) {
                if (!entry.isString())
                    continue;
                anyString = true;
                disabled.append(entry.toString());
            }
            if (arr.isEmpty() || anyString)
                p.disabledPacks = std::move(disabled);
        }
    }

    // Absent field = nullopt = "inherit / no presets", so a config written
    // before presets existed loads with every layer on its own parameters.
    // Non-string values are skipped rather than coerced: a preset id is a
    // lookup key, and "" would mean "no preset" — a different statement from
    // the malformed entry the author actually wrote.
    if (obj.contains(QLatin1String(JsonFieldPresetIds))) {
        const QJsonValue v = obj.value(QLatin1String(JsonFieldPresetIds));
        if (v.isObject()) {
            QVariantMap presets;
            const QJsonObject presetsObj = v.toObject();
            for (auto it = presetsObj.constBegin(); it != presetsObj.constEnd(); ++it) {
                if (!it.value().isString())
                    continue;
                presets.insert(it.key(), it.value().toString());
            }
            p.presetIds = std::move(presets);
        }
    }

    return p;
}

void DecorationProfile::overlay(DecorationProfile& dst, const DecorationProfile& src)
{
    if (src.chain)
        dst.chain = src.chain;
    if (src.parameters)
        dst.parameters = src.parameters;
    if (src.disabledPacks)
        dst.disabledPacks = src.disabledPacks;
    if (src.presetIds)
        dst.presetIds = src.presetIds;
}

bool DecorationProfile::operator==(const DecorationProfile& other) const
{
    if (chain != other.chain || disabledPacks != other.disabledPacks || presetIds != other.presetIds) {
        return false;
    }
    // ENGAGEMENT first, then the contents, the same policy ShaderProfile,
    // OverlayShaderProfile and ShaderPreset all apply. This type used to compare
    // `parameters` as raw QVariants while its three siblings JSON-normalised, which
    // is a drift rather than a decision: the settings setter on this tree compares a
    // map BUILT in C++ against one read back from disk, so a value whose type
    // changed CATEGORY on the way through (a bool stored as 1, a number stored as
    // "1") failed the no-op gate and re-emitted.
    //
    // Raw compare first and normalise only on a miss, for the reason written out in
    // the animation twin: QVariantMap equality cannot report a false EQUAL, only a
    // false unequal, and the normalisation is what costs.
    if (parameters.has_value() != other.parameters.has_value()) {
        return false;
    }
    if (!parameters.has_value() || *parameters == *other.parameters) {
        return true;
    }
    return QJsonObject::fromVariantMap(*parameters) == QJsonObject::fromVariantMap(*other.parameters);
}

DecorationProfile withPresetsResolved(const DecorationProfile& profile,
                                      const PhosphorShaders::ShaderPresetRegistry& presets,
                                      PhosphorShaders::ShaderFamily family)
{
    // Every pack's entry goes through resolveParams, including the ones naming no
    // preset, because that call is also where the pack's declared min/max is
    // applied. Early-returning on "no presets here" left a hand-edited or
    // schema-predating parameter value to reach the uniform unbounded, which is the
    // one thing this function is positioned to prevent: the flatten is the last
    // place the declared ranges and the stored values are both in hand.
    if ((!profile.presetIds || profile.presetIds->isEmpty())
        && (!profile.parameters || profile.parameters->isEmpty())) {
        return profile;
    }

    DecorationProfile out = profile;
    // storedParameters(), not effectiveParameters(): this IS the flatten, so reading
    // the raw map while the presets are still engaged is exactly right, and the
    // effective getter would warn about the one read that is not a mistake.
    QVariantMap params = out.storedParameters();
    bool resolvedAny = false;
    // Keyed on the PARAMETER map, so a pack with stored values and no preset is
    // clamped, and then on any preset-only pack the map does not mention.
    for (auto it = params.begin(); it != params.end(); ++it) {
        const QString presetId = profile.presetIds ? profile.presetIds->value(it.key()).toString() : QString();
        // The pack's own entry in `parameters` is the DELTA set, so it is the
        // second argument: preset values first, this layer's edits on top. An empty
        // presetId resolves to the deltas alone, clamped.
        it.value() = presets.resolveParams(family, it.key(), presetId, it.value().toMap());
        if (!presetId.isEmpty()) {
            resolvedAny = true;
        }
    }
    if (profile.presetIds) {
        for (auto it = profile.presetIds->constBegin(); it != profile.presetIds->constEnd(); ++it) {
            const QString presetId = it.value().toString();
            if (presetId.isEmpty() || params.contains(it.key())) {
                continue;
            }
            params.insert(it.key(), presets.resolveParams(family, it.key(), presetId, QVariantMap()));
            resolvedAny = true;
        }
    }
    // Never INVENT engagement, but do keep the clamp on a map that was already
    // engaged. nullopt and engaged-empty are different statements — engaged-empty
    // is "no parameters here, and do not inherit any" — and assigning
    // unconditionally turned the first into the second, which this function has no
    // business doing. Reachable whenever every entry in presetIds holds an empty
    // string, the sentinel an assignment writes to BLOCK an inherited preset
    // without naming one of its own, so the flatten of a blocking-only profile used
    // to silently also block inherited parameters.
    //
    // `has_value()` as well as `resolvedAny`, because the loop above now clamps a
    // stored map even when no preset was named, and gating that on resolvedAny
    // would have thrown the clamp away again.
    if (resolvedAny || profile.parameters.has_value()) {
        out.parameters = params;
    }
    // Drop only the entries this flatten CONSUMED, and keep the blocking ones.
    //
    // An empty presetId is the sentinel an assignment writes to block an inherited
    // preset without naming one of its own, which the paragraph above already
    // relies on. Resetting the whole map revoked those blocks: a profile saying
    // "this pack follows no preset" came out of the flatten saying nothing at all,
    // so a later overlay was free to re-apply the ancestor's preset. The animation
    // twin avoids this by returning unchanged on an engaged-empty presetId; here
    // the map is per-pack, so one profile can block one pack and resolve another
    // and neither an early return nor a wholesale reset is right.
    //
    // Idempotent for the same reason it was before: a second flatten over the
    // result finds only empty presetIds, resolves nothing, and preserves them.
    QVariantMap blocking;
    const QVariantMap declaredPresetIds = profile.presetIds.value_or(QVariantMap());
    for (auto it = declaredPresetIds.constBegin(); it != declaredPresetIds.constEnd(); ++it) {
        if (it.value().toString().isEmpty()) {
            blocking.insert(it.key(), QString());
        }
    }
    if (!blocking.isEmpty()) {
        out.presetIds = blocking;
    } else if (profile.presetIds.has_value() && profile.presetIds->isEmpty()) {
        // ENGAGED-EMPTY in, engaged-empty out. `{}` is the user's explicit "no presets
        // for any pack here", and resetting it to nullopt revoked that statement —
        // while the early return above preserves it for the same input when
        // `parameters` happens to be empty too, so the answer depended on an unrelated
        // field.
        out.presetIds = QVariantMap();
    } else {
        out.presetIds.reset();
    }
    return out;
}

} // namespace PhosphorSurfaceShaders
