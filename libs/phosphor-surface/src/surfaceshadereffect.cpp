// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSurface/SurfaceShaderEffect.h>

#include <PhosphorSurface/SurfaceShaderContract.h>

#include <QJsonArray>
#include <QJsonValue>
#include <QLoggingCategory>

namespace PhosphorSurfaceShaders {

namespace {
Q_LOGGING_CATEGORY(lcSurfaceShader, "phosphorsurfaceshaders.effect")
} // namespace

QJsonObject SurfaceShaderEffect::toJson() const
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

    if (isMultipass)
        obj.insert(QLatin1String("multipass"), true);
    if (animated)
        obj.insert(QLatin1String("animated"), true);
    if (!paddingParam.isEmpty())
        obj.insert(QLatin1String("paddingParam"), paddingParam);
    if (needsBackdrop)
        obj.insert(QLatin1String("needsBackdrop"), true);
    if (handlesOpacity)
        obj.insert(QLatin1String("handlesOpacity"), true);
    if (audio)
        obj.insert(QLatin1String("audio"), true);
    if (!bufferShaderPaths.isEmpty()) {
        QJsonArray arr;
        for (const auto& p : bufferShaderPaths)
            arr.append(p);
        obj.insert(QLatin1String("bufferShaders"), arr);
    }
    if (bufferFeedback)
        obj.insert(QLatin1String("bufferFeedback"), true);
    // qFuzzyCompare-against-default idiom: emit `bufferScale` only when
    // it diverges from the 1.0 default. The `+ 1.0` shift is the
    // standard Qt workaround for `qFuzzyCompare`'s zero-input
    // pathology — `qFuzzyCompare(0.0, 0.0)` returns true but
    // `qFuzzyCompare(1e-30, 0.0)` returns false. Comparing
    // `bufferScale + 1.0` against `2.0` keeps both operands away from
    // zero so the relative-tolerance check works for `bufferScale`
    // values like 0.125 too.
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

    if (!textures.isEmpty()) {
        QJsonArray texArr;
        for (const auto& t : textures) {
            // Skip empty-path entries to preserve fromJson(toJson(x))
            // round-trip stability — fromJson drops them on read, so
            // emitting them on write would cause the round-trip to
            // shrink the list silently. An entry with empty path but
            // non-empty wrap is also dropped: the wrap is meaningless
            // without a sampler bound to it, and `parseEffect` (see
            // SurfaceShaderRegistry::parseEffect) can produce exactly
            // this shape via its path-traversal guard, which clears path
            // while leaving wrap intact (defence in depth). Letting it
            // round-trip would silently smuggle a dead wrap value
            // through future scans.
            if (t.path.isEmpty())
                continue;
            QJsonObject tObj;
            tObj.insert(QLatin1String("path"), t.path);
            if (!t.wrap.isEmpty())
                tObj.insert(QLatin1String("wrap"), t.wrap);
            texArr.append(tObj);
        }
        if (!texArr.isEmpty())
            obj.insert(QLatin1String("textures"), texArr);
    }

    return obj;
}

SurfaceShaderEffect SurfaceShaderEffect::fromJson(const QJsonObject& obj)
{
    SurfaceShaderEffect e;
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
    e.animated = obj.value(QLatin1String("animated")).toBool(false);
    e.paddingParam = obj.value(QLatin1String("paddingParam")).toString();
    e.needsBackdrop = obj.value(QLatin1String("needsBackdrop")).toBool(false);
    e.handlesOpacity = obj.value(QLatin1String("handlesOpacity")).toBool(false);
    e.audio = obj.value(QLatin1String("audio")).toBool(false);
    const QJsonArray bufArr = obj.value(QLatin1String("bufferShaders")).toArray();
    for (const QJsonValue& v : bufArr) {
        const QString name = v.toString();
        if (!name.isEmpty())
            e.bufferShaderPaths.append(name);
    }
    e.bufferFeedback = obj.value(QLatin1String("bufferFeedback")).toBool(false);
    e.bufferScale = qBound(kMinBufferScale, obj.value(QLatin1String("bufferScale")).toDouble(1.0), kMaxBufferScale);
    // Buffer wrap/filter share the texture-slot `wrap` guard's rationale:
    // an unknown token is a typo or foreign vocabulary that the runtime
    // would silently coerce to its default anyway, and keeping it in the
    // struct re-persists the typo through toJson on the next save. Warn and
    // reset to empty ("runtime default") instead. Vocabularies match the
    // runtime normalisers (ShaderNodeRhi::normalizeWrapMode/FilterMode).
    const auto validatedWrap = [](QString wrap, const char* field) -> QString {
        if (!wrap.isEmpty() && !SurfaceShaderContract::isValidWrapToken(wrap)) {
            qCWarning(lcSurfaceShader) << "SurfaceShaderEffect::fromJson: unknown" << field << "value" << wrap
                                       << ", reset to runtime default";
            wrap.clear();
        }
        return wrap;
    };
    const auto validatedFilter = [](QString filter, const char* field) -> QString {
        if (!filter.isEmpty() && filter != QLatin1String("linear") && filter != QLatin1String("nearest")
            && filter != QLatin1String("mipmap")) {
            qCWarning(lcSurfaceShader) << "SurfaceShaderEffect::fromJson: unknown" << field << "value" << filter
                                       << ", reset to runtime default";
            filter.clear();
        }
        return filter;
    };
    e.bufferWrap = validatedWrap(obj.value(QLatin1String("bufferWrap")).toString(), "bufferWrap");
    // The per-buffer override lists are positionally aligned with
    // bufferShaderPaths, so EVERY entry is kept in place: an invalid token is
    // replaced with empty (that slot falls back to the default) and an
    // originally-empty entry stays as the explicit "default for this slot"
    // marker. Dropping either kind would shift every later buffer's override —
    // and since toJson re-emits empties, a dropped empty would break alignment
    // on the very next load of a saved pack.
    const QJsonArray wrapsArr = obj.value(QLatin1String("bufferWraps")).toArray();
    for (const QJsonValue& v : wrapsArr) {
        e.bufferWraps.append(validatedWrap(v.toString(), "bufferWraps"));
    }
    e.bufferFilter = validatedFilter(obj.value(QLatin1String("bufferFilter")).toString(), "bufferFilter");
    const QJsonArray filtersArr = obj.value(QLatin1String("bufferFilters")).toArray();
    for (const QJsonValue& v : filtersArr) {
        e.bufferFilters.append(validatedFilter(v.toString(), "bufferFilters"));
    }
    e.useDepthBuffer = obj.value(QLatin1String("depthBuffer")).toBool(false);

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

    // Cap the texture list at the contract budget. Surplus entries are
    // silently dropped — exposing more would require both runtimes to
    // grow more sampler bindings. A future contract bump
    // (kMaxUserTextureSlots > 3) would loosen this cap automatically.
    const QJsonArray texArr = obj.value(QLatin1String("textures")).toArray();
    e.textures.reserve(qMin<qsizetype>(texArr.size(), SurfaceShaderContract::kMaxUserTextureSlots));
    qsizetype slotIndex = 0;
    int droppedEmpty = 0;
    for (const QJsonValue& v : texArr) {
        if (e.textures.size() >= SurfaceShaderContract::kMaxUserTextureSlots)
            break;
        const QJsonObject tObj = v.toObject();
        TextureSlot t;
        t.path = tObj.value(QLatin1String("path")).toString();
        t.wrap = tObj.value(QLatin1String("wrap")).toString();
        // Validate wrap against the documented vocabulary. An empty
        // string is allowed and means "use the runtime default"
        // (clamp on both runtimes). Any other value is a typo or a
        // deprecated/foreign vocabulary import — log a warning and
        // reset to empty so the runtime applies its default rather
        // than carrying a string the runtime will silently coerce
        // to clamp anyway. Keeping unknown values in the in-memory
        // struct would also round-trip them back through toJson,
        // re-persisting the typo to disk on the next save.
        if (!t.wrap.isEmpty() && !SurfaceShaderContract::isValidWrapToken(t.wrap)) {
            qCWarning(lcSurfaceShader) << "SurfaceShaderEffect::fromJson: unknown wrap value" << t.wrap << "for slot"
                                       << slotIndex << ", reset to runtime default";
            t.wrap.clear();
        }
        // Drop entries with no path — they would map to a sampler with
        // nothing bound. The runtimes would fall back to transparent
        // black, but persisting the empty slot in JSON is just noise.
        // The visible warning here matters: TextureSlot has no explicit
        // slot-index field; an empty entry preceding a populated one
        // SHIFTS the populated entry's runtime slot. Loud so authors
        // notice the implicit re-mapping.
        if (t.path.isEmpty()) {
            ++droppedEmpty;
        } else {
            if (droppedEmpty > 0) {
                qCWarning(lcSurfaceShader)
                    << "SurfaceShaderEffect::fromJson: textures[" << slotIndex << "] populated after" << droppedEmpty
                    << "empty entries; runtime slot will be shifted by that count "
                       "(empty entries are dropped, not preserved as gaps).";
            }
            e.textures.append(std::move(t));
        }
        ++slotIndex;
    }

    return e;
}

bool SurfaceShaderEffect::operator==(const SurfaceShaderEffect& other) const
{
    // Equality is "is this the same effect from the same on-disk
    // origin?", not "do these two structs serialise identically."
    // `sourceDir` and `isUserEffect` are stamped by the registry loader
    // (`SurfaceShaderRegistry::parseEffect`) at scan time and are not
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
    if (isMultipass != other.isMultipass || animated != other.animated || paddingParam != other.paddingParam
        || needsBackdrop != other.needsBackdrop || handlesOpacity != other.handlesOpacity || audio != other.audio
        || bufferFeedback != other.bufferFeedback || useDepthBuffer != other.useDepthBuffer)
        return false;
    if (!qFuzzyCompare(bufferScale + 1.0, other.bufferScale + 1.0))
        return false;
    if (bufferShaderPaths != other.bufferShaderPaths || bufferWrap != other.bufferWrap
        || bufferWraps != other.bufferWraps || bufferFilter != other.bufferFilter
        || bufferFilters != other.bufferFilters)
        return false;
    if (parameters.size() != other.parameters.size())
        return false;
    for (qsizetype i = 0; i < parameters.size(); ++i) {
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
    if (textures != other.textures)
        return false;
    return true;
}

} // namespace PhosphorSurfaceShaders
