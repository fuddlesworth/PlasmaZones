// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "desktoptransitionmanager.h"

#include "plasmazoneseffect/plasmazoneseffect.h"
#include "shadertransitionmanager.h"
#include "plasmazoneseffect/shader_internal.h"
#include "transitionpasshelpers.h"
#include "compositor/effectlogging.h"

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>
#include <PhosphorShaders/ShaderParamPreamble.h>

#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

#include <memory>

// The assembly part of DesktopTransitionManager: how a pack's source BECOMES a
// compiled GLShader with cached uniform locations. desktoptransitionmanager.cpp
// keeps the drive part (resolve, begin, blend), desktoptransitioncapture.cpp the
// capture part, and desktoptransitionteardown.cpp the teardown part; the compile
// pipeline (read → entry-point scaffold → include expansion → param preamble →
// KWin define → generateCustomShader) only serves this question, so it lives here.
namespace PlasmaZones {

DesktopTransitionManager::CompiledDesktopShader* DesktopTransitionManager::compiledShader(const QString& effectId)
{
    auto cached = m_shaderCache.find(effectId);
    if (cached != m_shaderCache.end()) {
        return &cached->second; // may hold a null shader sentinel (compile failed) — caller checks
    }

    // Insert a default (null-shader) entry up front so every early-return path
    // caches a sentinel and never recompiles a broken pack every frame.
    CompiledDesktopShader& compiled = m_shaderCache.emplace(effectId, CompiledDesktopShader{}).first->second;

    ShaderTransitionManager& mgr = m_effect->m_shaderManager;
    const PhosphorAnimationShaders::AnimationShaderEffect eff = mgr.shaderRegistry().effect(effectId);
    if (!eff.isValid()) {
        qCWarning(lcEffect) << "Unknown desktop transition effect id" << effectId;
        return &compiled; // sentinel: unknown id
    }

    QFile shaderFile(eff.fragmentShaderPath);
    if (!shaderFile.open(QIODevice::ReadOnly)) {
        qCWarning(lcEffect) << "Failed to open desktop shader file" << eff.fragmentShaderPath;
        return &compiled;
    }
    const QString rawSource = QString::fromUtf8(shaderFile.readAll());
    if (rawSource.isEmpty()) {
        qCWarning(lcEffect) << "Desktop shader file is empty" << eff.fragmentShaderPath;
        return &compiled;
    }

    QStringList animIncludePaths;
    for (const QString& sp : mgr.shaderRegistry().searchPaths()) {
        const QString sharedDir = sp + QStringLiteral("/shared");
        if (QDir(sharedDir).exists()) {
            animIncludePaths.append(sharedDir);
        }
    }

    // Reuse the exact per-window assembly: entry-point scaffold -> include
    // expansion -> named-param preamble -> KWin default-block define.
    const QString assembledSource = PhosphorShaders::assembleEntryPoint(
        rawSource, PhosphorAnimationShaders::AnimationShaderRegistry::animationEntryPrologue(),
        PhosphorAnimationShaders::AnimationShaderRegistry::animationEntryCandidates());
    QString includeError;
    const QString currentDir = QFileInfo(eff.fragmentShaderPath).absolutePath();
    QString expanded = PhosphorShaders::ShaderIncludeResolver::expandIncludes(assembledSource, currentDir,
                                                                              animIncludePaths, &includeError);
    if (expanded.isEmpty()) {
        qCWarning(lcEffect) << "Failed to expand desktop shader includes for" << effectId << ":" << includeError;
        return &compiled;
    }
    expanded = PhosphorShaders::spliceAfterVersion(
        expanded, PhosphorAnimationShaders::AnimationShaderRegistry::paramPreamble(eff));
    const QByteArray fragWithKwinDefine = ShaderInternal::injectKwinDefineAfterVersion(expanded);

    // Full-screen quad vertex stage, shared with the strip pass — see
    // TransitionPass::outputQuadVertexSource for the projection rationale.
    const QByteArray vertWithKwinDefine =
        ShaderInternal::injectKwinDefineAfterVersion(QString::fromUtf8(TransitionPass::outputQuadVertexSource()));

    std::unique_ptr<KWin::GLShader> shader = KWin::ShaderManager::instance()->generateCustomShader(
        KWin::ShaderTrait::MapTexture, vertWithKwinDefine, fragWithKwinDefine);
    if (shader) {
        compiled.iFromDesktopLoc = shader->uniformLocation("uFromDesktop");
        compiled.iToDesktopLoc = shader->uniformLocation("uToDesktop");
        compiled.iTimeLoc = shader->uniformLocation("iTime");
        compiled.iResolutionLoc = shader->uniformLocation("iResolution");
        compiled.iFrameLoc = shader->uniformLocation("iFrame");
        compiled.iSwitchDeltaLoc = shader->uniformLocation("iSwitchDelta");
        for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams; ++slot) {
            compiled.customParamsLoc[slot] = shader->uniformLocation(ShaderInternal::kCustomParamsElementNames[slot]);
        }
        for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors; ++slot) {
            compiled.customColorsLoc[slot] = shader->uniformLocation(ShaderInternal::kCustomColorsElementNames[slot]);
        }
        compiled.shader = std::move(shader);
    } else {
        qCWarning(lcEffect) << "Failed to compile desktop transition shader for" << effectId;
    }
    return &compiled;
}

} // namespace PlasmaZones
