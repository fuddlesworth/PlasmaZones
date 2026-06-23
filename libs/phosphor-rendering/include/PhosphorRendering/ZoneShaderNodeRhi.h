// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorRendering/ShaderNodeRhi.h>
#include <PhosphorRendering/ZoneLabelTexture.h>
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
     * @brief Per-instance identifier, monotonically increasing across all
     * ZoneShaderNodeRhi instances in this process.
     *
     * Host items use this instead of pointer equality to detect node
     * recreation: when the scene graph destroys the previous node and
     * allocates a new one (e.g. on releaseResources), the new node gets a
     * fresh id. Pointer equality is ABA-vulnerable when the allocator reuses
     * the address — instance ids are not.
     */
    quint64 instanceId() const
    {
        return m_instanceId;
    }

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
     * @brief Stage the sparse zone-labels payload. Upload happens in prepare().
     *
     * The host item supplies glyph tiles + their positions (see
     * ZoneLabelTexture); the node composites them into a screen-sized texture
     * the shader samples at binding 1 (uZoneLabels). An empty payload binds a
     * 1×1 transparent fallback so the descriptor set stays well-formed (and no
     * full-screen texture is allocated when numbers are off).
     */
    void setLabelsTexture(const ZoneLabelTexture& labels);

    void prepare() override;
    void releaseResources() override;

private:
    /// Upload labels texture when dirty (called from prepare with a live cb).
    /// Composites by uploading each sparse glyph tile directly to its position
    /// in the screen-sized texture (and clearing vacated regions), so no
    /// full-screen CPU image is ever allocated.
    void uploadLabelsTexture(QRhi* rhi, QRhiCommandBuffer* cb);

    ZoneLabelTexture m_labels;
    /// Dest rects of the tiles uploaded last time, so the next upload can clear
    /// exactly the regions being vacated (no full-screen clear per change).
    /// Only committed to the new set after a fully-successful upload, so a
    /// pool-exhaustion retry re-clears the correct (old) regions.
    QList<QRect> m_prevTileRects;
    /// Latched true when the texture is (re)created (undefined contents) and
    /// cleared only after a successful full grid-clear upload. Persisting it
    /// across frames ensures a pool-exhaustion retry still performs the full
    /// clear instead of leaving GPU garbage in the non-tile regions.
    bool m_labelsNeedFullClear = false;
    std::unique_ptr<QRhiTexture> m_labelsTexture;
    std::unique_ptr<QRhiSampler> m_labelsSampler;
    quint64 m_instanceId = 0;
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
