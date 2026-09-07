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
    e.needsCursor = obj.value(QLatin1String("needsCursor")).toBool(false);

    e.isMultipass = obj.value(QLatin1String("multipass")).toBool(false);
    const QJsonArray bufArr = obj.value(QLatin1String("bufferShaders")).toArray();
    for (const QJsonValue& v : bufArr) {
        const QString bufName = v.toString();
        if (bufName.isEmpty()) {
            continue;
        }
        if (e.bufferShaderPaths.size() >= PointerShaderContract::kMaxBufferPasses) {
            qCWarning(lcPointerEffect) << "Pointer effect" << e.id << "declares more than"
                                       << PointerShaderContract::kMaxBufferPasses << "buffer passes; ignoring"
                                       << bufName;
            continue;
        }
        e.bufferShaderPaths.append(bufName);
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

    // Textures: capped at the contract budget; an empty path maps to nothing
    // and is dropped (which shifts later slots, so it is logged).
    const QJsonArray texArr = obj.value(QLatin1String("textures")).toArray();
    if (texArr.size() > PointerShaderContract::kMaxUserTextureSlots) {
        qCWarning(lcPointerEffect) << "Pointer effect" << e.id << "declares" << texArr.size() << "textures; cap is"
                                   << PointerShaderContract::kMaxUserTextureSlots << "; surplus dropped";
    }
    for (const QJsonValue& v : texArr) {
        if (e.textures.size() >= PointerShaderContract::kMaxUserTextureSlots) {
            break;
        }
        const QJsonObject tObj = v.toObject();
        TextureSlot t;
        t.path = tObj.value(QLatin1String("path")).toString();
        t.wrap = tObj.value(QLatin1String("wrap")).toString();
        if (!t.wrap.isEmpty() && !PointerShaderContract::isValidWrapToken(t.wrap)) {
            qCWarning(lcPointerEffect) << "Pointer effect" << e.id << "declares unknown wrap value" << t.wrap
                                       << "; reset to runtime default";
            t.wrap.clear();
        }
        if (t.path.isEmpty()) {
            qCWarning(lcPointerEffect) << "Pointer effect" << e.id
                                       << "has a texture entry with an empty path; dropped (later slots shift)";
            continue;
        }
        e.textures.append(std::move(t));
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

QVariantMap PointerShaderEffect::defaultParams() const
{
    QVariantMap out;
    for (const auto& p : parameters) {
        if (p.defaultValue.isValid() && !p.defaultValue.isNull()) {
            out.insert(p.id, p.defaultValue);
        }
    }
    return out;
}

double PointerShaderEffect::resolvedReach(const QVariantMap& params) const
{
    double value = reach;
    if (!reachParam.isEmpty()) {
        const auto declared = std::find_if(parameters.cbegin(), parameters.cend(), [this](const ParameterInfo& p) {
            return p.id == reachParam;
        });
        const bool numericType = declared != parameters.cend()
            && (declared->type == QLatin1String("float") || declared->type == QLatin1String("int"));
        if (numericType) {
            const auto it = params.constFind(reachParam);
            bool ok = false;
            double candidate = 0.0;
            if (it != params.constEnd()) {
                candidate = it->toDouble(&ok);
            }
            if (!ok && declared->defaultValue.isValid()) {
                candidate = declared->defaultValue.toDouble(&ok);
            }
            if (ok) {
                value = candidate;
            }
        }
    }
    return std::clamp(value, 0.0, kMaxReach);
}

} // namespace PhosphorPointerShaders
