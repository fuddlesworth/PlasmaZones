// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shaderregistry.h"
#include "shaderutils.h"
#include "logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUrl>

namespace PlasmaZones {

ShaderRegistry* ShaderRegistry::s_instance = nullptr;

ShaderRegistry::ShaderRegistry(QObject* parent)
    : BaseShaderRegistry(parent)
{
#ifdef PLASMAZONES_SHADERS_ENABLED
    m_shadersEnabled = true;
    qCInfo(lcCore) << "Shader effects enabled (Qt6::ShaderTools available at build time)";
#else
    m_shadersEnabled = false;
    qCInfo(lcCore) << "Shader effects disabled (Qt6::ShaderTools not available at build time)";
#endif

    if (m_shadersEnabled) {
        ensureUserShaderDirExists();
        setupFileWatcher();
        refresh();
    }

    s_instance = this;
}

ShaderRegistry::~ShaderRegistry()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

ShaderRegistry* ShaderRegistry::instance()
{
    return s_instance;
}

QString ShaderRegistry::noneShaderUuid()
{
    return QString();
}

bool ShaderRegistry::isNoneShader(const QString& id)
{
    return id.isEmpty();
}

// ===============================================================================
// Overlay-specific hook: parse multipass, wallpaper, buffer fields
// ===============================================================================

void ShaderRegistry::onShaderLoaded(const QString& id, const QJsonObject& root, const QString& shaderDir,
                                    bool isUserShader)
{
    Q_UNUSED(isUserShader)

    ShaderInfo info;

    // Copy base fields from the already-parsed BaseShaderInfo
    const BaseShaderInfo& base = baseShaders().value(id);
    static_cast<BaseShaderInfo&>(info) = base;

    QDir dir(shaderDir);

    // Vertex shader path (default: zone.vert)
    const QString vertShaderName = root.value(QLatin1String("vertexShader")).toString(QStringLiteral("zone.vert"));
    info.vertexShaderPath = dir.filePath(vertShaderName);

    // Multi-pass: one or more buffer pass shaders (A->B->C->D)
    info.isMultipass = root.value(QLatin1String("multipass")).toBool(false);
    const QJsonArray bufferShadersArray = root.value(QLatin1String("bufferShaders")).toArray();
    if (!bufferShadersArray.isEmpty()) {
        for (int i = 0; i < qMin(bufferShadersArray.size(), 4); ++i) {
            const QString name = bufferShadersArray.at(i).toString();
            if (!name.isEmpty()) {
                info.bufferShaderPaths.append(dir.filePath(name));
            }
        }
    }
    if (info.bufferShaderPaths.isEmpty()) {
        const QString bufferShaderName =
            root.value(QLatin1String("bufferShader")).toString(QStringLiteral("buffer.frag"));
        info.bufferShaderPaths.append(dir.filePath(bufferShaderName));
    }
    if (info.isMultipass) {
        bool allExist = true;
        for (const QString& path : info.bufferShaderPaths) {
            if (!QFile::exists(path)) {
                qCWarning(lcCore) << "Multipass shader missing buffer shader:" << path;
                allExist = false;
                break;
            }
        }
        if (!allExist) {
            info.bufferShaderPaths.clear();
        }
    }

    // Desktop wallpaper subscription
    info.useWallpaper = root.value(QLatin1String("wallpaper")).toBool(false);

    info.bufferFeedback = root.value(QLatin1String("bufferFeedback")).toBool(false);
    qreal scale = root.value(QLatin1String("bufferScale")).toDouble(1.0);
    info.bufferScale = qBound(0.125, scale, 1.0);
    info.bufferWrap = normalizeWrapMode(root.value(QLatin1String("bufferWrap")).toString(QStringLiteral("clamp")));

    info.useDepthBuffer = root.value(QLatin1String("depthBuffer")).toBool(false);

    const QJsonArray bufferWrapsArray = root.value(QLatin1String("bufferWraps")).toArray();
    if (!bufferWrapsArray.isEmpty()) {
        for (const QJsonValue& v : bufferWrapsArray) {
            info.bufferWraps.append(normalizeWrapMode(v.toString()));
        }
        const int needed = info.bufferShaderPaths.size();
        while (info.bufferWraps.size() < needed) {
            info.bufferWraps.append(info.bufferWrap);
        }
        while (info.bufferWraps.size() > needed) {
            info.bufferWraps.removeLast();
        }
    }

    info.bufferFilter =
        normalizeFilterMode(root.value(QLatin1String("bufferFilter")).toString(QStringLiteral("linear")));

    const QJsonArray bufferFiltersArray = root.value(QLatin1String("bufferFilters")).toArray();
    if (!bufferFiltersArray.isEmpty()) {
        for (const QJsonValue& v : bufferFiltersArray) {
            info.bufferFilters.append(normalizeFilterMode(v.toString()));
        }
        const int needed = info.bufferShaderPaths.size();
        while (info.bufferFilters.size() < needed) {
            info.bufferFilters.append(info.bufferFilter);
        }
        while (info.bufferFilters.size() > needed) {
            info.bufferFilters.removeLast();
        }
    }

    // Reject multipass shaders with missing buffer shaders (same validation as before)
    if (info.isMultipass && info.bufferShaderPaths.isEmpty()) {
        qCWarning(lcCore) << "Skipping multipass shader (missing buffer shader(s)):" << shaderDir;
        return;
    }

    m_overlayShaders.insert(id, info);
}

void ShaderRegistry::onShaderRemoved(const QString& id)
{
    m_overlayShaders.remove(id);
}

// ===============================================================================
// Overlay-specific query methods
// ===============================================================================

QList<ShaderRegistry::ShaderInfo> ShaderRegistry::availableShaders() const
{
    return m_overlayShaders.values();
}

ShaderRegistry::ShaderInfo ShaderRegistry::shader(const QString& id) const
{
    return m_overlayShaders.value(id);
}

QVariantMap ShaderRegistry::shaderInfo(const QString& id) const
{
    if (!m_overlayShaders.contains(id)) {
        return QVariantMap();
    }
    return overlayShaderInfoToVariantMap(m_overlayShaders.value(id));
}

QVariantList ShaderRegistry::availableShadersVariant() const
{
    QVariantList result;
    for (const ShaderInfo& info : m_overlayShaders) {
        result.append(overlayShaderInfoToVariantMap(info));
    }
    return result;
}

bool ShaderRegistry::shadersEnabled() const
{
    return m_shadersEnabled;
}

bool ShaderRegistry::userShadersEnabled() const
{
    return m_shadersEnabled;
}

bool ShaderRegistry::validateParams(const QString& id, const QVariantMap& params) const
{
    // Try overlay map first; fall back to base registry for shaders that
    // exist in m_shaders but were not promoted to m_overlayShaders (e.g.
    // multipass shaders with missing buffer files).
    const QList<ParameterInfo>* paramList = nullptr;

    auto overlayIt = m_overlayShaders.constFind(id);
    if (overlayIt != m_overlayShaders.constEnd()) {
        paramList = &overlayIt->parameters;
    } else {
        auto baseIt = baseShaders().constFind(id);
        if (baseIt != baseShaders().constEnd()) {
            paramList = &baseIt->parameters;
        } else {
            return false;
        }
    }

    for (const ParameterInfo& param : *paramList) {
        if (!params.contains(param.id)) {
            continue;
        }
        if (!validateParameterValue(param, params.value(param.id))) {
            qCWarning(lcCore) << "Invalid shader parameter:" << param.id << "for shader:" << id;
            return false;
        }
    }
    return true;
}

void ShaderRegistry::refresh()
{
    if (!m_shadersEnabled) {
        return;
    }
    BaseShaderRegistry::refresh();
}

void ShaderRegistry::reportShaderBakeStarted(const QString& shaderId)
{
    Q_EMIT shaderCompilationStarted(shaderId);
}

void ShaderRegistry::reportShaderBakeFinished(const QString& shaderId, bool success, const QString& error)
{
    Q_EMIT shaderCompilationFinished(shaderId, success, error);
}

// ===============================================================================
// Overlay-specific VariantMap (adds multipass/wallpaper/buffer fields)
// ===============================================================================

QVariantMap ShaderRegistry::overlayShaderInfoToVariantMap(const ShaderInfo& info) const
{
    // Start with base fields from the parent class helper
    QVariantMap map = BaseShaderRegistry::shaderInfo(info.id);

    // Override isValid with the overlay-aware version
    map[QLatin1String("isValid")] = info.isValid();

    // Overlay-specific fields
    map[QLatin1String("bufferShaderPaths")] = info.bufferShaderPaths;
    map[QLatin1String("bufferFeedback")] = info.bufferFeedback;
    map[QLatin1String("bufferScale")] = info.bufferScale;
    map[QLatin1String("bufferWrap")] = info.bufferWrap;
    if (!info.bufferWraps.isEmpty()) {
        map[QLatin1String("bufferWraps")] = QVariant::fromValue(info.bufferWraps);
    }
    map[QLatin1String("bufferFilter")] = info.bufferFilter;
    if (!info.bufferFilters.isEmpty()) {
        map[QLatin1String("bufferFilters")] = QVariant::fromValue(info.bufferFilters);
    }
    map[QLatin1String("wallpaper")] = info.useWallpaper;
    map[QLatin1String("depthBuffer")] = info.useDepthBuffer;

    return map;
}

} // namespace PlasmaZones
