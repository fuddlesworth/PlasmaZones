// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimationShaders/ShaderProfile.h>

#include <QJsonValue>

namespace PhosphorAnimationShaders {

ShaderProfile ShaderProfile::withDefaults() const
{
    ShaderProfile out = *this;
    if (!out.effectId)
        out.effectId = QString();
    if (!out.parameters)
        out.parameters = QVariantMap();
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

    return p;
}

void ShaderProfile::overlay(ShaderProfile& dst, const ShaderProfile& src)
{
    if (src.effectId)
        dst.effectId = src.effectId;
    if (src.parameters)
        dst.parameters = src.parameters;
}

bool ShaderProfile::operator==(const ShaderProfile& other) const
{
    return effectId == other.effectId && parameters == other.parameters;
}

} // namespace PhosphorAnimationShaders
