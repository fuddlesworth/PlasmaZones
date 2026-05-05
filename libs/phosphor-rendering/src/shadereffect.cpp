// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRendering/ShaderEffect.h>
#include <PhosphorRendering/ShaderNodeRhi.h>

#include "internal.h"

#include <PhosphorShaders/CustomParamsKey.h>
#include <PhosphorShaders/IUniformExtension.h>

#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QPainter>
#include <QQuickWindow>
#include <QSvgRenderer>

namespace PhosphorRendering {

// ============================================================================
// Default identity vertex shader
// ============================================================================
//
// Pushed to the node whenever a fragment-only consumer (e.g. SurfaceAnimator
// transition shaders, generic ShaderEffect QML usage) sets a fragmentShader
// without supplying its own vertex stage. The pipeline requires both stages
// to compile — without this fallback the bake bails with "Vertex or fragment
// shader source is empty" and m_shaderReady stays false forever, which the
// render path surfaces as a silent "render(): bail — shaderReady: false".
//
// Geometry-aware effects (slide/popin/morph/etc.) that need to translate or
// scale the quad must override this by calling node->setVertexShaderSource()
// or node->loadVertexShader() through a subclass — ZoneShaderItem is the
// in-tree example of that pattern.
//
// The QuadVertices buffer (see internal.h) emits positions in clip space
// (-1..1) so a pass-through is sufficient. We deliberately avoid binding
// the UBO here: SRB binding 0 is registered for both stages in the
// pipeline, but glslang strips the unused declaration during SPIR-V bake,
// keeping the default stage independent of any consumer-side UBO layout.
//
// Stored as `static const QString` (not QLatin1String) so the conversion
// to QString happens once at static-init. Previously a per-paint
// `QString(QLatin1String(...))` allocated a fresh QString every frame
// just to feed `setVertexShaderSource`, which short-circuits via equality.
static const QString kDefaultVertexShaderSource = QStringLiteral(R"(#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;
layout(location = 0) out vec2 vTexCoord;

void main() {
    vTexCoord = texCoord;
    gl_Position = vec4(position, 0.0, 1.0);
}
)");

// ============================================================================
// DRY helpers
// ============================================================================

// Setter macro for vec4 custom params (index into array)
#define PR_VEC4_SETTER(Name, idx)                                                                                      \
    void ShaderEffect::set##Name(const QVector4D& params)                                                              \
    {                                                                                                                  \
        if (m_customParams[idx] == params)                                                                             \
            return;                                                                                                    \
        m_customParams[idx] = params;                                                                                  \
        Q_EMIT customParamsChanged();                                                                                  \
        update();                                                                                                      \
    }

// Setter macro for QColor custom colors (index into array)
#define PR_COLOR_SETTER(Name, idx)                                                                                     \
    void ShaderEffect::set##Name(const QColor& color)                                                                  \
    {                                                                                                                  \
        if (m_customColors[idx] == color)                                                                              \
            return;                                                                                                    \
        m_customColors[idx] = color;                                                                                   \
        Q_EMIT customColorsChanged();                                                                                  \
        update();                                                                                                      \
    }

// ============================================================================
// Construction / Destruction
// ============================================================================

ShaderEffect::ShaderEffect(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);

    // When the scene graph is invalidated (e.g. window hide on Vulkan destroys
    // the QRhi), release GPU resources from the render node and mark shader
    // dirty so it reinitializes on the next show().
    //
    // sceneGraphAboutToStop fires on the render thread BEFORE teardown, while
    // the node and QRhi are still valid. Use DirectConnection because the
    // signal is emitted on the render thread.
    //
    // m_connectedWindow is a QPointer so a window destroyed out from under us
    // (reparent-during-teardown) leaves a null pointer instead of dangling.
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow* win) {
        if (m_connectedWindow) {
            disconnect(m_connectedWindow.data(), &QQuickWindow::sceneGraphAboutToStop, this, nullptr);
        }
        m_connectedWindow = win;
        if (win) {
            connect(
                win, &QQuickWindow::sceneGraphAboutToStop, this,
                [this]() {
                    if (m_renderNode) {
                        m_renderNode->releaseResources();
                    }
                    m_shaderDirty.store(true);
                },
                Qt::DirectConnection);
        }
    });
}

ShaderEffect::~ShaderEffect()
{
    // Disconnect the DirectConnection sceneGraphAboutToStop callback FIRST.
    // That lambda executes on the render thread and dereferences m_renderNode;
    // QObject::disconnect is thread-safe and blocks until any in-flight
    // invocation completes, so after this line the render thread cannot race
    // our subsequent member teardown.
    if (m_connectedWindow) {
        disconnect(m_connectedWindow.data(), &QQuickWindow::sceneGraphAboutToStop, this, nullptr);
        m_connectedWindow.clear();
    }

    // Invalidate the render node's back-pointer before our members are torn
    // down. The scene graph render thread may still call prepare()/render() on
    // the node between now and the node's deletion; without this, the item
    // pointer inside the node would dangle.
    //
    // If the window (and its scene graph) was already destroyed, the node has
    // been deleted by the SG and m_renderNode is dangling. window() returns
    // nullptr once the window is gone, so use that as liveness check.
    if (m_renderNode && window()) {
        m_renderNode->invalidateItem();
    }
    m_renderNode = nullptr;
}

// ============================================================================
// Uniform Extension
// ============================================================================

void ShaderEffect::setUniformExtension(std::shared_ptr<PhosphorShaders::IUniformExtension> extension)
{
    m_uniformExtension = std::move(extension);
    update();
}

std::shared_ptr<PhosphorShaders::IUniformExtension> ShaderEffect::uniformExtension() const
{
    return m_uniformExtension;
}

// ============================================================================
// Shadertoy Uniform Setters
// ============================================================================

void ShaderEffect::setITime(qreal time)
{
    // Relative comparison: a fixed 1e-9 absolute epsilon falls below ULP for
    // m_iTime once it grows past ~1, so animation would silently freeze after
    // a short runtime. Compare via qFuzzyCompare with +1.0 offset to handle
    // both near-zero and large values uniformly.
    if (qFuzzyCompare(m_iTime + 1.0, time + 1.0)) {
        return;
    }
    m_iTime = time;
    Q_EMIT iTimeChanged();
    update();
}

void ShaderEffect::setITimeDelta(qreal delta)
{
    if (qFuzzyCompare(m_iTimeDelta + 1.0, delta + 1.0)) {
        return;
    }
    m_iTimeDelta = delta;
    Q_EMIT iTimeDeltaChanged();
    update();
}

void ShaderEffect::setIFrame(int frame)
{
    if (m_iFrame == frame) {
        return;
    }
    m_iFrame = frame;
    Q_EMIT iFrameChanged();
    update();
}

void ShaderEffect::setIsReversed(bool reverse)
{
    if (m_isReversed == reverse) {
        return;
    }
    m_isReversed = reverse;
    // Intentionally no `Q_PROPERTY` / `isReversedChanged` signal —
    // SurfaceAnimator pushes this value imperatively at each leg
    // attach, never bound declaratively from QML, and a missing-signal
    // bug wouldn't surface in normal use. If this ever grows a
    // QML-binding consumer, add the Q_PROPERTY + signal in the same
    // patch; don't let the asymmetry with the rest of the
    // animation-state setters fester silently.
    update();
}

void ShaderEffect::setIResolution(const QSizeF& resolution)
{
    if (m_iResolution == resolution) {
        return;
    }
    m_iResolution = resolution;
    Q_EMIT iResolutionChanged();
    update();
}

void ShaderEffect::setIMouse(const QPointF& mouse)
{
    if (m_iMouse == mouse) {
        return;
    }
    m_iMouse = mouse;
    Q_EMIT iMouseChanged();
    update();
}

// ============================================================================
// Shader Source / Buffer Setters
// ============================================================================

static bool isLocalShaderUrl(const QUrl& url)
{
    if (!url.isValid() || url.isEmpty()) {
        return true;
    }
    const QString scheme = url.scheme();
    return url.isLocalFile() || scheme.isEmpty() || scheme == QLatin1String("file") || scheme == QLatin1String("qrc");
}

static QString localPathFromShaderUrl(const QUrl& url)
{
    if (!url.isValid() || url.isEmpty()) {
        return QString();
    }
    if (url.scheme() == QLatin1String("qrc")) {
        return QLatin1Char(':') + url.path();
    }
    const QString local = url.toLocalFile();
    if (!local.isEmpty()) {
        return local;
    }
    return url.path();
}

void ShaderEffect::setShaderSource(const QUrl& source)
{
    if (m_shaderSource == source) {
        return;
    }
    if (!isLocalShaderUrl(source)) {
        qCWarning(lcShaderNode) << "setShaderSource: unsupported URL scheme" << source.scheme()
                                << "— only file:// and qrc: are accepted";
        setError(QStringLiteral("Unsupported shader URL scheme: ") + source.scheme());
        return;
    }
    m_shaderSource = source;
    m_shaderDirty = true;
    setStatus(Status::Loading);
    Q_EMIT shaderSourceChanged();
    update();
}

void ShaderEffect::setVertexShaderUrl(const QUrl& source)
{
    if (m_vertexShaderUrl == source) {
        return;
    }
    if (!isLocalShaderUrl(source)) {
        qCWarning(lcShaderNode) << "setVertexShaderUrl: unsupported URL scheme" << source.scheme()
                                << "— only file:// and qrc: are accepted";
        setError(QStringLiteral("Unsupported vertex shader URL scheme: ") + source.scheme());
        return;
    }
    m_vertexShaderUrl = source;
    m_shaderDirty = true;
    if (source.isValid() && !source.isEmpty()) {
        setStatus(Status::Loading);
    }
    Q_EMIT vertexShaderUrlChanged();
    update();
}

void ShaderEffect::setSourceItem(QQuickItem* item)
{
    if (m_sourceItem.data() == item) {
        return;
    }
    // Self-reference (sampling literally `this`) is rejected; ANCESTOR
    // sampling is supported and load-bearing (SurfaceAnimator parents
    // shaderItem under shaderAnchor for coord-system mapping, then calls
    // setSourceItem(shaderAnchor) so the anchor's layer texture binds to
    // iChannel0). Qt's layer system uses a back-buffer so sampling an
    // ancestor reads last-frame's content — no infinite recursion. An
    // earlier ancestor-walk guard here silently broke every shader leg.
    if (item == this) {
        qCWarning(lcShaderNode) << "setSourceItem: refused — candidate is `this`; cannot sample own output.";
        return;
    }
    m_sourceItem = item;
    if (item) {
        // Force `layer.enabled = true` so the QQuickItem becomes a
        // texture provider. The naive single-step
        // `item->setProperty("layer.enabled", true)` doesn't work —
        // Qt's meta-object system doesn't auto-resolve nested property
        // paths; the call sets a brand new dynamic property called
        // "layer.enabled" on the item without ever touching
        // QQuickItemLayer. Diagnostic logging confirmed this:
        // `isTextureProvider()` stayed false immediately after a
        // setProperty call that "succeeded".
        //
        // The two-step access via `item->property("layer")` resolves
        // the QQuickItemLayer sub-object (a QObject in its own right)
        // and `layer->setProperty("enabled", true)` flips the real
        // backing flag, which synchronously triggers QSGLayer creation
        // and makes `isTextureProvider()` return true. Already-true is
        // idempotent — Qt's layer property setter early-returns on
        // unchanged values.
        //
        // We don't restore the previous value on unset because we
        // can't know whether the consumer wanted layer for other
        // reasons; callers that need symmetric teardown should track
        // and reset `layer.enabled` themselves.
        if (!item->isTextureProvider()) {
            QObject* layer = item->property("layer").value<QObject*>();
            if (layer) {
                layer->setProperty("enabled", true);
            }
        }
    }
    // No m_shaderDirty here. The SRB rebind is already covered:
    // updatePaintNode() pushes the new provider via
    // setSourceTextureProvider() which calls resetAllBindingsAndPipelines()
    // when the pointer changes. Forcing a full shader recompile every
    // sourceItem swap (the previous behaviour) was wasted work — the
    // baked QShader doesn't depend on which texture is bound.
    Q_EMIT sourceItemChanged();
    update();
}

void ShaderEffect::setShaderParams(const QVariantMap& params)
{
    if (m_shaderParams == params) {
        return;
    }
    m_shaderParams = params;

    // Parse the canonical slot-keyed entries that both registries
    // (`PhosphorShaders::ShaderRegistry::translateParamsToUniforms` for
    // overlay shaders, `PhosphorAnimationShaders::AnimationShaderRegistry::
    // translateAnimationParams` for animation shaders) emit:
    //   • `customParams1_x` … `customParams8_w` → `m_customParams[0..7]`
    //   • `customColor1`     … `customColor16`  → `m_customColors[0..15]`
    //
    // Slot-key format comes from `PhosphorShaders::CustomParams::slotKey`
    // — the cross-library canonical helper alongside `BaseUniforms`.
    //
    // Until this lived in the base class, only `ZoneShaderItem` (overlay)
    // performed the parse — animation shaders driven by bare `ShaderEffect`
    // (e.g. `SurfaceAnimator::runLeg` for daemon overlay-surface
    // transitions) silently dropped every declared parameter. Now any
    // consumer that calls `setShaderParams` with translated keys lands the
    // values in the UBO via `setCustomParamAt` / `setCustomColorAt`.
    auto extractFloat = [&params](const QString& key, float defaultVal) -> float {
        const auto it = params.constFind(key);
        if (it == params.constEnd()) {
            return defaultVal;
        }
        bool ok = false;
        const float val = it->toFloat(&ok);
        return ok ? val : defaultVal;
    };

    for (int i = 0; i < kMaxCustomParams; ++i) {
        QVector4D cp = customParamAt(i);
        cp.setX(extractFloat(PhosphorShaders::CustomParams::slotKey(i, 'x'), cp.x()));
        cp.setY(extractFloat(PhosphorShaders::CustomParams::slotKey(i, 'y'), cp.y()));
        cp.setZ(extractFloat(PhosphorShaders::CustomParams::slotKey(i, 'z'), cp.z()));
        cp.setW(extractFloat(PhosphorShaders::CustomParams::slotKey(i, 'w'), cp.w()));
        setCustomParamAt(i, cp);
    }

    auto extractColor = [&params](const QString& key, const QColor& defaultVal) -> QColor {
        const auto it = params.constFind(key);
        if (it == params.constEnd()) {
            return defaultVal;
        }
        const QVariant& val = *it;
        if (val.canConvert<QColor>()) {
            return val.value<QColor>();
        }
        if (val.typeId() == QMetaType::QString) {
            QColor color(val.toString());
            if (color.isValid()) {
                return color;
            }
        }
        return defaultVal;
    };

    for (int i = 0; i < kMaxCustomColors; ++i) {
        setCustomColorAt(i, extractColor(PhosphorShaders::CustomColors::colorKey(i), customColorAt(i)));
    }

    // ── User textures (uTexture0..3, uTexture0_wrap, uTexture0_svgSize) ──
    //
    // Single shared path for both runtimes that drive a ShaderEffect:
    //   • Overlay zone backgrounds (ZoneShaderItem inherits this class)
    //   • Animation overlay-surface transitions (SurfaceAnimator runs the
    //     animation shader through a ShaderEffect on a layer-enabled
    //     anchor; pack-bundled textures from metadata.json arrive in
    //     `params` after AnimationShaderRegistry merges them with any
    //     per-leg runtime overrides)
    //
    // Format mirrors the params keys ZoneShaderItem already accepted
    // before unification:
    //   • `uTextureN`       — file path (relative paths are NOT resolved
    //                         here; the caller hands us absolute paths so
    //                         the loader stays caller-agnostic)
    //   • `uTextureN_wrap`  — "clamp" / "repeat" / "mirror" (empty defaults
    //                         to the runtime's clamp behaviour)
    //   • `uTextureN_svgSize` — SVG rasterise max-axis dimension in logical
    //                         pixels (clamped 64..4096; ignored for bitmap
    //                         formats)
    //
    // Path-change detection: we track the last-resolved path per slot and
    // skip the file load when the path is unchanged. SVG-size changes
    // force a re-rasterise of the same path so a slider can drive
    // resolution live without the consumer having to re-emit the path.
    for (int i = 0; i < kMaxUserTextures; ++i) {
        const QString sizeKey = QStringLiteral("uTexture%1_svgSize").arg(i);
        const bool svgSizeChanged = params.contains(sizeKey);
        if (svgSizeChanged) {
            m_userTextureSvgSizes[i] = qBound(64, params.value(sizeKey).toInt(), 4096);
        }

        const QString texKey = QStringLiteral("uTexture%1").arg(i);
        const bool hasTexKey = params.contains(texKey);
        const QString incomingPath = hasTexKey ? params.value(texKey).toString() : m_userTexturePaths[i];
        const bool pathChanged = hasTexKey && (m_userTexturePaths[i] != incomingPath);
        const bool needsReload = pathChanged || (svgSizeChanged && !m_userTexturePaths[i].isEmpty());

        if (hasTexKey) {
            m_userTexturePaths[i] = incomingPath;
        }

        if (needsReload) {
            const QString path = m_userTexturePaths[i];
            QImage loaded;
            if (!path.isEmpty() && QFile::exists(path)) {
                const bool isSvg = path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)
                    || path.endsWith(QLatin1String(".svgz"), Qt::CaseInsensitive);
                if (isSvg) {
                    QSvgRenderer renderer(path);
                    if (renderer.isValid()) {
                        QSize size = renderer.defaultSize();
                        const int maxDim = m_userTextureSvgSizes[i];
                        if (!size.isEmpty()) {
                            size.scale(maxDim, maxDim, Qt::KeepAspectRatio);
                        } else {
                            size = QSize(maxDim, maxDim);
                        }
                        // Rasterise into ARGB32_Premultiplied — QPainter's
                        // source-over compositing is defined for premultiplied
                        // targets; rendering an SVG with semi-transparent
                        // strokes/fills onto a non-premul RGBA8888 produces
                        // subtly wrong alpha at partial-cover paths. Convert
                        // to RGBA8888 afterwards for the RHI upload path
                        // (which expects QRhiTexture::RGBA8 layout).
                        QImage rasterised(size, QImage::Format_ARGB32_Premultiplied);
                        rasterised.fill(Qt::transparent);
                        QPainter painter(&rasterised);
                        renderer.render(&painter);
                        painter.end();
                        loaded = rasterised.convertToFormat(QImage::Format_RGBA8888);
                    }
                } else {
                    loaded = QImage(path).convertToFormat(QImage::Format_RGBA8888);
                }
            }
            // Empty path → intentional clear (sampler reads transparent black).
            // Non-empty path that produced a null image → load failure (file
            // missing, parse error, OOM). In the failure case, KEEP the prior
            // image so a transient FS hiccup or a half-written replacement
            // file doesn't drop a previously-valid texture mid-session;
            // log a warning so the author notices.
            if (path.isEmpty() || !loaded.isNull()) {
                m_userTextureImages[i] = loaded;
            } else {
                qCWarning(lcShaderNode) << "ShaderEffect: failed to load user texture slot" << i << "from" << path
                                        << "— keeping previously-loaded image";
            }
        }

        const QString wrapKey = QStringLiteral("uTexture%1_wrap").arg(i);
        if (params.contains(wrapKey)) {
            m_userTextureWraps[i] = params.value(wrapKey).toString();
        }
    }

    Q_EMIT shaderParamsChanged();
    update();
}

void ShaderEffect::setBufferShaderPath(const QString& path)
{
    if (m_bufferShaderPath == path) {
        return;
    }
    m_bufferShaderPath = path;
    // Coalesce singular/plural updates: mutate both members then emit both
    // signals once. Previously each setter emitted `...Changed` twice and
    // scheduled two scene-graph updates when a QML binding drove the other
    // property; a single update() pass is enough.
    const QStringList newPaths = path.isEmpty() ? QStringList() : QStringList{path};
    const bool pathsChanged = (m_bufferShaderPaths != newPaths);
    if (pathsChanged) {
        m_bufferShaderPaths = newPaths;
    }
    // No m_shaderDirty here: changing buffer paths reloads the BUFFER shader
    // (handled by ShaderNodeRhi's own m_bufferShaderDirty), not the main shader.
    Q_EMIT bufferShaderPathChanged();
    if (pathsChanged) {
        Q_EMIT bufferShaderPathsChanged();
    }
    update();
}

void ShaderEffect::setBufferShaderPaths(const QStringList& paths)
{
    if (m_bufferShaderPaths == paths) {
        return;
    }
    m_bufferShaderPaths = paths;
    const QString newPath = paths.isEmpty() ? QString() : paths.constFirst();
    const bool singularChanged = (m_bufferShaderPath != newPath);
    if (singularChanged) {
        m_bufferShaderPath = newPath;
    }
    // Main shader is unaffected — see setBufferShaderPath above.
    Q_EMIT bufferShaderPathsChanged();
    if (singularChanged) {
        Q_EMIT bufferShaderPathChanged();
    }
    update();
}

void ShaderEffect::setBufferFeedback(bool enable)
{
    if (m_bufferFeedback == enable) {
        return;
    }
    m_bufferFeedback = enable;
    Q_EMIT bufferFeedbackChanged();
    update();
}

void ShaderEffect::setBufferScale(qreal scale)
{
    const qreal clamped = qBound(0.125, scale, 1.0);
    if (qFuzzyCompare(m_bufferScale, clamped)) {
        return;
    }
    m_bufferScale = clamped;
    Q_EMIT bufferScaleChanged();
    update();
}

void ShaderEffect::setBufferWrap(const QString& wrap)
{
    const QString use = ShaderNodeRhi::normalizeWrapMode(wrap);
    if (m_bufferWrap == use) {
        return;
    }
    m_bufferWrap = use;
    Q_EMIT bufferWrapChanged();
    update();
}

void ShaderEffect::setBufferWraps(const QStringList& wraps)
{
    if (m_bufferWraps == wraps) {
        return;
    }
    m_bufferWraps = wraps;
    Q_EMIT bufferWrapsChanged();
    update();
}

void ShaderEffect::setBufferFilter(const QString& filter)
{
    const QString use = ShaderNodeRhi::normalizeFilterMode(filter);
    if (m_bufferFilter == use) {
        return;
    }
    m_bufferFilter = use;
    Q_EMIT bufferFilterChanged();
    update();
}

void ShaderEffect::setBufferFilters(const QStringList& filters)
{
    if (m_bufferFilters == filters) {
        return;
    }
    m_bufferFilters = filters;
    Q_EMIT bufferFiltersChanged();
    update();
}

// ============================================================================
// Custom Parameters (DRY macro)
// ============================================================================

PR_VEC4_SETTER(CustomParams1, 0)
PR_VEC4_SETTER(CustomParams2, 1)
PR_VEC4_SETTER(CustomParams3, 2)
PR_VEC4_SETTER(CustomParams4, 3)
PR_VEC4_SETTER(CustomParams5, 4)
PR_VEC4_SETTER(CustomParams6, 5)
PR_VEC4_SETTER(CustomParams7, 6)
PR_VEC4_SETTER(CustomParams8, 7)

// ============================================================================
// Custom Colors (DRY macro)
// ============================================================================

PR_COLOR_SETTER(CustomColor1, 0)
PR_COLOR_SETTER(CustomColor2, 1)
PR_COLOR_SETTER(CustomColor3, 2)
PR_COLOR_SETTER(CustomColor4, 3)
PR_COLOR_SETTER(CustomColor5, 4)
PR_COLOR_SETTER(CustomColor6, 5)
PR_COLOR_SETTER(CustomColor7, 6)
PR_COLOR_SETTER(CustomColor8, 7)
PR_COLOR_SETTER(CustomColor9, 8)
PR_COLOR_SETTER(CustomColor10, 9)
PR_COLOR_SETTER(CustomColor11, 10)
PR_COLOR_SETTER(CustomColor12, 11)
PR_COLOR_SETTER(CustomColor13, 12)
PR_COLOR_SETTER(CustomColor14, 13)
PR_COLOR_SETTER(CustomColor15, 14)
PR_COLOR_SETTER(CustomColor16, 15)

#undef PR_VEC4_SETTER
#undef PR_COLOR_SETTER

// ============================================================================
// Indexed accessors — thin wrappers over the slot arrays for callers that
// already know which slot they want (keeps QML-facing Q_PROPERTYs intact while
// removing boilerplate from consumer code that iterates slots).
// ============================================================================

QVector4D ShaderEffect::customParamAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_customParams.size())) {
        return QVector4D();
    }
    return m_customParams[index];
}

void ShaderEffect::setCustomParamAt(int index, const QVector4D& params)
{
    if (index < 0 || index >= static_cast<int>(m_customParams.size())) {
        return;
    }
    if (m_customParams[index] == params) {
        return;
    }
    m_customParams[index] = params;
    Q_EMIT customParamsChanged();
    update();
}

QColor ShaderEffect::customColorAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_customColors.size())) {
        return QColor();
    }
    return m_customColors[index];
}

void ShaderEffect::setCustomColorAt(int index, const QColor& color)
{
    if (index < 0 || index >= static_cast<int>(m_customColors.size())) {
        return;
    }
    if (m_customColors[index] == color) {
        return;
    }
    m_customColors[index] = color;
    Q_EMIT customColorsChanged();
    update();
}

// ============================================================================
// Audio Spectrum
// ============================================================================

QVariant ShaderEffect::audioSpectrumVariant() const
{
    return QVariant::fromValue(m_audioSpectrum);
}

void ShaderEffect::setAudioSpectrumVariant(const QVariant& spectrum)
{
    // Fast path: QVector<float> from C++ (no per-element conversion)
    if (spectrum.metaType() == QMetaType::fromType<QVector<float>>()) {
        setAudioSpectrum(spectrum.value<QVector<float>>());
        return;
    }
    // Slow path: QVariantList from QML (JS array)
    const QVariantList list = spectrum.toList();
    QVector<float> vec;
    vec.reserve(list.size());
    for (const QVariant& v : list) {
        bool ok = false;
        const float f = v.toFloat(&ok);
        vec.append(ok ? qBound(0.0f, f, 1.0f) : 0.0f);
    }
    if (m_audioSpectrum == vec) {
        return;
    }
    m_audioSpectrum = std::move(vec);
    Q_EMIT audioSpectrumChanged();
    update();
}

void ShaderEffect::setAudioSpectrum(const QVector<float>& spectrum)
{
    if (m_audioSpectrum == spectrum) {
        return;
    }
    // Clamp values to [0,1] to match QML path behavior
    QVector<float> clamped;
    clamped.reserve(spectrum.size());
    for (const float v : spectrum) {
        clamped.append(qBound(0.0f, v, 1.0f));
    }
    m_audioSpectrum = std::move(clamped);
    Q_EMIT audioSpectrumChanged();
    update();
}

// ============================================================================
// User Textures
// ============================================================================

void ShaderEffect::setUserTexture(int slot, const QImage& image)
{
    if (slot < 0 || slot >= kMaxUserTextures) {
        qCWarning(lcShaderNode) << "setUserTexture: slot" << slot << "out of range [0," << (kMaxUserTextures - 1)
                                << "]";
        return;
    }
    m_userTextureImages[slot] = image;
    // Clear the cached path so a subsequent setShaderParams call with
    // the previously-cached path correctly forces a reload from disk.
    // Without this, a caller that mixes the direct image setter with
    // params-driven loads would see the directly-set image silently
    // persist when the next params push happens to repeat the old path
    // (path-change detection thinks nothing changed).
    m_userTexturePaths[slot].clear();
    update();
}

void ShaderEffect::setUserTextureWrap(int slot, const QString& wrap)
{
    if (slot < 0 || slot >= kMaxUserTextures) {
        qCWarning(lcShaderNode) << "setUserTextureWrap: slot" << slot << "out of range [0," << (kMaxUserTextures - 1)
                                << "]";
        return;
    }
    m_userTextureWraps[slot] = wrap;
    update();
}

// ============================================================================
// Wallpaper Texture
// ============================================================================

QImage ShaderEffect::wallpaperTexture() const
{
    QMutexLocker lock(&m_wallpaperTextureMutex);
    return m_wallpaperTexture;
}

void ShaderEffect::setWallpaperTexture(const QImage& image)
{
    {
        QMutexLocker lock(&m_wallpaperTextureMutex);
        if (m_wallpaperTexture.cacheKey() == image.cacheKey()) {
            return;
        }
        m_wallpaperTexture = image;
    }
    Q_EMIT wallpaperTextureChanged();
    update();
}

void ShaderEffect::setUseWallpaper(bool use)
{
    if (m_useWallpaper == use) {
        return;
    }
    m_useWallpaper = use;
    Q_EMIT useWallpaperChanged();
    update();
}

void ShaderEffect::setUseDepthBuffer(bool use)
{
    if (m_useDepthBuffer == use) {
        return;
    }
    m_useDepthBuffer = use;
    Q_EMIT useDepthBufferChanged();
    update();
}

// ============================================================================
// Shader Include Paths
// ============================================================================

void ShaderEffect::setShaderIncludePaths(const QStringList& paths)
{
    if (m_shaderIncludePaths == paths) {
        return;
    }
    m_shaderIncludePaths = paths;
    // Marking dirty alone is not enough: include expansion happens inside
    // loadFragmentShader()/loadVertexShader() and the expanded source is
    // cached on the node. A pure re-bake of the cached source would still
    // carry the OLD include contents. Inline the two-line reload instead of
    // calling reloadShader() so a QML binding on statusChanged can't loop
    // back through this setter.
    if (m_shaderSource.isValid() && !m_shaderSource.isEmpty()) {
        setStatus(Status::Loading);
    }
    m_shaderDirty = true;
    update();
}

// ============================================================================
// Shader Loading
// ============================================================================

void ShaderEffect::reloadShader()
{
    if (!m_shaderSource.isValid() || m_shaderSource.isEmpty()) {
        setStatus(Status::Null);
        return;
    }
    setStatus(Status::Loading);
    m_shaderDirty = true;
    update();
}

// ============================================================================
// Status Management
// ============================================================================

ShaderNodeRhi* ShaderEffect::createShaderNode()
{
    return new ShaderNodeRhi(this);
}

void ShaderEffect::setError(const QString& error)
{
    if (m_errorLog != error) {
        m_errorLog = error;
        Q_EMIT errorLogChanged();
    }
    setStatus(Status::Error);
}

void ShaderEffect::setStatus(Status newStatus)
{
    if (m_status != newStatus) {
        m_status = newStatus;
        Q_EMIT statusChanged();
    }
}

// ============================================================================
// Shared Property Sync (used by updatePaintNode and subclass overrides)
// ============================================================================

void ShaderEffect::syncBasePropertiesToNode(ShaderNodeRhi* node)
{
    // ── Shadertoy uniforms ───────────────────────────────────────────
    // iTime is passed as double; the node splits into wrapped-lo + wrap-offset
    // before GPU float32 cast.
    node->setTime(m_iTime);
    node->setTimeDelta(static_cast<float>(m_iTimeDelta));
    node->setFrame(m_iFrame);
    node->setIsReversed(m_isReversed);
    // Use logical pixels for iResolution (shader params depend on consistent
    // resolution; DPR mismatch handled by bilinear upscaling in the image pass).
    node->setResolution(static_cast<float>(width()), static_cast<float>(height()));
    node->setMousePosition(m_iMouse);

    // ── Custom parameters (indexed API) ──────────────────────────────
    for (int i = 0; i < kMaxCustomParams; ++i)
        node->setCustomParams(i, m_customParams[i]);

    // ── Custom colors (indexed API) ──────────────────────────────────
    for (int i = 0; i < kMaxCustomColors; ++i)
        node->setCustomColor(i, m_customColors[i]);

    // ── Audio spectrum ───────────────────────────────────────────────
    node->setAudioSpectrum(m_audioSpectrum);

    // ── Depth buffer and wallpaper ───────────────────────────────────
    node->setUseDepthBuffer(m_useDepthBuffer);
    node->setUseWallpaper(m_useWallpaper);
    {
        QMutexLocker lock(&m_wallpaperTextureMutex);
        node->setWallpaperTexture(m_wallpaperTexture);
    }

    // ── User textures (uTexture0..3 / SRB bindings 7..10) ────────────
    // Pushed here rather than in updatePaintNode so subclasses that
    // override updatePaintNode and delegate to syncBasePropertiesToNode
    // inherit texture sync without owning their own user-texture state
    // (see ZoneShaderItem). The arrays are populated by setShaderParams
    // and (for slot 0 on the SurfaceAnimator path) by
    // setSourceTextureProvider rebinding the surface FBO.
    for (int i = 0; i < kMaxUserTextures; ++i) {
        node->setUserTexture(i, m_userTextureImages[i]);
        node->setUserTextureWrap(i, m_userTextureWraps[i]);
    }

    // ── Uniform extension ────────────────────────────────────────────
    // Push the extension stored on this item (if any) so subclasses that
    // override updatePaintNode() and delegate to syncBasePropertiesToNode()
    // inherit the same install-through-item contract as the base class —
    // avoids a silent no-op on ShaderEffect::setUniformExtension when a
    // subclass forgot to sync.
    node->setUniformExtension(m_uniformExtension);

    // ── Multipass buffer configuration ───────────────────────────────
    QStringList effectivePaths = m_bufferShaderPaths;
    if (effectivePaths.isEmpty() && !m_bufferShaderPath.isEmpty()) {
        effectivePaths.append(m_bufferShaderPath);
    }
    if (effectivePaths.size() > kMaxBufferPasses) {
        effectivePaths.resize(kMaxBufferPasses);
    }
    node->setBufferShaderPaths(effectivePaths);
    node->setBufferFeedback(m_bufferFeedback);
    node->setBufferScale(m_bufferScale);
    node->setBufferWrap(m_bufferWrap);
    if (!m_bufferWraps.isEmpty()) {
        node->setBufferWraps(m_bufferWraps);
    }
    node->setBufferFilter(m_bufferFilter);
    if (!m_bufferFilters.isEmpty()) {
        node->setBufferFilters(m_bufferFilters);
    }
}

// ============================================================================
// Scene Graph Integration
// ============================================================================

QSGNode* ShaderEffect::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data)
{
    Q_UNUSED(data)

    if (width() <= 0 || height() <= 0) {
        if (oldNode) {
            if (m_renderNode) {
                m_renderNode->invalidateItem();
                m_renderNode = nullptr;
            }
            delete oldNode;
        }
        return nullptr;
    }

    auto* node = static_cast<ShaderNodeRhi*>(oldNode);
    bool freshNode = false;
    if (!node) {
        // Scene graph deleted the previous node (e.g. releaseResources), or first call.
        m_renderNode = nullptr;
        node = createShaderNode();
        m_renderNode = node;
        freshNode = true;
    }

    // ── Sync base properties (time, params, colors, audio, multipass, depth, wallpaper, user textures) ──
    syncBasePropertiesToNode(node);

    // ── Sync source texture provider (slot 0 / binding 7 override) ───
    // Pushed every paint pass so a setSourceItem(...) call after the
    // node already exists picks up immediately, and so a torn-down
    // source (QPointer auto-nulls) clears the binding instead of
    // leaving a dangling provider in the node.
    if (m_sourceItem && m_sourceItem->isTextureProvider()) {
        node->setSourceTextureProvider(m_sourceItem->textureProvider());
    } else {
        node->setSourceTextureProvider(nullptr);
    }

    // Uniform extension is synced inside syncBasePropertiesToNode() above.

    // ── Sync shader source (must compile before rendering) ───────────
    // freshNode covers SG-deletion + first-call: a brand-new node has no
    // shader baked, so trigger a load regardless of m_shaderDirty.
    // Do NOT retry on `!node->isShaderReady()` alone — a permanent load
    // failure (missing file, bad GLSL) would otherwise re-attempt every
    // frame and spam the journal at 60Hz. Real device-loss is handled by
    // sceneGraphAboutToStop, which sets m_shaderDirty=true; transient
    // errors retry on the next shaderSource change.
    const bool wasDirty = m_shaderDirty.exchange(false);
    const bool needLoad = wasDirty || freshNode;
    if (needLoad) {
        if (m_shaderSource.isValid() && !m_shaderSource.isEmpty()) {
            const QString fragPath = localPathFromShaderUrl(m_shaderSource);

            if (!fragPath.isEmpty()) {
                node->setShaderIncludePaths(m_shaderIncludePaths);
                bool vertLoaded = false;
                if (m_vertexShaderUrl.isValid() && !m_vertexShaderUrl.isEmpty()) {
                    const QString vertPath = localPathFromShaderUrl(m_vertexShaderUrl);
                    if (!vertPath.isEmpty()) {
                        vertLoaded = node->loadVertexShader(vertPath);
                        if (!vertLoaded) {
                            qCWarning(lcShaderNode)
                                << "Vertex shader load failed:" << vertPath << "— falling back to built-in default";
                        }
                    }
                }
                if (!vertLoaded) {
                    node->setVertexShaderSource(kDefaultVertexShaderSource);
                }
                if (node->loadFragmentShader(fragPath)) {
                    node->invalidateShader();
                    setStatus(Status::Ready);
                } else {
                    QString errorMsg = node->shaderError();
                    if (errorMsg.isEmpty()) {
                        errorMsg = QStringLiteral("Shader loading failed");
                    }
                    qCWarning(lcShaderNode) << "Fragment shader load failed:" << fragPath << "—" << errorMsg;
                    setError(errorMsg);
                }
            }
        } else {
            // Source cleared — stop rendering old shader
            node->setFragmentShaderSource(QString());
            node->invalidateShader();
            setStatus(Status::Null);
        }
    }

    // ── Update status from node state ────────────────────────────────
    if (node->isShaderReady() && m_status != Status::Ready) {
        setStatus(Status::Ready);
    } else if (!node->shaderError().isEmpty() && m_status != Status::Error) {
        setError(node->shaderError());
    }

    node->markDirty(QSGNode::DirtyMaterial);

    return node;
}

// ============================================================================
// Geometry Handling
// ============================================================================

void ShaderEffect::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);

    if (newGeometry.size() != oldGeometry.size()) {
        const QSizeF newSize = newGeometry.size();
        if (m_iResolution != newSize) {
            m_iResolution = newSize;
            Q_EMIT iResolutionChanged();
        }
        update();
    }
}

void ShaderEffect::itemChange(ItemChange change, const ItemChangeData& value)
{
    QQuickItem::itemChange(change, value);

    if (change == ItemVisibleHasChanged && value.boolValue) {
        // Item became visible — force scene graph update. On Vulkan, window hide
        // destroys the swapchain; update() calls during the hidden period are lost.
        m_shaderDirty = true;
        update();
    }
}

void ShaderEffect::componentComplete()
{
    QQuickItem::componentComplete();

    // Initialize resolution from item size if not explicitly set
    if (m_iResolution.isEmpty() && width() > 0 && height() > 0) {
        m_iResolution = QSizeF(width(), height());
        Q_EMIT iResolutionChanged();
    }

    // Load shader if source is set
    if (m_shaderSource.isValid() && !m_shaderSource.isEmpty()) {
        reloadShader();
    }
}

} // namespace PhosphorRendering
