// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorRendering/ShaderNodeRhi.h>
#include <PhosphorRendering/ZoneShaderCommon.h>
#include <PhosphorRendering/phosphorrendering_export.h>

#include <QImage>
#include <QQuickItem>

#include <memory>

#include <rhi/qrhi.h>

namespace PhosphorRendering {

/**
 * @brief QSGRenderNode for zone overlay rendering, delegating to ShaderNodeRhi.
 *
 * Inherits from ShaderNodeRhi which handles all base rendering: VBO, UBO,
 * SRBs, pipelines, multipass, textures, shader baking, etc.
 *
 * This subclass adds zone-specific state:
 * - Labels texture at binding 1 (via setExtraBinding)
 * - Zone count / highlighted count in BaseUniforms::appField0/appField1
 *
 * Zone UBO data (rects, colors, params) is written by ZoneUniformExtension,
 * which the host item registers via ShaderEffect::setUniformExtension(). The
 * node does not hold a QVector<ZoneData> cache; it only reports counts to the
 * shader via setZoneCounts().
 */
class PHOSPHORRENDERING_EXPORT ZoneShaderNodeRhi : public ShaderNodeRhi
{
public:
    explicit ZoneShaderNodeRhi(QQuickItem* item);
    ~ZoneShaderNodeRhi() override;

    /**
     * @brief Publish zone and highlighted counts to the shader.
     *
     * Writes BaseUniforms::appField0 (total) and appField1 (highlighted) — the
     * shader uses these to bound its per-zone loops and gate highlight logic.
     * Called from the host item's updatePaintNode() during the sync phase,
     * alongside the ZoneUniformExtension update that carries the actual
     * per-zone data.
     */
    void setZoneCounts(int total, int highlighted);

    /**
     * @brief Stage a labels texture image. Upload happens in prepare().
     *
     * The host item builds an RGBA image with zone numbers drawn at zone
     * centres; the shader samples it at binding 1 (uZoneLabels). A null /
     * empty image binds a 1×1 transparent fallback so the descriptor set
     * stays well-formed.
     */
    void setLabelsTexture(const QImage& image);

    void prepare() override;
    void releaseResources() override;

private:
    /// Upload labels texture when dirty (called from prepare with a live cb).
    void uploadLabelsTexture(QRhi* rhi, QRhiCommandBuffer* cb);

    QImage m_labelsImage;
    QImage m_transparentFallbackImage;
    std::unique_ptr<QRhiTexture> m_labelsTexture;
    std::unique_ptr<QRhiSampler> m_labelsSampler;
    bool m_labelsTextureDirty = false;
    bool m_labelsInitialized = false;
    // Cap how many times we retry RHI texture/sampler creation. Without a cap,
    // a permanent failure (driver wedged, persistent OOM) turns prepare() into
    // a hot loop that runs init + logs every frame forever. The vertex-shader
    // load in tools/shader-render's RenderEffect uses the same one-shot-latch
    // pattern; we mirror it here for the daemon's render path.
    int m_labelsInitFailureCount = 0;
    bool m_labelsInitGaveUp = false;
};

} // namespace PhosphorRendering
