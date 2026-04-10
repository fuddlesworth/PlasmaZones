// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "animationshaderregistry.h"
#include "../common/animationtreedata.h"
#include "logging.h"

#include <QDir>
#include <QJsonObject>

namespace PlasmaZones {

AnimationShaderRegistry* AnimationShaderRegistry::s_instance = nullptr;

AnimationShaderRegistry::AnimationShaderRegistry(QObject* parent)
    : BaseShaderRegistry(parent)
{
#ifdef PLASMAZONES_SHADERS_ENABLED
    ensureUserShaderDirExists();
    setupFileWatcher();
    refresh();
#else
    qCInfo(lcCore) << "Animation shader effects disabled (Qt6::ShaderTools not available)";
#endif

    s_instance = this;
}

void AnimationShaderRegistry::refresh()
{
#ifdef PLASMAZONES_SHADERS_ENABLED
    BaseShaderRegistry::refresh();
#endif
}

AnimationShaderRegistry::~AnimationShaderRegistry()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

AnimationShaderRegistry* AnimationShaderRegistry::instance()
{
    return s_instance;
}

QVariantList AnimationShaderRegistry::availableAnimationShadersVariant() const
{
    QVariantList list;
    for (auto it = m_animationShaders.constBegin(); it != m_animationShaders.constEnd(); ++it) {
        list.append(animationShaderInfoToVariantMap(it.value()));
    }
    return list;
}

QVariantMap AnimationShaderRegistry::animationShaderInfo(const QString& id) const
{
    auto it = m_animationShaders.constFind(id);
    if (it != m_animationShaders.constEnd()) {
        return animationShaderInfoToVariantMap(it.value());
    }
    return {};
}

AnimationShaderRegistry::AnimationShaderInfo AnimationShaderRegistry::animationShader(const QString& id) const
{
    return m_animationShaders.value(id);
}

void AnimationShaderRegistry::onShaderLoaded(const QString& id, const QJsonObject& root, const QString& shaderDir,
                                             bool isUserShader)
{
    Q_UNUSED(isUserShader)

    AnimationShaderInfo info;

    // Copy base fields from the already-parsed BaseShaderInfo
    const BaseShaderInfo& base = baseShaders().value(id);
    static_cast<BaseShaderInfo&>(info) = base;

    QDir dir(shaderDir);

    // Vertex shader (optional — empty string means KWin generates default MapTexture vertex shader)
    const QString vertShaderName = root.value(QLatin1String("vertexShader")).toString();
    if (!vertShaderName.isEmpty()) {
        info.vertexShaderPath = dir.filePath(vertShaderName);
    }

    // KWin-specific fragment shader (GLSL 1.10 with individual uniforms).
    // If metadata specifies "kwinFragmentShader", use that. Otherwise auto-detect
    // "effect_kwin.frag" alongside the main fragment shader.
    const QString kwinShaderName = root.value(QLatin1String("kwinFragmentShader")).toString();
    if (!kwinShaderName.isEmpty()) {
        info.kwinFragmentShaderPath = dir.filePath(kwinShaderName);
    } else {
        const QString autoKwin = dir.filePath(QStringLiteral("effect_kwin.frag"));
        if (QFile::exists(autoKwin)) {
            info.kwinFragmentShaderPath = autoKwin;
        }
    }

    // Recommended subdivision count (1 = single quad, higher = more vertices for deformation)
    info.subdivisions = qBound(1, root.value(QLatin1String("subdivisions")).toInt(1), 64);

    // Geometry mode: tells the C++ compositor which geometry transform to apply
    // alongside the shader's visual effects. Values: "morph" (default), "popin", "slidefade".
    const QString geo = root.value(QLatin1String("geometry")).toString(QStringLiteral("morph"));
    bool validGeo = false;
    for (int i = 0; i < ValidGeometryModeCount; ++i) {
        if (geo == QLatin1String(ValidGeometryModes[i])) {
            validGeo = true;
            break;
        }
    }
    if (!validGeo) {
        qCWarning(lcCore) << "AnimationShaderRegistry: unknown geometry mode" << geo << "for" << id
                          << "— falling back to morph";
        info.geometryMode = QStringLiteral("morph");
    } else {
        info.geometryMode = geo;
    }

    m_animationShaders[id] = info;

    qCDebug(lcCore) << "Animation shader loaded:" << id << "geometry:" << info.geometryMode << "vertex:"
                    << (info.vertexShaderPath.isEmpty() ? QStringLiteral("(default)") : info.vertexShaderPath)
                    << "subdivisions:" << info.subdivisions;
}

void AnimationShaderRegistry::onShaderRemoved(const QString& id)
{
    m_animationShaders.remove(id);
}

QVariantMap AnimationShaderRegistry::animationShaderInfoToVariantMap(const AnimationShaderInfo& info) const
{
    // Start with base fields from the parent class helper
    QVariantMap map = shaderInfo(info.id);

    // Add animation-specific fields
    map[QLatin1String("vertexShaderPath")] = info.vertexShaderPath;
    map[QLatin1String("kwinFragmentShaderPath")] = info.kwinFragmentShaderPath;
    map[QLatin1String("subdivisions")] = info.subdivisions;
    map[QLatin1String("hasVertexShader")] = !info.vertexShaderPath.isEmpty();
    map[QLatin1String("geometry")] = info.geometryMode;

    return map;
}

} // namespace PlasmaZones
