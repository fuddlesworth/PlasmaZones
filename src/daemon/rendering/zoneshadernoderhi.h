// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "zoneshadercommon.h"
#include "zoneuniformextension.h"

#include <PhosphorRendering/ShaderNodeRhi.h>

#include <QImage>
#include <QQuickItem>
#include <QVector>
#include <QVector4D>

#include <memory>

#include <plasmazones_rendering_export.h>
#include <rhi/qrhi.h>

namespace PlasmaZones {

/**
 * @brief QSGRenderNode for zone overlay rendering, delegating to PhosphorRendering::ShaderNodeRhi.
 *
 * Inherits from PhosphorRendering::ShaderNodeRhi which handles all base rendering:
 * VBO, UBO, SRBs, pipelines, multipass, textures, shader baking, etc.
 *
 * This subclass adds zone-specific state:
 * - Zone data arrays (via ZoneUniformExtension as IUniformExtension)
 * - Labels texture at binding 1 (via setExtraBinding)
 * - Zone count / highlighted count in BaseUniforms::appField0/appField1
 *
 * The zone extension data is appended after BaseUniforms in the UBO, matching
 * the GLSL UBO layout in common.glsl exactly.
 */
class ZoneShaderNodeRhi : public PhosphorRendering::ShaderNodeRhi
{
public:
    explicit ZoneShaderNodeRhi(QQuickItem* item);
    ~ZoneShaderNodeRhi() override;

    // ── Zone Data ─────────────────────────────────────────────────────
    void setZones(const QVector<ZoneData>& zones);
    void setZone(int index, const ZoneData& data);
    void setZoneCount(int count);
    void setHighlightedZones(const QVector<int>& indices);
    void clearHighlights();

    // ── Labels Texture (binding 1 via setExtraBinding) ────────────────
    void setLabelsTexture(const QImage& image);

    // ── Override prepare() to handle labels texture upload ─────────────
    void prepare() override;

    // ── Override releaseResources() to clean up labels texture ─────────
    void releaseResources() override;

private:
    /** Thread-safe QRhi accessor for labels texture management. */
    QRhi* safeRhiForLabels() const;

    /** Upload labels texture when dirty (called from prepare). */
    void uploadLabelsTexture(QRhi* rhi, QRhiCommandBuffer* cb);

    // Zone data
    QVector<ZoneData> m_zones;
    QVector<int> m_highlightedIndices;

    // Zone uniform extension (appends zone arrays after BaseUniforms)
    std::shared_ptr<ZoneUniformExtension> m_zoneExtension;

    // Labels texture (binding 1 via setExtraBinding)
    QImage m_labelsImage;
    QImage m_transparentFallbackImage;
    std::unique_ptr<QRhiTexture> m_labelsTexture;
    std::unique_ptr<QRhiSampler> m_labelsSampler;
    bool m_labelsTextureDirty = false;
    bool m_labelsInitialized = false;
};

/** Alias for PhosphorRendering::WarmShaderBakeResult — avoids type duplication. */
using WarmShaderBakeResult = PhosphorRendering::WarmShaderBakeResult;

/**
 * Pre-load cache warming: load, bake, and insert shaders for the given paths into the
 * shared bake cache. Safe to call from any thread (e.g. after ShaderRegistry::refresh()).
 * @return success and error message (e.g. from QShaderBaker) for UI reporting
 */
PLASMAZONES_RENDERING_EXPORT WarmShaderBakeResult warmShaderBakeCacheForPaths(const QString& vertexPath,
                                                                              const QString& fragmentPath);

} // namespace PlasmaZones
