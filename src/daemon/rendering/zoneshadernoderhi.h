// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "zoneshadercommon.h"

#include <PhosphorRendering/ShaderNodeRhi.h>

#include <QImage>
#include <QQuickItem>

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
 * - Labels texture at binding 1 (via setExtraBinding)
 * - Zone count / highlighted count in BaseUniforms::appField0/appField1
 *
 * Zone UBO data (rects, colors, params) is written by ZoneUniformExtension,
 * which is OWNED by ZoneShaderItem — the item registers it via
 * ShaderEffect::setUniformExtension() and drives updates directly. The node
 * does not hold a QVector<ZoneData> cache; it only reports counts to the
 * shader via setZoneCounts().
 */
class ZoneShaderNodeRhi : public PhosphorRendering::ShaderNodeRhi
{
public:
    explicit ZoneShaderNodeRhi(QQuickItem* item);
    ~ZoneShaderNodeRhi() override;

    /**
     * @brief Publish zone and highlighted counts to the shader.
     *
     * Writes BaseUniforms::appField0 (total) and appField1 (highlighted) —
     * the shader uses these to bound its per-zone loops and gate highlight
     * logic. Called from ZoneShaderItem::updatePaintNode() during the sync
     * phase, alongside the ZoneUniformExtension update that carries the
     * actual per-zone data.
     */
    void setZoneCounts(int total, int highlighted);

    // ── Labels Texture (binding 1 via setExtraBinding) ────────────────
    void setLabelsTexture(const QImage& image);

    // ── Override prepare() to handle labels texture upload ─────────────
    void prepare() override;

    // ── Override releaseResources() to clean up labels texture ─────────
    void releaseResources() override;

private:
    /** Upload labels texture when dirty (called from prepare). */
    void uploadLabelsTexture(QRhi* rhi, QRhiCommandBuffer* cb);

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
 *
 * @param vertexPath    absolute path to the vertex shader on disk
 * @param fragmentPath  absolute path to the fragment shader on disk
 * @param includePaths  directories to search for `#include` directives. Pass the
 *                      same list `ShaderRegistry::searchPaths()` returns so include
 *                      resolution matches the on-screen render path. If empty,
 *                      includes will only resolve relative to the shader file itself.
 * @return success and error message (e.g. from QShaderBaker) for UI reporting
 */
PLASMAZONES_RENDERING_EXPORT WarmShaderBakeResult warmShaderBakeCacheForPaths(const QString& vertexPath,
                                                                              const QString& fragmentPath,
                                                                              const QStringList& includePaths = {});

} // namespace PlasmaZones
