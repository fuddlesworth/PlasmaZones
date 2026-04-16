// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "internal.h"

#include <PhosphorRendering/ShaderCompiler.h>

#include <QFileInfo>
#include <cmath>

namespace PhosphorRendering {

// ============================================================================
// Uniform Extension
// ============================================================================

void ShaderNodeRhi::setUniformExtension(std::shared_ptr<PhosphorShell::IUniformExtension> extension)
{
    // Early-return if unchanged. ShaderEffect::updatePaintNode pushes the
    // extension every frame; without this guard, a subclass that doesn't
    // override updatePaintNode would tear down and rebuild the entire RHI
    // pipeline (UBO, SRBs, graphics pipeline) on every frame — guaranteed
    // render breakage. The reset is only needed when the extension size
    // can change, which only happens on a real swap.
    if (extension == m_uniformExtension) {
        return;
    }
    m_uniformExtension = std::move(extension);
    // Force UBO recreation with new size on next prepare()
    m_ubo.reset();
    m_initialized = false;
    m_didFullUploadOnce = false;
    resetAllBindingsAndPipelines();
}

// ============================================================================
// Timing / Resolution / Mouse Setters
// ============================================================================

void ShaderNodeRhi::setTime(double time)
{
    m_time = time;
    m_uniformsDirty = true;
    m_timeDirty = true;
    const float newTimeHi =
        static_cast<float>(std::floor(time / PhosphorShell::kShaderTimeWrap) * PhosphorShell::kShaderTimeWrap);
    if (newTimeHi != m_timeHi) {
        m_timeHi = newTimeHi;
        m_timeHiDirty = true;
    }
}

void ShaderNodeRhi::setTimeDelta(float delta)
{
    m_timeDelta = delta;
    m_uniformsDirty = true;
    m_timeDirty = true;
}

void ShaderNodeRhi::setFrame(int frame)
{
    m_frame = frame;
    m_uniformsDirty = true;
    m_timeDirty = true;
}

void ShaderNodeRhi::setResolution(float width, float height)
{
    if (m_width != width || m_height != height) {
        m_width = width;
        m_height = height;
        m_uniformsDirty = true;
        m_sceneDataDirty = true;
    }
}

void ShaderNodeRhi::setMousePosition(const QPointF& pos)
{
    m_mousePosition = pos;
    m_uniformsDirty = true;
    m_sceneDataDirty = true;
}

// ============================================================================
// Custom Params (indexed API)
// ============================================================================

void ShaderNodeRhi::setCustomParams(int index, const QVector4D& params)
{
    if (index < 0 || index >= kMaxCustomParams) {
        return;
    }
    if (m_customParams[index] == params) {
        return;
    }
    m_customParams[index] = params;
    m_uniformsDirty = true;
    m_sceneDataDirty = true;
}

// ============================================================================
// Custom Colors (indexed API)
// ============================================================================

void ShaderNodeRhi::setCustomColor(int index, const QColor& color)
{
    if (index < 0 || index >= kMaxCustomColors) {
        return;
    }
    if (m_customColors[index] == color) {
        return;
    }
    m_customColors[index] = color;
    m_uniformsDirty = true;
    m_sceneDataDirty = true;
}

// ============================================================================
// App Fields
// ============================================================================

void ShaderNodeRhi::setAppField0(int value)
{
    if (m_baseUniforms.appField0 == value) {
        return;
    }
    m_baseUniforms.appField0 = value;
    m_uniformsDirty = true;
    // Use the granular K_APP_FIELDS region (8 bytes) instead of the full
    // scene header (~512 bytes). PlasmaZones updates these on every hover.
    m_appFieldsDirty = true;
}

void ShaderNodeRhi::setAppField1(int value)
{
    if (m_baseUniforms.appField1 == value) {
        return;
    }
    m_baseUniforms.appField1 = value;
    m_uniformsDirty = true;
    m_appFieldsDirty = true;
}

// ============================================================================
// Extra Bindings (consumer-managed)
// ============================================================================

void ShaderNodeRhi::setExtraBinding(int binding, QRhiTexture* texture, QRhiSampler* sampler)
{
    // Binding 0 is the UBO, 2-5 are buffer channels, 6 is audio, 7-10 are user
    // textures, 11 is wallpaper, 12 is depth. Binding 1 is reserved-but-unmanaged
    // (consumers like PlasmaZones use it for labels). Reject conflicts with
    // managed built-in bindings.
    if (binding == 0 || (binding >= 2 && binding <= 12)) {
        qCWarning(lcShaderNode) << "setExtraBinding: binding" << binding
                                << "conflicts with a built-in binding (0, 2-12) — ignored";
        return;
    }
    m_extraBindings[binding] = ExtraBinding{texture, sampler};
    m_extraBindingsDirty = true;
    resetAllBindingsAndPipelines();
}

void ShaderNodeRhi::removeExtraBinding(int binding)
{
    auto it = m_extraBindings.find(binding);
    if (it != m_extraBindings.end()) {
        m_extraBindings.erase(it);
        m_extraBindingsDirty = true;
        resetAllBindingsAndPipelines();
    }
}

// ============================================================================
// Audio / User Texture / Wallpaper Setters
// ============================================================================

void ShaderNodeRhi::setAudioSpectrum(const QVector<float>& spectrum)
{
    if (m_audioSpectrum == spectrum) {
        return;
    }
    m_audioSpectrum = spectrum;
    m_audioSpectrumDirty = true;
    m_uniformsDirty = true;
    m_sceneDataDirty = true; // iAudioSpectrumSize lives in scene-header region
}

void ShaderNodeRhi::setUserTexture(int slot, const QImage& image)
{
    if (slot < 0 || slot >= kMaxUserTextures) {
        return;
    }
    if (m_userTextureImages[slot].cacheKey() == image.cacheKey()) {
        return;
    }
    m_userTextureImages[slot] = image;
    m_userTextureDirty[slot] = true;
    m_uniformsDirty = true;
    m_sceneDataDirty = true;
}

void ShaderNodeRhi::setUserTextureWrap(int slot, const QString& wrap)
{
    if (slot < 0 || slot >= kMaxUserTextures) {
        return;
    }
    const QString use = normalizeWrapMode(wrap);
    if (m_userTextureWraps[slot] == use) {
        return;
    }
    m_userTextureWraps[slot] = use;
    m_userTextureSamplers[slot].reset();
    resetAllBindingsAndPipelines();
}

void ShaderNodeRhi::setWallpaperTexture(const QImage& image)
{
    if (m_wallpaperImage.cacheKey() == image.cacheKey()) {
        return;
    }
    m_wallpaperImage = image;
    m_wallpaperDirty = true;
    m_uniformsDirty = true;
}

void ShaderNodeRhi::setUseWallpaper(bool use)
{
    if (m_useWallpaper == use) {
        return;
    }
    m_useWallpaper = use;
    resetAllBindingsAndPipelines();
    markDirty(QSGNode::DirtyMaterial);
}

void ShaderNodeRhi::setUseDepthBuffer(bool use)
{
    if (m_useDepthBuffer == use) {
        return;
    }
    m_useDepthBuffer = use;
    m_depthTexture.reset();
    m_depthSampler.reset();
    resetAllBindingsAndPipelines();
    markDirty(QSGNode::DirtyMaterial);
}

// ============================================================================
// Buffer Shader Path / Feedback / Scale / Wrap / Filter
// ============================================================================

void ShaderNodeRhi::setBufferShaderPath(const QString& path)
{
    setBufferShaderPaths(path.isEmpty() ? QStringList() : QStringList{path});
}

void ShaderNodeRhi::setBufferShaderPaths(const QStringList& paths)
{
    QStringList trimmed;
    for (int i = 0; i < qMin(paths.size(), kMaxBufferPasses); ++i) {
        if (!paths.at(i).isEmpty()) {
            trimmed.append(paths.at(i));
        }
    }
    if (m_bufferPaths == trimmed) {
        return;
    }
    m_bufferPaths = trimmed;
    m_bufferPath = trimmed.isEmpty() ? QString() : trimmed.constFirst();

    qCDebug(lcShaderNode) << "ShaderNodeRhi setBufferShaderPaths count=" << m_bufferPaths.size()
                          << "multiBufferMode=" << (m_bufferPaths.size() > 1);

    m_bufferShaderDirty = true;
    m_bufferShaderReady = false;
    m_bufferShaderRetries = 0;
    m_bufferFragmentShaderSource.clear();
    m_bufferMtime = 0;
    m_multiBufferShadersReady = false;
    m_multiBufferShaderDirty = true;
    m_multiBufferShaderRetries = 0;
    for (int i = 0; i < kMaxBufferPasses; ++i) {
        m_multiBufferFragmentShaderSources[i].clear();
        m_multiBufferFragmentShaders[i] = QShader();
        m_multiBufferMtimes[i] = 0;
    }
    if (m_bufferPaths.size() == 1) {
        const QString& path = m_bufferPaths.constFirst();
        if (QFileInfo::exists(path)) {
            QString err;
            m_bufferFragmentShaderSource = loadAndExpandShader(path, &err);
            if (!m_bufferFragmentShaderSource.isEmpty()) {
                m_bufferMtime = QFileInfo(path).lastModified().toMSecsSinceEpoch();
            }
        }
        m_bufferShaderDirty = true;
    }

    m_bufferPipeline.reset();
    m_bufferSrb.reset();
    m_bufferSrbB.reset();
    m_bufferTexture.reset();
    m_bufferTextureB.reset();
    m_bufferRenderTarget.reset();
    m_bufferRenderTargetB.reset();
    m_bufferRenderPassDescriptor.reset();
    m_bufferRenderPassDescriptorB.reset();
    m_bufferFeedbackCleared = false;
    for (int i = 0; i < kMaxBufferPasses; ++i) {
        m_multiBufferPipelines[i].reset();
        m_multiBufferSrbs[i].reset();
        m_multiBufferTextures[i].reset();
        m_multiBufferRenderTargets[i].reset();
        m_multiBufferRenderPassDescriptors[i].reset();
    }
    m_pipeline.reset();
    m_srb.reset();
    m_srbB.reset();
}

void ShaderNodeRhi::setBufferFeedback(bool enable)
{
    if (m_bufferFeedback == enable) {
        return;
    }
    m_bufferFeedback = enable;
    m_bufferPipeline.reset();
    m_bufferSrb.reset();
    m_bufferSrbB.reset();
    m_srb.reset();
    m_srbB.reset();
    m_bufferTextureB.reset();
    m_bufferRenderTargetB.reset();
    m_bufferRenderPassDescriptorB.reset();
    m_bufferFeedbackCleared = false;
}

void ShaderNodeRhi::setBufferScale(qreal scale)
{
    const qreal clamped = qBound(0.125, scale, 1.0);
    if (qFuzzyCompare(m_bufferScale, clamped)) {
        return;
    }
    m_bufferScale = clamped;
    m_bufferTexture.reset();
    m_bufferTextureB.reset();
    m_bufferRenderTarget.reset();
    m_bufferRenderTargetB.reset();
    m_bufferRenderPassDescriptor.reset();
    m_bufferRenderPassDescriptorB.reset();
    m_bufferPipeline.reset();
    m_bufferSrb.reset();
    m_bufferSrbB.reset();
    m_srb.reset();
    m_srbB.reset();
    m_bufferFeedbackCleared = false;
    for (int i = 0; i < kMaxBufferPasses; ++i) {
        m_multiBufferTextures[i].reset();
        m_multiBufferRenderTargets[i].reset();
        m_multiBufferRenderPassDescriptors[i].reset();
        m_multiBufferPipelines[i].reset();
        m_multiBufferSrbs[i].reset();
    }
}

void ShaderNodeRhi::setBufferWrap(const QString& wrap)
{
    const QString use = normalizeWrapMode(wrap);
    if (m_bufferWrapDefault == use) {
        return;
    }
    m_bufferWrapDefault = use;
    for (int i = 0; i < kMaxBufferPasses; ++i) {
        m_bufferWraps[i] = use;
        m_bufferSamplers[i].reset();
    }
    resetAllBindingsAndPipelines();
}

void ShaderNodeRhi::setBufferWraps(const QStringList& wraps)
{
    bool changed = false;
    for (int i = 0; i < kMaxBufferPasses; ++i) {
        const QString use = (i < wraps.size()) ? normalizeWrapMode(wraps.at(i)) : m_bufferWrapDefault;
        if (m_bufferWraps[i] != use) {
            m_bufferWraps[i] = use;
            m_bufferSamplers[i].reset();
            changed = true;
        }
    }
    if (changed) {
        resetAllBindingsAndPipelines();
    }
}

void ShaderNodeRhi::setBufferFilter(const QString& filter)
{
    const QString use = normalizeFilterMode(filter);
    if (m_bufferFilterDefault == use) {
        return;
    }
    m_bufferFilterDefault = use;
    for (int i = 0; i < kMaxBufferPasses; ++i) {
        m_bufferFilters[i] = use;
        m_bufferSamplers[i].reset();
    }
    resetAllBindingsAndPipelines();
}

void ShaderNodeRhi::setBufferFilters(const QStringList& filters)
{
    bool changed = false;
    for (int i = 0; i < kMaxBufferPasses; ++i) {
        const QString use = (i < filters.size()) ? normalizeFilterMode(filters.at(i)) : m_bufferFilterDefault;
        if (m_bufferFilters[i] != use) {
            m_bufferFilters[i] = use;
            m_bufferSamplers[i].reset();
            changed = true;
        }
    }
    if (changed) {
        resetAllBindingsAndPipelines();
    }
}

// ============================================================================
// Shader Loading (Vertex / Fragment)
// ============================================================================

bool ShaderNodeRhi::loadVertexShader(const QString& path)
{
    QString err;
    m_vertexShaderSource = loadAndExpandShader(path, &err);
    if (m_vertexShaderSource.isEmpty()) {
        m_shaderError = err.startsWith(QStringLiteral("Failed to open:"))
            ? QString(QStringLiteral("Failed to open vertex shader: ") + path)
            : QString(QStringLiteral("Vertex shader include: ") + err);
        return false;
    }
    m_vertexPath = path;
    m_vertexMtime = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    m_shaderDirty = true;
    return true;
}

bool ShaderNodeRhi::loadFragmentShader(const QString& path)
{
    QString err;
    m_fragmentShaderSource = loadAndExpandShader(path, &err);
    if (m_fragmentShaderSource.isEmpty()) {
        m_shaderError = err.startsWith(QStringLiteral("Failed to open:"))
            ? QString(QStringLiteral("Failed to open fragment shader: ") + path)
            : QString(QStringLiteral("Fragment shader include: ") + err);
        return false;
    }
    m_fragmentPath = path;
    m_fragmentMtime = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    m_shaderDirty = true;
    return true;
}

void ShaderNodeRhi::setVertexShaderSource(const QString& source)
{
    if (m_vertexShaderSource != source) {
        m_vertexShaderSource = source;
        if (source.isEmpty()) {
            m_vertexPath.clear();
            m_vertexMtime = 0;
        }
        m_shaderDirty = true;
    }
}

void ShaderNodeRhi::setFragmentShaderSource(const QString& source)
{
    if (m_fragmentShaderSource != source) {
        m_fragmentShaderSource = source;
        if (source.isEmpty()) {
            m_fragmentPath.clear();
            m_fragmentMtime = 0;
        }
        m_shaderDirty = true;
    }
}

bool ShaderNodeRhi::isShaderReady() const
{
    return m_shaderReady;
}

QString ShaderNodeRhi::shaderError() const
{
    return m_shaderError;
}

void ShaderNodeRhi::invalidateShader()
{
    m_shaderDirty = true;
}

void ShaderNodeRhi::invalidateUniforms()
{
    m_uniformsDirty = true;
    m_timeDirty = true;
    m_timeHiDirty = true;
    m_extensionDirty = true;
    m_sceneDataDirty = true;
    m_appFieldsDirty = true;
}

// ============================================================================
// Shader Include Paths
// ============================================================================

void ShaderNodeRhi::setShaderIncludePaths(const QStringList& paths)
{
    m_shaderIncludePaths = paths;
}

// ============================================================================
// Normalize helpers (static)
// ============================================================================

QString ShaderNodeRhi::normalizeWrapMode(const QString& wrap)
{
    return (wrap == QLatin1String("repeat")) ? QStringLiteral("repeat") : QStringLiteral("clamp");
}

QString ShaderNodeRhi::normalizeFilterMode(const QString& filter)
{
    if (filter == QLatin1String("nearest"))
        return QStringLiteral("nearest");
    if (filter == QLatin1String("mipmap"))
        return QStringLiteral("mipmap");
    return QStringLiteral("linear");
}

} // namespace PhosphorRendering
