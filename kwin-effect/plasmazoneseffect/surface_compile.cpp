// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Surface shader pack COMPILATION for the kwin-effect: registry path
// population, per-pack compile-and-cache (main pass + optional multipass
// buffer passes), and the per-window pack lookup. The RENDERING of compiled
// packs (buffer-pass draws, chain compositing, uniform pushes) lives in
// surfacelayers.cpp; the animation shader compile path this mirrors lives in
// shader_transitions.cpp.

#include "../plasmazoneseffect.h"
#include "shader_internal.h"

#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>
#include <PhosphorShaders/ShaderParamPreamble.h>
#include <PhosphorSurface/SurfaceShaderContract.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include <effect/effecthandler.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>

#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QIODevice>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector4D>

#include <array>
#include <utility>
#include <vector>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

using ShaderInternal::injectKwinDefineAfterVersion;
using ShaderInternal::kCustomColorsElementNames;
using ShaderInternal::kCustomParamsElementNames;

namespace {

// ── Fullscreen-quad primitive for the multipass SURFACE buffer passes ────────
//
// A buffer pass is an FBO→FBO blit: it samples the captured window surface
// (uTexture0) plus any prior buffer outputs (iChannelN) and writes its own FBO.
// There is no window quad to redirect here — the pass covers the whole target
// FBO — so we draw a unit triangle-strip quad in NDC directly, with NO
// modelViewProjectionMatrix (the position is already clip-space). The buffer's
// fragment uses surface_uniforms.glsl; injectKwinDefineAfterVersion on this
// vertex source isn't needed for the vertex stage (it declares no uniforms from
// the header), but the define IS injected so both stages compile against the
// same #version handling and the contract's KWin branch is selected in the frag.
//
// Attribute slots match KWin::VA_Position (0) / VA_TexCoord (1) per
// <opengl/glvertexbuffer.h>'s GLVertex2DLayout: explicit layout(location=N)
// decorations bind to those indices so vbo->setVertices(GLVertex2D{...}) feeds
// position@0 and texcoord@1 directly.
//
// NOTE: the fullscreen-quad DRAW helper (drawFullscreenQuad) lives in
// surfacelayers.cpp, the only TU that issues buffer-pass draws. It is not
// duplicated here (this TU only compiles the buffer-pass shaders) — under the
// kwin-effect's Unity build two anonymous-namespace definitions of the same
// helper would collide.
constexpr const char* kFullscreenQuadVertexSource =
    "#version 450\n"
    "layout(location = 0) in vec2 position;\n"
    "layout(location = 1) in vec2 texCoord;\n"
    "layout(location = 0) out vec2 vTexCoord;\n"
    "void main() {\n"
    "    vTexCoord = texCoord;\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "}\n";

} // namespace

SurfaceParamValues ShaderInternal::resolveSurfaceParamValues(const PhosphorSurfaceShaders::SurfaceShaderEffect& eff,
                                                             const QVariantMap& friendlyOverrides)
{
    namespace SC = PhosphorSurfaceShaders::SurfaceShaderContract;
    const QVariantMap surfaceParams =
        PhosphorSurfaceShaders::SurfaceShaderRegistry::translateSurfaceParams(eff, friendlyOverrides);
    SurfaceParamValues values;
    for (int slot = 0; slot < SC::kMaxCustomParams; ++slot) {
        auto pull = [&](char component) -> float {
            const auto it = surfaceParams.constFind(SC::slotKey(slot, component));
            if (it == surfaceParams.constEnd()) {
                return 0.0f;
            }
            bool ok = false;
            const float v = it->toFloat(&ok);
            return ok ? v : 0.0f;
        };
        values.params[static_cast<size_t>(slot)] = QVector4D(pull('x'), pull('y'), pull('z'), pull('w'));
    }
    for (int slot = 0; slot < SC::kMaxCustomColors; ++slot) {
        const auto it = surfaceParams.constFind(SC::colorKey(slot));
        if (it == surfaceParams.constEnd()) {
            continue;
        }
        const QColor c = it->value<QColor>();
        if (c.isValid()) {
            values.colors[static_cast<size_t>(slot)] = QVector4D(c.redF(), c.greenF(), c.blueF(), c.alphaF());
        }
    }
    return values;
}

void PlasmaZonesEffect::ensureSurfaceRegistryPaths()
{
    if (m_surfaceRegistryPathsAdded) {
        return;
    }
    m_surfaceRegistryPathsAdded = true;
    // Candidate dirs: every ${XDG_DATA_DIRS}/plasmazones/surface plus the user
    // data dir (~/.local/share/plasmazones/surface), where CMake installs
    // data/surface and where a user override would live. Added even when a dir
    // is missing so the registry's watcher promotes a parent-watch and picks up
    // packs that appear later (a fresh install). The daemon-delivered path
    // mechanism the animation registry uses (loadShaderRegistryFromDbus) is a
    // follow-up for surface packs; QStandardPaths covers the bundled pack today.
    QStringList paths;
    const QStringList bases = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    paths.reserve(bases.size());
    for (const QString& base : bases) {
        paths.append(base + QStringLiteral("/plasmazones/surface"));
    }
    if (!paths.isEmpty()) {
        m_surfaceShaderRegistry.addSearchPaths(paths);
    }
}

CompiledSurfacePack* PlasmaZonesEffect::compiledPack(const QString& packId,
                                                     const PhosphorSurfaceShaders::DecorationProfile& profile)
{
    if (!KWin::effects) {
        return nullptr;
    }
    if (packId.isEmpty()) {
        return nullptr;
    }
    // Per-pack-id cache: a hit returns the prior compile (success shader OR a
    // failure latch — compileFailed true + shader null) without re-attempting the
    // compile every frame. The whole map is cleared on a registry hot-reload
    // (effectsChanged) so a pack edit recompiles on the next paint.
    auto cacheIt = m_compiledPacks.find(packId);
    if (cacheIt != m_compiledPacks.end()) {
        return &cacheIt->second;
    }

    // First compile for this pack id. Callers are not only paint-cycle code:
    // reconcileDecorationShader runs from D-Bus reply lambdas, settings handlers,
    // and window-lifecycle slots, where KWin's GL context is not guaranteed
    // current (the snap-assist thumbnail capture makes this same guard before
    // its off-paint GL work). Make it current BEFORE inserting the cache slot:
    // a no-context failure must NOT latch compileFailed for the session — the
    // next caller (at latest the paint cycle itself) retries with a live
    // context.
    if (!KWin::effects->makeOpenGLContextCurrent()) {
        qCWarning(lcEffect) << "Surface shader pack" << packId
                            << "compile deferred: no current GL context; retrying on next use";
        return nullptr;
    }

    // Insert the cache slot up-front so every fail-closed early-return latches by
    // leaving compileFailed=true / shader=null on the cached entry. (The single
    // global path used member latch flags; the per-pack path latches on the slot.)
    CompiledSurfacePack& packState = m_compiledPacks[packId];
    packState.compileFailed = true; // pessimistic until the compile succeeds

    ensureSurfaceRegistryPaths();

    const PhosphorSurfaceShaders::SurfaceShaderEffect eff = m_surfaceShaderRegistry.effect(packId);
    if (eff.id.isEmpty() || eff.fragmentShaderPath.isEmpty()) {
        qCWarning(lcEffect) << "Surface shader pack" << packId << "not found in registry (effect count="
                            << m_surfaceShaderRegistry.availableEffects().size()
                            << ") — window decoration disabled this session";
        return &packState;
    }

    QFile fragFile(eff.fragmentShaderPath);
    if (!fragFile.open(QIODevice::ReadOnly)) {
        qCWarning(lcEffect) << "Failed to open surface shader" << eff.fragmentShaderPath;
        return &packState;
    }
    const QString rawSource = QString::fromUtf8(fragFile.readAll());
    if (rawSource.isEmpty()) {
        qCWarning(lcEffect) << "Surface shader file is empty" << eff.fragmentShaderPath;
        return &packState;
    }

    // Include paths: each search path's /shared dir (resolves
    // `#include <surface_uniforms.glsl>`) PLUS the search path root itself,
    // matching the daemon/validator resolution (surfaceshaderitem.cpp
    // surfaceIncludePaths) so a pack that resolves a root-level include on the
    // daemon and passes shadervalidate also links on the compositor.
    QStringList includePaths;
    for (const QString& sp : m_surfaceShaderRegistry.searchPaths()) {
        const QString sharedDir = sp + QStringLiteral("/shared");
        if (QDir(sharedDir).exists()) {
            includePaths.append(sharedDir);
        }
        includePaths.append(sp);
    }
    const QString currentDir = QFileInfo(eff.fragmentShaderPath).absolutePath();
    QString includeError;
    // Assemble an entry-only pack (a `vec4 pSurface(vec2 uv)` body with no
    // main()) into a full translation unit BEFORE include expansion — identical
    // to the daemon bake layer and the animation compile path — so the
    // prologue's `#include` resolves and the generated main() calls pSurface. A
    // pack that writes its own main() is returned unchanged.
    const QString assembledSource = PhosphorShaders::assembleEntryPoint(
        rawSource, PhosphorSurfaceShaders::SurfaceShaderRegistry::surfaceEntryPrologue(),
        PhosphorSurfaceShaders::SurfaceShaderRegistry::surfaceEntryCandidates());
    QString expanded = PhosphorShaders::ShaderIncludeResolver::expandIncludes(assembledSource, currentDir, includePaths,
                                                                              &includeError);
    if (expanded.isEmpty()) {
        qCWarning(lcEffect) << "Failed to expand surface shader includes for" << eff.id << ":" << includeError;
        return &packState;
    }
    // Named-param preamble (`#define p_<id> ...`) for pack-declared parameters —
    // empty for the border pack, whose state comes from the contract uniforms.
    expanded = PhosphorShaders::spliceAfterVersion(expanded,
                                                   PhosphorSurfaceShaders::SurfaceShaderRegistry::paramPreamble(eff));
    // Select the PLASMAZONES_KWIN branch of surface_uniforms.glsl (classic-GL
    // default-block uniforms; KWin::GLShader has no UBO bind path), exactly as
    // the animation compile path does.
    const QByteArray fragWithKwinDefine = injectKwinDefineAfterVersion(expanded);

    // Default surface vertex stage for frag-only packs (the border ships none).
    // Passthrough texcoord, NO Y-flip: the fragment reconstructs the top-down
    // device pixel itself for the rounded-rect SDF, so the geometry lines up
    // with the top-down frame uniforms. Shares the attribute-slot layout
    // (position@0, texCoord@1, modelViewProjectionMatrix) with the animation
    // default vertex, minus its Y-flip.
    static const QByteArray kSurfaceDefaultVertexSource = QByteArrayLiteral(
        "#version 450\n\n"
        "layout(location = 0) in vec2 position;\n"
        "layout(location = 1) in vec2 texCoord;\n\n"
        "layout(location = 0) out vec2 vTexCoord;\n\n"
        "uniform mat4 modelViewProjectionMatrix;\n\n"
        "void main() {\n"
        "    vTexCoord = texCoord;\n"
        "    gl_Position = modelViewProjectionMatrix * vec4(position, 0.0, 1.0);\n"
        "}\n");
    // Wrap the default vertex source through injectKwinDefineAfterVersion just
    // like the custom-vertex branch below (and the animation default vertex):
    // KWin rewrites our #version 450 down to the GL context core version (140),
    // where the `layout(location = N)` qualifiers are illegal without the ARB
    // extensions the inject helper enables. Passing the raw source here made
    // frag-only packs (the border ships no vertex stage) fail to compile on
    // NVIDIA (error C7548), latching the pack failed every frame.
    QByteArray vertWithKwinDefine = injectKwinDefineAfterVersion(QString::fromUtf8(kSurfaceDefaultVertexSource));
    if (!eff.vertexShaderPath.isEmpty()) {
        // Every failure below (unreadable / empty file, include expansion
        // error) warns and KEEPS the already-assigned default vertex stage,
        // matching the animation path's graceful degradation — feeding the
        // raw unexpanded source to the compiler would fail the whole pack
        // ("decoration disabled this session") for a vertex-only defect.
        QFile vertFile(eff.vertexShaderPath);
        if (!vertFile.open(QIODevice::ReadOnly)) {
            qCWarning(lcEffect) << "Failed to open surface vertex shader" << eff.vertexShaderPath << "for pack"
                                << packId << "— falling back to the default surface vertex stage";
        } else {
            const QString rawVert = QString::fromUtf8(vertFile.readAll());
            if (rawVert.isEmpty()) {
                qCWarning(lcEffect) << "Surface vertex shader file is empty" << eff.vertexShaderPath << "for pack"
                                    << packId << "— falling back to the default surface vertex stage";
            } else {
                QString vertIncErr;
                const QString expandedVert = PhosphorShaders::ShaderIncludeResolver::expandIncludes(
                    rawVert, QFileInfo(eff.vertexShaderPath).absolutePath(), includePaths, &vertIncErr);
                if (expandedVert.isEmpty()) {
                    qCWarning(lcEffect) << "Failed to expand surface vertex-shader includes for" << packId << ":"
                                        << vertIncErr << "— falling back to the default surface vertex stage";
                } else {
                    vertWithKwinDefine = injectKwinDefineAfterVersion(expandedVert);
                }
            }
        }
    }

    auto shader = KWin::ShaderManager::instance()->generateCustomShader(KWin::ShaderTrait::MapTexture,
                                                                        vertWithKwinDefine, fragWithKwinDefine);
    // KWin 6.7 removed GLShader::isValid(); generateCustomShader returns nullptr
    // when compilation or linking fails, so a null check is the validity test.
    if (!shader) {
        qCWarning(lcEffect) << "Failed to compile surface shader pack" << packId
                            << "— window decoration disabled this session";
        return &packState;
    }
    // Cache the contract uniform locations on the per-pack state. Each maps 1:1
    // to a surface contract uniform.
    namespace SC = PhosphorSurfaceShaders::SurfaceShaderContract;
    packState.uSurfaceSizeLoc = shader->uniformLocation(SC::kUSurfaceSize);
    packState.uFrameTopLeftLoc = shader->uniformLocation(SC::kUSurfaceFrameTopLeft);
    packState.uFrameSizeLoc = shader->uniformLocation(SC::kUSurfaceFrameSize);
    packState.uScaleLoc = shader->uniformLocation(SC::kUSurfaceScale);
    packState.uFocusedLoc = shader->uniformLocation(SC::kUSurfaceFocused);
    packState.uOpacityLoc = shader->uniformLocation(SC::kUSurfaceOpacity);
    // iTime — present (>= 0) only when the pack's main references it; that is the
    // signal the window must be driven to repaint continuously (windowSurfaceAnimates).
    packState.uTimeLoc = shader->uniformLocation(SC::kITime);
    // uTexture0 sampler — only consulted on the multi-pack composite path, which
    // runs the main pass as a fullscreen FBO pass and binds the running composite
    // to unit 0 itself. -1 on a single-pass border-only pack would be unusual
    // (it samples the surface), but harmless if so.
    packState.uTexture0Loc = shader->uniformLocation(SC::kUTexture0);
    // Backdrop sampling (needsBackdrop packs) — -1 for the common pack that
    // never references the scene behind the window.
    packState.uBackdropLoc = shader->uniformLocation(SC::kUBackdrop);
    packState.uBackdropRectLoc = shader->uniformLocation(SC::kUBackdropRect);
    packState.uHasBackdropLoc = shader->uniformLocation(SC::kUHasBackdrop);
    // Audio (surface_audio.glsl): both resolve to -1 for a pack that never
    // includes the module (the border, glow, …). A pack that reads getBass /
    // audioBar references iAudioSpectrumSize + uAudioSpectrum, so the linker
    // keeps them and iAudioSpectrumSizeLoc >= 0 flags the pack as audio-reactive.
    packState.iAudioSpectrumSizeLoc = shader->uniformLocation(SC::kIAudioSpectrumSize);
    packState.uAudioSpectrumLoc = shader->uniformLocation(SC::kUAudioSpectrum);
    // iMouse (hover-reactive packs) — -1 for the common pack that never reads
    // the cursor; pushBorderUniforms skips the push entirely then.
    packState.iMouseLoc = shader->uniformLocation(SC::kIMouse);

    // User-declared image textures (metadata `textures`): resolve the sampler +
    // iTextureResolution[N] element locations and upload each declared file
    // once — decorations are persistent, so the pack-lifetime cache owns the
    // GLTextures (freed with the shader under the same GL-context discipline).
    // Paths were made absolute + traversal-checked at registry scan time; an
    // unloadable file warns and leaves the slot null (transparent black).
    static const std::array<const char*, 3> kSurfaceUserTextureNames = {
        {SC::kUTexture1, SC::kUTexture2, SC::kUTexture3}};
    static const std::array<const char*, 3> kSurfaceTextureResNames = {
        {"iTextureResolution[0]", "iTextureResolution[1]", "iTextureResolution[2]"}};
    static_assert(SC::kMaxUserTextureSlots == 3, "surface user-texture name arrays must grow with the slot budget");
    for (int slot = 0; slot < SC::kMaxUserTextureSlots; ++slot) {
        packState.userTextureLoc[slot] = shader->uniformLocation(kSurfaceUserTextureNames[slot]);
        packState.iTextureResolutionLoc[slot] = shader->uniformLocation(kSurfaceTextureResNames[slot]);
    }
    for (int slot = 0; slot < eff.textures.size() && slot < SC::kMaxUserTextureSlots; ++slot) {
        const auto& texSlot = eff.textures.at(slot);
        if (texSlot.path.isEmpty()) {
            continue;
        }
        QImage img(texSlot.path);
        if (img.isNull()) {
            qCWarning(lcEffect) << "Surface pack" << packId << "texture slot" << slot << "failed to load"
                                << texSlot.path << "— sampler reads transparent";
            continue;
        }
        std::unique_ptr<KWin::GLTexture> tex = KWin::GLTexture::upload(img);
        if (!tex) {
            qCWarning(lcEffect) << "Surface pack" << packId << "texture slot" << slot << "GL upload failed for"
                                << texSlot.path;
            continue;
        }
        tex->setFilter(GL_LINEAR);
        // Wrap vocabulary matches the shared contract (clamp default).
        GLenum wrap = GL_CLAMP_TO_EDGE;
        if (texSlot.wrap == QLatin1String("repeat")) {
            wrap = GL_REPEAT;
        } else if (texSlot.wrap == QLatin1String("mirror")) {
            wrap = GL_MIRRORED_REPEAT;
        }
        tex->setWrapMode(wrap);
        packState.userTextures[slot] = std::move(tex);
    }

    // MAIN-pass multipass channel locations: the buffer-pass outputs are bound
    // here (idle drawWindow path) as iChannel0..3 so the main effect.frag can
    // sample the pre-rendered buffer textures. -1 for a single-pass pack (the
    // border never references these — the linker drops them). The literal
    // element names match the surface contract declarations in
    // surface_uniforms.glsl ("iChannel0".."iChannel3",
    // "iChannelResolution[0]".."[3]").
    static const std::array<const char*, 4> kIChannelNames = {{"iChannel0", "iChannel1", "iChannel2", "iChannel3"}};
    static const std::array<const char*, 4> kIChannelResNames = {
        {"iChannelResolution[0]", "iChannelResolution[1]", "iChannelResolution[2]", "iChannelResolution[3]"}};
    for (int i = 0; i < 4; ++i) {
        packState.iChannelLoc[i] = shader->uniformLocation(kIChannelNames[i]);
        packState.iChannelResolutionLoc[i] = shader->uniformLocation(kIChannelResNames[i]);
    }

    // Pack-declared parameters: cache the customParams/customColors element
    // locations and resolve the slot VALUES. float/int/bool params land in
    // customParams[N], colours in customColors[N] (the generated p_<id> preamble
    // maps p_<id> to the right lane). The override source is the resolved
    // DecorationProfile's parameters[packId] merged over the pack's declared
    // defaults via translateSurfaceParams; baked once at first compile (the cache
    // is pack-keyed — see CompiledSurfacePack). The border pack declares none, so
    // all locations resolve to -1 and push nothing.
    for (int slot = 0; slot < SC::kMaxCustomParams; ++slot) {
        packState.customParamsLoc[slot] = shader->uniformLocation(kCustomParamsElementNames[slot]);
    }
    for (int slot = 0; slot < SC::kMaxCustomColors; ++slot) {
        packState.customColorsLoc[slot] = shader->uniformLocation(kCustomColorsElementNames[slot]);
    }
    const QVariantMap packParamOverrides = profile.effectiveParameters().value(packId).toMap();
    const SurfaceParamValues baked = ShaderInternal::resolveSurfaceParamValues(eff, packParamOverrides);
    packState.customParamsValues = baked.params;
    packState.customColorsValues = baked.colors;

    // ── Multipass buffer passes (idle drawWindow path) ──────────────────────
    //
    // Only a multipass pack with declared buffers compiles passes here;
    // single-pass packs (the border) leave packState.bufferPasses empty and pay
    // nothing — the cheap OffscreenData path in drawWindow is byte-for-byte
    // unchanged for them. If ANY buffer pass fails to compile we leave the vector
    // empty and warn: the pack then renders single-pass (fail closed), exactly
    // like the daemon/animation degradation. (packState.bufferPasses starts empty
    // for this freshly-inserted cache slot.)
    //
    // bufferFeedback (sampling the prior frame's own buffer) is ignored in this
    // first cut — each pass only sees uTexture0 + strictly-earlier buffer outputs.
    if (eff.isMultipass && !eff.bufferShaderPaths.isEmpty()) {
        // The fullscreen-quad vertex stage is shared by every buffer pass; the
        // PLASMAZONES_KWIN define is injected so it travels the same #version
        // handling as the frag (the vertex declares no contract uniforms, but
        // keeping both stages on the same define path avoids surprises).
        const QByteArray bufVert = injectKwinDefineAfterVersion(QString::fromUtf8(kFullscreenQuadVertexSource));

        std::vector<CompiledSurfaceBufferPass> passes;
        passes.reserve(static_cast<size_t>(eff.bufferShaderPaths.size()));
        bool allCompiled = true;
        for (const QString& bufPath : eff.bufferShaderPaths) {
            QFile bufFile(bufPath);
            if (!bufFile.open(QIODevice::ReadOnly)) {
                qCWarning(lcEffect) << "Failed to open surface buffer pass" << bufPath << "for pack" << eff.id;
                allCompiled = false;
                break;
            }
            const QString bufRaw = QString::fromUtf8(bufFile.readAll());
            if (bufRaw.isEmpty()) {
                qCWarning(lcEffect) << "Surface buffer pass is empty" << bufPath << "for pack" << eff.id;
                allCompiled = false;
                break;
            }
            QString bufIncErr;
            QString bufExpanded = PhosphorShaders::ShaderIncludeResolver::expandIncludes(
                bufRaw, QFileInfo(bufPath).absolutePath(), includePaths, &bufIncErr);
            if (bufExpanded.isEmpty()) {
                qCWarning(lcEffect) << "Failed to expand surface buffer-pass includes for" << bufPath << ":"
                                    << bufIncErr;
                allCompiled = false;
                break;
            }
            // Buffer passes are param-free by contract: bakeBufferShaders on the
            // daemon and validateSurfacePack both skip the p_<id> preamble, and a
            // buffer source reads parameters by their raw customParams slot (see
            // surface_blur.glsl). Splicing the preamble HERE would let a buffer
            // pass reference a p_<id> name on the compositor while the same pack
            // fails to compile on the daemon and the validator — so skip it too,
            // keeping all three buffer-pass compile paths identical. The raw
            // customParams/customColors locations are still cached below because
            // buffer passes read those uniforms directly.
            const QByteArray bufFrag = injectKwinDefineAfterVersion(bufExpanded);
            auto bufShader =
                KWin::ShaderManager::instance()->generateCustomShader(KWin::ShaderTrait::MapTexture, bufVert, bufFrag);
            if (!bufShader) {
                qCWarning(lcEffect) << "Failed to compile surface buffer pass" << bufPath << "for pack" << eff.id;
                allCompiled = false;
                break;
            }

            CompiledSurfaceBufferPass pass;
            pass.uTexture0Loc = bufShader->uniformLocation(SC::kUTexture0);
            pass.uTimeLoc = bufShader->uniformLocation(SC::kITime);
            pass.uSurfaceSizeLoc = bufShader->uniformLocation(SC::kUSurfaceSize);
            pass.uScaleLoc = bufShader->uniformLocation(SC::kUSurfaceScale);
            pass.uBackdropLoc = bufShader->uniformLocation(SC::kUBackdrop);
            pass.uBackdropRectLoc = bufShader->uniformLocation(SC::kUBackdropRect);
            pass.uHasBackdropLoc = bufShader->uniformLocation(SC::kUHasBackdrop);
            // Audio (surface_audio.glsl) — a buffer pass may read the spectrum
            // too; both -1 when it doesn't (the linker drops the unreferenced
            // uniforms), exactly like the main pass above.
            pass.iAudioSpectrumSizeLoc = bufShader->uniformLocation(SC::kIAudioSpectrumSize);
            pass.uAudioSpectrumLoc = bufShader->uniformLocation(SC::kUAudioSpectrum);
            for (int i = 0; i < 4; ++i) {
                pass.iChannelLoc[i] = bufShader->uniformLocation(kIChannelNames[i]);
                pass.iChannelResolutionLoc[i] = bufShader->uniformLocation(kIChannelResNames[i]);
            }
            for (int slot = 0; slot < SC::kMaxCustomParams; ++slot) {
                pass.customParamsLoc[slot] = bufShader->uniformLocation(kCustomParamsElementNames[slot]);
            }
            for (int slot = 0; slot < SC::kMaxCustomColors; ++slot) {
                pass.customColorsLoc[slot] = bufShader->uniformLocation(kCustomColorsElementNames[slot]);
            }
            pass.shader = std::move(bufShader);
            passes.push_back(std::move(pass));
        }
        if (allCompiled) {
            packState.bufferPasses = std::move(passes);
        } else {
            qCWarning(lcEffect) << "Surface pack" << eff.id
                                << "has a failing buffer pass — rendering single-pass (iChannels unbound)";
        }
    }

    packState.shader = std::move(shader);
    packState.compileFailed = false; // success — clear the pessimistic latch
    return &packState;
}

} // namespace PlasmaZones
