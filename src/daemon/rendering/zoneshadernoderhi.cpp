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
    // The ZoneUniformExtension is owned by the ZoneShaderItem and is pushed
    // down to this node each frame through ShaderEffect::syncBasePropertiesToNode().
    // No extension allocation happens here.

    // 1x1 transparent fallback for when labels are disabled
    m_transparentFallbackImage = QImage(1, 1, QImage::Format_RGBA8888);
    m_transparentFallbackImage.fill(Qt::transparent);

    // Set PlasmaZones-specific shader include paths (locateAll to get both
    // user dir ~/.local/share and system dir /usr/share — locate() only returns
    // the first match which may be the user dir that lacks common.glsl)
    setShaderIncludePaths(QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/shaders"), QStandardPaths::LocateDirectory));
}

ZoneShaderNodeRhi::~ZoneShaderNodeRhi()
{
    // The parent's m_extraBindings map stores raw pointers to our
    // m_labelsTexture / m_labelsSampler. Those unique_ptr members run their
    // destructors AFTER this body returns (normal member-destruction order),
    // but the parent destructor runs AFTER them — so a hypothetical future
    // ~ShaderNodeRhi that walked m_extraBindings would see dangling
    // pointers. Call removeExtraBinding() now so the invariant "entries in
    // m_extraBindings always point to live resources" holds for the entire
    // teardown sequence.
    removeExtraBinding(1);
}

// ============================================================================
// Zone Counts
// ============================================================================

void ZoneShaderNodeRhi::setZoneCounts(int total, int highlighted)
{
    const int clampedTotal = qBound(0, total, MaxZones);
    const int clampedHighlighted = qBound(0, highlighted, clampedTotal);
    setAppField0(clampedTotal);
    setAppField1(clampedHighlighted);
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

    // Initialize labels texture resources on first use. Only flip
    // m_labelsInitialized after BOTH resources create successfully — otherwise
    // a transient RHI failure (device lost, OOM) would leave the init block
    // skipped forever while m_labelsTextureDirty stays set.
    if (!m_labelsInitialized) {
        std::unique_ptr<QRhiTexture> tex(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        if (!tex->create()) {
            return; // retry next frame
        }
        std::unique_ptr<QRhiSampler> sam(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                                         QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!sam->create()) {
            return; // retry next frame; tex deleted by unique_ptr
        }
        m_labelsTexture = std::move(tex);
        m_labelsSampler = std::move(sam);
        m_labelsInitialized = true;
        setExtraBinding(1, m_labelsTexture.get(), m_labelsSampler.get());
    }

    const QSize targetSize = (!m_labelsImage.isNull() && m_labelsImage.width() > 0 && m_labelsImage.height() > 0)
        ? m_labelsImage.size()
        : QSize(1, 1);
    if (m_labelsTexture->pixelSize() != targetSize) {
        std::unique_ptr<QRhiTexture> resized(rhi->newTexture(QRhiTexture::RGBA8, targetSize));
        if (!resized->create()) {
            return; // keep dirty; retry next frame with old texture still bound
        }
        m_labelsTexture = std::move(resized);
        // Re-register the extra binding with the new texture pointer.
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
    // Only clear the dirty flag after a successful upload completes.
    m_labelsTextureDirty = false;
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
