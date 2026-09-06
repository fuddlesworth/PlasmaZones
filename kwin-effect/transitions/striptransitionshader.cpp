// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "striptransitionmanager.h"

#include "plasmazoneseffect/plasmazoneseffect.h"
#include "plasmazoneseffect/shader_internal.h"
#include "shadertransitionmanager.h"
#include "transitionpasshelpers.h"
#include "compositor/effectlogging.h"

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>
#include <PhosphorShaders/ShaderParamPreamble.h>

#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QStringList>

#include <memory>

// The assembly part of StripTransitionManager: how a strip pack's source
// BECOMES a compiled GLShader with cached uniform locations. Verbatim the
// desktop pipeline's shape (desktoptransitionshader.cpp) — read → entry-point
// scaffold → include expansion → param preamble → KWin define →
// generateCustomShader — differing only in which uniform locations are cached
// (the strip contract's uStrip / uBelow / iStripMotion / iStripAxis /
// iStripRect instead of the desktop's two samplers and iSwitchDelta).
namespace PlasmaZones {

StripTransitionManager::CompiledStripShader* StripTransitionManager::compiledShader(const QString& effectId)
{
    auto cached = m_shaderCache.find(effectId);
    if (cached != m_shaderCache.end()) {
        return &cached->second; // may hold a null shader sentinel (compile failed) — caller checks
    }

    // Insert a default (null-shader) entry up front so every early-return path
    // caches a sentinel and never recompiles a broken pack every frame.
    CompiledStripShader& compiled = m_shaderCache.emplace(effectId, CompiledStripShader{}).first->second;

    ShaderTransitionManager& mgr = m_effect->m_shaderManager;
    const PhosphorAnimationShaders::AnimationShaderEffect eff = mgr.shaderRegistry().effect(effectId);
    if (!eff.isValid()) {
        qCWarning(lcEffect) << "Unknown strip transition effect id" << effectId;
        return &compiled; // sentinel: unknown id
    }
    // Re-validate the strip contract HERE, not only in notifyLeg: a pack
    // hot-reload clears this cache (registry commit) without re-running
    // notifyLeg, so a pack edited mid-leg to drop "strip" from appliesTo
    // would otherwise recompile and run with uStrip unconsumed — pack-only
    // pixels over the whole output. The sentinel routes the armed pass to
    // the same abandon path as an uninstall.
    if (!PhosphorAnimationShaders::shaderEffectAppliesToEventPath(eff,
                                                                  PhosphorAnimation::ProfilePaths::ScrollingView)) {
        qCWarning(lcEffect) << "Effect" << effectId << "no longer declares the strip contract; abandoning strip pass";
        return &compiled; // sentinel: class contract lost (hot-reload edit)
    }

    QFile shaderFile(eff.fragmentShaderPath);
    if (!shaderFile.open(QIODevice::ReadOnly)) {
        qCWarning(lcEffect) << "Failed to open strip shader file" << eff.fragmentShaderPath;
        return &compiled;
    }
    const QString rawSource = QString::fromUtf8(shaderFile.readAll());
    if (rawSource.isEmpty()) {
        qCWarning(lcEffect) << "Strip shader file is empty" << eff.fragmentShaderPath;
        return &compiled;
    }
    // A pack that ships its own main() keeps it: assembleEntryPoint wraps
    // only entry-only sources. That bypasses the generated main's
    // PZ_FINALIZE_COLOR call, which on this pass is strip_transition.glsl's
    // re-composite of the pack's output over the undisplaced below-strip
    // content. Without it the quad (drawn with blending off) writes the
    // strip layer alone and every gap between the columns turns black for
    // the leg. Same predicate the scaffold acts on, checked on the raw body
    // it sees; abandon to the plain translation like a compile failure.
    if (PhosphorShaders::definesMain(rawSource)) {
        qCWarning(lcEffect) << "Strip effect" << effectId
                            << "defines its own main() and bypasses the strip entry point; abandoning strip pass";
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
    // The source-string legend the resolver's `#line <n> <i>` directives
    // refer to: a driver diagnostic names source string i, and without the
    // legend the journal shows an integer nothing maps back to a file.
    // Index 0 is the top-level source, which the resolver leaves for the
    // caller to fill.
    QStringList sourcePaths;
    QString expanded = PhosphorShaders::ShaderIncludeResolver::expandIncludes(
        assembledSource, currentDir, animIncludePaths, &includeError, nullptr, &sourcePaths);
    if (!sourcePaths.isEmpty()) {
        sourcePaths[0] = eff.fragmentShaderPath;
    }
    if (expanded.isEmpty()) {
        qCWarning(lcEffect) << "Failed to expand strip shader includes for" << effectId << ":" << includeError;
        return &compiled;
    }
    expanded = PhosphorShaders::spliceAfterVersion(
        expanded, PhosphorAnimationShaders::AnimationShaderRegistry::paramPreamble(eff));
    const QByteArray fragWithKwinDefine = ShaderInternal::injectKwinDefineAfterVersion(expanded);

    const QByteArray vertWithKwinDefine =
        ShaderInternal::injectKwinDefineAfterVersion(QString::fromUtf8(TransitionPass::outputQuadVertexSource()));

    std::unique_ptr<KWin::GLShader> shader = KWin::ShaderManager::instance()->generateCustomShader(
        KWin::ShaderTrait::MapTexture, vertWithKwinDefine, fragWithKwinDefine);
    bool abandonedForContract = false;
    if (shader && shader->uniformLocation("uStrip") < 0) {
        // A pack that never samples uStrip has it optimised out at link
        // time. The quad REPLACES the output (blend off), so running such a
        // pack discards the live scene for sourceless pixels — abandon to
        // the plain translation instead, via the same null-shader sentinel
        // as a compile failure.
        qCWarning(lcEffect) << "Strip effect" << effectId
                            << "never samples uStrip (getStripColor); abandoning strip pass";
        shader.reset();
        abandonedForContract = true;
    }
    if (shader && shader->uniformLocation("uBelow") < 0) {
        // The generated main's re-composite always samples uBelow, so a
        // link that optimised it out means the re-composite is not in the
        // program: strip_transition.glsl no longer installs it, or an
        // include shadowed it. Same consequence as a bypassed entry point
        // (black gaps), same abandonment; the definesMain check above
        // catches the pack-side cause before the compile.
        qCWarning(lcEffect) << "Strip effect" << effectId
                            << "links without the below-strip re-composite (uBelow); abandoning strip pass";
        shader.reset();
        abandonedForContract = true;
    }
    if (shader) {
        compiled.uStripLoc = shader->uniformLocation("uStrip");
        compiled.uBelowLoc = shader->uniformLocation("uBelow");
        compiled.iTimeLoc = shader->uniformLocation("iTime");
        compiled.iResolutionLoc = shader->uniformLocation("iResolution");
        compiled.iFrameLoc = shader->uniformLocation("iFrame");
        compiled.iStripMotionLoc = shader->uniformLocation("iStripMotion");
        compiled.iStripAxisLoc = shader->uniformLocation("iStripAxis");
        compiled.iStripRectLoc = shader->uniformLocation("iStripRect");
        for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams; ++slot) {
            compiled.customParamsLoc[slot] = shader->uniformLocation(ShaderInternal::kCustomParamsElementNames[slot]);
        }
        for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors; ++slot) {
            compiled.customColorsLoc[slot] = shader->uniformLocation(ShaderInternal::kCustomColorsElementNames[slot]);
        }
        compiled.shader = std::move(shader);
    } else if (!abandonedForContract) {
        // Only a genuine compile/link failure reaches here — the contract
        // abandonments above already reported their own reason. The legend
        // turns the driver's `<i>:<line>` into a file, since the pack's
        // includes are pasted in as numbered source strings.
        QStringList legend;
        for (qsizetype i = 0; i < sourcePaths.size(); ++i) {
            legend << QString::number(i) + QLatin1Char('=') + QFileInfo(sourcePaths.at(i)).fileName();
        }
        qCWarning(lcEffect) << "Failed to compile strip transition shader for" << effectId
                            << "(source strings:" << legend.join(QLatin1String(", ")) << ")";
    }
    return &compiled;
}

} // namespace PlasmaZones
