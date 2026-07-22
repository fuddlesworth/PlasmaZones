// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"
#include "shader_internal.h"

#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>
#include <PhosphorShaders/ShaderParamPreamble.h>

#include <effect/effecthandler.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLoggingCategory>
#include <QPainter>
#include <QPointer>
#include <QRunnable>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QSvgRenderer>
#include <QThreadPool>

#include <limits>
#include <memory>
#include <unordered_set>
#include <utility>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

using ShaderInternal::injectKwinDefineAfterVersion;
using ShaderInternal::kCustomColorsElementNames;
using ShaderInternal::kCustomParamsElementNames;
using ShaderInternal::kITextureResolutionKeys;
using ShaderInternal::kUserTextureSamplerNames;
using ShaderInternal::loadUserTextureImage;

/// Load a user-texture file into a QImage. Mirrors the daemon-side path
/// in `PhosphorRendering::ShaderEffect::setShaderParams`: PNG/JPG/etc.
/// load via `QImage`; `.svg` / `.svgz` rasterise via `QSvgRenderer` at
/// the requested max-axis dimension (defaulting to 1024 to match
/// ZoneShaderItem's pre-unification SVG size). The `QImage::Format_RGBA8888`
/// conversion ensures `KWin::GLTexture::upload` always sees a consistent
/// pixel layout regardless of the source file's native format. Returns
/// a null QImage on any failure; the caller logs and skips the slot.
///
/// SVG rasterise target: `Format_ARGB32_Premultiplied` first, then
/// convert to `Format_RGBA8888` for the GPU upload. QPainter's
/// source-over compositing is defined for premultiplied targets;
/// rendering an SVG with semi-transparent strokes/fills directly into
/// `Format_RGBA8888` produces subtly wrong alpha at partial-cover paths.
/// Matches the daemon path's behaviour exactly so the same SVG renders
/// identically on both runtimes.
QImage ShaderInternal::loadUserTextureImage(const QString& path, int svgMaxDim)
{
    if (path.isEmpty()) {
        return {};
    }
    const bool isSvg = path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)
        || path.endsWith(QLatin1String(".svgz"), Qt::CaseInsensitive);
    if (isSvg) {
        QSvgRenderer renderer(path);
        if (!renderer.isValid()) {
            return {};
        }
        QSize size = renderer.defaultSize();
        if (!size.isEmpty()) {
            size.scale(svgMaxDim, svgMaxDim, Qt::KeepAspectRatio);
        } else {
            size = QSize(svgMaxDim, svgMaxDim);
        }
        QImage rasterised(size, QImage::Format_ARGB32_Premultiplied);
        rasterised.fill(Qt::transparent);
        QPainter painter(&rasterised);
        renderer.render(&painter);
        painter.end();
        return rasterised.convertToFormat(QImage::Format_RGBA8888);
    }
    return QImage(path).convertToFormat(QImage::Format_RGBA8888);
}

// ─────────────────────────────────────────────────────────────────────────────
// Async texture pre-warm.
//
// Pattern: a `QRunnable` posted to `m_shaderManager.m_textureLoaderPool` performs the
// CPU-bound load (`loadUserTextureImage` — QImage decode for raster
// formats, QSvgRenderer rasterise for SVG/SVGZ) on a worker thread,
// producing a `QImage` in `Format_RGBA8888`. The worker then dispatches
// a queued slot back to `this` via `QMetaObject::invokeMethod(...,
// Qt::QueuedConnection)` so the GL upload (`KWin::GLTexture::upload`)
// runs on the GL-context thread (the compositor thread). The cache
// insert and in-flight set bookkeeping happen entirely on the
// compositor thread, so no locking is needed against
// `m_shaderManager.m_textureCache` or `m_shaderManager.m_textureLoadsInFlight`.
//
// Thread-safety notes:
//   • The worker only reads the captured path string (`m_path`) and
//     `m_svgMaxDim` — both POD captured-by-value at submission time.
//     It NEVER touches `m_shaderManager.m_textureCache` or `m_shaderManager.m_textureLoadsInFlight`;
//     all access to those members happens on the compositor thread,
//     either at submission time or inside the queued upload lambda.
//     The submission-time generation captured into the worker lets
//     the queued lambda detect a hot-reload that cleared the cache
//     and discard the upload before touching `m_shaderManager.m_textureLoadsInFlight`.
//   • `QSvgRenderer` is NOT thread-safe across instances (it owns
//     mutable rasteriser state during `render()` per Qt docs).
//     `loadUserTextureImage` constructs a fresh `QSvgRenderer` per
//     call, so each worker invocation gets its own renderer — safe.
//   • `QImage(path)` (PNG/JPG/etc. decode) is thread-safe across
//     instances per Qt docs.
//   • `KWin::GLTexture::upload` MUST run on the GL thread; that's
//     why the upload is dispatched back via QueuedConnection rather
//     than completed inline on the worker.
// ─────────────────────────────────────────────────────────────────────────────
void PlasmaZonesEffect::evictLruTextureIfOverBound(const ShaderTransition* pending)
{
    // Build the set of cache pointers currently referenced by any
    // active transition's userTextures slots ONCE. Eviction skips
    // every one of these — the transition holds a raw non-owning
    // pointer that would dangle if we erased the entry. The set
    // doesn't change between iterations because the eviction below
    // only removes NON-in-flight entries; the set of in-flight
    // pointers is invariant across the loop, so we hoist the build.
    std::unordered_set<const CachedTexture*> inFlight;
    for (const auto& [_, transition] : m_shaderManager.shaderTransitions()) {
        for (CachedTexture* tex : transition.userTextures) {
            if (tex) {
                inFlight.insert(tex);
            }
        }
    }
    // The transition still being BUILT is not in shaderTransitions() yet (it is
    // inserted once every slot is resolved), so the slots it has already filled would
    // otherwise be evictable by the sweep its own next slot triggers — leaving it
    // holding a dangling pointer. It survives today only by arithmetic: the soft bound
    // is many times the slot count and every entry it touches carries the newest access
    // tick, so it can never be the victim. That is a property of two constants, not a
    // guarantee, and neither constant knows it is load-bearing.
    if (pending) {
        for (CachedTexture* tex : pending->userTextures) {
            if (tex) {
                inFlight.insert(tex);
            }
        }
    }
    while (m_shaderManager.m_textureCache.size() > ShaderTransitionManager::kTextureCacheSoftBound) {
        // Find the cache entry with the smallest lastAccessTick that is
        // NOT in-flight. If every entry is in flight (pathological;
        // would require >ShaderTransitionManager::kTextureCacheSoftBound concurrent transitions
        // each referencing a unique texture), break — the cache
        // transiently exceeds the bound rather than tearing a live
        // pointer. Self-heals on the next eviction once a transition
        // ends.
        auto evictIt = m_shaderManager.m_textureCache.end();
        quint64 oldestTick = std::numeric_limits<quint64>::max();
        for (auto it = m_shaderManager.m_textureCache.begin(); it != m_shaderManager.m_textureCache.end(); ++it) {
            if (inFlight.count(&it->second) > 0) {
                continue;
            }
            if (it->second.lastAccessTick < oldestTick) {
                oldestTick = it->second.lastAccessTick;
                evictIt = it;
            }
        }
        if (evictIt == m_shaderManager.m_textureCache.end()) {
            return; // every entry is in flight; no safe eviction this pass
        }
        qCDebug(lcEffect) << "evictLruTextureIfOverBound: evicting" << evictIt->first
                          << "(lastAccessTick=" << evictIt->second.lastAccessTick
                          << ", cache size=" << m_shaderManager.m_textureCache.size() << ")";
        m_shaderManager.m_textureCache.erase(evictIt);
    }
}

void PlasmaZonesEffect::warmUserTextureAsync(const QString& absolutePath)
{
    if (absolutePath.isEmpty()) {
        return;
    }
    // Already warm — fast path, no allocation.
    if (m_shaderManager.m_textureCache.find(absolutePath) != m_shaderManager.m_textureCache.end()) {
        return;
    }
    // Already in flight — a worker is mid-load; deduplicate to avoid
    // duplicate GPU uploads when several transitions request the
    // same path before the first one completes.
    if (m_shaderManager.m_textureLoadsInFlight.contains(absolutePath)) {
        return;
    }
    m_shaderManager.m_textureLoadsInFlight.insert(absolutePath);

    // SVG default size matches `loadUserTextureImage`'s 1024 max-axis.
    // Captured by value into the worker. The cache is path-keyed; if
    // we ever need per-asset size variants the cache key must include
    // the rasterised dimension, otherwise two callers requesting the
    // same SVG at different sizes would race on whichever one wins.
    constexpr int svgMaxDim = 1024;

    // Capture the cache generation at submission time. The queued
    // upload lambda compares this against the live
    // `m_shaderManager.m_textureCacheGeneration` and discards if mismatched — i.e. a
    // hot-reload (`effectsChanged`) bumped the generation between
    // submission and upload, so this worker's bytes are stale and
    // must not re-populate the cleared cache.
    const quint64 submissionGeneration = m_shaderManager.m_textureCacheGeneration;

    class Loader : public QRunnable
    {
    public:
        Loader(QPointer<PlasmaZonesEffect> effect, QString path, int svgMaxDim, quint64 submissionGeneration)
            : m_effect(std::move(effect))
            , m_path(std::move(path))
            , m_svgMaxDim(svgMaxDim)
            , m_submissionGeneration(submissionGeneration)
        {
        }
        void run() override
        {
            QImage img = loadUserTextureImage(m_path, m_svgMaxDim);
            QPointer<PlasmaZonesEffect> effect = m_effect;
            QString path = m_path;
            const quint64 submissionGeneration = m_submissionGeneration;
            // Bounce back to the compositor thread for the GL upload.
            // The QPointer guards against the effect being destroyed
            // while the worker was running — destructor's
            // `m_shaderManager.m_textureLoaderPool.waitForDone()` already protects
            // against this for the in-process teardown case, but the
            // QPointer is defence-in-depth for any future caller that
            // schedules this without owning the pool's lifetime.
            QMetaObject::invokeMethod(
                effect.data(),
                [effect, path, img = std::move(img), submissionGeneration]() mutable {
                    if (!effect) {
                        return;
                    }
                    // Generation check FIRST — before touching
                    // `m_shaderManager.m_textureCache` or `m_shaderManager.m_textureLoadsInFlight`. If
                    // the cache was cleared underneath us by a hot-
                    // reload (`effectsChanged`) the in-flight set was
                    // already cleared too; touching it now would mean
                    // racing with state the lambda has no business
                    // mutating. Discard cleanly.
                    if (submissionGeneration != effect->m_shaderManager.m_textureCacheGeneration) {
                        qCDebug(lcEffect) << "warmUserTextureAsync: discarding stale upload for" << path
                                          << "(generation mismatch — cache cleared during load)";
                        return;
                    }
                    effect->m_shaderManager.m_textureLoadsInFlight.remove(path);
                    if (img.isNull()) {
                        qCWarning(lcEffect) << "warmUserTextureAsync: load failed for" << path;
                        return;
                    }
                    // This lands from the EVENT LOOP, between frames — not inside a
                    // paint cycle — so the compositor's GL context is not guaranteed
                    // current. Everything below is GL: the upload, the filter/wrap
                    // state, and the LRU sweep, whose eviction destroys a GLTexture and
                    // therefore calls glDeleteTextures. Issuing any of that against no
                    // context (or, worse, another thread's) is undefined. Every other
                    // off-paint GL site in the effect makes the context current first
                    // and says so; this one was missed. Bail rather than latch: the next
                    // use of the path loads it synchronously inside a paint.
                    if (!KWin::effects || !KWin::effects->makeOpenGLContextCurrent()) {
                        qCWarning(lcEffect)
                            << "warmUserTextureAsync: no current GL context, discarding upload for" << path;
                        return;
                    }
                    // Re-check the cache: another transition may have
                    // synchronously loaded this path while we were on
                    // the worker. Honour the existing entry; dropping
                    // ours avoids a redundant GPU upload.
                    if (effect->m_shaderManager.m_textureCache.find(path)
                        != effect->m_shaderManager.m_textureCache.end()) {
                        return;
                    }
                    std::unique_ptr<KWin::GLTexture> gpuTex = KWin::GLTexture::upload(img);
                    if (!gpuTex) {
                        qCWarning(lcEffect) << "warmUserTextureAsync: GLTexture::upload failed for" << path;
                        return;
                    }
                    gpuTex->setFilter(GL_LINEAR);
                    gpuTex->setWrapMode(GL_CLAMP_TO_EDGE);
                    CachedTexture cachedTex;
                    cachedTex.texture = std::move(gpuTex);
                    cachedTex.lastAppliedWrap = GL_CLAMP_TO_EDGE;
                    cachedTex.lastAccessTick = ++effect->m_shaderManager.m_textureCacheAccessTick;
                    effect->m_shaderManager.m_textureCache.emplace(path, std::move(cachedTex));
                    // No `pending` to guard: this is a WARM, not a bind. Nothing holds a
                    // pointer to the entry we just inserted, so the worst the sweep can do
                    // is discard the warm we just paid for — which is exactly what being
                    // over the bound means. A live transition's slots are protected by the
                    // shaderTransitions() scan inside.
                    effect->evictLruTextureIfOverBound(/*pending=*/nullptr);
                    qCDebug(lcEffect) << "warmUserTextureAsync: cached" << path;
                },
                Qt::QueuedConnection);
        }

    private:
        QPointer<PlasmaZonesEffect> m_effect;
        QString m_path;
        int m_svgMaxDim;
        quint64 m_submissionGeneration;
    };

    // Pass `this` as a QPointer so the conversion happens at the call
    // site (where `this` is known live), not inside the ctor body where
    // a freed `effect` mid-construction would silently degrade to a raw
    // pointer that never registers with QPointer's tracker.
    auto* loader = new Loader(QPointer<PlasmaZonesEffect>(this), absolutePath, svgMaxDim, submissionGeneration);
    loader->setAutoDelete(true);
    m_shaderManager.m_textureLoaderPool.start(loader);
}

const CachedShader*
PlasmaZonesEffect::compileOrLoadAnimationShader(const QString& effectId,
                                                const PhosphorAnimationShaders::AnimationShaderEffect& eff)
{
    // KWin-specific default vertex stage. Hardcoded here rather than
    // loaded from `data/animations/shared/animation.vert` because that
    // file is shared with the daemon's RHI surface pipeline, which
    // requires all uniforms to live in UBOs (default-block uniforms
    // aren't supported under Qt-RHI/SPIR-V) and supplies vertex
    // positions already in clip space — exactly the opposite of what
    // KWin's classic-GL OffscreenEffect needs:
    //
    //   • Positions arrive in screen-pixel space (KWin's
    //     `OffscreenData::paint` writes window-rect pixel coords into
    //     the streaming buffer at `GLVertex2DLayout`'s position slot).
    //   • The pixel→NDC projection lives in the
    //     `modelViewProjectionMatrix` default-block uniform, which KWin
    //     sets via `Mat4Uniform::ModelViewProjectionMatrix` (mapped to
    //     uniform name `modelViewProjectionMatrix` per
    //     `KWin::GLShader::resolveLocations` in
    //     `<opengl/glshader.cpp>`). Skipping the multiplication leaves
    //     the redirected quad at coords like (1920, 1080) — entirely
    //     outside the [-1, 1] viewport — and the transition shader
    //     runs but paints to nothing. Manifest: `tryBeginShader`
    //     resolves, `beginShaderTransition` installs cleanly, no
    //     compile warnings, but the user sees no visible animation.
    //   • Attribute slot indices match `KWin::VA_Position` (0) and
    //     `KWin::VA_TexCoord` (1) per `<opengl/glvertexbuffer.h>`;
    //     explicit `layout(location = N)` decorations bypass KWin's
    //     `bindAttributeLocation("position", ...)` lookup (which is
    //     name-only and would mismatch our `texCoord` vs KWin's
    //     `texcoord`).
    //
    // texCoord is flipped to a Y-down screen UV. KWin's
    // `OffscreenData::paint` populates the texCoord attribute from its
    // bottom-origin offscreen FBO (Y-up). The shader contract — and the
    // daemon's `kDefaultVertexShaderSource` — expect `vTexCoord` Y-down
    // (y = 0 at the top), so flip here. The redirected window texture
    // is Y-up to match KWin's FBO, which is why shaders read it through
    // `surfaceColor()` (which re-flips the sample coordinate) instead
    // of sampling `uTexture0` directly — see the canonical header
    // `data/animations/shared/animation_uniforms.glsl`. Without the
    // flip, `vTexCoord` is Y-up on this path: the window still composes
    // upright (the Y-up texture cancels it) but every screen-space
    // effect — matrix rain direction, directional wipes — renders
    // inverted. The other kwin-vs-daemon difference is `gl_Position`:
    // KWin needs the modelViewProjectionMatrix to place the redirected
    // quad, the daemon emits clip-space directly.
    //
    // Authors that ship a per-shader vertex stage via metadata's
    // `vertexShader` field own the matrix / attribute contract
    // themselves: emit the same `vTexCoord` flip under
    // `#ifdef PLASMAZONES_KWIN` and multiply `position` by
    // modelViewProjectionMatrix on the kwin path. See the canonical
    // GLSL header (`data/animations/shared/animation_uniforms.glsl`).
    static const QByteArray kKwinDefaultVertexSource = QByteArrayLiteral(
        "#version 450\n"
        "\n"
        "layout(location = 0) in vec2 position;\n"
        "layout(location = 1) in vec2 texCoord;\n"
        "\n"
        "layout(location = 0) out vec2 vTexCoord;\n"
        "\n"
        "uniform mat4 modelViewProjectionMatrix;\n"
        "\n"
        "void main() {\n"
        "    vTexCoord = vec2(texCoord.x, 1.0 - texCoord.y);\n"
        "    gl_Position = modelViewProjectionMatrix * vec4(position, 0.0, 1.0);\n"
        "}\n");

    auto cacheIt = m_shaderManager.m_shaderCache.find(effectId);
    if (cacheIt == m_shaderManager.m_shaderCache.end()) {
        // Diagnostic-once-per-compile: log multipass degradation when the
        // shader is first compiled for this session, not on every transition
        // install. Lifecycle events (window.move on a drag, window.focus on
        // alt-tab) can fire beginShaderTransition many times in quick
        // succession against an already-cached effect; a per-install log
        // would flood the journal. Cache invalidation (effectsChanged →
        // m_shaderManager.m_shaderCache.clear) re-fires the log at the next install, which
        // is the right semantic for hot-reload.
        if (eff.isMultipass) {
            qCInfo(lcEffect) << "Animation effect" << effectId
                             << "is multipass — compositor path runs single-pass only (buffer passes skipped)";
        }

        QFile shaderFile(eff.fragmentShaderPath);
        if (!shaderFile.open(QIODevice::ReadOnly)) {
            qCWarning(lcEffect) << "Failed to open shader file" << eff.fragmentShaderPath;
            return nullptr;
        }
        const QString rawSource = QString::fromUtf8(shaderFile.readAll());
        if (rawSource.isEmpty()) {
            qCWarning(lcEffect) << "Shader file is empty" << eff.fragmentShaderPath;
            return nullptr;
        }
        QStringList animIncludePaths;
        for (const QString& sp : m_shaderManager.m_animationShaderRegistry.searchPaths()) {
            const QString sharedDir = sp + QStringLiteral("/shared");
            if (QDir(sharedDir).exists()) {
                animIncludePaths.append(sharedDir);
            }
        }
        QString includeError;
        const QString currentDir = QFileInfo(eff.fragmentShaderPath).absolutePath();
        // T1.5: assemble an entry-only animation pack (pTransition / pIn+pOut,
        // no main()) into a full translation unit BEFORE expansion — identical to
        // the daemon's loadFragmentShader — so the prologue's `#include` resolves
        // and the generated main() dispatches by direction. A traditional main()
        // pack is returned unchanged. Same prologue + candidates the daemon uses,
        // so both runtimes compile the same source.
        const QString assembledSource = PhosphorShaders::assembleEntryPoint(
            rawSource, PhosphorAnimationShaders::AnimationShaderRegistry::animationEntryPrologue(),
            PhosphorAnimationShaders::AnimationShaderRegistry::animationEntryCandidates());
        QString expanded = PhosphorShaders::ShaderIncludeResolver::expandIncludes(assembledSource, currentDir,
                                                                                  animIncludePaths, &includeError);
        if (expanded.isEmpty()) {
            qCWarning(lcEffect) << "Failed to expand shader includes for" << effectId << ":" << includeError;
            return nullptr;
        }

        // T1.1: splice the generated named-param preamble (`#define p_<id> ...`)
        // after `#version`, identically to the daemon's loadFragmentShader. The
        // accessors it emits (`customParams[N].xyzw`, `customColors[N]`,
        // `uTexture<N>`) are declared in BOTH branches of animation_uniforms.glsl
        // — including the PLASMAZONES_KWIN default-block branch injected below —
        // so the same preamble compiles on this GL path and the daemon RHI path.
        // Done before injectKwinDefineAfterVersion so the KWIN define still lands
        // first after `#version`; the preamble's defines are pure text macros,
        // expanded only where used (after the UBO include), so ordering is safe.
        expanded = PhosphorShaders::spliceAfterVersion(
            expanded, PhosphorAnimationShaders::AnimationShaderRegistry::paramPreamble(eff));

        // HDR colour management. Installing this shader via
        // OffscreenEffect::setShader REPLACES KWin's present shader, which
        // normally carries the sRGB → output-colorspace conversion
        // (ShaderTrait::TransformColorspace in KWin's own base.frag). Without
        // it the transition writes sRGB verbatim into KWin's blending space —
        // on an HDR output that space is gamma2.2 in the display's container
        // colorimetry with 1.0 = peak luminance, so every window animation
        // rendered dim and desaturated. Same bug class as the decoration
        // present shader fix in decoration_render.cpp (kPresentFragment); the
        // conversion below mirrors it, and KWin's base.frag, step for step:
        // encoding → nits, colorimetry transform, tonemap, destination
        // encoding.
        //
        // Deliberately NOT sourceEncodingToNitsInDestinationColorspace(): it
        // folds doTonemapping() in at the end, which would tonemap twice — a
        // double compression on HDR, precisely the case this exists for. No
        // opacity modulate either: unlike the present shader, this path's
        // window-rule opacity is folded into the sample by surfaceColor()
        // (animation_uniforms.glsl), and this shader is compiled without the
        // Modulate trait so KWin's TRAIT_MODULATE slot never applies.
        //
        // The override must be spliced AFTER expandIncludes ran above:
        // colormanagement.glsl lives in KWin's `:/opengl/` resource, which
        // KWin's GLShader::preprocess resolves for every source it compiles
        // (custom ones included) — the phosphor include resolver would fail
        // the whole expansion on it. The `#define` lands before the expanded
        // animation_uniforms.glsl text, whose `#ifndef PZ_FINALIZE_COLOR`
        // identity default then yields to this override; the generated entry
        // main()s and bmw_compat's setOutputColor route their fragColor
        // writes through the macro.
        //
        // The colorspace uniforms come free: KWin's OffscreenData::paint
        // calls setColorspaceUniforms(sRGB, renderTarget.colorDescription(),
        // Perceptual) on whatever shader setShader() installed. Do not push
        // them from drawWindow — that is a redundant write KWin overwrites.
        //
        // On SDR this whole block is an exact no-op (identity colorimetry,
        // round-tripping transfer functions, tonemap degrades to a clamp that
        // cannot bite), so "SDR pixel-identical" is the regression test.
        //
        // The desktop-switch path (desktoptransitionmanager.cpp) deliberately
        // does NOT get this splice: its capture FBOs inherit the output's
        // colorDescription, so both blend inputs already live in the blending
        // space and converting again would double-transform. It keeps the
        // identity default.
        static const QString kFinalizeColorBlock = QStringLiteral(
            "#include \"colormanagement.glsl\"\n"
            "vec4 pzFinalizeColor(vec4 c) {\n"
            "    c = encodingToNits(c, sourceNamedTransferFunction,\n"
            "                       sourceTransferFunctionParams.x, sourceTransferFunctionParams.y);\n"
            "    c.rgb = (colorimetryTransform * vec4(c.rgb, 1.0)).rgb;\n"
            "    c.rgb = doTonemapping(c.rgb);\n"
            "    return nitsToDestinationEncoding(c);\n"
            "}\n"
            "#define PZ_FINALIZE_COLOR(c) pzFinalizeColor(c)\n");
        expanded = PhosphorShaders::spliceAfterVersion(expanded, kFinalizeColorBlock);

        // Selects the default-block branch in `animation_uniforms.glsl`.
        // KWin's `KWin::GLShader` API addresses default-block uniforms only
        // (no UBO bind path), so the canonical header's `#ifdef
        // PLASMAZONES_KWIN` branch emits plain `uniform float iTime;`-style
        // declarations instead of the daemon's `layout(std140, binding=0)
        // uniform AnimationUniforms { ... };`. The macro must land AFTER the
        // shader's `#version` line — the GLSL spec disallows tokens before
        // `#version` other than whitespace and comments — so the helper
        // below splices it between the version directive and the rest of
        // the source.
        const QByteArray fragWithKwinDefine = injectKwinDefineAfterVersion(expanded);

        // Route the built-in default vertex source through the same injection
        // as custom vertex stages so it gets the layout(location) extension
        // enables (it uses explicit locations too, and KWin compiles it at
        // #version 140 — see injectKwinDefineAfterVersion).
        QByteArray vertWithKwinDefine = injectKwinDefineAfterVersion(QString::fromUtf8(kKwinDefaultVertexSource));
        if (!eff.vertexShaderPath.isEmpty()) {
            QFile vertFile(eff.vertexShaderPath);
            if (!vertFile.open(QIODevice::ReadOnly)) {
                qCWarning(lcEffect) << "Failed to open vertex shader" << eff.vertexShaderPath << "for effect"
                                    << effectId << "— falling back to KWin default vertex stage";
            } else {
                const QString rawVert = QString::fromUtf8(vertFile.readAll());
                if (rawVert.isEmpty()) {
                    qCWarning(lcEffect) << "Vertex shader file is empty" << eff.vertexShaderPath << "for effect"
                                        << effectId << "— falling back to KWin default vertex stage";
                } else {
                    const QString vertDir = QFileInfo(eff.vertexShaderPath).absolutePath();
                    QString vertIncErr;
                    const QString expandedVert = PhosphorShaders::ShaderIncludeResolver::expandIncludes(
                        rawVert, vertDir, animIncludePaths, &vertIncErr);
                    if (expandedVert.isEmpty()) {
                        qCWarning(lcEffect) << "Failed to expand vertex shader includes for" << effectId << ":"
                                            << vertIncErr << "— falling back to KWin default vertex stage";
                    } else {
                        // Same named-param preamble as the fragment stage: a
                        // vertex-driven pack (wobble's velocity lag, the
                        // pendulum swing) reads its `p_<id>` params in the
                        // VERT, and the daemon bake already splices the
                        // preamble into both stages — without this the GL
                        // compile failed on the undefined identifiers and
                        // the transition silently never installed.
                        const QString vertWithParams = PhosphorShaders::spliceAfterVersion(
                            expandedVert, PhosphorAnimationShaders::AnimationShaderRegistry::paramPreamble(eff));
                        vertWithKwinDefine = injectKwinDefineAfterVersion(vertWithParams);
                    }
                }
            }
        }

        // TransformColorspace is DECLARATIVE ONLY here, exactly as on the
        // decoration present shader (decoration_render.cpp): with a custom
        // fragment source generateCustomShader uses the traits for nothing
        // but listDefines, and colormanagement.glsl never reads
        // TRAIT_TRANSFORM_COLORSPACE. The conversion is the explicit
        // pzFinalizeColor splice above and runs with or without this flag; it
        // is declared so the shader's traits describe what it actually does.
        auto shader = KWin::ShaderManager::instance()->generateCustomShader(
            KWin::ShaderTrait::MapTexture | KWin::ShaderTrait::TransformColorspace, vertWithKwinDefine,
            fragWithKwinDefine);
        // KWin 6.7 removed GLShader::isValid(); generateCustomShader now returns
        // nullptr when compilation or linking fails, so a null check is the
        // validity test.
        if (!shader) {
            qCWarning(lcEffect) << "Failed to compile shader transition" << effectId
                                << "— caching the failure so subsequent transitions skip the recompile "
                                   "until the next shader hot-reload.";
            // Cache a null-shader sentinel. A failed compile must NOT be
            // retried on every transition: without this the cache miss recurs
            // each time and the full read+assemble+expand+compile re-runs on
            // the compositor thread — a per-command stall (the same failure
            // mode the GLSL #extension fix addressed for the morph shader).
            // The sentinel is distinguishable from a live entry because a
            // successful compile always emplaces a non-null shader. It is
            // cleared by the effectsChanged handler's m_shaderCache.clear()
            // (shader hot-reload / settings change), so a corrected shader
            // recompiles on the next reload.
            m_shaderManager.m_shaderCache.emplace(effectId, CachedShader{});
            return nullptr;
        }

        CachedShader cached;
        // Animation-shader contract — names sourced from
        // `PhosphorAnimationShaders::AnimationShaderContract`. Both the
        // daemon overlay-surface execution site and this compositor
        // window-content execution site resolve the same names.
        cached.iTimeLoc = shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kITime);
        cached.iResolutionLoc =
            shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIResolution);
        cached.iTimeDeltaLoc = shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kITimeDelta);
        cached.iFrameLoc = shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIFrame);
        cached.iDateLoc = shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIDate);
        cached.iMouseLoc = shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIMouse);
        cached.iIsReversedLoc =
            shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIIsReversed);
        cached.iSurfaceScreenPosLoc =
            shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kISurfaceScreenPos);
        cached.iAnchorSizeLoc =
            shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIAnchorSize);
        cached.iAnchorPosInFboLoc =
            shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIAnchorPosInFbo);
        cached.iAnchorRectInTextureLoc =
            shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIAnchorRectInTexture);
        // Geometry-morph uniforms (snap / maximize / layout-switch
        // cross-fade). -1 when the shader is not a morph shader (doesn't
        // read them) — paintWindow guards on >= 0 so non-morph transitions
        // pay nothing.
        cached.iFromRectLoc = shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIFromRect);
        cached.iToRectLoc = shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIToRect);
        // Task-manager icon rect for minimize-to-icon packs (genie,
        // phosphor-siphon). -1 for every pack that doesn't declare it —
        // same pay-nothing guard.
        cached.iIconRectLoc = shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIIconRect);
        cached.iOldWindowLoc = shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kUOldWindow);
        // Surface-layer-stack uniforms — every animation shader resolves these
        // (declared in the shared header and read through surfaceColor()), so
        // they are valid whenever the shader samples the window surface. The
        // kwin-effect binds the layered surface + flag each frame; a window with
        // no surface layers pushes the flag as 0 and the shader samples the bare
        // uTexture0. See AnimationShaderContract::kUSurfaceLayer / kIHasSurfaceLayer.
        cached.uSurfaceLayerLoc =
            shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kUSurfaceLayer);
        cached.iHasSurfaceLayerLoc =
            shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIHasSurfaceLayer);
        cached.iHasOldWindowLoc =
            shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIHasOldWindow);
        // Audio spectrum (opt-in audio.glsl module; -1 for non-audio packs).
        cached.iAudioSpectrumSizeLoc =
            shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIAudioSpectrumSize);
        cached.uAudioSpectrumLoc =
            shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kUAudioSpectrum);
        cached.iMoveVelocityLoc =
            shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIMoveVelocity);
        cached.iMoveOffsetLoc =
            shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIMoveOffset);
        cached.iMoveVelocity2Loc =
            shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIMoveVelocity2);
        cached.iMoveTrailLoc = shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIMoveTrail);
        cached.iMoveMeshLoc = shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIMoveMesh);
        cached.iLayerRectInTextureLoc =
            shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kILayerRectInTexture);
        // uTexture0 — for the transition-time composite retarget (see
        // CachedShader::uTexture0Loc). The name is the contract's literal
        // sampler identifier (animation_uniforms.glsl, both branches).
        cached.uTexture0Loc = shader->uniformLocation("uTexture0");
        // SetOpacity rule opacity — a separate concern from the morph uniforms
        // above: applies to ALL shaders (compositor path only), so surfaceColor
        // can dim the surface for a SetOpacity rule. See
        // AnimationShaderContract::kIWindowOpacity.
        cached.iWindowOpacityLoc =
            shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIWindowOpacity);
        // Cache element locations for the per-effect declared parameter
        // slots: `customParams[0..kMaxCustomParams-1]` for float / int /
        // bool params, and `customColors[0..kMaxCustomColors-1]` for color
        // params. Each declared parameter lands in one of these slots —
        // see `AnimationShaderRegistry::translateAnimationParams` for
        // the exact mapping. `glGetUniformLocation` returns -1 for slots
        // the shader didn't reference (e.g. a one-param effect that the
        // GLSL compiler optimises away the unused tail of either array);
        // the per-frame push loop in paintWindow guards against -1 to
        // skip the setUniform call.
        for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams; ++slot) {
            // Pre-baked element-name table — no per-slot QByteArray
            // alloc. Sized + static_asserted against the contract budget
            // at the namespace-level definition.
            cached.customParamsLoc[slot] = shader->uniformLocation(kCustomParamsElementNames[slot]);
        }
        for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors; ++slot) {
            cached.customColorsLoc[slot] = shader->uniformLocation(kCustomColorsElementNames[slot]);
        }
        // User textures: resolve sampler + iTextureResolution[N] uniform
        // locations only. The actual texture upload happens per-leg
        // inside `beginShaderTransition`'s body below — keyed by the
        // resolved path in `m_shaderManager.m_textureCache` so two legs with different
        // override paths don't collide on the per-effect cache.
        for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots; ++slot) {
            // GLSL sampler name: uTexture1..3 (slot+1 because uTexture0 is
            // the redirected surface, not user-declared). Matches the
            // overlay shader convention in data/overlays/shared/textures.glsl.
            // Pre-baked from the file-scope `kUserTextureSamplerNames` /
            // `kITextureResolutionKeys` arrays — no per-slot QByteArray
            // alloc per shader install.
            cached.userTextureLoc[slot] = shader->uniformLocation(kUserTextureSamplerNames[slot]);
            cached.iTextureResolutionLoc[slot] = shader->uniformLocation(kITextureResolutionKeys[slot]);
        }
        cached.shader = std::move(shader);
        cacheIt = m_shaderManager.m_shaderCache.emplace(effectId, std::move(cached)).first;
    }
    return &cacheIt->second;
}

} // namespace PlasmaZones
