// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorPointer/PointerShaderEffect.h>

#include <PhosphorPointer/PointerShaderContract.h>

#include <PhosphorFsLoader/PackPathGuard.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QSet>

#include <algorithm>

namespace PhosphorPointerShaders {

namespace {
Q_LOGGING_CATEGORY(lcPointerEffect, "phosphorpointershaders.effect")

/// Resolve @p rawPath against @p sourceDir and confine it there. Absolute
/// paths a PACK FILE declares are subject to the same containment (Reject
/// policy): a pack ships its own assets, so an absolute path outside it is a
/// mistake or an escape. Returns an empty string on rejection.
QString confine(const QString& rawPath, const QString& sourceDir, const QString& effectId, const char* what)
{
    if (rawPath.isEmpty()) {
        return {};
    }
    const auto resolved =
        PhosphorFsLoader::resolveWithinDirectory(rawPath, sourceDir, PhosphorFsLoader::AbsolutePathPolicy::Reject);
    if (!resolved) {
        qCWarning(lcPointerEffect).noquote()
            << "Pointer effect" << effectId << what << rawPath << "resolves outside source dir" << sourceDir
            << "— rejected (path traversal guard)";
        return {};
    }
    return *resolved;
}

double clampedNumber(const QJsonObject& obj, const char* key, double fallback, double lo, double hi,
                     const QString& effectId)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (v.isUndefined()) {
        return fallback;
    }
    if (!v.isDouble()) {
        qCWarning(lcPointerEffect) << "Pointer effect" << effectId << "declares a non-numeric" << key << "; using"
                                   << fallback;
        return fallback;
    }
    return std::clamp(v.toDouble(fallback), lo, hi);
}

} // namespace

PointerShaderEffect PointerShaderEffect::fromJson(const QJsonObject& obj, const QString& sourceDir, bool isUser)
{
    PointerShaderEffect e;
    e.id = obj.value(QLatin1String("id")).toString();
    e.name = obj.value(QLatin1String("name")).toString();
    e.description = obj.value(QLatin1String("description")).toString();
    e.author = obj.value(QLatin1String("author")).toString();
    e.version = obj.value(QLatin1String("version")).toString();
    e.category = obj.value(QLatin1String("category")).toString();
    e.fragmentShaderPath = obj.value(QLatin1String("fragmentShader")).toString();
    e.vertexShaderPath = obj.value(QLatin1String("vertexShader")).toString();
    e.previewPath = obj.value(QLatin1String("preview")).toString();
    e.sourceDir = sourceDir;
    e.isUserEffect = isUser;

    const QString layerToken = obj.value(QLatin1String("layer")).toString();
    // The inverse of this parse is PointerShaderEffect::layerToken below; the
    // two spellings must stay in step.
    if (layerToken.isEmpty() || layerToken == QLatin1String("below")) {
        e.layer = Layer::Below;
    } else if (layerToken == QLatin1String("above")) {
        e.layer = Layer::Above;
    } else {
        qCWarning(lcPointerEffect) << "Pointer effect" << e.id << "declares unknown layer" << layerToken
                                   << "; using below";
        e.layer = Layer::Below;
    }

    e.reach = clampedNumber(obj, "reach", 64.0, 0.0, kMaxReach, e.id);
    e.reachParam = obj.value(QLatin1String("reachParam")).toString();
    e.trailSeconds = clampedNumber(obj, "trailSeconds", 1.0, 0.0, 60.0, e.id);
    if (e.trailSeconds == 0.0) {
        // Kept as declared (the validator already reports it as an error),
        // but the runtime has to say why nothing ever appears: liveness is a
        // strict "younger than trailSeconds" test, so at 0 no event is ever
        // young enough and the chain never requests a frame.
        qCWarning(lcPointerEffect) << "Pointer effect" << e.id
                                   << "declares trailSeconds 0, so it is never live and never draws";
    }
    // Defaults TRUE, unlike its neighbours: an undeclared pack keeps its say
    // in the chain's sample spacing, so an older pack that does read the trail
    // is never silently dropped out of the decision.
    e.samplesTrail = obj.value(QLatin1String("samplesTrail")).toBool(true);
    // Clamped to trailSeconds at the boundary as well as in resolvedTrailWindow,
    // so a declaration past the liveness window is reported once at load rather
    // than silently narrowed on every query.
    e.trailWindowSeconds = clampedNumber(obj, "trailWindowSeconds", 0.0, 0.0, e.trailSeconds, e.id);
    e.trailWindowParam = obj.value(QLatin1String("trailWindowParam")).toString();
    e.needsCursor = obj.value(QLatin1String("needsCursor")).toBool(false);

    e.isMultipass = obj.value(QLatin1String("multipass")).toBool(false);
    const QJsonArray bufArr = obj.value(QLatin1String("bufferShaders")).toArray();
    QStringList ignoredBuffers;
    for (const QJsonValue& v : bufArr) {
        const QString bufName = v.toString();
        if (bufName.isEmpty()) {
            continue;
        }
        if (e.bufferShaderPaths.size() >= PointerShaderContract::kMaxBufferPasses) {
            ignoredBuffers.append(bufName);
            continue;
        }
        e.bufferShaderPaths.append(bufName);
    }
    if (!ignoredBuffers.isEmpty()) {
        qCWarning(lcPointerEffect).noquote()
            << "Pointer effect" << e.id << "declares more than" << PointerShaderContract::kMaxBufferPasses
            << "buffer passes; ignoring" << ignoredBuffers.join(QLatin1String(", "));
    }
    e.bufferFeedback = obj.value(QLatin1String("bufferFeedback")).toBool(false);
    e.bufferScale = clampedNumber(obj, "bufferScale", 1.0, kMinBufferScale, kMaxBufferScale, e.id);

    // Parameters: declaration order assigns customParams / customColors
    // lanes, so the list stays ordered. A duplicate id would redefine the
    // same p_ macro and fail the shader compile, so the later entry is dropped.
    const QJsonArray params = obj.value(QLatin1String("parameters")).toArray();
    QSet<QString> seenParamIds;
    for (const QJsonValue& v : params) {
        if (e.parameters.size() >= PointerShaderContract::kMaxDeclaredParameters) {
            qCWarning(lcPointerEffect) << "Pointer effect" << e.id << "declares more than"
                                       << PointerShaderContract::kMaxDeclaredParameters
                                       << "parameters; surplus dropped";
            break;
        }
        const QJsonObject pObj = v.toObject();
        ParameterInfo p;
        p.id = pObj.value(QLatin1String("id")).toString();
        if (p.id.isEmpty()) {
            continue;
        }
        if (seenParamIds.contains(p.id)) {
            qCWarning(lcPointerEffect) << "Pointer effect" << e.id << "declares parameter id" << p.id
                                       << "more than once; ignoring the later entry";
            continue;
        }
        seenParamIds.insert(p.id);
        p.name = pObj.value(QLatin1String("name")).toString();
        p.type = pObj.value(QLatin1String("type")).toString();
        if (p.type == QLatin1String("image")) {
            // Neither the slot translator nor the preamble maps an image
            // parameter to anything, so the entry would be carried and
            // silently inert. Say so and point at the mechanism that works.
            qCWarning(lcPointerEffect) << "Pointer effect" << e.id << "declares parameter" << p.id
                                       << "with type image, which pointer packs do not support; declare the"
                                       << "texture in the top-level textures array instead (it binds as uTexture<N>)";
        }
        p.description = pObj.value(QLatin1String("description")).toString();
        p.group = pObj.value(QLatin1String("group")).toString();
        if (pObj.contains(QLatin1String("default"))) {
            p.defaultValue = pObj.value(QLatin1String("default")).toVariant();
        }
        if (pObj.contains(QLatin1String("min"))) {
            p.minValue = pObj.value(QLatin1String("min")).toVariant();
        }
        if (pObj.contains(QLatin1String("max"))) {
            p.maxValue = pObj.value(QLatin1String("max")).toVariant();
        }
        if (pObj.contains(QLatin1String("step"))) {
            p.stepValue = pObj.value(QLatin1String("step")).toVariant();
        }
        e.parameters.append(std::move(p));
    }

    // Textures: an empty path maps to nothing and is dropped (which shifts
    // later slots, so it is logged), then the survivors are capped at the
    // contract budget. The cap warning counts survivors, not raw entries, so
    // a list that only exceeds the cap through empty entries is not reported
    // as losing a texture it never had.
    const QJsonArray texArr = obj.value(QLatin1String("textures")).toArray();
    int declaredTextures = 0;
    for (const QJsonValue& v : texArr) {
        const QJsonObject tObj = v.toObject();
        TextureSlot t;
        t.path = tObj.value(QLatin1String("path")).toString();
        t.wrap = tObj.value(QLatin1String("wrap")).toString();
        if (t.path.isEmpty()) {
            qCWarning(lcPointerEffect) << "Pointer effect" << e.id
                                       << "has a texture entry with an empty path; dropped (later slots shift)";
            continue;
        }
        ++declaredTextures;
        if (e.textures.size() >= PointerShaderContract::kMaxUserTextureSlots) {
            continue;
        }
        if (!t.wrap.isEmpty() && !PointerShaderContract::isValidWrapToken(t.wrap)) {
            qCWarning(lcPointerEffect) << "Pointer effect" << e.id << "declares unknown wrap value" << t.wrap
                                       << "; reset to runtime default";
            t.wrap.clear();
        }
        e.textures.append(std::move(t));
    }
    if (declaredTextures > PointerShaderContract::kMaxUserTextureSlots) {
        qCWarning(lcPointerEffect) << "Pointer effect" << e.id << "declares" << declaredTextures << "textures; cap is"
                                   << PointerShaderContract::kMaxUserTextureSlots << "; surplus dropped";
    }

    if (sourceDir.isEmpty()) {
        // In-memory effect: nothing to anchor against, paths stay verbatim.
        if (e.isMultipass && e.bufferShaderPaths.isEmpty()) {
            e.isMultipass = false;
        }
        if (!e.isMultipass) {
            e.bufferShaderPaths.clear();
            e.bufferFeedback = false;
            e.bufferScale = 1.0;
        }
        return e;
    }

    // Resolve and confine every declared path to the pack dir. A rejected
    // fragment path fail-closes the pack (an empty frag compiles nothing).
    e.fragmentShaderPath = confine(e.fragmentShaderPath, sourceDir, e.id, "fragment shader");
    e.vertexShaderPath = confine(e.vertexShaderPath, sourceDir, e.id, "vertex shader");
    if (!e.previewPath.isEmpty()) {
        e.previewPath = confine(e.previewPath, sourceDir, e.id, "preview");
    } else {
        const QString conventional = QDir(sourceDir).filePath(QStringLiteral("preview.png"));
        if (QFile::exists(conventional)) {
            e.previewPath = conventional;
        }
    }

    for (auto& tex : e.textures) {
        const QString resolved = confine(tex.path, sourceDir, e.id, "texture");
        if (resolved.isEmpty()) {
            tex.wrap.clear();
        }
        tex.path = resolved;
    }
    e.textures.erase(std::remove_if(e.textures.begin(), e.textures.end(),
                                    [](const TextureSlot& t) {
                                        return t.path.isEmpty();
                                    }),
                     e.textures.end());

    // Multipass is fail-closed on any missing or escaping buffer: the pack
    // degrades to single-pass with a diagnostic rather than running a
    // partial chain.
    if (e.isMultipass && !e.bufferShaderPaths.isEmpty()) {
        QStringList resolved;
        QStringList missing;
        for (const QString& bufPath : e.bufferShaderPaths) {
            const QString abs = confine(bufPath, sourceDir, e.id, "buffer shader");
            if (!abs.isEmpty() && QFile::exists(abs)) {
                resolved.append(abs);
            } else if (abs.isEmpty()) {
                missing.append(bufPath + QLatin1String(" (rejected: escapes pack dir)"));
            } else {
                missing.append(abs + QLatin1String(" (not found)"));
            }
        }
        if (missing.isEmpty()) {
            e.bufferShaderPaths = resolved;
        } else {
            qCWarning(lcPointerEffect).noquote()
                << "Pointer effect" << e.id << "is missing" << missing.size() << "of" << e.bufferShaderPaths.size()
                << "declared buffer shader(s); disabling multipass. Missing:" << missing.join(QLatin1String(", "));
            e.isMultipass = false;
        }
    } else if (e.isMultipass) {
        e.isMultipass = false;
    }
    if (!e.isMultipass) {
        e.bufferShaderPaths.clear();
        e.bufferFeedback = false;
        e.bufferScale = 1.0;
    }

    return e;
}

QString PointerShaderEffect::layerToken(Layer layer)
{
    return layer == Layer::Above ? QStringLiteral("above") : QStringLiteral("below");
}

double PointerShaderEffect::resolvedParam(const QString& paramId, const QVariantMap& params, double fallback) const
{
    if (paramId.isEmpty()) {
        return fallback;
    }
    const auto declared = std::find_if(parameters.cbegin(), parameters.cend(), [&paramId](const ParameterInfo& p) {
        return p.id == paramId;
    });
    const bool numericType = declared != parameters.cend()
        && (declared->type == QLatin1String("float") || declared->type == QLatin1String("int"));
    if (!numericType) {
        return fallback;
    }
    // The user's value when present, the declaration's default when not: a
    // pack asked about before any override exists still answers with what it
    // will actually run at.
    const auto it = params.constFind(paramId);
    bool ok = false;
    double candidate = 0.0;
    if (it != params.constEnd()) {
        candidate = it->toDouble(&ok);
    }
    if (!ok && declared->defaultValue.isValid()) {
        candidate = declared->defaultValue.toDouble(&ok);
    }
    return ok ? candidate : fallback;
}

double PointerShaderEffect::resolvedTrailWindow(const QVariantMap& params) const
{
    if (!samplesTrail) {
        // Reads nothing, so it has no claim on the spacing.
        return 0.0;
    }
    // The declared static window when there is one, else the liveness figure,
    // and then the named parameter over either.
    const double declaredWindow = trailWindowSeconds > 0.0 ? trailWindowSeconds : trailSeconds;
    const double value = resolvedParam(trailWindowParam, params, declaredWindow);
    // Never past `trailSeconds`: the host stops feeding the ring that long
    // after the last event, so a window claiming more would spread the slots
    // across time the pack can never see samples from, coarsening the part it
    // CAN see for nothing.
    return std::clamp(value, 0.0, trailSeconds);
}

double PointerShaderEffect::resolvedReach(const QVariantMap& params) const
{
    const double value = resolvedParam(reachParam, params, reach);
    // The floor is deliberate (see kMinReach): a reach of 0 leaves the damage
    // rect of a single-sample burst with no area, so the pass would sit live
    // painting nothing. The ceiling bounds the per-frame repaint region.
    return std::clamp(value, kMinReach, kMaxReach);
}

} // namespace PhosphorPointerShaders
