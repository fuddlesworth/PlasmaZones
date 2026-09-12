// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/ShaderProfile.h>

#include <PhosphorShaders/ShaderPresetRegistry.h>

#include <QJsonObject>
#include <QJsonValue>

namespace PhosphorAnimationShaders {

ShaderProfile ShaderProfile::withDefaults() const
{
    ShaderProfile out = *this;
    if (!out.effectId)
        out.effectId = QString();
    if (!out.parameters)
        out.parameters = QVariantMap();
    if (!out.presetId)
        out.presetId = QString();
    return out;
}

QJsonObject ShaderProfile::toJson() const
{
    QJsonObject obj;
    if (effectId)
        obj.insert(QLatin1String(JsonFieldEffectId), *effectId);
    if (parameters) {
        QJsonObject paramsObj;
        for (auto it = parameters->constBegin(); it != parameters->constEnd(); ++it)
            paramsObj.insert(it.key(), QJsonValue::fromVariant(it.value()));
        obj.insert(QLatin1String(JsonFieldParameters), paramsObj);
    }
    if (presetId)
        obj.insert(QLatin1String(JsonFieldPresetId), *presetId);
    return obj;
}

ShaderProfile ShaderProfile::fromJson(const QJsonObject& obj)
{
    ShaderProfile p;

    if (obj.contains(QLatin1String(JsonFieldEffectId))) {
        const QJsonValue v = obj.value(QLatin1String(JsonFieldEffectId));
        if (v.isString())
            p.effectId = v.toString();
    }

    if (obj.contains(QLatin1String(JsonFieldParameters))) {
        const QJsonValue v = obj.value(QLatin1String(JsonFieldParameters));
        if (v.isObject()) {
            QVariantMap params;
            const QJsonObject paramsObj = v.toObject();
            for (auto it = paramsObj.constBegin(); it != paramsObj.constEnd(); ++it)
                params.insert(it.key(), it.value().toVariant());
            p.parameters = std::move(params);
        }
    }

    if (obj.contains(QLatin1String(JsonFieldPresetId))) {
        const QJsonValue v = obj.value(QLatin1String(JsonFieldPresetId));
        if (v.isString())
            p.presetId = v.toString();
    }

    return p;
}

void ShaderProfile::overlay(ShaderProfile& dst, const ShaderProfile& src)
{
    if (src.effectId)
        dst.effectId = src.effectId;
    if (src.parameters)
        dst.parameters = src.parameters;
    if (src.presetId)
        dst.presetId = src.presetId;
}

bool ShaderProfile::operator==(const ShaderProfile& other) const
{
    if (effectId != other.effectId || presetId != other.presetId) {
        return false;
    }
    // ENGAGEMENT first, then a JSON-normalised compare of the contents. nullopt
    // and engaged-empty are different statements on this type, so they must not
    // compare equal, and `QJsonObject::fromVariantMap` on an absent map would
    // flatten both to `{}`.
    if (parameters.has_value() != other.parameters.has_value()) {
        return false;
    }
    if (!parameters.has_value()) {
        return true;
    }
    // Raw compare FIRST, and normalise only when it misses. Two maps that are
    // already identical are the overwhelmingly common case — a repeat write, a
    // whole-tree compare where one path moved — and the normalisation below builds
    // a QJsonObject per side. PathKeyedOverrides::sameOverrides runs this once per
    // override, and the tree's sanitizer bounds are 1024 overrides of 64 params, so
    // without this an unchanged tree built ~2048 QJsonObjects on every write, at
    // slider-drag rate. QVariantMap::operator== cannot report a false EQUAL here,
    // only a false unequal, so skipping the normalisation on a hit is safe.
    if (*parameters == *other.parameters) {
        return true;
    }
    // Normalised through JSON rather than compared as raw QVariants, the same way
    // the overlay profile and ShaderPreset do, so that equality means the same
    // thing on all three: a value whose type changed CATEGORY (a bool arriving as
    // 1, a number as "1") compares equal here even though QVariant says otherwise.
    //
    // Deliberately NOT justified by naming a caller that drifts. The one this
    // comment used to cite, Settings::setShaderProfileTree, builds BOTH sides
    // through fromJson(QJsonObject::fromVariantMap(...)), so no category drift is
    // possible there — the comment was describing a hazard its own example could
    // not produce. The guarantee is worth having anyway: a no-op gate that fails
    // open re-emits to the daemon and the compositor, and the cost of being wrong
    // is paid by a caller this type cannot see.
    return QJsonObject::fromVariantMap(*parameters) == QJsonObject::fromVariantMap(*other.parameters);
}

ShaderProfile withPresetsResolved(const ShaderProfile& profile, const PhosphorShaders::ShaderPresetRegistry& presets)
{
    if (!profile.presetId || profile.presetId->isEmpty()) {
        // No preset to apply, but resolveParams is also where the pack's declared
        // min/max is enforced, so returning unchanged left a hand-edited or
        // schema-predating value to reach the uniform unbounded. The flatten is the
        // last place the declared ranges and the stored values are both in hand.
        //
        // An empty presetId resolves to the deltas alone, clamped. `has_value()`
        // gates it so nullopt is never turned into an engaged-empty map, which is a
        // different statement on this type, and so an assignment that stores nothing
        // stays exactly as it was.
        if (!profile.parameters.has_value() || profile.parameters->isEmpty()) {
            return profile;
        }
        ShaderProfile out = profile;
        out.parameters = presets.resolveParams(PhosphorShaders::ShaderFamily::Animation, profile.effectiveEffectId(),
                                               QString(), *profile.parameters);
        return out;
    }

    ShaderProfile out = profile;
    // The profile's own parameters are the DELTA set, so they are the second
    // argument: preset values first, this assignment's edits on top. The pack is
    // `effectiveEffectId()` — resolved by the walk-up before this runs, which is
    // why flattening must not happen per node.
    // `storedParameters()`, not `effectiveParameters()`: this IS the flatten, so
    // reading the raw map while the preset is still engaged is exactly right, and
    // the effective getter would warn about the one read that is not a mistake.
    const QVariantMap resolved =
        presets.resolveParams(PhosphorShaders::ShaderFamily::Animation, profile.effectiveEffectId(), *profile.presetId,
                              profile.storedParameters());
    // Engaged only when there is something to engage it WITH, the same rule the
    // decoration twin applies. nullopt and engaged-empty are different
    // statements — engaged-empty is "no parameters, and do not inherit any" —
    // and a flatten that turned one into the other would invent a block the user
    // never wrote. Reachable when the named preset no longer exists and the
    // assignment had no parameters of its own, which resolves to an empty map.
    if (!resolved.isEmpty() || profile.parameters.has_value()) {
        out.parameters = resolved;
    }
    // Cleared so a second flatten is a no-op rather than a double application
    // the moment anything overlays two already-flattened profiles.
    out.presetId.reset();
    return out;
}

} // namespace PhosphorAnimationShaders
