// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zoneshadernoderhi.h"

#include <QFileInfo>
#include <QQuickWindow>
#include <QStandardPaths>

#include "../../core/logging.h"
#include <PhosphorRendering/ShaderCompiler.h>

namespace PlasmaZones {

ZoneShaderNodeRhi::ZoneShaderNodeRhi(QQuickItem* item)
    : PhosphorRendering::ShaderNodeRhi(item)
{
    // Create zone uniform extension and attach to parent
    m_zoneExtension = std::make_shared<ZoneUniformExtension>();
    setUniformExtension(m_zoneExtension);

    // 1x1 transparent fallback for when labels are disabled
    m_transparentFallbackImage = QImage(1, 1, QImage::Format_RGBA8888);
    m_transparentFallbackImage.fill(Qt::transparent);

    // Set PlasmaZones-specific shader include paths (locateAll to get both
    // user dir ~/.local/share and system dir /usr/share — locate() only returns
    // the first match which may be the user dir that lacks common.glsl)
    setShaderIncludePaths(QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/shaders"), QStandardPaths::LocateDirectory));
}

ZoneShaderNodeRhi::~ZoneShaderNodeRhi() = default;

// ============================================================================
// Zone Data Setters
// ============================================================================

void ZoneShaderNodeRhi::updateHighlightedCount()
{
    int count = 0;
    for (const auto& zone : m_zones) {
        if (zone.isHighlighted)
            ++count;
    }
    setAppField1(count);
}

void ZoneShaderNodeRhi::setZones(const QVector<ZoneData>& zones)
{
    const int count = qMin(zones.size(), MaxZones);
    m_zones = zones.mid(0, count);

    // Update extension with new zone data
    m_zoneExtension->updateFromZones(m_zones);

    // Update appField0 (zoneCount) and appField1 (highlightedCount)
    setAppField0(m_zones.size());
    updateHighlightedCount();

    invalidateUniforms();
}

void ZoneShaderNodeRhi::setZone(int index, const ZoneData& data)
{
    if (index >= 0 && index < MaxZones) {
        if (index >= m_zones.size()) {
            m_zones.resize(index + 1);
        }
        m_zones[index] = data;
        m_zoneExtension->updateFromZones(m_zones);

        setAppField0(m_zones.size());
        updateHighlightedCount();

        invalidateUniforms();
    }
}

void ZoneShaderNodeRhi::setZoneCount(int count)
{
    if (count >= 0 && count <= MaxZones) {
        m_zones.resize(count);
        m_zoneExtension->updateFromZones(m_zones);
        setAppField0(m_zones.size());
        invalidateUniforms();
    }
}

void ZoneShaderNodeRhi::setHighlightedZones(const QVector<int>& indices)
{
    m_highlightedIndices = indices;
    for (int i = 0; i < m_zones.size(); ++i) {
        m_zones[i].isHighlighted = indices.contains(i);
    }
    m_zoneExtension->updateFromZones(m_zones);

    updateHighlightedCount();

    invalidateUniforms();
}

void ZoneShaderNodeRhi::clearHighlights()
{
    m_highlightedIndices.clear();
    for (auto& zone : m_zones) {
        zone.isHighlighted = false;
    }
    m_zoneExtension->updateFromZones(m_zones);
    setAppField1(0);
    invalidateUniforms();
}

// ============================================================================
// Labels Texture (binding 1 via setExtraBinding)
// ============================================================================

void ZoneShaderNodeRhi::setLabelsTexture(const QImage& image)
{
    m_labelsImage = image;
    m_labelsTextureDirty = true;
}

void ZoneShaderNodeRhi::uploadLabelsTexture(QRhi* rhi, QRhiCommandBuffer* cb)
{
    if (!m_labelsTextureDirty) {
        return;
    }

    // Initialize labels texture resources on first use
    if (!m_labelsInitialized) {
        m_labelsInitialized = true;
        m_labelsTexture.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        if (!m_labelsTexture->create()) {
            return;
        }
        m_labelsSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                              QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!m_labelsSampler->create()) {
            return;
        }
        setExtraBinding(1, m_labelsTexture.get(), m_labelsSampler.get());
    }

    m_labelsTextureDirty = false;
    const QSize targetSize = (!m_labelsImage.isNull() && m_labelsImage.width() > 0 && m_labelsImage.height() > 0)
        ? m_labelsImage.size()
        : QSize(1, 1);
    if (m_labelsTexture->pixelSize() != targetSize) {
        m_labelsTexture.reset(rhi->newTexture(QRhiTexture::RGBA8, targetSize));
        if (!m_labelsTexture->create()) {
            return;
        }
        // Re-register the extra binding with the new texture
        setExtraBinding(1, m_labelsTexture.get(), m_labelsSampler.get());
    }
    QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
    if (batch) {
        const QImage& src = (!m_labelsImage.isNull() && m_labelsImage.width() > 0 && m_labelsImage.height() > 0)
            ? m_labelsImage
            : m_transparentFallbackImage;
        batch->uploadTexture(m_labelsTexture.get(), src);
        cb->resourceUpdate(batch);
    }
}

// ============================================================================
// prepare() — upload labels texture, then delegate to parent
// ============================================================================

void ZoneShaderNodeRhi::prepare()
{
    // Upload labels texture BEFORE parent's prepare(). The parent's
    // ensurePipeline() will include our extra binding (labels at 1) in its
    // SRB creation via appendExtraBindings().
    QRhiCommandBuffer* cb = commandBuffer();
    if (cb) {
        QRhi* rhi = cb->rhi();
        if (rhi) {
            uploadLabelsTexture(rhi, cb);
        }
    }

    // Delegate to parent for all base rendering (VBO, UBO, SRBs, pipelines,
    // multipass, textures, shader baking, uniform upload, buffer passes)
    PhosphorRendering::ShaderNodeRhi::prepare();
}

// ============================================================================
// releaseResources() — clean up labels, then delegate to parent
// ============================================================================

void ZoneShaderNodeRhi::releaseResources()
{
    qCInfo(lcOverlay) << "ZoneShaderNodeRhi::releaseResources() — releasing labels RHI resources";
    m_labelsTexture.reset();
    m_labelsSampler.reset();
    m_labelsInitialized = false;
    m_labelsTextureDirty = true;
    removeExtraBinding(1);

    // Delegate to parent
    PhosphorRendering::ShaderNodeRhi::releaseResources();
}

// ============================================================================
// warmShaderBakeCacheForPaths — pre-load shader cache warming
// ============================================================================

WarmShaderBakeResult warmShaderBakeCacheForPaths(const QString& vertexPath, const QString& fragmentPath,
                                                 const QStringList& includePaths)
{
    if (vertexPath.isEmpty() || fragmentPath.isEmpty()) {
        WarmShaderBakeResult result;
        result.errorMessage = QStringLiteral("Vertex or fragment path is empty");
        return result;
    }

    // Caller-provided include paths are authoritative when supplied (the daemon
    // hands us ShaderRegistry::searchPaths(), which exactly matches what the
    // render path uses). When omitted, fall back to the well-known system data
    // dirs — this avoids the previous heuristic of "parent of the shader's
    // parent dir", which silently broke for shaders not nested two levels
    // deep under a recognised root.
    QStringList paths = includePaths;
    if (paths.isEmpty()) {
        const QStringList systemShaderDirs =
            QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/shaders"),
                                      QStandardPaths::LocateDirectory);
        for (const QString& dir : systemShaderDirs) {
            if (!paths.contains(dir))
                paths.append(dir);
        }
    }

    return PhosphorRendering::warmShaderBakeCacheForPaths(vertexPath, fragmentPath, paths);
}

} // namespace PlasmaZones
