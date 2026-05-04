// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationShaderEffect.h>

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
    if (!previewPath.isEmpty())
        obj.insert(QLatin1String("preview"), previewPath);
    // Clamp on emit so the JSON never carries an out-of-range value that
    // fromJson would silently re-clamp on read. This makes `fromJson(toJson(x))`
    // idempotent — successive round-trips produce a stable struct, even
    // when the source `boundsPadding` was assigned out-of-range
    // programmatically. (A single round-trip from an out-of-range source
    // intentionally does NOT preserve the original out-of-range value;
    // that's the whole point of the clamp.) The lower-bound check
    // (`> 0.0` vs `>= 0.0`) is intentional: 0.0 is the documented default
    // and is omitted from the JSON to keep authored metadata.json files
    // terse.
    {
        const qreal clampedPadding = qBound(0.0, boundsPadding, 2.0);
        if (clampedPadding > 0.0)
            obj.insert(QLatin1String("boundsPadding"), clampedPadding);
    }

    if (!parameters.isEmpty()) {
        QJsonArray params;
        for (const auto& p : parameters) {
            QJsonObject pObj;
            pObj.insert(QLatin1String("id"), p.id);
            if (!p.name.isEmpty())
                pObj.insert(QLatin1String("name"), p.name);
            if (!p.type.isEmpty())
                pObj.insert(QLatin1String("type"), p.type);
            if (!p.description.isEmpty())
                pObj.insert(QLatin1String("description"), p.description);
            if (!p.group.isEmpty())
                pObj.insert(QLatin1String("group"), p.group);
            if (p.defaultValue.isValid())
                pObj.insert(QLatin1String("default"), QJsonValue::fromVariant(p.defaultValue));
            if (p.minValue.isValid())
                pObj.insert(QLatin1String("min"), QJsonValue::fromVariant(p.minValue));
            if (p.maxValue.isValid())
                pObj.insert(QLatin1String("max"), QJsonValue::fromVariant(p.maxValue));
            if (p.stepValue.isValid())
                pObj.insert(QLatin1String("step"), QJsonValue::fromVariant(p.stepValue));
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
    e.previewPath = obj.value(QLatin1String("preview")).toString();
    // Clamp negatives at the input boundary — a negative boundsPadding
    // would silently propagate into surfaceanimator.cpp's geometry math
    // (negative-area shader bounds) and into the shader's uv→anchorUv
    // remap (sample outside [0,1] always → fully transparent leg).
    //
    // Upper cap of 2.0 keeps the FBO area sane: at pad=2.0 the shader
    // effect is 5× the anchor on each axis (k=1+2*pad=5) → 25× area.
    // A 1080p anchor at RGBA8 then needs ~200 MB of FBO, already at the
    // edge of what Vulkan validation will pass on integrated GPUs.
    // No shipping shader needs >1.0 padding (morph uses 0.5); 2.0
    // accommodates plugin authors with extreme silhouette warps without
    // permitting the prior 4.0-cap excess (9× axis = 81× area).
    e.boundsPadding = qBound(0.0, obj.value(QLatin1String("boundsPadding")).toDouble(0.0), 2.0);

    const QJsonArray params = obj.value(QLatin1String("parameters")).toArray();
    e.parameters.reserve(params.size());
    for (const QJsonValue& v : params) {
        const QJsonObject pObj = v.toObject();
        ParameterInfo p;
        p.id = pObj.value(QLatin1String("id")).toString();
        p.name = pObj.value(QLatin1String("name")).toString();
        p.type = pObj.value(QLatin1String("type")).toString();
        p.description = pObj.value(QLatin1String("description")).toString();
        p.group = pObj.value(QLatin1String("group")).toString();
        if (pObj.contains(QLatin1String("default")))
            p.defaultValue = pObj.value(QLatin1String("default")).toVariant();
        if (pObj.contains(QLatin1String("min")))
            p.minValue = pObj.value(QLatin1String("min")).toVariant();
        if (pObj.contains(QLatin1String("max")))
            p.maxValue = pObj.value(QLatin1String("max")).toVariant();
        if (pObj.contains(QLatin1String("step")))
            p.stepValue = pObj.value(QLatin1String("step")).toVariant();
        e.parameters.append(std::move(p));
    }

    return e;
}

bool AnimationShaderEffect::operator==(const AnimationShaderEffect& other) const
{
    // Equality is "is this the same effect from the same on-disk
    // origin?", not "do these two structs serialise identically."
    // `sourceDir` and `isUserEffect` are stamped by the registry loader
    // (`AnimationShaderRegistry::parseEffect`) at scan time and are not
    // round-tripped through `toJson`. A struct freshly built from
    // `fromJson(toJson(x))` therefore has empty `sourceDir` and
    // `isUserEffect == false`, so `x == fromJson(toJson(x))` is FALSE
    // whenever `x` came from the registry — that's the contract, not a
    // bug. Tests that need round-trip equality should compare against a
    // copy that's had `sourceDir` / `isUserEffect` cleared.
    if (id != other.id || name != other.name || description != other.description)
        return false;
    if (author != other.author || version != other.version || category != other.category)
        return false;
    if (fragmentShaderPath != other.fragmentShaderPath || vertexShaderPath != other.vertexShaderPath)
        return false;
    if (sourceDir != other.sourceDir || isUserEffect != other.isUserEffect)
        return false;
    if (previewPath != other.previewPath)
        return false;
    if (!qFuzzyCompare(boundsPadding + 1.0, other.boundsPadding + 1.0))
        return false;
    if (parameters.size() != other.parameters.size())
        return false;
    for (int i = 0; i < parameters.size(); ++i) {
        const auto& a = parameters[i];
        const auto& b = other.parameters[i];
        if (a.id != b.id || a.name != b.name || a.type != b.type)
            return false;
        if (a.description != b.description || a.group != b.group)
            return false;
        if (a.defaultValue != b.defaultValue || a.minValue != b.minValue || a.maxValue != b.maxValue
            || a.stepValue != b.stepValue)
            return false;
    }
    return true;
}

} // namespace PhosphorAnimationShaders
