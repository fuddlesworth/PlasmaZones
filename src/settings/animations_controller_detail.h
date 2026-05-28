// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Shared helpers between animationspagecontroller.cpp and
// animationspagecontroller_shaders.cpp. The two TUs split the same class
// across files to stay under the 800-line cap; both need to convert
// shader-effect / parameter / shader-profile values to QVariantMap for
// QML consumption. Inline definitions here ensure both TUs get their own
// copy without relying on unity-build TU merging for cross-TU linkage.

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>

#include <QLatin1Char>
#include <QLatin1String>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace PlasmaZones {
namespace animations_controller_detail {

inline QVariantMap parameterInfoToMap(const PhosphorAnimationShaders::AnimationShaderEffect::ParameterInfo& p)
{
    // Keys mirror PhosphorRendering::ShaderRegistry::parameterInfoToVariantMap
    // so animation packs and overlay packs share QML editor components.
    // Optional fields are emitted only when valid/non-empty.
    QVariantMap m;
    m.insert(QLatin1String("id"), p.id);
    m.insert(QLatin1String("name"), p.name);
    m.insert(QLatin1String("type"), p.type);
    if (!p.description.isEmpty())
        m.insert(QLatin1String("description"), p.description);
    if (!p.group.isEmpty())
        m.insert(QLatin1String("group"), p.group);
    if (p.defaultValue.isValid())
        m.insert(QLatin1String("default"), p.defaultValue);
    if (p.minValue.isValid())
        m.insert(QLatin1String("min"), p.minValue);
    if (p.maxValue.isValid())
        m.insert(QLatin1String("max"), p.maxValue);
    if (p.stepValue.isValid())
        m.insert(QLatin1String("step"), p.stepValue);
    return m;
}

inline QVariantMap effectToMap(const PhosphorAnimationShaders::AnimationShaderEffect& effect)
{
    QVariantMap m;
    m.insert(QLatin1String("id"), effect.id);
    m.insert(QLatin1String("name"), effect.name);
    m.insert(QLatin1String("description"), effect.description);
    m.insert(QLatin1String("author"), effect.author);
    m.insert(QLatin1String("version"), effect.version);
    m.insert(QLatin1String("category"), effect.category);
    m.insert(QLatin1String("isUserEffect"), effect.isUserEffect);
    // `previewPath` is resolved to an absolute path by the registry's
    // `parseEffect`, so QML can pass it directly to `Image.source` (with
    // a `file://` scheme prefix). Empty when the pack didn't ship a
    // preview — the page renders a placeholder for that case.
    m.insert(QLatin1String("previewPath"), effect.previewPath);
    QVariantList params;
    params.reserve(effect.parameters.size());
    for (const auto& p : effect.parameters) {
        params.append(parameterInfoToMap(p));
    }
    m.insert(QLatin1String("parameters"), params);
    return m;
}

inline QVariantMap shaderProfileToMap(const PhosphorAnimationShaders::ShaderProfile& profile)
{
    QVariantMap m;
    if (profile.effectId)
        m.insert(QLatin1String("effectId"), *profile.effectId);
    if (profile.parameters)
        m.insert(QLatin1String("parameters"), *profile.parameters);
    return m;
}

/// Collect every override path strictly DEEPER than @p path
/// (i.e. starting with `<path>.`). Centralises the prefix-match math
/// so shaderOverrideDescendantCount and clearShaderOverrideDescendants
/// share one definition of "descendant" — the trailing `.` boundary
/// is what excludes both the path itself ("popup") and unrelated
/// names with shared character-prefix ("popups"). Inline in this
/// header so sibling helpers in this namespace can call it without
/// depending on unity-build TU merging.
inline QStringList collectShaderOverrideDescendants(const PhosphorAnimationShaders::ShaderProfileTree& tree,
                                                    const QString& path)
{
    QStringList out;
    if (path.isEmpty())
        return out;
    const QString prefix = path + QLatin1Char('.');
    const QStringList paths = tree.overriddenPaths();
    for (const QString& p : paths) {
        if (p.startsWith(prefix))
            out.append(p);
    }
    return out;
}

} // namespace animations_controller_detail
} // namespace PlasmaZones
