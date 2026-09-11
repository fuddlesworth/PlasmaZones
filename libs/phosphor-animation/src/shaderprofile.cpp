// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/ShaderProfile.h>

#include <PhosphorShaders/ShaderPresetRegistry.h>

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
    return effectId == other.effectId && parameters == other.parameters && presetId == other.presetId;
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
    out.parameters = presets.resolveParams(PhosphorShaders::ShaderFamily::Animation, profile.effectiveEffectId(),
                                           *profile.presetId, profile.effectiveParameters());
    // Cleared so a second flatten is a no-op rather than a double application
    // the moment anything overlays two already-flattened profiles.
    out.presetId.reset();
    return out;
}

} // namespace PhosphorAnimationShaders
