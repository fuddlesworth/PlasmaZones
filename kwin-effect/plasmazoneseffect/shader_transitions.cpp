// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"
#include "shader_internal.h"
#include "shader_resolve.h"
#include "window_query.h"

#include "../windowanimator.h"

#include <PhosphorAnimation/AnimationLimits.h>
#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/ProfileTree.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>
#include <PhosphorShaders/ShaderParamPreamble.h>
#include <PhosphorRules/ExclusionRules.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleSet.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

#include <effect/effecthandler.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>

#include <QByteArray>
#include <QChar>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPainter>
#include <QPointer>
#include <QRunnable>
#include <QStringList>
#include <QSvgRenderer>
#include <QThreadPool>
#include <QTimer>
#include <QVariantMap>
#include <QVector4D>

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
using ShaderInternal::kUserTextureWrapKeys;
using ShaderInternal::shaderClockNowMs;

/// Splice `#define PLASMAZONES_KWIN` between the shader's `#version`
/// directive and the rest of the source. The macro selects the
/// default-block branch in `data/animations/shared/animation_uniforms.glsl`,
/// which is what KWin's classic-GL `KWin::GLShader` API requires (no UBO
/// bind path). The GLSL spec disallows non-comment, non-whitespace tokens
/// before `#version`, so the define cannot just be prepended verbatim —
/// we find the first newline at or after the `#version` line and inject
/// after it.
///
/// Defensive fallback: if no `#version` is present (a hand-rolled
/// shader that ships without one), synthesize `#version 450` and warn.
/// KWin on Wayland with modern Mesa runs core profile, where the
/// directive is mandatory — a bare `#define` would fail compile with
/// a confusing line-1 error. The bake test on the daemon side already
/// catches any built-in shader that ships without `#version`; the
/// fallback exists to surface third-party packs that violate the
/// contract with a useful journal entry rather than a cryptic GLSL
/// error.
QByteArray ShaderInternal::injectKwinDefineAfterVersion(const QString& source)
{
    // Strip a leading UTF-8 BOM (U+FEFF) before anything else. The BOM
    // is not a Unicode whitespace category, so QString::trimmed() does
    // NOT remove it — without this strip a BOM-prefixed shader's first
    // line is "﻿#version 450", trimmed("...") still leads with the
    // BOM, the `startsWith("#version")` check fails, and we fall into
    // the "no #version" prepend path. That writes
    // `#define PLASMAZONES_KWIN\n﻿#version 450...`, which the
    // GLSL compiler rejects because `#version` must be the first
    // non-comment token of the source.
    QString working = source;
    if (working.startsWith(QChar(0xFEFF))) {
        working.remove(0, 1);
    }
    if (working.isEmpty()) {
        // BOM-only source (or genuinely empty input) — emit a debug
        // breadcrumb so a hot-reload that lands here surfaces the real
        // cause faster than "GLSL compile failed: line 1 unexpected".
        // Debug-level rather than warn because empty input from the
        // bake test or unit fixtures is also legal.
        qCDebug(lcEffect) << "injectKwinDefineAfterVersion: empty source after BOM strip — returning bare define";
    }

    // Detect line ending so the injected define matches the source's
    // convention. GLSL compilers accept mixed CRLF/LF, but mixing
    // produces visually inconsistent diffs and trips lints. If any
    // CRLF appears in the source, emit "\r\n"; otherwise plain "\n".
    const bool useCrlf = working.contains(QStringLiteral("\r\n"));
    const QString eol = useCrlf ? QStringLiteral("\r\n") : QStringLiteral("\n");
    // KWin 6.7's generateCustomShader compiles custom effect shaders at GLSL
    // #version 140 (it rewrites our #version 450 down to the GL context's core
    // version). At 140 the `layout(location = N)` qualifiers our vertex stages
    // declare on in/out attributes are illegal without these ARB extensions, so
    // the vertex shader fails to compile (NVIDIA error C7548). Failed compiles
    // are NOT cached (the compile path returns false without inserting into
    // m_shaderCache), so every transition then re-runs the whole
    // assemble+compile on the compositor thread — the cause of the severe
    // per-command window-movement / mode-change lag. The daemon's Qt-RHI/SPIR-V
    // path (no PLASMAZONES_KWIN) needs the explicit locations for SPIR-V, so we
    // enable the extensions on the KWin path rather than stripping the
    // qualifiers. `: enable` is a harmless no-op on the fragment stage and on
    // drivers that already expose explicit locations in core 140. The
    // directives precede every declaration (only KWin's #defines and the
    // source's leading comments come before them), which is all NVIDIA's
    // compiler requires.
    const QString defineLine = QStringLiteral("#extension GL_ARB_explicit_attrib_location : enable") + eol
        + QStringLiteral("#extension GL_ARB_separate_shader_objects : enable") + eol
        + QStringLiteral("#define PLASMAZONES_KWIN") + eol;

    // Walk the source line-by-line and find the FIRST line whose
    // non-whitespace prefix is `#version`. A naive
    // `source.indexOf("#version")` would match `#version` substrings
    // embedded in `// ...` line comments or `/* ... */` block comments,
    // splicing the define into the comment body — the macro silently
    // disappears and the shader compiles against the wrong UBO ABI.
    //
    // `foundVersion` is tracked separately from `realVersionEnd` because
    // a shader whose `#version` line ends at EOF without a trailing
    // newline (rare but legal: a manual editor save that strips the
    // final LF) hits `lineEnd == -1` on the match, leaving
    // `realVersionEnd == -1`. Without the boolean we can't distinguish
    // "no #version directive at all" (prepend define) from "#version
    // at EOF, no newline" (must append `\n` + define). Conflating the
    // two would emit `#define PLASMAZONES_KWIN\n#version 450`, which
    // the GLSL compiler rejects because `#version` must be the first
    // directive in the translation unit.
    int searchFrom = 0;
    int realVersionEnd = -1;
    bool foundVersion = false;
    bool inBlockComment = false;
    while (searchFrom < working.size()) {
        const int lineEnd = working.indexOf(QLatin1Char('\n'), searchFrom);
        const int lineStop = (lineEnd < 0) ? working.size() : lineEnd;
        QStringView line = QStringView(working).mid(searchFrom, lineStop - searchFrom);
        // Strip block comments (single-line forms only — multi-line
        // detection is handled across iterations via inBlockComment).
        if (inBlockComment) {
            const int closeIdx = line.indexOf(QLatin1String("*/"));
            if (closeIdx < 0) {
                searchFrom = (lineEnd < 0) ? working.size() : lineEnd + 1;
                continue;
            }
            line = line.mid(closeIdx + 2);
            inBlockComment = false;
        }
        // Drop line and same-line block comments before checking.
        QString stripped;
        stripped.reserve(line.size());
        for (int i = 0; i < line.size();) {
            if (i + 1 < line.size() && line[i] == QLatin1Char('/') && line[i + 1] == QLatin1Char('/')) {
                break; // rest of line is comment
            }
            if (i + 1 < line.size() && line[i] == QLatin1Char('/') && line[i + 1] == QLatin1Char('*')) {
                const int closeIdx = line.indexOf(QLatin1String("*/"), i + 2);
                if (closeIdx < 0) {
                    inBlockComment = true;
                    break;
                }
                i = closeIdx + 2;
                continue;
            }
            stripped.append(line[i]);
            ++i;
        }
        const QString trimmed = stripped.trimmed();
        if (trimmed.startsWith(QLatin1String("#version"))) {
            realVersionEnd = lineEnd; // newline AFTER the version line, or -1 if at EOF
            foundVersion = true;
            break;
        }
        if (lineEnd < 0)
            break;
        searchFrom = lineEnd + 1;
    }

    if (!foundVersion) {
        // No #version directive (or it was shadowed by an early break
        // mid block-comment). KWin on Wayland with modern Mesa runs
        // core profile, where `#version` is mandatory and a bare
        // `#define` as the first token would fail compile with a
        // confusing line-1 error. Synthesize a `#version 450` directive
        // matching the rest of the animation suite, prepend the define,
        // and warn so the author sees the contract violation in the
        // journal.
        qCWarning(lcEffect) << "Shader source has no #version directive — synthesizing `#version 450`. "
                               "Animation and surface packs MUST declare `#version 450` (the canonical contract); "
                               "the shader-validate CI gate enforces this for the bundled packs.";
        const QString header = QStringLiteral("#version 450") + eol + defineLine;
        return (header + working).toUtf8();
    }
    if (realVersionEnd < 0) {
        // `#version` line ends at EOF with no trailing newline. The
        // GLSL spec requires `#version` to be the FIRST directive, so
        // we cannot prepend the define; we must append it (with a
        // separator newline) so the compiler still sees `#version`
        // first. Without this branch the `!foundVersion` path above
        // would run instead and emit invalid `#define\n#version`
        // GLSL.
        return (working + QLatin1Char('\n') + defineLine).toUtf8();
    }
    working.insert(realVersionEnd + 1, defineLine);
    return working.toUtf8();
}

namespace {

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
inline QImage loadUserTextureImage(const QString& path, int svgMaxDim = 1024)
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

/// Translate a metadata / params wrap-mode string to the GL enum the
/// kwin-effect applies at bind time. Empty / unrecognised values fall
/// through to `GL_CLAMP_TO_EDGE` (the GL default and the daemon's
/// default per `ShaderNodeRhi::setUserTextureWrap`'s normalisation).
inline GLenum wrapStringToEnum(const QString& wrap)
{
    const QString lower = wrap.toLower();
    if (lower == QLatin1String("repeat"))
        return GL_REPEAT;
    if (lower == QLatin1String("mirror") || lower == QLatin1String("mirrored"))
        return GL_MIRRORED_REPEAT;
    return GL_CLAMP_TO_EDGE;
}

/// Parse a D-Bus setting variant containing a JSON-encoded string and
/// dispatch to one of two callers based on the document's top-level
/// shape. Used by the three `load*FromDbus` setting fetchers in
/// `shader_transitions.cpp` — `loadShaderProfileFromDbus`,
/// `loadMotionProfileTreeFromDbus`, `loadShaderRegistryFromDbus`. Each
/// loader differs only in (a) which shape it expects and (b) what it
/// does with the parsed JSON, so every other piece (UTF-8 decode,
/// document-shape check, malformed-payload warning text) collapses
/// into a single helper call. `loadRuleAnimationsFromDbus` is the
/// odd one out — it issues a raw `QDBusMessage::createMethodCall` to
/// `getAllRules` and parses with `QJsonDocument::fromJson` directly,
/// because it slices the parsed rules through
/// `excludeRulesFrom` / `excludeAnimationsRulesFrom` before sinking.
///
/// The `name` argument feeds the warning so the failure site is
/// identifiable in journals; pass the same `SettingProperty` constant
/// the loader requested.
///
/// `objectSink` runs when the document is a top-level JSON object;
/// `arraySink` runs when it is a top-level JSON array. Pass a
/// no-op (empty std::function) for the shape the caller doesn't
/// expect — a payload of the wrong shape logs and is dropped.
inline void dispatchJsonSetting(QLatin1String name, const QVariant& v,
                                std::function<void(const QJsonObject&)> objectSink,
                                std::function<void(const QJsonArray&)> arraySink)
{
    const QJsonDocument doc = QJsonDocument::fromJson(v.toString().toUtf8());
    if (doc.isObject() && objectSink) {
        objectSink(doc.object());
    } else if (doc.isArray() && arraySink) {
        arraySink(doc.array());
    } else {
        // Name the expected shape explicitly from which sink the caller
        // wired — covers all four combinations (object-only, array-only,
        // both, neither). Picking from the truthy ternary would lie when
        // both sinks are bound, or when neither is.
        const char* expected = (objectSink && arraySink) ? "object or array"
            : objectSink                                 ? "object"
            : arraySink                                  ? "array"
                                                         : "(no shape — caller wired neither sink)";
        qCWarning(lcEffect) << "Failed to parse" << name << "from D-Bus — payload is not a JSON" << expected;
    }
}

} // namespace

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

bool PlasmaZonesEffect::beginShaderTransition(KWin::EffectWindow* window,
                                              const PhosphorAnimationShaders::ShaderProfile& profile, int durationMs,
                                              bool reverse, bool holdCloseGrab, bool holdAddedGrab,
                                              bool animateMinimized,
                                              std::shared_ptr<const PhosphorAnimation::Curve> progressCurve)
{
    const QString effectId = profile.effectiveEffectId();
    if (effectId.isEmpty() || !window)
        return false;

    // A timing curve is meaningless on the animator-driven path (durationMs == 0):
    // that leg reads its progress from the WindowAnimator, whose own profile
    // ALREADY carries the curve, so applying one here would double-ease. The
    // pairing is unrepresentable in the transition (progressCurve is stored only
    // under durationMs > 0, and types.h documents the null-on-that-path invariant
    // as fact), so normalise it away in release instead of letting a caller's
    // curve vanish silently. Assert in debug so the miswiring surfaces at once.
    Q_ASSERT(durationMs > 0 || !progressCurve);
    if (durationMs <= 0 && progressCurve) {
        qCWarning(lcEffect) << "beginShaderTransition: progressCurve supplied with durationMs <= 0 — dropping it; the "
                               "animator-driven path already carries the curve";
        progressCurve.reset();
    }

    // Global animations toggle. Mirrors the daemon's
    // `SurfaceAnimator::beginShow/beginHide` early-out when
    // `setEnabled(false)`. Gating here (rather than only in
    // `tryBeginShaderForEvent`) covers BOTH callsite categories
    // uniformly: window-lifecycle events that flow through
    // `tryBeginShaderForEvent`, and the window.movement.* geometry events that flow through
    // `applyWindowGeometry → beginShaderTransition` directly. Without
    // this gate that path would still install shader transitions
    // even with global animations off.
    if (!m_windowAnimator->isEnabled()) {
        return false;
    }

    // OffscreenEffect's `redirect()` allocates an FBO sized to the
    // window's frame geometry. A window with a genuinely collapsed
    // geometry reports 0×0 (or 1×1) here, and FBO creation aborts
    // with `GL_INVALID_VALUE … <levels>, <width> and <height> must
    // be 1 or greater` followed by `GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT`
    // — the redirect silently leaves the window in a half-broken
    // state that contaminates every subsequent transition until KWin
    // itself reallocates the offscreen data. Skip the install on
    // collapsed surfaces.
    //
    // A MINIMIZED window is rejected UNLESS the caller opted in via
    // animateMinimized (the going-to-minimized leg of
    // window.appearance.minimize is the only opt-in). Minimizing keeps
    // the frame geometry and the last committed buffer intact; painting
    // is merely disabled via PAINT_DISABLED_BY_MINIMIZE, which the
    // EffectWindowVisibleRef installed below lifts for the transition's
    // lifetime (KWin's own Magic Lamp / Squash mechanism). Every OTHER
    // event that can reach here on a minimized window — a snap-commit
    // batch racing the minimize→float bookkeeping, window.focus, a
    // maximizedChanged race — must keep the historical silent no-op:
    // installing (and force-showing) there would animate a window the
    // user believes is minimized. A genuinely 0-sized geometry always
    // bails.
    const QRectF geo = window->frameGeometry();
    if (window->isMinimized() && !animateMinimized) {
        qCDebug(lcEffect) << "beginShaderTransition: skipping minimized window for non-minimize event" << effectId
                          << "window=" << window->windowClass();
        return false;
    }
    if (geo.width() < 1.0 || geo.height() < 1.0) {
        qCDebug(lcEffect) << "beginShaderTransition: skipping collapsed surface" << effectId
                          << "window=" << window->windowClass() << "geo=" << geo
                          << "isMinimized=" << window->isMinimized();
        return false;
    }

    auto eff = m_shaderManager.m_animationShaderRegistry.effect(effectId);
    if (!eff.isValid()) {
        qCWarning(lcEffect) << "beginShaderTransition: registry has no effect" << effectId << "— registry effect count="
                            << m_shaderManager.m_animationShaderRegistry.availableEffects().size();
        return false;
    }
    // Symmetric mirror of the desktop-pass guard (DesktopTransitionManager::
    // begin). This is exclusively the per-window OffscreenEffect path —
    // desktop.switch never routes here — so a desktop-contract pack
    // (appliesTo:["desktop"], two-texture getFromColor/getToColor sampling
    // uFromDesktop/uToDesktop) is always misassigned: on a per-window surface
    // those samplers are unbound and it would paint garbage. It can only reach
    // here via a hand-edited config that assigns a desktop pack at window/global
    // scope (the settings pickers filter desktop packs out of every window
    // event). Refuse it so the window keeps its normal behavior. Guarding on the
    // desktop class specifically — not full class-matching — keeps geometry /
    // appearance / universal shaders untouched. This split is deliberate: a
    // desktop pack on a per-window surface paints GARBAGE (unbound samplers),
    // so it must be refused at this chokepoint; a move/geometry class mismatch
    // paints safely but dead, and is enforced upstream by
    // resolvedShaderAppliesToEvent at every resolution route into here
    // (tryBeginShaderForEvent, applyWindowGeometry). A future direct caller
    // must route through that gate too.
    if (eff.appliesTo.contains(PhosphorAnimation::ProfilePaths::EventClassDesktop)) {
        qCWarning(lcEffect) << "beginShaderTransition: refusing desktop-contract shader" << effectId
                            << "on a per-window event — desktop packs sample unbound uFromDesktop/uToDesktop";
        return false;
    }

    // Everything below THIS point is GL: it compiles the pack's shader (glCreateShader /
    // glLinkProgram), uploads its user textures (glTexImage2D), and runs the LRU eviction,
    // whose victim's destructor is glDeleteTextures. And every caller reaches here OFF the
    // paint cycle — a KWin window signal, a D-Bus reply, a drag ending — where the
    // compositor's context is not current. endShaderTransition, its counterpart, has done
    // this from the start and says why; the begin side never did.
    //
    // Placed here, BELOW the early-outs, not at the top of the function. Making a context
    // current is not free, and the busiest caller by far is a superseded drag geometry
    // update that turns around at one of the guards above without touching GL at all.
    ensureGlContextCurrent();

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
            return false;
        }
        const QString rawSource = QString::fromUtf8(shaderFile.readAll());
        if (rawSource.isEmpty()) {
            qCWarning(lcEffect) << "Shader file is empty" << eff.fragmentShaderPath;
            return false;
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
            return false;
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
            return false;
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

    // A cached null-shader sentinel marks a prior compile failure (see the
    // "Failed to compile" branch above). Skip the transition without
    // re-attempting the expensive compile on every trigger; the effectsChanged
    // handler clears m_shaderCache on hot-reload, so a corrected shader
    // recompiles then. A successfully compiled entry always holds a non-null
    // shader, so this never false-positives on a live transition.
    if (!cacheIt->second.shader) {
        return false;
    }

    // Detect supersession before the teardown so we can skip the
    // redundant unredirect+redirect cycle. KWin's offscreen-effect
    // pipeline reallocates the offscreen render target on every
    // unredirect→redirect, and a back-to-back supersession (e.g. an
    // autotile-reorder drag firing window.move at 60 Hz) would
    // otherwise pay that cost every frame.
    auto* existing = m_shaderManager.findTransition(window);
    const bool isSameWindowSupersession = existing != nullptr;
    // Same-effect short-circuit. KWin fires multiple lifecycle events for
    // a freshly-opened window in quick succession (windowAdded →
    // windowActivated, and windowMaximizedStateChanged if it opens
    // maximized). If the user has the SAME shader bound to several of
    // these events (a common case for window.open + window.focus =
    // fly-in), each event lands here with the same `cacheIt` entry, and
    // without this guard the supersession path erases the in-flight
    // transition and re-inserts a fresh one with startTimeMs = now —
    // restarting the animation from t=0 every time. With a 2 s open
    // duration the user sees multiple staggered copies of the window
    // sliding in (one per restart). Pointer-compare cached entries:
    // m_shaderCache is keyed by effectId, so equal pointers means same
    // effectId. Skip when direction (reverse) and timing mode
    // (durationMs > 0) also match — a true "second trigger of the same
    // already-playing transition" — so a reverse leg or a mode flip
    // still supersedes correctly.
    if (isSameWindowSupersession && existing->cached == &cacheIt->second && existing->reverse == reverse
        && ((existing->durationMs > 0) == (durationMs > 0))) {
        // Same-effect short-circuit: prior leg is intact and continues
        // running. Caller (`tryBeginShaderForEvent`) MUST NOT schedule a
        // fresh teardown timer — the prior leg's own timer (or animator
        // completion) owns the teardown, and a new timer carrying the
        // SAME generation as that prior leg would fire on a shorter
        // duration and cut its animation short.
        return false;
    }
    // Carry the prior transition's closeGrabHeld through supersession so
    // ref/unref stay balanced. If the prior transition refWindow'd the
    // closing window, the ref must stay held (the new transition takes
    // ownership of the release). Without this, erasing the prior entry
    // would lose track of the ref and leak the EffectWindow forever.
    // Symmetric: if neither prior nor new transition holds the grab, no
    // ref work happens — supersession of two non-close transitions is a
    // no-op for ref accounting.
    // Capture EVERY supersession-carry flag from the prior transition
    // BEFORE the erase below — the `existing` pointer is invalidated by
    // the erase call, and any later read (e.g. for transition.addedGrabHeld)
    // would be UB. closeGrabHeld + addedGrabHeld both need to carry through
    // so ref/unref stay balanced; if EITHER prior or new install holds the
    // grab, the new transition's endShaderTransition will balance.
    const bool existingHeldGrab = isSameWindowSupersession ? existing->closeGrabHeld : false;
    const bool existingAddedHeldGrab = isSameWindowSupersession ? existing->addedGrabHeld : false;
    // Acquire the minimized-window paint lifeline BEFORE the supersession
    // erase below drops the prior transition's ref, so the per-reason
    // visible count never transiently hits zero across a supersession on a
    // still-minimized window. Held in a local so every early-return path
    // between here and the transition stamp releases it automatically
    // (RAII); the stamp copies it into the transition, and KWin's
    // per-reason accounting keeps the copy churn balanced. Reaching here
    // minimized implies animateMinimized — the guard at the top already
    // rejected the other case.
    KWin::EffectWindowVisibleRef minimizedPaintLifeline;
    if (window->isMinimized()) {
        minimizedPaintLifeline = KWin::EffectWindowVisibleRef(window, KWin::EffectWindow::PAINT_DISABLED_BY_MINIMIZE);
    }
    if (isSameWindowSupersession) {
        // Erase the prior bookkeeping but skip the unredirect — we're
        // about to re-shader this same window. setShader() below
        // overwrites the shader pointer; no need to null it first.
        m_shaderManager.eraseTransition(window);
        existing = nullptr;
    }
    // else: window is not currently shaderized; falls through to the
    // redirect() call below (no-op endShaderTransition since the map
    // doesn't have the entry).

    const auto& cachedEntry = cacheIt->second;
    ShaderTransition transition;
    transition.cached = &cachedEntry;
    // Surface-extent shaders (metadata `fboExtent: "surface"`) render past
    // the window bounds; `apply()` and paintWindow read this flag to expand
    // the drawn quad + anchor uniforms to the window's output.
    transition.surfaceExtent =
        (eff.fboExtentKind == PhosphorAnimationShaders::AnimationShaderEffect::FboExtentKind::Surface);
    // Vertex-stage grid deformation (e.g. `flow`): subdivide the surface
    // quad so the vertex shader has interior vertices to displace. Only
    // meaningful for surface-extent shaders, mirroring `apply()`'s guard.
    transition.gridSubdivisions = transition.surfaceExtent ? eff.geometryGridSubdivisions : 0;

    // Soft-body lattice constants (iMoveMesh consumers): read the pack's
    // named params so a mesh pack can tune the physics live in the settings
    // UI without a rebuild. Each falls back to the generic default when the
    // pack doesn't declare it, so a non-mesh pack costs nothing. The same
    // named values also reach the shader as p_<name> customParams, so the
    // pack can expose them as ordinary sliders. Values arrive from
    // user-editable metadata, so they are clamped to keep the explicit spring
    // integrator (mesh_sim.cpp) stable. Each per-node update is
    // v' = drag*v - K*t*x, p += t*moveFactor*v' (t = 10 ms substep, K the
    // spring constant); the 2x2 state matrix has det = drag and
    // trace = 1 - K*t^2*moveFactor + drag, so both eigenvalues stay inside the
    // unit circle iff drag < 1 AND K*t^2*moveFactor < 2*(1+drag). drag < 1
    // alone is NOT sufficient: a stiff pack diverges, `settled` never trips,
    // and the transition drives a full-screen-repaint runaway until the 4 s
    // safety teardown. So clamp drag < 1, keep the stiffnesses non-negative,
    // then cap moveFactor to the product bound below.
    // Materialised once here and reused for the slot translation below.
    const QVariantMap params = profile.effectiveParameters();
    {
        auto readParam = [&](const char* name, qreal& out, qreal lo, qreal hi) {
            const auto it = params.constFind(QLatin1String(name));
            if (it != params.constEnd() && it->canConvert<double>()) {
                out = qBound(lo, it->toDouble(), hi);
            }
        };
        readParam("sheetStiffness", transition.meshParams.stiffness, 0.0, 1.0);
        readParam("gripStiffness", transition.meshParams.gripStiffness, 0.0, 1.0);
        readParam("springiness", transition.meshParams.drag, 0.0, 0.99);
        readParam("moveFactor", transition.meshParams.moveFactor, 0.0, 1.0);

        // Enforce K*t^2*moveFactor < 2*(1+drag) so the lattice always settles.
        // K is the effective spring constant: grip nodes use gripStiffness
        // directly (single DOF), while the free-sheet neighbour springs can
        // reach ~2x sheetStiffness at the highest lattice mode, so take
        // max(gripStiffness, 2*sheetStiffness). Cap moveFactor to 80% of the
        // bound for margin; the shipped KWin preset (k=0.018, gk=0.16,
        // mf=0.16, drag=0.82) sits at ~70% and is left untouched.
        constexpr qreal kMeshSubstepMs = 10.0; // matches mesh_sim.cpp integrator step
        const qreal effectiveK = qMax(transition.meshParams.gripStiffness, 2.0 * transition.meshParams.stiffness);
        if (effectiveK > 0.0) {
            const qreal moveFactorLimit =
                0.8 * 2.0 * (1.0 + transition.meshParams.drag) / (kMeshSubstepMs * kMeshSubstepMs * effectiveK);
            transition.meshParams.moveFactor = qMin(transition.meshParams.moveFactor, moveFactorLimit);
        }
    }

    // Translate the friendly parameter map (e.g. {"direction": 1,
    // "parallax": 0.2}) to slot keys, then pack each
    // `customParams<N>_<x|y|z|w>` set into a vec4 we can blast in one
    // setUniform call per slot. Translation honours the metadata
    // declaration order — same allocation the daemon's
    // SurfaceAnimator::runLeg path uses, so a single ShaderProfile
    // produces identical visuals on either runtime.
    const QVariantMap translated =
        PhosphorAnimationShaders::AnimationShaderRegistry::translateAnimationParams(eff, params);
    for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams; ++slot) {
        auto pull = [&](char component) -> float {
            const QString key = PhosphorAnimationShaders::AnimationShaderContract::slotKey(slot, component);
            const auto it = translated.constFind(key);
            if (it == translated.constEnd())
                return 0.0f;
            bool ok = false;
            const float v = it->toFloat(&ok);
            return ok ? v : 0.0f;
        };
        transition.customParamsValues[slot] = QVector4D(pull('x'), pull('y'), pull('z'), pull('w'));
    }
    for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors; ++slot) {
        const QString key = PhosphorAnimationShaders::AnimationShaderContract::colorKey(slot);
        const auto it = translated.constFind(key);
        if (it == translated.constEnd()) {
            continue;
        }
        // Registry-side `translateAnimationParams` coerces every color
        // to a valid QColor — unparseable inputs fall through to the
        // declared default and finally to `Qt::transparent`, which
        // `isValid()` reports as true. So under the documented contract
        // this guard never fires. It exists purely as defence-in-depth
        // against a future caller that bypasses the registry encoder
        // (e.g. injects a raw QString into a profile's
        // effectiveParameters() pass-through) — `redF/greenF/blueF/alphaF`
        // on an invalid QColor are undefined per Qt docs. Falling through
        // to the default-init (0,0,0,0) keeps the slot at transparent
        // black, matching the registry's documented Qt::transparent
        // fallback.
        const QColor c = it->value<QColor>();
        if (!c.isValid()) {
            continue;
        }
        transition.customColorsValues[slot] = QVector4D(c.redF(), c.greenF(), c.blueF(), c.alphaF());
    }
    // User textures: resolve per-leg paths from translated params.
    // translateAnimationParams enriches the map with `uTextureN` /
    // `uTextureN_wrap` keys (pack defaults from `eff.textures` merged
    // with any `friendlyParams` runtime overrides), with relative paths
    // already resolved against `eff.sourceDir`. We look each path up in
    // `m_shaderManager.m_textureCache` (keyed by absolute path so two effects sharing
    // the same texture file share one upload) and stash a non-owning
    // pointer in the transition. Wrap mode is stored per-transition so
    // two legs sharing a path can run with different wrap modes
    // without invalidating each other's cache entry.
    //
    // Pre-warm: kick an async load for every declared texture path
    // BEFORE the synchronous fallback loop below. On the very first
    // transition for a given path the cache is cold and the
    // synchronous loader still runs (so the first frame is correct);
    // every subsequent transition for that path either hits the warm
    // cache (worker completed) or hits the in-flight dedupe (worker
    // still running, synchronous loader picks up the slack one more
    // time). The warmup itself is cheap — early-out on cache hit and
    // on in-flight membership — so a second pass over the same path
    // costs only a hash lookup.
    for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots; ++slot) {
        // Path key shares the GLSL sampler name (`uTexture<N>`) — see
        // the metadata enrichment in `translateAnimationParams`.
        const QString path = translated.value(QLatin1String(kUserTextureSamplerNames[slot])).toString();
        if (!path.isEmpty()) {
            warmUserTextureAsync(path);
        }
    }
    for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots; ++slot) {
        const int glslSlot = slot + 1; // uTexture0 is the surface; only used for the freshly-loaded log
        const QString path = translated.value(QLatin1String(kUserTextureSamplerNames[slot])).toString();
        const QString wrap = translated.value(QLatin1String(kUserTextureWrapKeys[slot])).toString();
        transition.userTextureWrap[slot] = wrapStringToEnum(wrap);
        if (path.isEmpty()) {
            transition.userTextures[slot] = nullptr;
            continue;
        }
        auto texIt = m_shaderManager.m_textureCache.find(path);
        bool freshlyLoaded = false;
        if (texIt != m_shaderManager.m_textureCache.end()) {
            // Bump the access tick on lookup so the LRU sweep sees this
            // path as "fresh" — keeps frequently-used textures warm
            // even if a flood of unique single-use textures pushes the
            // cache over its bound.
            texIt->second.lastAccessTick = ++m_shaderManager.m_textureCacheAccessTick;
        }
        if (texIt == m_shaderManager.m_textureCache.end()) {
            // Synchronous fallback — the warm path didn't promote in
            // time (or this is the very first transition for this
            // path). Subsequent transitions for the same path will
            // hit the cache and pay zero load cost.
            //
            // This load runs on the compositor thread (PNG decode +
            // GLTexture::upload, or QSvgRenderer rasterise + upload).
            // Surface the cost in the journal so cold-install stutter
            // is attributable rather than mysterious. Don't skip the
            // load — the first transition still needs a rendered
            // texture or the slot would sample transparent black.
            qCInfo(lcEffect) << "synchronous texture load on compositor thread:" << path
                             << "(cache miss; first transition for this effect)";
            const QImage img = loadUserTextureImage(path);
            if (img.isNull()) {
                qCWarning(lcEffect) << "User texture failed to load:" << path << "for effect" << effectId << "slot"
                                    << slot;
                transition.userTextures[slot] = nullptr;
                continue;
            }
            std::unique_ptr<KWin::GLTexture> gpuTex = KWin::GLTexture::upload(img);
            if (!gpuTex) {
                qCWarning(lcEffect) << "GLTexture::upload failed:" << path << "for effect" << effectId;
                transition.userTextures[slot] = nullptr;
                continue;
            }
            gpuTex->setFilter(GL_LINEAR);
            // Force the GL state and the tracker into the same starting
            // condition. A freshly-uploaded GLTexture has GL_REPEAT on
            // GL_TEXTURE_WRAP_S/T per GL spec; without an explicit
            // setWrapMode here the tracker would default to
            // GL_CLAMP_TO_EDGE and the first bind requesting clamp
            // would skip the setWrapMode call (tracker comparison
            // matches), leaving the texture at REPEAT — silently wrong
            // sampling on the first frame.
            gpuTex->setWrapMode(GL_CLAMP_TO_EDGE);
            CachedTexture cachedTex;
            cachedTex.texture = std::move(gpuTex);
            cachedTex.lastAppliedWrap = GL_CLAMP_TO_EDGE;
            cachedTex.lastAccessTick = ++m_shaderManager.m_textureCacheAccessTick;
            texIt = m_shaderManager.m_textureCache.emplace(path, std::move(cachedTex)).first;
            freshlyLoaded = true;
        }
        // Publish the slot BEFORE any eviction can run. The transition under
        // construction is not in shaderTransitions() yet, so the sweep only knows
        // about the slots we hand it — and the entry whose insertion pushed the cache
        // over the bound would otherwise be the one entry nothing protects, because it
        // is not assigned until after the sweep. It survived on arithmetic (a fresh
        // entry carries the newest access tick, so the LRU scan reaches it last), which
        // holds only while the soft bound stays far above the slot count. That is a
        // property of two constants, neither of which knows it is load-bearing. Assign
        // first, then sweep, and the bound stops mattering.
        transition.userTextures[slot] = &texIt->second;
        if (freshlyLoaded) {
            // std::map only invalidates the erased iterator, so `texIt` survives the
            // sweep — and `transition` now covers every slot it holds, this one included.
            evictLruTextureIfOverBound(&transition);
        }
        // One-shot diagnostic per (effectId, slot, path) tuple — fires
        // on first upload only, so a leg that re-uses an already-cached
        // texture stays silent. Lets a journal scan answer "did matrix's
        // glyph atlas actually load on the kwin path?" without per-paint
        // spam.
        if (freshlyLoaded) {
            const QSize sz = texIt->second.texture->size();
            qCInfo(lcEffect) << "User texture loaded:" << path << "size=" << sz << "for effect" << effectId << "slot"
                             << slot << "(uTexture" << glslSlot << ")";
        }
    }
    // Bump generation for every install so the timer-driven teardown in
    // tryBeginShaderForEvent can detect supersession (a fresh transition
    // installed before the prior timer fires) and bail without killing the
    // successor. Counter is monotonic per-process; 64-bit so practically
    // unbounded. Two non-install writers share the counter: the drag-start
    // (re-)hold (windowStartUserMovedResized), which fences a prior drag's
    // release timers on a re-grab, and the mesh-drag release handler
    // (windowFinishUserMovedResized), which bumps when it hands the
    // lifetime to the settle gate.
    transition.generation = ++m_shaderManager.m_shaderTransitionGenerationCounter;
    transition.reverse = reverse;
    // Stamp the close-grab flag so endShaderTransition knows to release
    // refWindow + WindowClosedGrabRole on teardown. The new transition
    // inherits the prior transition's grab if supersession was a close-
    // on-close case (so the ref isn't double-incremented or lost). If
    // EITHER the prior or new install wants the grab, we treat it as
    // held — the new transition's endShaderTransition will balance.
    transition.closeGrabHeld = holdCloseGrab || existingHeldGrab;
    transition.addedGrabHeld = holdAddedGrab || existingAddedHeldGrab;
    // Keep a minimized window paintable for the transition's lifetime.
    // The ref was acquired into `minimizedPaintLifeline` BEFORE the
    // supersession erase (see the comment there), so a superseding
    // install on a still-minimized window genuinely holds its own ref
    // before the prior one drops. RAII — released when the transition
    // entry is erased (endShaderTransition / windowDeleted / supersession
    // all destroy the ShaderTransition, whose member dtor unrefs; the
    // copy here refs and the local's scope-end unrefs, balanced by
    // KWin's per-reason accounting).
    transition.visibleRef = minimizedPaintLifeline;
    // Icon target for minimize-to-icon packs. Captured unconditionally —
    // the rect is a stored value on the EffectWindow, and only shaders
    // that declare iIconRect ever read the pushed uniform. A window in no
    // task manager reports an empty rect, which stays a null QRectF here
    // and reaches the shader as (0, 0, 0, 0) = "no icon target".
    {
        const auto icon = window->iconGeometry();
        if (icon.width() >= 1.0 && icon.height() >= 1.0) {
            transition.iconRect = QRectF(icon.x(), icon.y(), icon.width(), icon.height());
        }
    }
    if (durationMs > 0) {
        transition.durationMs = durationMs;
        transition.startTimeMs = shaderClockNowMs();
        // Per-event timing curve (global → "All" → node → rule). paintWindow
        // eases the linear time progress through it. Stored only on the
        // time-driven path; a durationMs == 0 animator-driven transition reads a
        // curve-shaped value from the WindowAnimator, so a curve here would
        // double-ease. Fresh CurveState for a stateful (spring) curve.
        transition.progressCurve = std::move(progressCurve);
        transition.progressCurveState = PhosphorAnimation::CurveState{};
    }

    // Claim the closing window for our shader animation. Done HERE — after
    // every early-return path (effectId empty, collapsed surface, registry
    // miss, shader compile fail, supersession dedup) has been cleared and
    // we're committed to installing the transition. Setting the grab
    // earlier would leak it on any of those skip paths, leaving the window
    // stranded in closing state with no transition to release it.
    //
    // Without WindowClosedGrabRole, KWin's normal teardown destroys the
    // closing window as soon as `slotWindowClosed` returns — OffscreenEffect's
    // `redirect` is auto-released on deletion (per the docstring at
    // /usr/include/kwin/effect/offscreeneffect.h:53), so paintWindow never
    // gets a frame to run the close shader on. Setting the grab here,
    // while the window is still in the closing-but-not-yet-deleted window
    // of validity, blocks final destruction until endShaderTransition
    // releases it. The data role's value is the Effect's `this` pointer
    // per KWin convention so other effects can detect the grab.
    // refWindow() is the actual lifeline — KWin's docs at
    // effecthandler.h:835 explicitly say "An effect which wants to
    // animate the window closing should connect to this signal and
    // reference the window by using refWindow". Without it, the
    // EffectWindow is destroyed as soon as slotWindowClosed returns
    // regardless of WindowClosedGrabRole — the grab role only tells
    // OTHER effects to skip the window, it does NOT keep the window
    // alive. paintWindow needs both the ref (for the EffectWindow* to
    // remain valid across paint cycles) and the redirect (for the
    // offscreen FBO snapshot).
    //
    // WindowClosedGrabRole is set in addition so KWin's built-in close
    // animations (fade, glide, etc.) skip this window — their
    // isFadeWindow check tests `effect.isGrabbed(w, WindowClosedGrabRole)`
    // (see /usr/share/kwin-wayland/effects/fade/contents/code/main.js)
    // and bails when grabbed. Without the grab, the built-in fade
    // would race our shader.
    //
    // Only ref/grab when the caller is asking for the grab AND the prior
    // transition didn't already hold one — otherwise we'd double-
    // increment the refcount and leak the EffectWindow on the single
    // unrefWindow in endShaderTransition.
    if (holdCloseGrab && !existingHeldGrab) {
        window->refWindow();
        window->setData(KWin::WindowClosedGrabRole, QVariant::fromValue(static_cast<void*>(this)));
    }

    // WindowAddedGrabRole tells every OTHER effect in the chain
    // ("isFadeWindow" style checks in KWin's stock fade / scale / slide /
    // glide built-ins) to ignore this window for the window-added
    // animation. Without it, KWin's stock fade-in renders the window at
    // its natural position while our shader simultaneously animates the
    // same window at the UV-shifted position; both renders end up in
    // the framebuffer and the user sees multiple visible copies. The
    // role only matters for the duration of the install — we clear it
    // in endShaderTransition. No refWindow needed (the window isn't
    // being torn down; it just opened). `existingAddedHeldGrab` is
    // computed at the transition.addedGrabHeld stamp above; we only
    // need to call setData when this install is acquiring the grab
    // fresh (the supersession path inherits via the flag).
    if (holdAddedGrab && !existingAddedHeldGrab) {
        window->setData(KWin::WindowAddedGrabRole, QVariant::fromValue(static_cast<void*>(this)));
    }

    // Emplace the transition entry FIRST, before redirect/setShader. If
    // either of those throws — or if we hit a later failure path — we
    // need a transition entry to tear down so the window doesn't end up
    // redirected with a shader installed but no bookkeeping. RAII guard
    // erases the entry if we don't successfully reach the bottom of the
    // function (either of the two op paths below threw).
    // Snapshot the grab flags BEFORE the move-into-map so the scope-guard
    // rollback path doesn't have to read them back through the inserted
    // pointer (which is null on a contract-violating duplicate-key insert,
    // see insertTransition's docstring). These values came from
    // `holdCloseGrab || existingHeldGrab` / `holdAddedGrab ||
    // existingAddedHeldGrab` computed above.
    const bool transitionHadCloseGrab = transition.closeGrabHeld;
    const bool transitionHadAddedGrab = transition.addedGrabHeld;
    auto* inserted = m_shaderManager.insertTransition(window, std::move(transition));
    if (!inserted) {
        // Contract violation: the supersession branch above did not erase
        // the prior entry, or a concurrent install raced us. Release the
        // grab refs we just acquired so the window doesn't strand in
        // closing/added state, then bail. The new shader/redirect is not
        // installed because we never reach setShader/redirect below.
        // Roll back only what THIS install freshly acquired: with an inherited
        // grab (existing*HeldGrab) the prior entry still owns the role AND the
        // ref — the only way this insert fails with one is the prior entry NOT
        // having been erased — and clearing its role here would let KWin's
        // stock fade/glide re-engage on a window it is mid-animating; its own
        // endShaderTransition releases both, and unreffing here too would
        // double-release.
        if (holdAddedGrab && !existingAddedHeldGrab && window) {
            window->setData(KWin::WindowAddedGrabRole, QVariant());
        }
        if (holdCloseGrab && !existingHeldGrab && window) {
            window->setData(KWin::WindowClosedGrabRole, QVariant());
            QPointer<PlasmaZonesEffect> selfGuard(this);
            KWin::EffectWindow* heldWindow = window;
            QMetaObject::invokeMethod(
                this,
                [selfGuard, heldWindow]() {
                    if (!selfGuard) {
                        return;
                    }
                    heldWindow->unrefWindow();
                },
                Qt::QueuedConnection);
        }
        return false;
    }
    bool emplaceCommitted = false;
    auto emplaceGuard = qScopeGuard([&]() {
        if (emplaceCommitted) {
            return;
        }
        // The same-window supersession path above erased the prior
        // transition entry directly (no endShaderTransition call →
        // no grab release), and the new transition inherited that
        // grab via `transition.closeGrabHeld = holdCloseGrab ||
        // existingHeldGrab`. If redirect()/setShader() throws after
        // the insert, simply erasing the new entry would leak the
        // inherited (or freshly-acquired) close grab and strand the
        // window in closing state with no release path. Mirror
        // endShaderTransition's grab-release sequence here so the
        // ref + role clear stay balanced on the rollback path.
        //
        // Use the pre-move snapshot rather than reading through
        // `inserted->` — `transitionHadCloseGrab` / `transitionHadAddedGrab`
        // captured the values that landed in the map.
        const bool releaseCloseGrab = transitionHadCloseGrab;
        const bool releaseAddedGrab = transitionHadAddedGrab;
        if (releaseAddedGrab && window) {
            // Symmetric WindowAddedGrabRole rollback. No ref to release.
            window->setData(KWin::WindowAddedGrabRole, QVariant());
        }
        if (releaseCloseGrab && window) {
            // Clear WindowClosedGrabRole synchronously while the
            // ref we hold guarantees `window` is still alive. The
            // role clear is a courtesy for other effects.
            window->setData(KWin::WindowClosedGrabRole, QVariant());
            // Defer unrefWindow to the next event-loop iteration —
            // matches endShaderTransition's deferred-unref reasoning.
            // beginShaderTransition is reachable from paintWindow
            // via tryBeginShaderForEvent → animator callbacks, and
            // a synchronous unref here could destroy the
            // EffectWindow while a paint cycle still holds it.
            QPointer<PlasmaZonesEffect> selfGuard(this);
            KWin::EffectWindow* heldWindow = window;
            QMetaObject::invokeMethod(
                this,
                [selfGuard, heldWindow]() {
                    if (!selfGuard) {
                        return;
                    }
                    heldWindow->unrefWindow();
                },
                Qt::QueuedConnection);
        }
        m_shaderManager.eraseTransition(window);
    });

    if (!isSameWindowSupersession) {
        redirect(window);
    }
    // setShader replaces any prior shader pointer (idempotent for the
    // same shader, so same-effect supersession is correct here). Vertex-
    // deform transitions go through KWin's default texture shader so
    // `OffscreenData::paint` samples uTexture0 at the natural quad
    // texcoords; the translate happens at the vertices in `apply()`.
    setShader(window, cachedEntry.shader.get());
    emplaceCommitted = true;

    // Kick the compositor into painting now so paintWindow fires and
    // the transition's iTime starts advancing. Without this, a shader
    // installed on a stable window (e.g. window.focus on a window with
    // no in-flight damage) would sit in m_shaderManager.m_shaderTransitions for its
    // full duration without ever reaching paintWindow. Interactive
    // events (window.move) don't need this because the drag is its own
    // continuous repaint source. postPaintScreen drives subsequent
    // frames via per-window expanded-geometry layer repaints.
    //
    // Fall back to frameGeometry when expanded is empty — a window
    // with no shadow / decoration extents reports an empty expanded
    // rect, and `addLayerRepaint` on an empty rect is a silent no-op
    // that would deny the transition its first paint.
    QRect repaintRect = window->expandedGeometry().toAlignedRect();
    if (repaintRect.isEmpty()) {
        repaintRect = window->frameGeometry().toAlignedRect();
    }
    // A surface-extent transition paints across the whole output. The
    // off-frame band the shader sweeps is covered by the unconditional
    // `effects->addRepaintFull()` at the end of this function —
    // `addLayerRepaint` itself clips its argument back to the window-
    // item's bounding rect via the scene's `mapFromScene` (see
    // paint_pipeline.cpp's commentary), so widening `repaintRect` to
    // `output->geometry()` here only enlarges the layer repaint within
    // the scene-clipped bounds the window already covers — it does NOT
    // by itself reach the off-frame band. The widening still matters
    // for the bounded layer-repaint correctness inside that frame.
    //
    // If `screen()` returns null (transient/popup at install time, monitor
    // unplug mid-attach), the surface-extent contract cannot be honoured
    // with a frame-sized fallback: paint_pipeline.cpp's apply() and the
    // anchor-uniform feed already rely on the same `output` and would also
    // degrade. Fall back to a full repaint so the shader's first paint is
    // not silently clipped to the frame, and log so the missing screen is
    // visible in support traces.
    if (eff.fboExtentKind == PhosphorAnimationShaders::AnimationShaderEffect::FboExtentKind::Surface) {
        if (const auto* output = window->screen()) {
            repaintRect = output->geometry();
        } else {
            // No screen — surface-extent contract cannot be honoured with a
            // frame-sized fallback (paint_pipeline.cpp's apply() and the
            // anchor-uniform feed both depend on the same output). The
            // unconditional `effects->addRepaintFull()` immediately below
            // covers this case — log and fall through.
            qCWarning(lcEffect) << "Surface-extent transition" << effectId
                                << "installed on window with no screen() — relying on the unconditional"
                                << "addRepaintFull below to mark first-frame damage";
        }
    }
    window->addLayerRepaint(repaintRect);
    if (KWin::effects) {
        // Match the null-guard the constructor and destructor use for
        // KWin::effects access — this method is callable from public
        // entry points (animator-completion callback, programmatic
        // shader installs from the future plugin API), and a future
        // caller during compositor teardown could land here with
        // KWin::effects null.
        KWin::effects->addRepaintFull();
    }
    return true;
}

void PlasmaZonesEffect::endShaderTransition(KWin::EffectWindow* window)
{
    if (!window)
        return;
    // Hands the redirect back (KWin destroys the window's offscreen texture and
    // framebuffer) and destroys the transition's own snapshot texture. This is the primary
    // teardown for every time-driven animation and it fires from a QTimer between frames,
    // dozens of times a minute, with no current context.
    ensureGlContextCurrent();
    // Drop the expiry-pending guard regardless of whether the
    // transition still exists. If a synchronous teardown beat the
    // queued slot to the punch, the queued slot must not see this
    // window flagged as still-pending or it would skip a future
    // expiry's re-queue.
    m_shaderManager.m_pendingShaderExpiryEnd.remove(window);
    auto* st = m_shaderManager.findTransition(window);
    if (!st) {
        return;
    }
    const bool releaseCloseGrab = st->closeGrabHeld;
    const bool releaseAddedGrab = st->addedGrabHeld;
    // Surface-extent transitions paint across the whole output, far past
    // the window's own geometry. On teardown KWin only damages the
    // window's frame as it unredirects, so the off-frame pixels the
    // shader touched (a fly-in's slide path, a bounce's overshoot) keep
    // the last shader frame until something else repaints them — the
    // "glitch that only clears when you move the window" symptom. Capture
    // the output rect now, while `it` and `window` are valid, and force
    // one output-level repaint once teardown is complete.
    // Guard against teardown on a window that's already been destroyed
    // (windowDeleted may have raced our timer). setShader / unredirect on a
    // deleted EffectWindow is undefined behaviour in KWin's offscreen-effect
    // pipeline; just drop our bookkeeping. The windowDeleted handler at the
    // KWin::effects connection erases m_shaderManager.m_shaderTransitions for the same
    // window, so this is a defence-in-depth against ordering races.
    //
    // The surface-extent post-teardown repaint capture lives INSIDE this
    // guard for the same reason: `window->screen()` on a deleted
    // EffectWindow is the same UB class as `setShader`/`unredirect`. A
    // deleted window can't sweep off-frame pixels anyway — the windowDeleted
    // handler has already erased our bookkeeping and the redirected FBO is
    // gone — so skipping the repaint is correct.
    QRect surfaceExtentRepaint;
    if (!window->isDeleted()) {
        if (st->surfaceExtent) {
            if (const auto* output = window->screen()) {
                surfaceExtentRepaint = output->geometry();
            }
        }
        // Border-vs-animation slot handover: if the window still has a border,
        // the border shader — not "no shader" — is the correct resting state.
        // The animation transition borrowed the OffscreenEffect setShader slot
        // (its begin overrode whatever the border path had set); on teardown,
        // hand the slot BACK to the border shader and KEEP the redirect, rather
        // than unredirecting. Unredirecting here would drop the border outline
        // on every snapped window the moment its open/focus/move animation
        // ends. reconcileDecorationShader re-applies the border shader (setShader +
        // redirect, both idempotent) and stamps WindowDecoration::shaderApplied so
        // the per-frame uniform push and removeWindowDecoration resume owning the
        // slot. We must erase the transition FIRST so reconcileDecorationShader's
        // own findTransition() check sees no live transition and takes the
        // apply branch.
        m_shaderManager.eraseTransition(window);
        st = nullptr;
        const QString wid = getWindowId(window);
        // A CLOSING window's border entry was kept alive so renderSurfaceChain
        // could composite it under the close animation (slotWindowClosed defers
        // the removal); the animation is over and the window is going away, so
        // drop it now rather than re-applying a resting border to a corpse.
        if (releaseCloseGrab) {
            removeWindowDecoration(wid);
        }
        const bool stillBordered = m_windowDecorations.contains(wid);
        if (stillBordered) {
            reconcileDecorationShader(wid, window);
        } else if (!releaseCloseGrab) {
            // First decoration opportunity for a freshly-opened window. Its
            // window.open animation borrowed the OffscreenEffect redirect/shader
            // slot, so creating the border back in slotWindowAdded would fight the
            // in-flight transition (and the open animation visibly broke). Now
            // that the transition is torn down, create it here so the border
            // appears the moment the open animation ends — no focus change needed.
            // updateWindowDecoration self-gates (app-window filter + non-empty chain)
            // and re-applies via reconcileDecorationShader (the transition is already
            // erased, so it takes the apply branch). Skipped for a CLOSING window
            // (releaseCloseGrab) — no point decorating a window on its way out.
            // If the window isn't decoratable, hand the slot back to KWin.
            updateWindowDecoration(wid, window);
            if (!m_windowDecorations.contains(wid)) {
                setShader(window, nullptr);
                unredirect(window);
            }
        } else {
            setShader(window, nullptr);
            unredirect(window);
        }
    } else {
        m_shaderManager.eraseTransition(window);
        st = nullptr;
        // Clear the redirect's bound shader BEFORE the border/multipass
        // teardown below destroys the layer textures its uniforms still
        // reference: the grab release and the Deleted's destruction are not
        // frame-synchronous, so an EXPIRY-FRAME paint of the ref-held corpse
        // would otherwise re-run the animation program with iHasSurfaceLayer
        // stale at 1 and its texture freshly deleted — an unbound sampler
        // reads opaque black across the whole quad. setShader on the ref-held
        // deleted window is the same operation close-begin already performs
        // (KWin's setShader is a keyed no-op only for un-redirected windows;
        // our redirect is live until the unref).
        setShader(window, nullptr);
        // Deleted window: drop the border entry the close path deferred.
        // The rest of removeWindowDecoration's GL side is a no-op here
        // (findWindowById resolves nothing for a deleted id); the
        // windowDeleted handler remains the backstop.
        removeWindowDecoration(getWindowId(window), window);
    }
    if (!surfaceExtentRepaint.isEmpty() && KWin::effects) {
        KWin::effects->addRepaint(KWin::Rect(surfaceExtentRepaint));
    }
    if (releaseAddedGrab && !window->isDeleted()) {
        // Clear WindowAddedGrabRole now that the open transition is
        // done. Symmetric with the install-side setData; no refcount
        // change because we didn't refWindow on the install side
        // (the window opened, it's already live without our ref).
        window->setData(KWin::WindowAddedGrabRole, QVariant());
    }
    if (releaseCloseGrab) {
        // Clear WindowClosedGrabRole while `window` is still alive
        // (the ref we hold via refWindow() guarantees refcount >= 1
        // here). The role clear is a courtesy for other effects;
        // doing it now avoids touching `window` after the deferred
        // unref below.
        window->setData(KWin::WindowClosedGrabRole, QVariant());
        // Defer unrefWindow to the next event-loop iteration. This is
        // CRITICAL because endShaderTransition is reachable from
        // paintWindow's expired-transition fall-through path,
        // and a synchronous unrefWindow there would destroy the
        // EffectWindow while paintWindow still holds it as `w`. The
        // caller would then deref a freed pointer when it falls
        // through to OffscreenEffect::drawWindow — exactly the crash
        // backtrace observed: paintWindow → drawWindow → finalDrawWindow
        // → windowItem() on a destroyed EffectWindow. By queueing the
        // unref through the event loop, KWin's destruction happens
        // AFTER the current paint cycle has finished using `w`.
        //
        // The QPointer guards the queued lambda against `this`
        // destruction across the queue boundary; the raw `window*`
        // capture is fine because the ref we hold ensures the
        // EffectWindow stays alive until the lambda runs (the lambda
        // is what releases it).
        QPointer<PlasmaZonesEffect> selfGuard(this);
        KWin::EffectWindow* heldWindow = window;
        QMetaObject::invokeMethod(
            this,
            [selfGuard, heldWindow]() {
                if (!selfGuard) {
                    return;
                }
                heldWindow->unrefWindow();
            },
            Qt::QueuedConnection);
    }
}

bool PlasmaZonesEffect::resolvedShaderAppliesToEvent(const QString& effectId, const QString& profilePath) const
{
    // See the header doc. Routed through the canonical predicate
    // (shaderEffectAppliesToEventPath) — the same one the settings pickers
    // filter with — so runtime refusal and picker filtering can never drift.
    const auto eff = m_shaderManager.m_animationShaderRegistry.effect(effectId);
    if (!eff.isValid()) {
        // Unknown id: pass through. The pack may still be scanning, and
        // beginShaderTransition's registry-miss warning stays the single
        // reporter for genuinely unknown ids.
        return true;
    }
    if (!PhosphorAnimationShaders::shaderEffectAppliesToEventPath(eff, profilePath)) {
        qCDebug(lcEffect) << "shader" << effectId << "does not apply to event" << profilePath
                          << "(appliesTo=" << eff.appliesTo << ") — skipping transition";
        return false;
    }
    return true;
}

void PlasmaZonesEffect::tryBeginShaderForEvent(KWin::EffectWindow* window, const QString& profilePath, int durationMs,
                                               bool reverse, bool holdCloseGrab, bool holdAddedGrab,
                                               bool animateMinimized)
{
    if (!window || durationMs <= 0) {
        // Defensive guard. The current call sites all pass
        // `animationDurationMs()` which the daemon-bringup loader
        // clamps to `[MinAnimationDurationMs, MaxAnimationDurationMs]`
        // = [50, 2000], so 0 cannot reach this code through normal
        // flow. The authoritative no-animations gate is
        // `m_windowAnimator->isEnabled()` checked just below — that
        // covers the user-toggled case. This guard exists to fail
        // closed if a future programmatic call site bypasses the
        // clamp; a Timing Rule intentionally cannot rescue a
        // 0/negative duration since the value is treated as "caller
        // didn't supply one" rather than the "inherit per-event
        // default" sentinel that the rule layer recognises.
        return;
    }
    // Fast-path early-out on the global animations toggle. The
    // authoritative gate also lives in `beginShaderTransition` (so
    // window.movement.* callers via `applyWindowGeometry` are gated too), but
    // dispatching there would still pay the shader-tree resolve cost
    // — this skips it entirely when the global toggle is off.
    if (!m_windowAnimator->isEnabled()) {
        return;
    }
    // Window-filtering gate. `shouldAnimateWindow` honours the user's
    // Animations.WindowFiltering exclusions (transient / min-size /
    // app / class) AND lets a Rule carrying any effect-consumed
    // (Tag::Effect) action override the filter when the rule's match
    // expression resolves for the window's full WindowQuery (AppId /
    // WindowClass / Title / WindowRole / DesktopFile / WindowType / Pid /
    // state flags). Skipping this for shader transitions only would leave
    // the motion-side cascade in `applyWindowGeometry` doing its own check;
    // both call sites gate identically so the filter is a single concept
    // across the two paths.
    if (!shouldAnimateWindow(window)) {
        return;
    }
    // Cascade: per-window animation Rule → ShaderProfileTree
    // (per-event default). The rule layer wins for matching windows;
    // an engaged-empty effectId on the rule deliberately blocks the
    // tree fallthrough (the user's "no animation for this app on this
    // event" sentinel).
    //
    // Build the full per-window query once and reuse it for every
    // resolver call below — same shape `shouldAnimateWindow` already
    // uses for the rule-override gate, so a rule that passes the gate
    // also resolves its slot. Caching across resolver calls is built
    // into the evaluator's `resolveCached(windowId, …)` path; the query
    // here is only the match input, not the cache key.
    const PhosphorRules::WindowQuery query = ruleQuery(window);
    const QString windowId = getWindowId(window);
    const auto& profileTree = m_shaderManager.profileTree();
    // Per-event motion profile (curve + duration) in ONE walk, via the shared
    // SSOT: global animator profile → category "All" → per-node motion-tree
    // override → per-window Rule. The daemon mirrors its motion
    // PhosphorProfileRegistry into `motionProfileTree` over D-Bus, so a user-set
    // `window.open` = 900 ms (or an "All" curve) wins over the global default.
    //
    // The base is the animator's global profile, so when NO node overrides the
    // result is the global (never the library 150 ms default), and BOTH the
    // duration and the curve come from the SAME base — no cross-field base
    // skew. `effectiveDuration()` feeds the combined resolver as its
    // `defaultDurationMs` (the Rule timing slot still layers on top, matching
    // the resolver's documented rule → per-event → global contract), and
    // `.curve` shapes the time-driven `iTime`: paintWindow eases the linear
    // progress through it so a node's curve (e.g. "Ease Out") applies to its
    // shader exactly as it does on the animator-driven snap path. Null curve →
    // linear iTime.
    const PhosphorAnimation::Profile eventMotion = resolveEventMotionProfile(profilePath, query, windowId);
    const int baseDurationMs = qRound(eventMotion.effectiveDuration());
    const std::shared_ptr<const PhosphorAnimation::Curve> progressCurve = eventMotion.curve;
    // Combined cascade: ONE cached evaluator walk feeds BOTH the shader-slot
    // and timing-slot reads. The pre-refactor pair of `resolveAnimationShader
    // Profile` + `resolveAnimationDuration` ran two priority-order walks per
    // event (same query, both bypassing the per-window match cache); the
    // combined shim issues a single `resolveCached(windowId, …)` and reads
    // both slots from the same `ResolvedActions`. Semantics are identical:
    // rule wins per-slot, with engaged-empty effectId still blocking the tree
    // fallthrough and durationMs <= 0 still meaning "inherit".
    //
    // Clamp the resolved duration to the upstream `durationMs` floor: if
    // the cascade collapses to <= 0 (corrupt persisted rule, missing
    // motion-tree node feeding baseDurationMs), the QTimer::singleShot
    // below would fire on the next event-loop tick and tear down the
    // just-installed transition before its first paint. The input
    // `durationMs` was already clamped by the daemon-bringup loader to
    // [MinAnimationDurationMs, MaxAnimationDurationMs], and the
    // `durationMs <= 0` guard at the top of `tryBeginShaderForEvent`
    // rejects non-positive inputs, so `durationMs` here is a safe
    // positive floor.
    const auto resolved = PlasmaZones::resolveAnimationShaderProfile(m_shaderManager.animationRuleEvaluator(),
                                                                     profileTree, windowId, query, profilePath);
    const auto& profile = resolved.profile;
    // The duration comes from the motion cascade ALONE. resolveEventMotionProfile
    // already applied the Rule timing slot and clamped the result into the
    // envelope, so there is exactly one read and one clamp of that slot; the shader
    // resolver deliberately no longer re-reads it.
    int effectiveDurationMs = baseDurationMs;
    if (effectiveDurationMs <= 0) {
        effectiveDurationMs = durationMs;
    }
    // Spring lifetime, shared with the desktop switch: a stateful curve derives its
    // own lifetime from settleTime() and ignores the duration entirely. The result
    // drives BOTH the paint active-window and the teardown timer below, so the two
    // stay in lockstep.
    effectiveDurationMs = ShaderInternal::resolveTransitionLifetimeMs(effectiveDurationMs, progressCurve.get());
    if (profile.effectiveEffectId().isEmpty()) {
        // Default-state path: a fresh user with no shader overrides
        // anywhere in the tree resolves every event to empty effectId,
        // which is correct ("no shader assigned"). Logging at WARNING
        // for that floods the journal with bogus failures every time a
        // window opens, closes, or moves. Only WARN when the tree has
        // overrides (so an empty resolve here is genuinely surprising —
        // the documented prune / D-Bus-race scenarios), otherwise
        // demote to DEBUG.
        const int ruleCount = m_shaderManager.animationRuleSet().count();
        if (profileTree.overriddenPaths().isEmpty() && ruleCount == 0) {
            qCDebug(lcEffect) << "tryBeginShader[" << profilePath
                              << "]: no shader assigned (tree empty — default state)";
        } else {
            qCWarning(lcEffect) << "tryBeginShader[" << profilePath
                                << "]: no shader assigned (cascade returned empty effectId, tree size="
                                << profileTree.overriddenPaths().size() << " rules=" << ruleCount << ")";
        }
        return;
    }
    // Runtime applicability gate — see resolvedShaderAppliesToEvent. The
    // pickers keep class-mismatched packs unselectable, but a Rule's
    // OverrideAnimationShader slot or a stale / hand-edited config bypasses
    // them. Most material on the held-drag leg (window.movement.move): a
    // crossfade pack there would install a dead transition that pins
    // full-output repaints for the whole drag. The tree itself can no longer
    // deliver one there (ShaderProfileTree::resolve takes no ancestor overlay
    // for the move leaf), so this catches the rule-layer and stale-config
    // routes — and, symmetrically, a move-physics or desktop pack forced onto
    // any other window leg.
    if (!resolvedShaderAppliesToEvent(profile.effectiveEffectId(), profilePath)) {
        return;
    }
    const bool installed = beginShaderTransition(window, profile, effectiveDurationMs, reverse, holdCloseGrab,
                                                 holdAddedGrab, animateMinimized, progressCurve);
    auto* transition = m_shaderManager.findTransition(window);
    // Mark the held-move leg by IDENTITY. The drag handlers must not infer it from
    // liveness: `window.movement.move` is opt-in with no default shader, so the
    // common case installs nothing at all and `findTransition` would hand them an
    // unrelated leg to pin and reverse. See ShaderTransition::heldMove.
    //
    // We may stamp when `installed` is false, but ONLY for the same-effect
    // short-circuit — where the live leg genuinely IS the pack this event resolved
    // (a pack whose `appliesTo` admits both "move" and another class can already be
    // running for that other event). `beginShaderTransition` returns false from many
    // other places — compile failure, the cached null-shader sentinel, a registry
    // miss, a refused pack, a collapsed surface — and in EVERY one of those the live
    // leg is something else entirely, most reachably the `window.focus` leg the click
    // that began this drag just installed. Stamping that would pin it for the drag,
    // kill its teardown timer, and play the focus animation BACKWARD on release: the
    // exact bug this flag exists to prevent, re-introduced from the write side.
    //
    // So test what the short-circuit itself tests — does the live leg's cached shader
    // point at THIS event's pack? The null-shader sentinel is excluded by the
    // `->shader` check, which matters because that sentinel is sticky: once a pack
    // fails to compile, every later drag in the session would otherwise mis-stamp.
    //
    // Only ever write TRUE. A non-move event short-circuiting into a live held-move
    // leg must leave the flag alone — supersession builds a fresh ShaderTransition
    // whose default is already false, so the false case needs no code, and writing it
    // would re-introduce the mislabelling from the other direction.
    bool ownsResolvedLeg = installed;
    if (!ownsResolvedLeg && transition) {
        const auto cacheIt = m_shaderManager.m_shaderCache.find(profile.effectiveEffectId());
        ownsResolvedLeg = cacheIt != m_shaderManager.m_shaderCache.end() && cacheIt->second.shader
            && transition->cached == &cacheIt->second;
    }
    if (transition && ownsResolvedLeg && profilePath == PhosphorAnimation::ProfilePaths::WindowMove) {
        transition->heldMove = true;
    }
    if (!installed || !transition) {
        // Either beginShaderTransition no-op'd (compile fail, invalid id,
        // collapsed surface, animations disabled) and there is nothing
        // to teardown, OR the same-effect short-circuit kept the prior
        // leg in flight — in which case the prior leg's own teardown
        // timer (or animator-completion callback) owns the teardown.
        // Scheduling a fresh timer here would carry the prior leg's
        // generation and fire on this event's (likely shorter) duration,
        // cutting the still-running animation short.
        return;
    }
    // Capture the just-installed transition's generation so the deferred
    // teardown bails if a successor has replaced us by the time the timer
    // fires. Without this, two events overlapping on the same window
    // (window.move during window.snapIn, window.focus interrupting
    // window.maximize) leave a stale timer that tears down the SUCCESSOR
    // when its own timer hasn't fired yet.
    scheduleShaderTransitionTeardown(window, transition->generation, effectiveDurationMs);
}

void PlasmaZonesEffect::scheduleShaderTransitionTeardown(KWin::EffectWindow* window, quint64 generation, int delayMs)
{
    QPointer<KWin::EffectWindow> safeWindow(window);
    QTimer::singleShot(qMax(1, delayMs), this, [this, safeWindow, generation]() {
        // Two-tier guard: QPointer catches QObject destruction,
        // endShaderTransition's isDeleted() catches KWin's deletion-animation phase
        if (!safeWindow) {
            return;
        }
        const auto* live = m_shaderManager.findTransition(safeWindow);
        if (!live || live->generation != generation) {
            // A newer transition replaced us (last-event-wins) and owns its own
            // timer — leave it alone.
            return;
        }
        // HELD transitions (the interactive drag) outlive their nominal duration by
        // design: the user is still dragging. windowFinishUserMovedResized owns
        // their teardown (a settle tail after release), so the duration timer stands
        // down. A mesh-backed drag released BEFORE this timer fires is covered by the
        // generation check above instead: the release handler clears the hold flag
        // but bumps the generation when it hands the lifetime to the settle gate.
        if (live->holdUntilRelease) {
            return;
        }
        // Re-check against the transition's OWN clock rather than trusting the delay
        // we were armed with. `startTimeMs` is REBASED every frame a window spends
        // under restore suppression (a window repositioned on open is withheld from
        // compositing until its configure lands, and its animation must not play
        // invisibly in the meantime — see paint_pipeline). The install-time arming is
        // therefore up to kRestoreSuppressDeadlineMs (250 ms) too early, and firing
        // it would tear the leg down while its timeline still had a quarter second
        // to run — the open animation is cut mid-flight and the window pops. Re-arm
        // for the remainder instead. Any future rebase gets the same treatment for
        // free, which is why this is a re-check and not a suppression special case.
        const qint64 remaining =
            static_cast<qint64>(live->durationMs) - (ShaderInternal::shaderClockNowMs() - live->startTimeMs);
        if (remaining > 0) {
            scheduleShaderTransitionTeardown(safeWindow.data(), generation,
                                             static_cast<int>(qMin<qint64>(remaining, 60000)));
            return;
        }
        endShaderTransition(safeWindow);
    });
}

void PlasmaZonesEffect::loadShaderProfileFromDbus()
{
    // The key is named INLINE, not bound to a local alias first. An alias saves nothing
    // here and costs real safety: the registry-contract test scrapes these call sites to
    // prove every key the effect fetches is registered daemon-side, and an alias forces
    // it to resolve an identifier back to a constant — which it has now got wrong three
    // separate times, each time silently checking the wrong key while its own self-check
    // balanced. Name the constant where it is used and there is nothing to resolve.
    PhosphorProtocol::ClientHelpers::loadSettingAsync(
        this, PhosphorProtocol::Service::SettingProperty::ShaderProfileTree, [this](const QVariant& v) {
            dispatchJsonSetting(PhosphorProtocol::Service::SettingProperty::ShaderProfileTree, v,
                                [this](const QJsonObject& obj) {
                                    auto& tree = m_shaderManager.profileTree();
                                    tree = PhosphorAnimationShaders::ShaderProfileTree::fromJson(obj);
                                    qCDebug(lcEffect) << "loadShaderProfileFromDbus: tree loaded with"
                                                      << tree.overriddenPaths().size()
                                                      << "overrides — paths=" << tree.overriddenPaths();
                                    // A tree edit can assign or unassign an audio-reactive
                                    // animation pack; re-evaluate the cava run gate so the
                                    // provider is warm before the first transition needs it.
                                    scheduleEffectAudioSync();
                                    // It can also assign or clear the `desktop.peek` pack;
                                    // keep KWin's own show-desktop effects unloaded exactly
                                    // while ours owns that animation.
                                    syncShowDesktopEffectSuppression();
                                },
                                /*arraySink=*/{});
        });
}

void PlasmaZonesEffect::slotRulesChanged()
{
    // Coalesce burst signals: the daemon emits one `rulesChanged` per per-rule
    // mutation, so a 50-rule batch edit would otherwise drive 50 sequential
    // `getAllRules` round-trips + JSON parses + filter walks. The timer is a
    // single-shot 50ms debounce (set up in the constructor); each call here
    // re-arms it, so only the trailing edge of the burst triggers a refresh.
    m_animationRulesRefreshDebounce.start();
}

void PlasmaZonesEffect::loadRuleAnimationsFromDbus()
{
    // Fetch the unified Rule store via getAllRules (returns a JSON
    // string of a v4 RuleSet), deserialise, filter to rules whose
    // action list contains any effect-consumed (Tag::Effect) action, and
    // hand them to the shader manager. The shader manager mirrors them into
    // m_animationRuleSet so the per-event slot lookup in shader_resolve.cpp
    // resolves the cascade against the unified rule store directly.
    const QDBusMessage msg = QDBusMessage::createMethodCall(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
        QString(PhosphorProtocol::Service::Interface::Rules), QStringLiteral("getAllRules"));
    const QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        const QDBusPendingReply<QString> reply = *w;
        if (reply.isError()) {
            // Daemon may not be up yet at startup; the rulesChanged
            // subscription below will deliver the next change. Log at debug
            // so the noise stays out of normal-startup logs.
            qCDebug(lcEffect) << "loadRuleAnimationsFromDbus: getAllRules failed:" << reply.error().message();
            return;
        }
        const QByteArray payload = reply.value().toUtf8();
        const QJsonDocument doc = QJsonDocument::fromJson(payload);
        if (!doc.isObject()) {
            qCWarning(lcEffect) << "loadRuleAnimationsFromDbus: getAllRules returned non-object JSON";
            return;
        }
        const auto setOpt = PhosphorRules::RuleSet::fromJson(doc.object());
        if (!setOpt) {
            qCWarning(lcEffect) << "loadRuleAnimationsFromDbus: RuleSet::fromJson refused payload";
            return;
        }
        // Sample the prior rule set for SetOpacity BEFORE setRuleAnimationRules
        // overwrites it. Repaint is needed on BOTH bookends — rule appears
        // (currently-natural-opacity windows need to apply it) AND rule
        // disappears (currently-dimmed windows need to revert). The earlier
        // single-bookend form left previously-dimmed windows stuck at their
        // last-painted opacity when the user removed the last SetOpacity rule.
        bool hadSetOpacity = false;
        const auto& priorRules = m_shaderManager.animationRuleSet().rules();
        for (const PhosphorRules::Rule& rule : priorRules) {
            for (const PhosphorRules::RuleAction& action : rule.actions) {
                if (action.type == PhosphorRules::ActionType::SetOpacity) {
                    hadSetOpacity = true;
                    break;
                }
            }
            if (hadSetOpacity) {
                break;
            }
        }

        QList<PhosphorRules::Rule> animationRules;
        for (const PhosphorRules::Rule& rule : setOpt->rules()) {
            if (!rule.enabled) {
                // Skip disabled rules — they exist in the store but must not
                // contribute to the evaluator. (RuleEvaluator already gates
                // on enabled for its own walks, but pruning here keeps the
                // rule-set size minimal and the priority-order index smaller.)
                continue;
            }
            // Admit the rule to the evaluator if ANY action is effect-consumed,
            // i.e. carries Tag::Effect (hasTag below). The authoritative
            // membership list is the descriptor tag assignments in
            // ruleaction.cpp — animation overrides, SetOpacity, the appearance
            // family (SetBorder*, SetHideTitleBar, OverrideDecorationChain),
            // and SetWindowLayer.
            bool admitted = false;
            for (const PhosphorRules::RuleAction& action : rule.actions) {
                if (PhosphorRules::ActionRegistry::instance().hasTag(action.type, PhosphorRules::Tag::Effect)) {
                    admitted = true;
                    break;
                }
            }
            if (admitted) {
                animationRules.append(rule);
            }
        }
        m_shaderManager.setRuleAnimationRules(std::move(animationRules));
        // A rule edit can route transitions to (or away from) an audio-reactive
        // animation pack via an EffectId payload — re-evaluate the cava run gate.
        scheduleEffectAudioSync();
        // The new-state SetOpacity predicate is computed by rebuildAnimationRuleSet
        // (see ShaderTransitionManager::hasOpacityRules) — read it back rather than
        // re-scanning the rule list a second time here.
        const bool hasSetOpacity = m_shaderManager.hasOpacityRules();
        qCDebug(lcEffect) << "loadRuleAnimationsFromDbus: forwarded" << m_shaderManager.animationRuleSet().count()
                          << "total animation rules to the evaluator";

        // Per-window border / title-bar rules ride the same animation rule set
        // (Tag::Effect admits them). Refresh borders so an edited /
        // added / removed SetBorder* / SetHideTitleBar rule applies immediately
        // — updateAllDecorations re-merges every window and reconciles rule-hidden
        // title bars against the fresh evaluator.
        updateAllDecorations();

        // Update the drag-gate exclusion rule set from the same unified
        // payload — `loadRuleAnimationsFromDbus` is the effect's one
        // and only rule-store sync point, so the snapping-exclusion gate
        // refreshes here too rather than chasing a second D-Bus fetch. The
        // filter keeps only enabled rules with a terminal Exclude action;
        // setRules bumps the bound rule set's revision so
        // m_snappingExclusionEvaluator's per-revision sort index rebuilds
        // on its next walk (these evaluators call uncached `resolve()`, so
        // there is no per-window match cache to drop — the sort index is
        // the only revision-keyed artifact).
        m_snappingExclusionRuleSet.setRules(PhosphorRules::ExclusionRules::excludeRulesFrom(*setOpt).rules());

        // Same refresh for the animation-side exclusion rule set, sliced
        // for `ExcludeAnimations`-action rules. The two slices stay
        // independent so a user can have a window excluded from animations
        // but NOT from snap (or vice versa).
        m_animationExclusionRuleSet.setRules(
            PhosphorRules::ExclusionRules::excludeAnimationsRulesFrom(*setOpt).rules());
        // Force a full repaint on EITHER bookend so a user-authored rule
        // applies to static (un-damaged) windows immediately AND so a
        // removed rule reverts previously-dimmed windows immediately, not
        // on the next incidental damage event. OverrideAnimation* rules
        // fire on lifecycle events so they don't need this kick;
        // SetOpacity continuously alters paint output regardless of
        // animation state and needs the kick on both transitions.
        if ((hasSetOpacity || hadSetOpacity) && KWin::effects) {
            KWin::effects->addRepaintFull();
        }
    });
}

void PlasmaZonesEffect::loadMotionProfileTreeFromDbus()
{
    // The key is named INLINE, not bound to a local alias first. An alias saves nothing
    // here and costs real safety: the registry-contract test scrapes these call sites to
    // prove every key the effect fetches is registered daemon-side, and an alias forces
    // it to resolve an identifier back to a constant — which it has now got wrong three
    // separate times, each time silently checking the wrong key while its own self-check
    // balanced. Name the constant where it is used and there is nothing to resolve.
    PhosphorProtocol::ClientHelpers::loadSettingAsync(
        this, PhosphorProtocol::Service::SettingProperty::MotionProfileTree, [this](const QVariant& v) {
            dispatchJsonSetting(PhosphorProtocol::Service::SettingProperty::MotionProfileTree, v,
                                [this](const QJsonObject& obj) {
                                    // ProfileTree::fromJson resolves each node's optional
                                    // `curve` field through a CurveRegistry. The effect now
                                    // resolves per-event curve AND duration from this tree
                                    // (resolveEventMotionProfile reads `.curve` to shape the
                                    // time-driven iTime), so parse with the effect's own
                                    // `m_curveRegistry` — the SAME registry the Rule path
                                    // resolves against (shader_resolve.cpp) — rather than a
                                    // throwaway.
                                    //
                                    // Builtins are sufficient here and no CurveLoader is
                                    // needed in the compositor: a Profile persists its curve
                                    // BY SPEC, not by registry key (Profile::toJson writes
                                    // `curve->toString()`, e.g. "0.34,1.20,0.64,1.00" or
                                    // "spring:<omega>,<zeta>"; the friendly name rides in the
                                    // separate `presetName` field). A user curve pack
                                    // (data/curves/*.json) is a named preset over a BUILTIN
                                    // typeId, so it round-trips through its spec and the
                                    // ctor-registered builtin factories resolve it. Sharing
                                    // one registry keeps both curve paths on identical
                                    // resolution rules rather than two that can drift.
                                    auto& tree = m_shaderManager.motionProfileTree();
                                    tree = PhosphorAnimation::ProfileTree::fromJson(obj, m_curveRegistry);
                                    qCDebug(lcEffect) << "loadMotionProfileTreeFromDbus: tree loaded with"
                                                      << tree.overriddenPaths().size()
                                                      << "per-event overrides — paths=" << tree.overriddenPaths();
                                },
                                /*arraySink=*/{});
        });
}

PhosphorAnimation::Profile PlasmaZonesEffect::resolveEventMotionProfile(const QString& profilePath,
                                                                        const PhosphorRules::WindowQuery& query,
                                                                        const QString& windowId) const
{
    // Cascade base: the WindowAnimator's global profile carries the authoritative
    // global curve + duration (from animationEasingCurve / animationDuration).
    //
    // Before the async settings load lands, the animator's `duration` is still
    // nullopt, so effectiveDuration() falls back to Profile::DefaultDuration
    // while `animationDurationMs()` reports Limits::DefaultAnimationDurationMs.
    // Callers rely on those two agreeing (it is what makes the pre-load window
    // resolve to the same duration either way), so pin the coupling here rather
    // than let a future bump to one silently skew it.
    static_assert(
        qRound(PhosphorAnimation::Profile::DefaultDuration) == PhosphorAnimation::Limits::DefaultAnimationDurationMs,
        "Profile::DefaultDuration and Limits::DefaultAnimationDurationMs must agree, or the pre-settings-load "
        "motion cascade skews against animationDurationMs()");
    const PhosphorAnimation::Profile& base = m_windowAnimator->profile();
    // Category "All" → per-node: overlay only the motion-tree override chain for
    // this path onto the global base (overlayChainOnto skips the tree's own
    // baseline and returns base untouched when nothing in the chain overrides,
    // so an empty tree keeps the animator's global). Gated on a non-empty
    // override set to keep the default-state fast-path (no chain walk).
    const auto& motionTree = m_shaderManager.motionProfileTree();
    PhosphorAnimation::Profile resolved =
        motionTree.hasAnyOverride() ? motionTree.overlayChainOnto(profilePath, base) : base;
    // Rule override (top of the cascade): a per-window Timing / Curve rule for
    // this (window, event) replaces the resolved curve / duration. Skipped for
    // windowless events (desktop switch) and when no rules are configured.
    if (query.hasWindow() && !m_shaderManager.animationRuleSet().isEmpty()) {
        resolved = PlasmaZones::resolveAnimationMotionProfile(m_shaderManager.animationRuleEvaluator(), resolved, query,
                                                              profilePath, windowId, m_curveRegistry);
    }
    // Clamp the resolved duration into the animation envelope HERE, at the one
    // place every consumer shares. The motion tree hands a node's duration
    // through unclamped (ProfileTree does no bounding, and Profile::fromJson
    // accepts any finite positive value up to one hour), and the tree is rebuilt
    // from hand-editable profile JSON.
    //
    // The two shader consumers re-clamp the DURATION downstream via
    // resolveTransitionLifetimeMs. The animator calls that helper too, but only
    // for the spring maxLifetimeMs cap — its PARAMETRIC duration goes from
    // applyWindowGeometry straight into WindowAnimator::startAnimation, whose
    // own clampProfile bounds to [0, 10000] ms, a different, looser envelope.
    // Without this a `"duration": 5000` node would run a 2 s shader leg on
    // window.open but a 5 s animator leg on a snap, with its durationMs == 0
    // shader riding along and pinning per-frame repaints for the full 5 s.
    // Clamping at the source keeps all three consumers on one envelope; the
    // downstream clamps then become idempotent.
    if (resolved.duration) {
        resolved.duration =
            static_cast<qreal>(qBound(PhosphorAnimation::Limits::MinAnimationDurationMs, qRound(*resolved.duration),
                                      PhosphorAnimation::Limits::MaxAnimationDurationMs));
    }
    return resolved;
}

void PlasmaZonesEffect::slotMotionProfileTreeChanged()
{
    // A per-event animation duration was edited (daemon rescanned a
    // `profiles/*.json` override). Re-fetch so per-event durations apply
    // live, without a logout/login. loadCachedSettings() also re-fetches
    // it on settingsChanged; this dedicated path covers the profile-file
    // edits that deliberately do NOT ride settingsChanged.
    loadMotionProfileTreeFromDbus();
}

void PlasmaZonesEffect::slotSessionIdleChanged(bool idle)
{
    if (m_sessionIdle == idle) {
        return;
    }
    m_sessionIdle = idle;
    if (!m_pauseAnimationWhenIdle) {
        // Track the state anyway — the setting can be turned on mid-session, and
        // the next paint should already know whether we are idle.
        return;
    }
    if (!idle) {
        // Waking. A paused chain emits no damage of its own, so nothing would put
        // it back in the paint loop without this.
        repaintAllDecorations();
    }
    // Going idle needs no repaint: the windows simply stop being driven and keep
    // presenting the composite they already hold.
}

void PlasmaZonesEffect::loadShaderRegistryFromDbus()
{
    // The key is named INLINE, not bound to a local alias first. An alias saves nothing
    // here and costs real safety: the registry-contract test scrapes these call sites to
    // prove every key the effect fetches is registered daemon-side, and an alias forces
    // it to resolve an identifier back to a constant — which it has now got wrong three
    // separate times, each time silently checking the wrong key while its own self-check
    // balanced. Name the constant where it is used and there is nothing to resolve.
    PhosphorProtocol::ClientHelpers::loadSettingAsync(
        this, PhosphorProtocol::Service::SettingProperty::AnimationShaderSearchPaths, [this](const QVariant& v) {
            dispatchJsonSetting(PhosphorProtocol::Service::SettingProperty::AnimationShaderSearchPaths, v,
                                /*objectSink=*/{}, [this](const QJsonArray& arr) {
                                    QStringList paths;
                                    for (const auto& entry : arr) {
                                        if (entry.isString())
                                            paths.append(entry.toString());
                                    }
                                    if (!paths.isEmpty()) {
                                        m_shaderManager.m_animationShaderRegistry.addSearchPaths(paths);
                                    }
                                    qCDebug(lcEffect)
                                        << "loadShaderRegistryFromDbus: added" << paths.size()
                                        << "search paths — registry effect count="
                                        << m_shaderManager.m_animationShaderRegistry.availableEffects().size();
                                });
        });
}

} // namespace PlasmaZones
