// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRendering/ShaderEffect.h>
#include <PhosphorRendering/ShaderNodeRhi.h>

#include <PhosphorShell/IUniformExtension.h>

#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>

namespace PhosphorRendering {

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
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow* win) {
        if (m_connectedWindow) {
            disconnect(m_connectedWindow, &QQuickWindow::sceneGraphAboutToStop, this, nullptr);
            m_connectedWindow = nullptr;
        }
        if (win) {
            m_connectedWindow = win;
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

void ShaderEffect::setUniformExtension(std::shared_ptr<PhosphorShell::IUniformExtension> extension)
{
    m_uniformExtension = std::move(extension);
    update();
}

std::shared_ptr<PhosphorShell::IUniformExtension> ShaderEffect::uniformExtension() const
{
    return m_uniformExtension;
}

// ============================================================================
// Shadertoy Uniform Setters
// ============================================================================

void ShaderEffect::setITime(qreal time)
{
    constexpr qreal epsilon = 1e-9;
    if (qAbs(m_iTime - time) < epsilon) {
        return;
    }
    m_iTime = time;
    Q_EMIT iTimeChanged();
    update();
}

void ShaderEffect::setITimeDelta(qreal delta)
{
    constexpr qreal epsilon = 1e-9;
    if (qAbs(m_iTimeDelta - delta) < epsilon) {
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

void ShaderEffect::setShaderSource(const QUrl& source)
{
    if (m_shaderSource == source) {
        return;
    }
    m_shaderSource = source;
    m_shaderDirty = true;
    setStatus(Status::Loading);
    Q_EMIT shaderSourceChanged();
    update();
}

void ShaderEffect::setShaderParams(const QVariantMap& params)
{
    if (m_shaderParams == params) {
        return;
    }
    m_shaderParams = params;
    Q_EMIT shaderParamsChanged();
    update();
}

void ShaderEffect::setBufferShaderPath(const QString& path)
{
    if (m_bufferShaderPath == path) {
        return;
    }
    m_bufferShaderPath = path;
    const QStringList newPaths = path.isEmpty() ? QStringList() : QStringList{path};
    if (m_bufferShaderPaths != newPaths) {
        m_bufferShaderPaths = newPaths;
        Q_EMIT bufferShaderPathsChanged();
    }
    m_shaderDirty = true;
    Q_EMIT bufferShaderPathChanged();
    update();
}

void ShaderEffect::setBufferShaderPaths(const QStringList& paths)
{
    if (m_bufferShaderPaths == paths) {
        return;
    }
    m_bufferShaderPaths = paths;
    const QString newPath = paths.isEmpty() ? QString() : paths.constFirst();
    if (m_bufferShaderPath != newPath) {
        m_bufferShaderPath = newPath;
        Q_EMIT bufferShaderPathChanged();
    }
    m_shaderDirty = true;
    update();
    Q_EMIT bufferShaderPathsChanged();
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
    const QString use = (wrap == QLatin1String("repeat")) ? QStringLiteral("repeat") : QStringLiteral("clamp");
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
    // Normalize to "nearest", "linear", or "mipmap"
    QString use;
    if (filter == QLatin1String("nearest")) {
        use = QStringLiteral("nearest");
    } else if (filter == QLatin1String("mipmap")) {
        use = QStringLiteral("mipmap");
    } else {
        use = QStringLiteral("linear");
    }
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
    if (slot < 0 || slot >= 4) {
        return;
    }
    m_userTextureImages[slot] = image;
    update();
}

void ShaderEffect::setUserTextureWrap(int slot, const QString& wrap)
{
    if (slot < 0 || slot >= 4) {
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
    // Use logical pixels for iResolution (shader params depend on consistent
    // resolution; DPR mismatch handled by bilinear upscaling in the image pass).
    node->setResolution(static_cast<float>(width()), static_cast<float>(height()));
    node->setMousePosition(m_iMouse);

    // ── Custom parameters (indexed API) ──────────────────────────────
    for (int i = 0; i < 8; ++i)
        node->setCustomParams(i, m_customParams[i]);

    // ── Custom colors (indexed API) ──────────────────────────────────
    for (int i = 0; i < 16; ++i)
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

    // ── Multipass buffer configuration ───────────────────────────────
    QStringList effectivePaths = m_bufferShaderPaths;
    if (effectivePaths.isEmpty() && !m_bufferShaderPath.isEmpty()) {
        effectivePaths.append(m_bufferShaderPath);
    }
    while (effectivePaths.size() > 4) {
        effectivePaths.removeLast();
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
    if (!node) {
        // Scene graph deleted the previous node (e.g. releaseResources), or first call.
        m_renderNode = nullptr;
        node = new ShaderNodeRhi(this);
        m_renderNode = node;
    }

    // ── Sync base properties (time, params, colors, audio, multipass, depth, wallpaper) ──
    syncBasePropertiesToNode(node);

    // ── Sync user textures (bindings 7-10) ───────────────────────────
    for (int i = 0; i < 4; ++i) {
        node->setUserTexture(i, m_userTextureImages[i]);
        node->setUserTextureWrap(i, m_userTextureWraps[i]);
    }

    // ── Sync uniform extension ───────────────────────────────────────
    node->setUniformExtension(m_uniformExtension);

    // ── Sync shader source (must compile before rendering) ───────────
    const bool wasDirty = m_shaderDirty.exchange(false);
    const bool needLoad = wasDirty || (m_shaderSource.isValid() && !m_shaderSource.isEmpty() && !node->isShaderReady());
    if (needLoad) {
        if (m_shaderSource.isValid() && !m_shaderSource.isEmpty()) {
            QString fragPath = m_shaderSource.toLocalFile();
            if (m_shaderSource.scheme() == QLatin1String("qrc")) {
                fragPath = QLatin1Char(':') + m_shaderSource.path();
            }

            if (!fragPath.isEmpty()) {
                node->setShaderIncludePaths(m_shaderIncludePaths);
                if (node->loadFragmentShader(fragPath)) {
                    node->invalidateShader();
                    setStatus(Status::Ready);
                } else {
                    QString errorMsg = node->shaderError();
                    if (errorMsg.isEmpty()) {
                        errorMsg = QStringLiteral("Shader loading failed");
                    }
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
