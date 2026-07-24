// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRendering/ShaderEffect.h>
#include <PhosphorRendering/ShaderNodeRhi.h>

#include "internal.h"

#include <PhosphorShaders/CustomParamsKey.h>
#include <PhosphorShaders/IUniformExtension.h>

#include <QElapsedTimer>
#include <QMutexLocker>
#include <QPainter>
#include <QQuickWindow>
#if QT_VERSION < QT_VERSION_CHECK(6, 11, 0)
#include <QScreen>
#endif
#include <QSvgRenderer>

#include <cmath>

namespace PhosphorRendering {

// Keep the public ShaderEffect.h mirror constant in lock-step with the
// authoritative ShaderNodeRhi.h value. Compile-time check — a future drift
// would otherwise silently mis-size m_userTexture* member arrays.
static_assert(kMaxUserTextureSlots == kMaxUserTextures,
              "ShaderEffect::kMaxUserTextureSlots must equal ShaderNodeRhi::kMaxUserTextures");

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
// (-1..1) so a pass-through is sufficient. We bind the UBO at binding 0 as a
// single-field view (`mat4 qt_Matrix` at offset 0), layout-compatible with the
// full BaseUniforms block the fragment stage declares (qt_Matrix is its first
// member). qt_Matrix carries the per-backend NDC Y-orientation correction
// (identity on Y-down-NDC backends like Vulkan, a Y-flip on Y-up-NDC backends
// like OpenGL) — without applying it the fixed-NDC fullscreen quad presents
// upside down on OpenGL when rendered direct-to-window (the daemon animation
// path). ShaderNodeRhi sets this value (see shadernoderhiuniforms.cpp).
//
// Stored as `static const QString` (not QLatin1String) so the conversion
// to QString happens once at static-init. Previously a per-paint
// `QString(QLatin1String(...))` allocated a fresh QString every frame
// just to feed `setVertexShaderSource`, which short-circuits via equality.
static const QString kDefaultVertexShaderSource = QStringLiteral(R"(#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;
layout(location = 0) out vec2 vTexCoord;

layout(std140, binding = 0) uniform DefaultVertexUniforms {
    mat4 qt_Matrix;
};

void main() {
    vTexCoord = texCoord;
    gl_Position = qt_Matrix * vec4(position, 0.0, 1.0);
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

QImage ShaderEffect::loadUserTextureFile(const QString& path, int svgMaxDim)
{
    if (path.isEmpty()) {
        return {};
    }
    const bool isSvg = path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)
        || path.endsWith(QLatin1String(".svgz"), Qt::CaseInsensitive);
    if (!isSvg) {
        return QImage(path).convertToFormat(QImage::Format_RGBA8888);
    }
    QSvgRenderer renderer(path);
    if (!renderer.isValid()) {
        return {};
    }
    QSize size = renderer.defaultSize();
    // Cap the requested per-axis size at the library ceiling regardless of
    // the per-slot setting — defends against subclasses or future setters
    // that bypass the setShaderParams parse-time clamp.
    const int maxDim = qBound(64, svgMaxDim, kMaxSvgDimension);
    if (!size.isEmpty()) {
        size.scale(maxDim, maxDim, Qt::KeepAspectRatio);
    } else {
        size = QSize(maxDim, maxDim);
    }
    // Byte-budget guard: even after the per-axis ceiling, a near-square
    // doc can request ~maxDim² × 4 bytes (16 MiB at 2048²). Downscale
    // proportionally so the pixel area stays within kMaxSvgPixelBytes / 4
    // pixels and warn.
    const qint64 pixelBytes = static_cast<qint64>(size.width()) * size.height() * 4;
    if (pixelBytes > kMaxSvgPixelBytes) {
        const double scale = std::sqrt(static_cast<double>(kMaxSvgPixelBytes) / pixelBytes);
        const QSize scaledSize(qMax(1, static_cast<int>(size.width() * scale)),
                               qMax(1, static_cast<int>(size.height() * scale)));
        qCWarning(lcShaderNode) << "ShaderEffect::loadUserTextureFile: SVG rasterise size" << size
                                << "exceeds byte budget" << kMaxSvgPixelBytes << "B, downscaling to" << scaledSize
                                << "for path" << path;
        size = scaledSize;
    }
    // Rasterise into ARGB32_Premultiplied — QPainter's source-over
    // compositing is defined for premultiplied targets; rendering an SVG
    // with semi-transparent strokes/fills onto a non-premul RGBA8888
    // produces subtly wrong alpha at partial-cover paths. Convert to
    // RGBA8888 afterwards for the RHI upload path (which expects
    // QRhiTexture::RGBA8 layout).
    QImage rasterised(size, QImage::Format_ARGB32_Premultiplied);
    rasterised.fill(Qt::transparent);
    QPainter painter(&rasterised);
    renderer.render(&painter);
    painter.end();
    return rasterised.convertToFormat(QImage::Format_RGBA8888);
}

ShaderEffect::ShaderEffect(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);

    m_userTextureSvgSizes.fill(kDefaultUserTextureSvgSize);

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
        // Idempotency guard: same-window re-emit (Qt has been observed to
        // emit windowChanged with the unchanged QQuickWindow* in certain
        // QQuickItem reparenting flows). Without this early-out we
        // disconnect-then-reconnect the same `sceneGraphAboutToStop`
        // signal, which is benign but burns ~2 lookups per emit.
        if (m_connectedWindow.data() == win) {
            return;
        }
        if (m_connectedWindow) {
            disconnect(m_connectedWindow.data(), &QQuickWindow::sceneGraphAboutToStop, this, nullptr);
        }
        m_connectedWindow = win;

        // Cleared for BOTH branches, not just the detach. The old window's scene
        // graph owns the node and has already taken it for deletion by the time
        // windowChanged fires, on a reparent as much as on a detach. Keeping the
        // pointer through a window-A -> window-B move leaves it dangling AND
        // makes the destructor's `node && window()` guard pass, because the new
        // window is attached — so invalidateItem() would run on freed memory.
        // The next updatePaintNode re-registers, so nothing is lost.
        m_renderNode.store(nullptr, std::memory_order_release);

        if (win) {
            connect(
                win, &QQuickWindow::sceneGraphAboutToStop, this,
                [this]() {
                    if (ShaderNodeRhi* node = m_renderNode.load(std::memory_order_acquire)) {
                        node->releaseResources();
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
    // That lambda executes on the render thread and touches this object.
    //
    // Be exact about what this buys, because an earlier version of this comment
    // claimed the opposite of ShaderEffect.h's and both cannot be right:
    // QObject::disconnect is thread-safe but does NOT block a direct-connection
    // slot that is already running (doActivate releases the sender's lock
    // across the call). It guarantees no FUTURE invocation, nothing more. What
    // keeps teardown safe today is that sceneGraphAboutToStop is emitted only
    // while the render thread is tearing the scene graph down, which does not
    // overlap an item destruction in practice. Narrow, not closed.
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
    if (ShaderNodeRhi* node = m_renderNode.load(std::memory_order_acquire); node && window()) {
        node->invalidateItem();
    }
    m_renderNode.store(nullptr, std::memory_order_release);
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

void ShaderEffect::setPlaying(bool playing)
{
    if (m_playing == playing) {
        return;
    }
    m_playing = playing;
    if (m_playing) {
        // Reset the wall-clock baseline so the next tick produces a sensible
        // delta (not a several-second jump from the time the property was
        // last toggled). iTime is *not* reset — toggling playing off and on
        // resumes from whatever iTime value the shader was at.
        m_playingLastFrameSeconds = 0.0;
    }
    updatePlayingConnection();
    Q_EMIT playingChanged();
}

void ShaderEffect::updatePlayingConnection()
{
    // Always tear down the previous connection before deciding whether to
    // re-establish it. itemChange (window changes) and setPlaying both call
    // through here; tearing down unconditionally avoids leaking a stale
    // connection to a previous window.
    QObject::disconnect(m_playingConnection);
    m_playingConnection = {};
    if (!m_playing) {
        return;
    }
    QQuickWindow* w = window();
    if (!w) {
        // Item not parented to a window yet. itemChange(ItemSceneChange)
        // will re-call us when the item gets a window.
        return;
    }
    // afterAnimating fires once per frame on the GUI thread, immediately
    // before the render thread is asked to synchronize. That's the right
    // place to advance per-frame uniforms — the values we set here land in
    // the next sync without thread-marshalling. afterFrameEnd would fire on
    // the render thread under Qt's threaded render loop, and emitting our
    // *Changed signals from there could trigger QML JS bindings on the
    // wrong thread (V4 is GUI-thread-only).
    m_playingConnection = QObject::connect(w, &QQuickWindow::afterAnimating, this, &ShaderEffect::onPlayingTick);
    // Kick a first frame so the shader paints the new state immediately
    // (otherwise it'd wait until something else dirties the scene).
    update();
}

void ShaderEffect::onPlayingTick()
{
    if (!m_playing) {
        return;
    }
    // QElapsedTimer wall-clock seconds — monotonic, immune to NTP jumps.
    // Static across this TU because nsecsElapsed() needs a fixed start
    // anchor; the monotonic value is read into a per-instance baseline
    // (m_playingLastFrameSeconds) so each instance's delta is fully
    // independent of every other.
    static QElapsedTimer s_clock;
    if (!s_clock.isValid()) {
        s_clock.start();
    }
    const qreal now = s_clock.nsecsElapsed() * 1e-9;

    // Skip the per-frame property pump for invisible / off-screen /
    // zero-sized items, and for shaders that aren't ready (compile
    // failure, still loading, no source set). afterAnimating fires on
    // EVERY frame of the host window, so without these gates every
    // playing ShaderEffect on the window pays setITime / setITimeDelta /
    // setIFrame / 3×update cost regardless. The visual side-effect of
    // skipping is that animation appears frozen — desired behaviour.
    //
    // Crucially, update m_playingLastFrameSeconds even on the skip path
    // so the next visible tick computes a SMALL delta (the time between
    // two consecutive frames) instead of a huge one (the time since the
    // item was last visible — which would produce a giant iTime jump
    // and a visible animation skip on re-show).
    if (!isVisible() || width() <= 0 || height() <= 0 || m_status.load(std::memory_order_acquire) != Status::Ready) {
        m_playingLastFrameSeconds = now;
        return;
    }

    const qreal delta = (m_playingLastFrameSeconds > 0.0) ? (now - m_playingLastFrameSeconds) : 0.0;
    m_playingLastFrameSeconds = now;

    // Increment iTime by the frame delta rather than assigning `now`
    // directly so toggling `playing` off and on doesn't produce a giant
    // visual jump — iTime is the shader's animation clock, not wall time.
    setITime(m_iTime + delta);
    setITimeDelta(delta);
    setIFrame(m_iFrame + 1);
    // setITime/setITimeDelta/setIFrame each call update(); the scene graph
    // coalesces multiple update() requests on the same frame so this is
    // cheap.
}

void ShaderEffect::setIsReversed(bool reverse)
{
    if (m_isReversed == reverse) {
        return;
    }
    m_isReversed = reverse;
    // Exposed as a Q_PROPERTY (isReversed) for QML-binding parity with the
    // rest of the animation-state setters. SurfaceAnimator still pushes this
    // imperatively at each leg attach; the property + signal close the
    // asymmetry without changing the imperative call site.
    Q_EMIT isReversedChanged();
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
    // uTexture0). Qt's layer system uses a back-buffer so sampling an
    // ancestor reads last-frame's content — no infinite recursion. An
    // earlier ancestor-walk guard here silently broke every shader leg.
    if (item == this) {
        if (!m_warnedSelfSourceItem) {
            qCWarning(lcShaderNode) << "setSourceItem: refused — candidate is `this`; cannot sample own output.";
            m_warnedSelfSourceItem = true;
        }
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
// Shader Include Paths
// ============================================================================

void ShaderEffect::setParamPreamble(const QString& preamble)
{
    if (m_paramPreamble == preamble) {
        return;
    }
    m_paramPreamble = preamble;
    Q_EMIT paramPreambleChanged();
    // Same reload requirement as setShaderIncludePaths: the preamble is spliced
    // inside the node's loadFragmentShader and the expanded+spliced source is
    // cached, so a pure re-bake would carry the OLD defines. Force a full
    // reload (not just a dirty flag) so the node re-splices. Inlined rather
    // than calling reloadShader() to keep a statusChanged binding from looping
    // back through this setter.
    if (m_shaderSource.isValid() && !m_shaderSource.isEmpty()) {
        setStatus(Status::Loading);
    }
    m_shaderDirty = true;
    update();
}

void ShaderEffect::setEntryScaffold(const QString& prologue, const QList<PhosphorShaders::EntryCandidate>& candidates)
{
    if (m_entryPrologue == prologue && m_entryCandidates == candidates) {
        return;
    }
    m_entryPrologue = prologue;
    m_entryCandidates = candidates;
    // Same reload requirement as setParamPreamble: the scaffold is applied
    // inside the node's loadFragmentShader and the assembled+expanded source is
    // cached, so a pure re-bake would carry the OLD scaffold. Force a full
    // reload; inlined (not reloadShader()) to keep a statusChanged binding from
    // looping back through this setter.
    if (m_shaderSource.isValid() && !m_shaderSource.isEmpty()) {
        setStatus(Status::Loading);
    }
    m_shaderDirty = true;
    update();
}

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
    bool changed = false;
    {
        QMutexLocker lock(&m_errorLogMutex);
        changed = (m_errorLog != error);
        if (changed) {
            m_errorLog = error;
        }
    }
    if (changed) {
        Q_EMIT errorLogChanged();
    }
    setStatus(Status::Error);
}

void ShaderEffect::setStatus(Status newStatus)
{
    // Single atomic swap, not load-compare-store. Status is written from the
    // render thread and read from the GUI thread, so a split read and write
    // lets two concurrent transitions both pass the compare and emit
    // statusChanged() twice, or interleave into a transition nobody reports.
    const Status previous = m_status.exchange(newStatus, std::memory_order_acq_rel);
    if (previous == newStatus) {
        return;
    }
    Q_EMIT statusChanged();
}

// ============================================================================
// Shared Property Sync (used by updatePaintNode and subclass overrides)
// ============================================================================

qreal ShaderEffect::effectiveResolutionScale() const
{
    const bool needsPhysical = !m_uniformExtension || m_uniformExtension->requiresPhysicalResolution();
    if (needsPhysical && window() && window()->screen()) {
        return window()->effectiveDevicePixelRatio();
    }
    return 1.0;
}

void ShaderEffect::syncBasePropertiesToNode(ShaderNodeRhi* node)
{
    // ── Shadertoy uniforms ───────────────────────────────────────────
    // iTime is passed as double; the node splits into wrapped-lo + wrap-offset
    // before GPU float32 cast.
    node->setTime(m_iTime);
    node->setTimeDelta(static_cast<float>(m_iTimeDelta));
    node->setFrame(m_iFrame);
    node->setIsReversed(m_isReversed);
    // iResolution unit semantics — controlled by the installed uniform
    // extension's `requiresPhysicalResolution()`:
    //
    //   * physical (default, overlay path): DPR-scaled so the value
    //     matches `gl_FragCoord` — which is the viewport coordinate of
    //     the rasterised fragment. QtQuick's QRhi viewport is set in
    //     physical pixels (item.size * DPR), so `gl_FragCoord` ranges
    //     0..viewport.size in physical pixels too. Zone-overlay
    //     fragment shaders that compute SDF masks via
    //     `gl_FragCoord / iResolution` need both operands in physical
    //     pixels; setting iResolution in logical pixels at DPR > 1
    //     caused the rounded-rect mask to cover only the logical-sized
    //     region of the physical-sized surface (transparent edge
    //     stripe).
    //
    //   * logical (animation path): the AnimationUniformExtension
    //     reports `requiresPhysicalResolution() == false`. Animation
    //     shaders use the vertex-stage `vTexCoord` (auto-interpolated
    //     0..1 over the rasterised quad regardless of DPR) and pair
    //     `iResolution` with logical-pixel companions (`iAnchorSize`,
    //     `iAnchorPosInFbo`, `iSurfaceScreenPos`) for UV / clip-space
    //     ratios. Scaling iResolution by DPR there would mismatch
    //     units across the UBO and shrink rendered output by 1/DPR
    //     (fly-in) or shift the anchor UV remap (broken-glass, morph).
    //
    // `m_iResolution` itself stays in logical units (Q_PROPERTY
    // semantics — QML callers expect the same units they bound it
    // from). Only the GPU-bound value is conditionally multiplied.
    const qreal dpr = effectiveResolutionScale();
    node->setResolution(static_cast<float>(m_iResolution.width() * dpr),
                        static_cast<float>(m_iResolution.height() * dpr));
    // Scale the mouse by the SAME dpr as the resolution so iMouse.xy (pixels)
    // and iMouse.zw (normalised by the node's width/height) stay in the
    // device-pixel space of iResolution / fragCoord. Without this, on a scaled
    // display a mouse-position shader lands at 1/dpr of the cursor (up-left).
    // The Q_PROPERTY itself stays logical — QML callers bind logical units,
    // exactly as they do for iResolution; only the GPU-bound value is scaled.
    // The off-region sentinel (-1,-1) becomes (-dpr,-dpr): still negative, so
    // any `iMouse.x < 0` region check still reads it as "cursor outside".
    node->setMousePosition(QPointF(m_iMouse.x() * dpr, m_iMouse.y() * dpr));

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
            // Invalidate oldNode DIRECTLY, not the tracked pointer: if
            // m_renderNode were ever null while oldNode is live, keying off the
            // tracked pointer would delete the node without severing its item
            // back-pointer. Both subclass overrides already do it this way.
            static_cast<ShaderNodeRhi*>(oldNode)->invalidateItem();
            m_renderNode.store(nullptr, std::memory_order_release);
            delete oldNode;
        }
        return nullptr;
    }

    auto* node = static_cast<ShaderNodeRhi*>(oldNode);
    bool freshNode = false;
    if (!node) {
        // Scene graph deleted the previous node (e.g. releaseResources), or first call.
        m_renderNode.store(nullptr, std::memory_order_release);
        node = createShaderNode();
        freshNode = true;
    }
    // Register unconditionally, not only on the fresh-node path. windowChanged
    // clears m_renderNode, and a reuse-path frame after that would otherwise
    // leave it null forever, so the destructor's `node && window()` guard would
    // never sever the node's item back-pointer. QQuickItemPrivate::derefWindow()
    // makes that sequence unreachable today, but the guard must not depend on
    // it. One release store per frame is unmeasurable.
    m_renderNode.store(node, std::memory_order_release);

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
                // Push the generated named-param preamble (T1.1) before the
                // load so loadFragmentShader splices it and keys the bake cache
                // on it. Empty (the default / zone-shader path) is a no-op.
                node->setParamPreamble(m_paramPreamble);
                // Push the T1.4 entry-point scaffold so an entry-only fragment
                // is assembled before expansion. Empty (animation path) no-op.
                node->setEntryScaffold(m_entryPrologue, m_entryCandidates);
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
                    // Drop the node's shader before reporting. On the node-REUSE
                    // path a previously good bake is still resident, so
                    // isShaderReady() stays true and the status block below
                    // would set Ready again three lines later, leaving the item
                    // claiming Ready while errorLog holds a real failure.
                    node->setFragmentShaderSource(QString());
                    node->invalidateShader();
                    setError(errorMsg);
                }
            } else {
                // The URL passed isLocalShaderUrl() but carries no usable path
                // (a host-only file:// URL, or a qrc: URL with an empty path).
                // m_shaderDirty has already been consumed above, so returning
                // silently here would pin the item at Status::Loading forever
                // with an empty errorLog and no retry on any later frame.
                qCWarning(lcShaderNode) << "Shader URL resolved to an empty local path:" << m_shaderSource;
                // Same reuse-path clobber as the load-failure arm above: drop
                // the resident bake so isShaderReady() cannot revert the error.
                node->setFragmentShaderSource(QString());
                node->invalidateShader();
                setError(QStringLiteral("Shader URL resolved to an empty local path: ") + m_shaderSource.toString());
            }
        } else {
            // Source cleared — stop rendering old shader
            node->setFragmentShaderSource(QString());
            node->invalidateShader();
            setStatus(Status::Null);
        }
    }

    // ── Update status from node state ────────────────────────────────
    const Status currentStatus = m_status.load(std::memory_order_acquire);
    if (node->isShaderReady() && currentStatus != Status::Ready) {
        setStatus(Status::Ready);
    } else if (!node->shaderError().isEmpty() && currentStatus != Status::Error) {
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
    } else if (change == ItemSceneChange) {
        // Re-hook the playing-mode tick connection to whatever window the
        // item is now in (or tear it down if value.window is null). Without
        // this, a ShaderEffect created with playing=true before being
        // parented to a window would never tick — the connection in
        // setPlaying short-circuits when window() is null.
        updatePlayingConnection();
        // Same for the output-scale subscriptions: they hang off the window
        // and its screen, so they have to follow the item between windows.
        updateScaleConnections();
    }
}

void ShaderEffect::updateScaleConnections()
{
    // Tear down unconditionally before re-establishing, so moving between
    // windows cannot leave a connection to the previous one behind.
    QObject::disconnect(m_scaleConnection);
    m_scaleConnection = {};
    // Unconditional: a default-constructed Connection disconnects cleanly, and
    // on 6.11+ this is simply always that.
    QObject::disconnect(m_screenDprConnection);
    m_screenDprConnection = {};

    QQuickWindow* w = window();
    if (!w) {
        // Not parented yet. itemChange(ItemSceneChange) re-calls us once the
        // item has a window.
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 11, 0)
    // One connection covers every case, because devicePixelRatioChanged is the
    // NOTIFY for effectiveDevicePixelRatio, which is exactly what
    // effectiveResolutionScale() reads. It fires for a screen move, for a
    // screen rescaled in place, and for a per-surface scale change that never
    // touches a screen at all (Wayland's fractional-scale-v1 preferred scale),
    // which no QScreen signal reports.
    m_scaleConnection = QObject::connect(w, &QQuickWindow::devicePixelRatioChanged, this, [this]() {
        update();
    });
#else
    // Before 6.11 the accessor has no notifier, so approximate it. The window
    // moving to another screen changes effectiveDevicePixelRatio; re-subscribe
    // to the new screen's own signal at the same time.
    m_scaleConnection = QObject::connect(w, &QWindow::screenChanged, this, [this]() {
        updateScaleConnections();
        update();
    });

    // A screen can also be rescaled in place without the window ever moving.
    // logicalDotsPerInch is the value that tracks Qt's high-DPI scale factor.
    // physicalDotsPerInch is derived from geometry over physical size, and a
    // scale change alters neither, so it would simply never fire.
    if (QScreen* s = w->screen()) {
        m_screenDprConnection = QObject::connect(s, &QScreen::logicalDotsPerInchChanged, this, [this]() {
            update();
        });
    }
#endif

    // Kick a frame on the establish path too, exactly as
    // updatePlayingConnection() does. The window we just attached to may have a
    // different scale from the one last pushed to the node, and no change
    // signal fires for that: it is a change from OUR side, not the window's.
    // Leaning on QQuickItem's own add-to-window dirtying would be the same
    // "some later frame happens to repaint" assumption this function exists to
    // remove.
    update();
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
