// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Pointer shader pack COMPILATION for the kwin-effect: registry search-path
// population, per-pack compile-and-cache (main pass plus optional multipass
// buffer stages), user-texture upload and uniform-location caching. The DRAW
// lives in pointerdecorationpaint.cpp. This mirrors
// plasmazoneseffect/surface_compile.cpp step for step, because the two
// families share the same assembly contract (entry-point assembly, param
// preamble spliced after #version, include expansion, KWin define injection).

#include "pointerdecorationpass.h"

#include "plasmazoneseffect/shader_internal.h"
#include "compositor/effectlogging.h"

#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>
#include <PhosphorShaders/ShaderParamPreamble.h>

#include <effect/effecthandler.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>

#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QImage>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <utility>

namespace PlasmaZones {

namespace PPS = PhosphorPointerShaders;
namespace PSC = PhosphorPointerShaders::PointerShaderContract;

using ShaderInternal::injectKwinDefineAfterVersion;
using ShaderInternal::kCustomColorsElementNames;
using ShaderInternal::kCustomParamsElementNames;

namespace {

// The vertex stage of BOTH the main pointer pass and every buffer stage.
//
// data/pointer/shared/pointer.vert is the PREVIEW's vertex stage: it reads
// qt_Matrix out of the UBO, which does not exist on the compositor branch of
// the contract (KWin::GLShader has no UBO bind path), so it is deliberately
// never compiled here — the same split surface.vert and the surface compile
// path already have.
//
// Positions arrive in the RenderViewport's device coordinate space and are
// projected by KWin's own matrix, which encodes RenderTarget::transform() (the
// output rotation/flip combined with the buffer's FlipY) and the render
// offset. Emitting clip-space directly is only equivalent on an unrotated
// output with a zero offset, so the projection is applied unconditionally,
// exactly as TransitionPass::outputQuadVertexSource explains for the
// screen-level transition passes.
//
// The BUFFER stages reuse this with an identity MVP: their quad is already in
// clip space, and setting the identity matrix costs one uniform push and
// keeps a single vertex source for the whole family.
//
// Attribute slots match KWin::VA_Position (0) / VA_TexCoord (1) per
// <opengl/glvertexbuffer.h>'s GLVertex2DLayout, so vbo->setVertices of
// GLVertex2D feeds position@0 and texcoord@1 directly.
constexpr const char* kPointerVertexSource =
    "#version 450\n"
    "layout(location = 0) in vec2 position;\n"
    "layout(location = 1) in vec2 texCoord;\n"
    "layout(location = 0) out vec2 vTexCoord;\n"
    "uniform mat4 modelViewProjectionMatrix;\n"
    "void main() {\n"
    "    vTexCoord = texCoord;\n"
    "    gl_Position = modelViewProjectionMatrix * vec4(position, 0.0, 1.0);\n"
    "}\n";

// Element names for the pointer contract's indexed uniforms. The literals
// must match the declarations in data/pointer/shared/pointer_uniforms.glsl;
// the static_asserts pin the array lengths to the contract budgets so a bump
// is a compile error rather than an out-of-range read.
constexpr std::array<const char*, 4> kIChannelNames = {
    {PSC::kIChannel0, PSC::kIChannel1, PSC::kIChannel2, PSC::kIChannel3}};
constexpr std::array<const char*, 4> kIChannelResNames = {
    {"iChannelResolution[0]", "iChannelResolution[1]", "iChannelResolution[2]", "iChannelResolution[3]"}};
constexpr std::array<const char*, PSC::kMaxUserTextureSlots> kUserTextureNames = {
    {PSC::kUTexture1, PSC::kUTexture2, PSC::kUTexture3}};
constexpr std::array<const char*, PSC::kMaxUserTextureSlots> kTextureResNames = {
    {"iTextureResolution[0]", "iTextureResolution[1]", "iTextureResolution[2]"}};
static_assert(PSC::kMaxUserTextureSlots == 3, "pointer user-texture name arrays must grow with the slot budget");

// uPointerTrail[0..31]. KWin::GLShader exposes no array upload, so each
// element needs its own location and its own setUniform; a pack that never
// reads the trail links without the array and every slot stays -1, so it pays
// nothing.
constexpr std::array<const char*, PSC::kMaxTrailPoints> kTrailElementNames = {
    {"uPointerTrail[0]",  "uPointerTrail[1]",  "uPointerTrail[2]",  "uPointerTrail[3]",  "uPointerTrail[4]",
     "uPointerTrail[5]",  "uPointerTrail[6]",  "uPointerTrail[7]",  "uPointerTrail[8]",  "uPointerTrail[9]",
     "uPointerTrail[10]", "uPointerTrail[11]", "uPointerTrail[12]", "uPointerTrail[13]", "uPointerTrail[14]",
     "uPointerTrail[15]", "uPointerTrail[16]", "uPointerTrail[17]", "uPointerTrail[18]", "uPointerTrail[19]",
     "uPointerTrail[20]", "uPointerTrail[21]", "uPointerTrail[22]", "uPointerTrail[23]", "uPointerTrail[24]",
     "uPointerTrail[25]", "uPointerTrail[26]", "uPointerTrail[27]", "uPointerTrail[28]", "uPointerTrail[29]",
     "uPointerTrail[30]", "uPointerTrail[31]"}};
static_assert(PSC::kMaxTrailPoints == 32, "kTrailElementNames must grow to match kMaxTrailPoints");

/// The include paths a pointer pack resolves `#include <pointer_lib.glsl>`
/// against: each registered search path's `shared` dir plus the search path
/// root, which is exactly what PointerShaderRegistry::includePathsFor derives
/// from a pack dir. Built from the registry's paths rather than from one
/// pack's dir so a pack in the user dir can still include the bundled shared
/// headers, matching the surface compile path.
QStringList includePathsFrom(const PPS::PointerShaderRegistry& registry)
{
    QStringList includePaths;
    const QStringList searchPaths = registry.searchPaths();
    includePaths.reserve(searchPaths.size() * 2);
    for (const QString& sp : searchPaths) {
        const QString sharedDir = sp + QStringLiteral("/shared");
        if (QDir(sharedDir).exists()) {
            includePaths.append(sharedDir);
        }
        includePaths.append(sp);
    }
    return includePaths;
}

} // namespace

void PointerDecorationPass::ensureRegistryPaths()
{
    if (m_registryPathsAdded) {
        return;
    }
    m_registryPathsAdded = true;
    // Candidate dirs: every ${XDG_DATA_DIRS}/plasmazones/pointer plus the user
    // data dir (~/.local/share/plasmazones/pointer), where CMake installs
    // data/pointer and where a user override would live. Added even when a dir
    // is missing so the registry's watcher promotes a parent-watch and picks up
    // packs that appear later (a fresh install).
    //
    // ASCENDING priority, system lowest first and the writable user dir LAST.
    // standardLocations hands back the user location FIRST, and the scan
    // strategy reverse-iterates the registered paths with first-wins on an id
    // collision — so registering that order verbatim would let /usr/share
    // claim a bundled id before the user dir could, and a user pack overriding
    // a bundled one would be silently shadowed. Every sibling registry in this
    // tree reverses for exactly this reason (ensureSurfaceRegistryPaths,
    // shader_warmup.cpp, animationbootstrap.cpp).
    QStringList paths;
    QStringList bases = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    std::reverse(bases.begin(), bases.end());
    paths.reserve(bases.size());
    for (const QString& base : bases) {
        paths.append(base + QStringLiteral("/plasmazones/pointer"));
    }
    if (!paths.isEmpty()) {
        m_registry.addSearchPaths(paths);
    }
}

void PointerDecorationPass::cacheUniformLocations(KWin::GLShader* shader, PointerUniformLocations& out)
{
    out.iTime = shader->uniformLocation(PSC::kITime);
    out.iResolution = shader->uniformLocation(PSC::kIResolution);
    out.iMouse = shader->uniformLocation(PSC::kIMouse);
    out.uPointerVelocity = shader->uniformLocation(PSC::kUPointerVelocity);
    out.uPointerPress = shader->uniformLocation(PSC::kUPointerPress);
    out.uPointerRelease = shader->uniformLocation(PSC::kUPointerRelease);
    out.uPointerState = shader->uniformLocation(PSC::kUPointerState);
    out.uCursorRect = shader->uniformLocation(PSC::kUCursorRect);
    out.uPointerFlags = shader->uniformLocation(PSC::kUPointerFlags);
    out.uCursorSprite = shader->uniformLocation(PSC::kUCursorSprite);
    for (int i = 0; i < PSC::kMaxTrailPoints; ++i) {
        out.uPointerTrail[static_cast<size_t>(i)] = shader->uniformLocation(kTrailElementNames[static_cast<size_t>(i)]);
    }
    for (int slot = 0; slot < PSC::kMaxCustomParams; ++slot) {
        out.customParams[static_cast<size_t>(slot)] =
            shader->uniformLocation(kCustomParamsElementNames[static_cast<size_t>(slot)]);
    }
    for (int slot = 0; slot < PSC::kMaxCustomColors; ++slot) {
        out.customColors[static_cast<size_t>(slot)] =
            shader->uniformLocation(kCustomColorsElementNames[static_cast<size_t>(slot)]);
    }
    for (int slot = 0; slot < PSC::kMaxUserTextureSlots; ++slot) {
        out.userTextures[static_cast<size_t>(slot)] =
            shader->uniformLocation(kUserTextureNames[static_cast<size_t>(slot)]);
        out.iTextureResolution[static_cast<size_t>(slot)] =
            shader->uniformLocation(kTextureResNames[static_cast<size_t>(slot)]);
    }
    for (int i = 0; i < 4; ++i) {
        out.iChannel[static_cast<size_t>(i)] = shader->uniformLocation(kIChannelNames[static_cast<size_t>(i)]);
        out.iChannelResolution[static_cast<size_t>(i)] =
            shader->uniformLocation(kIChannelResNames[static_cast<size_t>(i)]);
    }
}

PointerDecorationPass::CompiledPointerPack* PointerDecorationPass::compiledPack(const EngagedLayer& layer)
{
    if (!KWin::effects || layer.effectId.isEmpty()) {
        return nullptr;
    }
    // Per-pack-id cache: a hit returns the prior compile, INCLUDING a failure
    // latch (an entry whose shader is null), so a broken pack is not
    // re-compiled every frame. The whole map is dropped on a registry
    // hot-reload (invalidateShaderCache), so a fixed pack recompiles then.
    const auto cacheIt = m_packCache.find(layer.effectId);
    if (cacheIt != m_packCache.end()) {
        return &cacheIt->second;
    }

    // First compile for this id. This can be reached from the paint thread
    // (where the context is current) but the make-current is unconditional so
    // an off-paint caller is covered too. It runs BEFORE the cache slot is
    // inserted: a no-context failure must NOT latch for the session — the
    // next frame retries with a live context.
    if (!KWin::effects->makeOpenGLContextCurrent()) {
        qCWarning(lcEffect) << "Pointer pack" << layer.effectId
                            << "compile deferred: no current GL context; retrying on next use";
        return nullptr;
    }

    // Insert the slot up-front so every fail-closed early return latches by
    // leaving shader == nullptr on the cached entry.
    CompiledPointerPack& packState = m_packCache[layer.effectId];

    const PPS::PointerShaderEffect& eff = layer.effect;

    QFile fragFile(eff.fragmentShaderPath);
    if (!fragFile.open(QIODevice::ReadOnly)) {
        qCWarning(lcEffect) << "Failed to open pointer shader" << eff.fragmentShaderPath << "for pack" << eff.id;
        return &packState;
    }
    const QString rawSource = QString::fromUtf8(fragFile.readAll());
    if (rawSource.isEmpty()) {
        qCWarning(lcEffect) << "Pointer shader file is empty" << eff.fragmentShaderPath;
        return &packState;
    }

    const QStringList includePaths = includePathsFrom(m_registry);
    const QString currentDir = QFileInfo(eff.fragmentShaderPath).absolutePath();

    // Assemble an entry-only pack (a `vec4 pPointer(vec2 uv)` body with no
    // main()) into a full translation unit BEFORE include expansion, so the
    // prologue's `#include <pointer_lib.glsl>` resolves and the generated
    // main() calls pPointer. A pack that writes its own main() is returned
    // unchanged. Identical to the surface and animation compile paths, and to
    // what plasmazones-shader-validate reproduces.
    const QString assembledSource =
        PhosphorShaders::assembleEntryPoint(rawSource, PPS::PointerShaderRegistry::pointerEntryPrologue(),
                                            PPS::PointerShaderRegistry::pointerEntryCandidates());
    QString includeError;
    QString expanded = PhosphorShaders::ShaderIncludeResolver::expandIncludes(assembledSource, currentDir, includePaths,
                                                                              &includeError);
    if (expanded.isEmpty()) {
        qCWarning(lcEffect) << "Failed to expand pointer shader includes for" << eff.id << ":" << includeError;
        return &packState;
    }
    // Named-param preamble (`#define p_<id> customParamsN_x` / `customColorN`)
    // for the pack's declared parameters, spliced after #version.
    expanded = PhosphorShaders::spliceAfterVersion(expanded, PPS::PointerShaderRegistry::paramPreamble(eff));
    // Select the PLASMAZONES_KWIN branch of pointer_uniforms.glsl (classic-GL
    // default-block uniforms).
    const QByteArray fragWithKwinDefine = injectKwinDefineAfterVersion(expanded);

    // The vertex stage travels the same define path as the fragment: KWin
    // rewrites #version 450 down to the GL context core version (140), where
    // the layout(location = N) qualifiers are illegal without the ARB
    // extensions the inject helper enables. Passing raw source here made
    // frag-only surface packs fail to link on NVIDIA (error C7548).
    QByteArray vertWithKwinDefine = injectKwinDefineAfterVersion(QString::fromUtf8(kPointerVertexSource));
    if (!eff.vertexShaderPath.isEmpty()) {
        // Every failure below warns and KEEPS the default vertex stage rather
        // than failing the whole pack for a vertex-only defect — the same
        // graceful degradation the surface path applies. NOTE the bundled
        // data/pointer/shared/pointer.vert is the PREVIEW's stage (it reads
        // qt_Matrix from the UBO) and is deliberately not the default here; a
        // pack that names its own vertex shader is expected to write one that
        // compiles against the KWin branch.
        QFile vertFile(eff.vertexShaderPath);
        if (!vertFile.open(QIODevice::ReadOnly)) {
            qCWarning(lcEffect) << "Failed to open pointer vertex shader" << eff.vertexShaderPath << "for pack"
                                << eff.id << "— falling back to the default pointer vertex stage";
        } else {
            const QString rawVert = QString::fromUtf8(vertFile.readAll());
            if (rawVert.isEmpty()) {
                qCWarning(lcEffect) << "Pointer vertex shader file is empty" << eff.vertexShaderPath << "for pack"
                                    << eff.id << "— falling back to the default pointer vertex stage";
            } else {
                QString vertIncErr;
                const QString expandedVert = PhosphorShaders::ShaderIncludeResolver::expandIncludes(
                    rawVert, QFileInfo(eff.vertexShaderPath).absolutePath(), includePaths, &vertIncErr);
                if (expandedVert.isEmpty()) {
                    qCWarning(lcEffect) << "Failed to expand pointer vertex-shader includes for" << eff.id << ":"
                                        << vertIncErr << "— falling back to the default pointer vertex stage";
                } else {
                    vertWithKwinDefine = injectKwinDefineAfterVersion(expandedVert);
                }
            }
        }
    }

    auto shader = KWin::ShaderManager::instance()->generateCustomShader(KWin::ShaderTrait::MapTexture,
                                                                        vertWithKwinDefine, fragWithKwinDefine);
    // KWin 6.7 removed GLShader::isValid(); generateCustomShader returns
    // nullptr when compilation or linking fails, so the null check IS the
    // validity test.
    if (!shader) {
        qCWarning(lcEffect) << "Failed to compile pointer shader pack" << eff.id << "— layer disabled until reload";
        return &packState;
    }
    cacheUniformLocations(shader.get(), packState.loc);

    // Pack-declared parameters: the metadata defaults merged with this
    // layer's overrides, resolved into the customParams[] / customColors[]
    // slot pools the generated p_<id> preamble addresses. Baked once at
    // compile, and the cache is keyed on pack id alone, so these values would
    // outlive an edit that changed only the parameters. setProfile() drops the
    // whole cache on every real profile change for exactly that reason; if
    // that call ever goes away, a parameter edit stops reaching the GPU.
    const QVariantMap translated = PPS::PointerShaderRegistry::translatePointerParams(eff, layer.parameters);
    for (int slot = 0; slot < PSC::kMaxCustomParams; ++slot) {
        auto pull = [&](char comp) -> float {
            const auto it = translated.constFind(PSC::paramKey(slot, comp));
            if (it == translated.constEnd()) {
                return 0.0f;
            }
            bool ok = false;
            const float v = it->toFloat(&ok);
            return ok ? v : 0.0f;
        };
        packState.customParams[static_cast<size_t>(slot)] = QVector4D(pull('x'), pull('y'), pull('z'), pull('w'));
    }
    for (int slot = 0; slot < PSC::kMaxCustomColors; ++slot) {
        const auto it = translated.constFind(PSC::colorKey(slot));
        if (it == translated.constEnd()) {
            continue;
        }
        const QColor c = it->value<QColor>();
        if (c.isValid()) {
            packState.customColors[static_cast<size_t>(slot)] = QVector4D(c.redF(), c.greenF(), c.blueF(), c.alphaF());
        }
    }

    // User-declared image textures (metadata `textures`). Paths were made
    // absolute and traversal-checked at registry scan time; an unloadable file
    // warns and leaves the slot null, which reads as transparent black.
    for (int slot = 0; slot < eff.textures.size() && slot < PSC::kMaxUserTextureSlots; ++slot) {
        const auto& texSlot = eff.textures.at(slot);
        if (texSlot.path.isEmpty()) {
            continue;
        }
        const QImage img(texSlot.path);
        if (img.isNull()) {
            qCWarning(lcEffect) << "Pointer pack" << eff.id << "texture slot" << slot << "failed to load"
                                << texSlot.path << "— sampler reads transparent";
            continue;
        }
        std::unique_ptr<KWin::GLTexture> tex = KWin::GLTexture::upload(img);
        if (!tex) {
            qCWarning(lcEffect) << "Pointer pack" << eff.id << "texture slot" << slot << "GL upload failed for"
                                << texSlot.path;
            continue;
        }
        tex->setFilter(GL_LINEAR);
        // Wrap vocabulary matches the shared contract (clamp is the default).
        GLenum wrap = GL_CLAMP_TO_EDGE;
        if (texSlot.wrap == QLatin1String("repeat")) {
            wrap = GL_REPEAT;
        } else if (texSlot.wrap == QLatin1String("mirror")) {
            wrap = GL_MIRRORED_REPEAT;
        }
        tex->setWrapMode(wrap);
        packState.userTextures[static_cast<size_t>(slot)] = std::move(tex);
    }

    // ── Multipass buffer stages ─────────────────────────────────────────────
    //
    // A buffer stage is an FBO-to-FBO pass: it samples earlier stages'
    // outputs (and, with `bufferFeedback`, its OWN previous frame) and writes
    // its ping-pong target. If ANY stage fails to compile the whole LAYER is
    // dropped rather than rendered single-pass: unlike a surface decoration,
    // a pointer pack that declares buffers is usually accumulating its own
    // canvas, and running its main pass with unbound channels would paint
    // garbage over the desktop. Fail closed, with the pack's own name in the
    // warning.
    //
    // Buffer sources are param-free by contract: the daemon's bake layer and
    // the validator both skip the p_<id> preamble for them and a buffer
    // source addresses parameters by their raw customParams slot, so the
    // preamble is skipped here too and all three compile paths stay identical.
    if (eff.isMultipass && !eff.bufferShaderPaths.isEmpty()) {
        std::vector<CompiledBufferPass> passes;
        passes.reserve(static_cast<size_t>(eff.bufferShaderPaths.size()));
        bool allCompiled = true;
        for (const QString& bufPath : eff.bufferShaderPaths) {
            QFile bufFile(bufPath);
            if (!bufFile.open(QIODevice::ReadOnly)) {
                qCWarning(lcEffect) << "Failed to open pointer buffer pass" << bufPath << "for pack" << eff.id;
                allCompiled = false;
                break;
            }
            const QString bufRaw = QString::fromUtf8(bufFile.readAll());
            if (bufRaw.isEmpty()) {
                qCWarning(lcEffect) << "Pointer buffer pass is empty" << bufPath << "for pack" << eff.id;
                allCompiled = false;
                break;
            }
            QString bufIncErr;
            const QString bufExpanded = PhosphorShaders::ShaderIncludeResolver::expandIncludes(
                bufRaw, QFileInfo(bufPath).absolutePath(), includePaths, &bufIncErr);
            if (bufExpanded.isEmpty()) {
                qCWarning(lcEffect) << "Failed to expand pointer buffer-pass includes for" << bufPath << ":"
                                    << bufIncErr;
                allCompiled = false;
                break;
            }
            const QByteArray bufFrag = injectKwinDefineAfterVersion(bufExpanded);
            auto bufShader = KWin::ShaderManager::instance()->generateCustomShader(KWin::ShaderTrait::MapTexture,
                                                                                   vertWithKwinDefine, bufFrag);
            if (!bufShader) {
                qCWarning(lcEffect) << "Failed to compile pointer buffer pass" << bufPath << "for pack" << eff.id;
                allCompiled = false;
                break;
            }
            CompiledBufferPass pass;
            cacheUniformLocations(bufShader.get(), pass.loc);
            pass.shader = std::move(bufShader);
            passes.push_back(std::move(pass));
        }
        if (!allCompiled) {
            qCWarning(lcEffect) << "Pointer pack" << eff.id
                                << "has a failing buffer pass — layer disabled until reload";
            // Leave shader null on the cached entry: that IS the failure latch.
            return &packState;
        }
        packState.bufferPasses = std::move(passes);
    }

    packState.shader = std::move(shader);
    return &packState;
}

} // namespace PlasmaZones
