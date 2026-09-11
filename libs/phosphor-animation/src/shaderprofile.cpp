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
    // Normalised through JSON rather than compared as raw QVariants, the same
    // way the overlay profile does and for the same reason: the settings setters
    // compare a map BUILT in C++ against one read back from disk, and a value
    // whose type changed CATEGORY on the way through (a bool stored as 1, a
    // number stored as "1") would not compare equal, so the no-op gate would
    // fail open and every repeat write would emit.
    //
    // Not a live bug today: QVariant equality promotes across int, qlonglong and
    // double, which is the only difference the current round trip produces. This
    // is the cheap insurance against the next type that travels differently, and
    // it matters because the signal on the other side of that gate reaches the
    // daemon and the compositor.
    return QJsonObject::fromVariantMap(*parameters) == QJsonObject::fromVariantMap(*other.parameters);
}

ShaderProfile withPresetsResolved(const ShaderProfile& profile, const PhosphorShaders::ShaderPresetRegistry& presets)
{
    if (!profile.presetId || profile.presetId->isEmpty()) {
        return profile;
    }

    ShaderProfile out = profile;
    // The profile's own parameters are the DELTA set, so they are the second
    // argument: preset values first, this assignment's edits on top. The pack is
    // `effectiveEffectId()` — resolved by the walk-up before this runs, which is
    // why flattening must not happen per node.
    const QVariantMap resolved =
        presets.resolveParams(PhosphorShaders::ShaderFamily::Animation, profile.effectiveEffectId(), *profile.presetId,
                              profile.effectiveParameters());
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
