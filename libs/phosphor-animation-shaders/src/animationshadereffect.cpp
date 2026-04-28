// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimationShaders/AnimationShaderEffect.h>

#include <QJsonArray>
#include <QJsonValue>

namespace PhosphorAnimationShaders {

QJsonObject AnimationShaderEffect::toJson() const
{
    QJsonObject obj;
    obj.insert(QLatin1String("id"), id);
    if (!name.isEmpty())
        obj.insert(QLatin1String("name"), name);
    if (!description.isEmpty())
        obj.insert(QLatin1String("description"), description);
    if (!author.isEmpty())
        obj.insert(QLatin1String("author"), author);
    if (!version.isEmpty())
        obj.insert(QLatin1String("version"), version);
    if (!category.isEmpty())
        obj.insert(QLatin1String("category"), category);
    if (!fragmentShaderPath.isEmpty())
        obj.insert(QLatin1String("fragmentShader"), fragmentShaderPath);
    if (!vertexShaderPath.isEmpty())
        obj.insert(QLatin1String("vertexShader"), vertexShaderPath);
    if (!kwinFragmentShaderPath.isEmpty())
        obj.insert(QLatin1String("kwinFragmentShader"), kwinFragmentShaderPath);
    if (!previewPath.isEmpty())
        obj.insert(QLatin1String("preview"), previewPath);

    if (!parameters.isEmpty()) {
        QJsonArray params;
        for (const auto& p : parameters) {
            QJsonObject pObj;
            pObj.insert(QLatin1String("id"), p.id);
            if (!p.name.isEmpty())
                pObj.insert(QLatin1String("name"), p.name);
            if (!p.type.isEmpty())
                pObj.insert(QLatin1String("type"), p.type);
            if (p.defaultValue.isValid())
                pObj.insert(QLatin1String("default"), QJsonValue::fromVariant(p.defaultValue));
            if (p.minValue.isValid())
                pObj.insert(QLatin1String("min"), QJsonValue::fromVariant(p.minValue));
            if (p.maxValue.isValid())
                pObj.insert(QLatin1String("max"), QJsonValue::fromVariant(p.maxValue));
            params.append(pObj);
        }
        obj.insert(QLatin1String("parameters"), params);
    }

    return obj;
}

AnimationShaderEffect AnimationShaderEffect::fromJson(const QJsonObject& obj)
{
    AnimationShaderEffect e;
    e.id = obj.value(QLatin1String("id")).toString();
    e.name = obj.value(QLatin1String("name")).toString();
    e.description = obj.value(QLatin1String("description")).toString();
    e.author = obj.value(QLatin1String("author")).toString();
    e.version = obj.value(QLatin1String("version")).toString();
    e.category = obj.value(QLatin1String("category")).toString();
    e.fragmentShaderPath = obj.value(QLatin1String("fragmentShader")).toString();
    e.vertexShaderPath = obj.value(QLatin1String("vertexShader")).toString();
    e.kwinFragmentShaderPath = obj.value(QLatin1String("kwinFragmentShader")).toString();
    e.previewPath = obj.value(QLatin1String("preview")).toString();

    const QJsonArray params = obj.value(QLatin1String("parameters")).toArray();
    e.parameters.reserve(params.size());
    for (const QJsonValue& v : params) {
        const QJsonObject pObj = v.toObject();
        ParameterInfo p;
        p.id = pObj.value(QLatin1String("id")).toString();
        p.name = pObj.value(QLatin1String("name")).toString();
        p.type = pObj.value(QLatin1String("type")).toString();
        if (pObj.contains(QLatin1String("default")))
            p.defaultValue = pObj.value(QLatin1String("default")).toVariant();
        if (pObj.contains(QLatin1String("min")))
            p.minValue = pObj.value(QLatin1String("min")).toVariant();
        if (pObj.contains(QLatin1String("max")))
            p.maxValue = pObj.value(QLatin1String("max")).toVariant();
        e.parameters.append(std::move(p));
    }

    return e;
}

bool AnimationShaderEffect::operator==(const AnimationShaderEffect& other) const
{
    if (id != other.id || name != other.name || description != other.description)
        return false;
    if (author != other.author || version != other.version || category != other.category)
        return false;
    if (fragmentShaderPath != other.fragmentShaderPath || vertexShaderPath != other.vertexShaderPath)
        return false;
    if (kwinFragmentShaderPath != other.kwinFragmentShaderPath)
        return false;
    if (sourceDir != other.sourceDir || isUserEffect != other.isUserEffect)
        return false;
    if (previewPath != other.previewPath)
        return false;
    if (parameters.size() != other.parameters.size())
        return false;
    for (int i = 0; i < parameters.size(); ++i) {
        const auto& a = parameters[i];
        const auto& b = other.parameters[i];
        if (a.id != b.id || a.name != b.name || a.type != b.type)
            return false;
        if (a.defaultValue != b.defaultValue || a.minValue != b.minValue || a.maxValue != b.maxValue)
            return false;
    }
    return true;
}

} // namespace PhosphorAnimationShaders
