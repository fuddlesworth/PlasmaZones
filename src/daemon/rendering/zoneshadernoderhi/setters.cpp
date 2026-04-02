// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../zoneshadernoderhi.h"
#include "internal.h"
#include "../rendering_macros.h"

#include "../../../core/logging.h"

#include <QFileInfo>
#include <QLatin1String>

namespace PlasmaZones {

// ============================================================================
// Normalize helpers for wrap/filter mode strings
// ============================================================================

// Normalize wrap mode string: only "repeat" is recognized, everything else → "clamp"
static QString normalizeWrapMode(const QString& wrap)
{
    return (wrap == QLatin1String("repeat")) ? QStringLiteral("repeat") : QStringLiteral("clamp");
}

// Normalize filter mode string: "nearest" and "mipmap" are recognized, everything else → "linear"
static QString normalizeFilterMode(const QString& filter)
{
    if (filter == QLatin1String("nearest") || filter == QLatin1String("mipmap")) {
        return filter;
    }
    return QStringLiteral("linear");
}

// ============================================================================
// Zone Data Setters
// ============================================================================

void ZoneShaderNodeRhi::setZones(const QVector<ZoneData>& zones)
{
    int count = qMin(zones.size(), MaxZones);
    m_zones = zones.mid(0, count);
    m_uniformsDirty = true;
    m_zoneDataDirty = true;
}

void ZoneShaderNodeRhi::setZone(int index, const ZoneData& data)
{
    if (index >= 0 && index < MaxZones) {
        if (index >= m_zones.size()) {
            m_zones.resize(index + 1);
        }
        m_zones[index] = data;
        m_uniformsDirty = true;
        m_zoneDataDirty = true;
    }
}

void ZoneShaderNodeRhi::setZoneCount(int count)
{
    if (count >= 0 && count <= MaxZones) {
        m_zones.resize(count);
        m_uniformsDirty = true;
        m_zoneDataDirty = true;
    }
}

void ZoneShaderNodeRhi::setHighlightedZones(const QVector<int>& indices)
{
    m_highlightedIndices = indices;
    for (int i = 0; i < m_zones.size(); ++i) {
        m_zones[i].isHighlighted = indices.contains(i);
    }
    m_uniformsDirty = true;
    m_zoneDataDirty = true;
}

void ZoneShaderNodeRhi::clearHighlights()
{
    m_highlightedIndices.clear();
    for (auto& zone : m_zones) {
        zone.isHighlighted = false;
    }
    m_uniformsDirty = true;
    m_zoneDataDirty = true;
}

// ============================================================================
// Timing / Resolution / Mouse Setters
// ============================================================================

void ZoneShaderNodeRhi::setTime(float time)
{
    m_time = time;
    m_uniformsDirty = true;
    m_timeDirty = true;
    m_zoneDataDirty = true; // iDate is in scene block; re-upload when time changes
}
void ZoneShaderNodeRhi::setTimeDelta(float delta)
{
    m_timeDelta = delta;
    m_uniformsDirty = true;
    m_timeDirty = true;
    m_zoneDataDirty = true; // iDate is in scene block; re-upload when time changes
}
void ZoneShaderNodeRhi::setFrame(int frame)
{
    m_frame = frame;
    m_uniformsDirty = true;
    m_timeDirty = true;
    m_zoneDataDirty = true; // iDate is in scene block; re-upload when time changes
}
void ZoneShaderNodeRhi::setResolution(float width, float height)
{
    if (m_width != width || m_height != height) {
        m_width = width;
        m_height = height;
        m_uniformsDirty = true;
        m_zoneDataDirty = true;
    }
}
void ZoneShaderNodeRhi::setMousePosition(const QPointF& pos)
{
    m_mousePosition = pos;
    m_uniformsDirty = true;
    m_zoneDataDirty = true;
}

// ============================================================================
// Custom Params (DRY macro — 4 invocations replace 24 lines)
// ============================================================================

RHINODE_PARAMS_SETTER(CustomParams1, m_customParams1)
RHINODE_PARAMS_SETTER(CustomParams2, m_customParams2)
RHINODE_PARAMS_SETTER(CustomParams3, m_customParams3)
RHINODE_PARAMS_SETTER(CustomParams4, m_customParams4)
RHINODE_PARAMS_SETTER(CustomParams5, m_customParams5)
RHINODE_PARAMS_SETTER(CustomParams6, m_customParams6)
RHINODE_PARAMS_SETTER(CustomParams7, m_customParams7)
RHINODE_PARAMS_SETTER(CustomParams8, m_customParams8)

// ============================================================================
// Custom Colors (DRY macro — 8 invocations replace 48 lines)
// ============================================================================

RHINODE_COLOR_SETTER(CustomColor1, m_customColor1)
RHINODE_COLOR_SETTER(CustomColor2, m_customColor2)
RHINODE_COLOR_SETTER(CustomColor3, m_customColor3)
RHINODE_COLOR_SETTER(CustomColor4, m_customColor4)
RHINODE_COLOR_SETTER(CustomColor5, m_customColor5)
RHINODE_COLOR_SETTER(CustomColor6, m_customColor6)
RHINODE_COLOR_SETTER(CustomColor7, m_customColor7)
RHINODE_COLOR_SETTER(CustomColor8, m_customColor8)
RHINODE_COLOR_SETTER(CustomColor9, m_customColor9)
RHINODE_COLOR_SETTER(CustomColor10, m_customColor10)
RHINODE_COLOR_SETTER(CustomColor11, m_customColor11)
RHINODE_COLOR_SETTER(CustomColor12, m_customColor12)
RHINODE_COLOR_SETTER(CustomColor13, m_customColor13)
RHINODE_COLOR_SETTER(CustomColor14, m_customColor14)
RHINODE_COLOR_SETTER(CustomColor15, m_customColor15)
RHINODE_COLOR_SETTER(CustomColor16, m_customColor16)

// ============================================================================
// Labels / Audio / User Texture Setters
// ============================================================================

void ZoneShaderNodeRhi::setLabelsTexture(const QImage& image)
{
    m_labelsImage = image;
    m_labelsTextureDirty = true;
    m_uniformsDirty = true;
}

void ZoneShaderNodeRhi::setAudioSpectrum(const QVector<float>& spectrum)
{
    if (m_audioSpectrum == spectrum) {
        return;
    }
    m_audioSpectrum = spectrum;
    m_audioSpectrumDirty = true;
    m_uniformsDirty = true;
}

void ZoneShaderNodeRhi::setUserTexture(int slot, const QImage& image)
{
    if (slot < 0 || slot >= kMaxUserTextures) {
        return;
    }
    // QImage uses implicit sharing; cacheKey() is O(1) and avoids
    // re-uploading the identical texture to the GPU every frame.
    if (m_userTextureImages[slot].cacheKey() == image.cacheKey()) {
        return;
    }
    m_userTextureImages[slot] = image;
    m_userTextureDirty[slot] = true;
    m_uniformsDirty = true;
    m_zoneDataDirty = true;
}

void ZoneShaderNodeRhi::setUserTextureWrap(int slot, const QString& wrap)
{
    if (slot < 0 || slot >= kMaxUserTextures) {
        return;
    }
    const QString use = normalizeWrapMode(wrap);
    if (m_userTextureWraps[slot] == use) {
        return;
    }
    m_userTextureWraps[slot] = use;
    // Force sampler recreation with new wrap mode
    m_userTextureSamplers[slot].reset();
    resetAllSrbs();
}

void ZoneShaderNodeRhi::setWallpaperTexture(const QImage& image)
{
    // Compare by data pointer and size: convertToFormat() creates a new QImage
    // with a different cacheKey() even for identical content, so cacheKey() is
    // unreliable. QImage uses implicit sharing, so constBits() identity + size
    // is a fast check for the same underlying data.
    if (m_wallpaperImage.constBits() == image.constBits() && m_wallpaperImage.size() == image.size()) {
        return;
    }
    m_wallpaperImage = image;
    m_wallpaperDirty = true;
    m_uniformsDirty = true;
}

void ZoneShaderNodeRhi::setUseWallpaper(bool use)
{
    if (m_useWallpaper == use) {
        return;
    }
    m_useWallpaper = use;
    resetAllSrbs();
}

void ZoneShaderNodeRhi::appendUserTextureBindings(QVector<QRhiShaderResourceBinding>& bindings) const
{
    for (int t = 0; t < kMaxUserTextures; ++t) {
        if (m_userTextures[t] && m_userTextureSamplers[t]) {
            bindings.append(QRhiShaderResourceBinding::sampledTexture(7 + t, QRhiShaderResourceBinding::FragmentStage,
                                                                      m_userTextures[t].get(),
                                                                      m_userTextureSamplers[t].get()));
        }
    }
}

void ZoneShaderNodeRhi::appendWallpaperBinding(QVector<QRhiShaderResourceBinding>& bindings) const
{
    if (m_useWallpaper && m_wallpaperTexture && m_wallpaperSampler) {
        bindings.append(QRhiShaderResourceBinding::sampledTexture(11, QRhiShaderResourceBinding::FragmentStage,
                                                                  m_wallpaperTexture.get(), m_wallpaperSampler.get()));
    }
}

void ZoneShaderNodeRhi::appendDepthBinding(QVector<QRhiShaderResourceBinding>& bindings) const
{
    if (m_useDepthBuffer && m_depthTexture && m_depthSampler) {
        bindings.append(QRhiShaderResourceBinding::sampledTexture(12, QRhiShaderResourceBinding::FragmentStage,
                                                                  m_depthTexture.get(), m_depthSampler.get()));
    }
}

void ZoneShaderNodeRhi::setUseDepthBuffer(bool use)
{
    if (m_useDepthBuffer == use) {
        return;
    }
    m_useDepthBuffer = use;
    // Force recreation of render targets and pipelines
    m_depthTexture.reset();
    m_depthSampler.reset();
    resetAllSrbs();
    markDirty(QSGNode::DirtyMaterial);
}

void ZoneShaderNodeRhi::resetAllSrbs()
{
    m_srb.reset();
    m_srbB.reset();
    m_bufferSrb.reset();
    m_bufferSrbB.reset();
    for (int i = 0; i < kMaxBufferPasses; ++i) {
        m_multiBufferSrbs[i].reset();
    }
}

// ============================================================================
// Buffer Shader Path / Feedback / Scale / Wrap
// ============================================================================

void ZoneShaderNodeRhi::setBufferShaderPath(const QString& path)
{
    setBufferShaderPaths(path.isEmpty() ? QStringList() : QStringList{path});
}

void ZoneShaderNodeRhi::setBufferShaderPaths(const QStringList& paths)
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

    qCDebug(lcOverlay) << "ZoneShaderNodeRhi setBufferShaderPaths count=" << m_bufferPaths.size()
                       << "multiBufferMode=" << (m_bufferPaths.size() > 1);

    m_bufferShaderDirty = true;
    m_bufferShaderReady = false;
    m_bufferFragmentShaderSource.clear();
    m_bufferMtime = 0;
    m_multiBufferShadersReady = false;
    m_multiBufferShaderDirty = true;
    for (int i = 0; i < kMaxBufferPasses; ++i) {
        m_multiBufferFragmentShaderSources[i].clear();
        m_multiBufferFragmentShaders[i] = QShader();
        m_multiBufferMtimes[i] = 0;
    }
    if (m_bufferPaths.size() == 1) {
        const QString& path = m_bufferPaths.constFirst();
        if (QFileInfo::exists(path)) {
            QString err;
            m_bufferFragmentShaderSource = detail::loadAndExpandShader(path, &err);
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

void ZoneShaderNodeRhi::setBufferFeedback(bool enable)
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

void ZoneShaderNodeRhi::setBufferScale(qreal scale)
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

void ZoneShaderNodeRhi::setBufferWrap(const QString& wrap)
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
    resetAllSrbs();
}

void ZoneShaderNodeRhi::setBufferWraps(const QStringList& wraps)
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
        resetAllSrbs();
    }
}

void ZoneShaderNodeRhi::setBufferFilter(const QString& filter)
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
    resetAllSrbs();
}

void ZoneShaderNodeRhi::setBufferFilters(const QStringList& filters)
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
        resetAllSrbs();
    }
}

// ============================================================================
// Shader Loading (Vertex / Fragment)
// ============================================================================

bool ZoneShaderNodeRhi::loadVertexShader(const QString& path)
{
    QString err;
    m_vertexShaderSource = detail::loadAndExpandShader(path, &err);
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

bool ZoneShaderNodeRhi::loadFragmentShader(const QString& path)
{
    QString err;
    m_fragmentShaderSource = detail::loadAndExpandShader(path, &err);
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

void ZoneShaderNodeRhi::setVertexShaderSource(const QString& source)
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

void ZoneShaderNodeRhi::setFragmentShaderSource(const QString& source)
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

bool ZoneShaderNodeRhi::isShaderReady() const
{
    return m_shaderReady;
}

QString ZoneShaderNodeRhi::shaderError() const
{
    return m_shaderError;
}

void ZoneShaderNodeRhi::invalidateShader()
{
    m_shaderDirty = true;
}

void ZoneShaderNodeRhi::invalidateUniforms()
{
    m_uniformsDirty = true;
    m_timeDirty = true;
    m_zoneDataDirty = true;
}

} // namespace PlasmaZones
