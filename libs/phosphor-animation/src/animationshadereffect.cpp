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
    if (boundsPadding > 0.0)
        obj.insert(QLatin1String("boundsPadding"), boundsPadding);
    if (isMultipass)
        obj.insert(QLatin1String("multipass"), true);
    if (!bufferShaderPaths.isEmpty()) {
        QJsonArray arr;
        for (const auto& p : bufferShaderPaths)
            arr.append(p);
        obj.insert(QLatin1String("bufferShaders"), arr);
    }
    if (useWallpaper)
        obj.insert(QLatin1String("wallpaper"), true);
    if (bufferFeedback)
        obj.insert(QLatin1String("bufferFeedback"), true);
    if (!qFuzzyCompare(bufferScale + 1.0, 2.0))
        obj.insert(QLatin1String("bufferScale"), bufferScale);
    if (!bufferWrap.isEmpty())
        obj.insert(QLatin1String("bufferWrap"), bufferWrap);
    if (!bufferWraps.isEmpty()) {
        QJsonArray arr;
        for (const auto& w : bufferWraps)
            arr.append(w);
        obj.insert(QLatin1String("bufferWraps"), arr);
    }
    if (!bufferFilter.isEmpty())
        obj.insert(QLatin1String("bufferFilter"), bufferFilter);
    if (!bufferFilters.isEmpty()) {
        QJsonArray arr;
        for (const auto& f : bufferFilters)
            arr.append(f);
        obj.insert(QLatin1String("bufferFilters"), arr);
    }
    if (useDepthBuffer)
        obj.insert(QLatin1String("depthBuffer"), true);

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
    e.previewPath = obj.value(QLatin1String("preview")).toString();
    e.isMultipass = obj.value(QLatin1String("multipass")).toBool(false);
    const QJsonArray bufArr = obj.value(QLatin1String("bufferShaders")).toArray();
    for (const QJsonValue& v : bufArr) {
        const QString name = v.toString();
        if (!name.isEmpty())
            e.bufferShaderPaths.append(name);
    }
    e.useWallpaper = obj.value(QLatin1String("wallpaper")).toBool(false);
    e.bufferFeedback = obj.value(QLatin1String("bufferFeedback")).toBool(false);
    e.bufferScale = qBound(0.125, obj.value(QLatin1String("bufferScale")).toDouble(1.0), 1.0);
    e.bufferWrap = obj.value(QLatin1String("bufferWrap")).toString();
    const QJsonArray wrapsArr = obj.value(QLatin1String("bufferWraps")).toArray();
    for (const QJsonValue& v : wrapsArr) {
        const QString w = v.toString();
        if (!w.isEmpty())
            e.bufferWraps.append(w);
    }
    e.bufferFilter = obj.value(QLatin1String("bufferFilter")).toString();
    const QJsonArray filtersArr = obj.value(QLatin1String("bufferFilters")).toArray();
    for (const QJsonValue& v : filtersArr) {
        const QString f = v.toString();
        if (!f.isEmpty())
            e.bufferFilters.append(f);
    }
    e.useDepthBuffer = obj.value(QLatin1String("depthBuffer")).toBool(false);

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
    if (sourceDir != other.sourceDir || isUserEffect != other.isUserEffect)
        return false;
    if (previewPath != other.previewPath)
        return false;
    if (!qFuzzyCompare(boundsPadding + 1.0, other.boundsPadding + 1.0))
        return false;
    if (isMultipass != other.isMultipass || useWallpaper != other.useWallpaper || bufferFeedback != other.bufferFeedback
        || useDepthBuffer != other.useDepthBuffer)
        return false;
    if (!qFuzzyCompare(bufferScale + 1.0, other.bufferScale + 1.0))
        return false;
    if (bufferShaderPaths != other.bufferShaderPaths || bufferWrap != other.bufferWrap
        || bufferWraps != other.bufferWraps || bufferFilter != other.bufferFilter
        || bufferFilters != other.bufferFilters)
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
