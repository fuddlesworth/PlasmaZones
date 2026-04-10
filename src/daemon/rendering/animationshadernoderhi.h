// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "animationshadercommon.h"

#include <plasmazones_rendering_export.h>
#include <QMatrix4x4>
#include <QQuickItem>
#include <QSGRenderNode>
#include <QVector4D>
#include <array>
#include <atomic>
#include <memory>

#include <rhi/qrhi.h>

namespace PlasmaZones {

/**
 * @brief QSGRenderNode for animation transition shaders on overlay content
 *
 * Renders a fullscreen quad with a fragment shader loaded from disk
 * (GLSL 450, baked at runtime via QShaderBaker). The shader operates
 * on the overlay content texture with pz_progress driving the effect.
 *
 * Follows the same architecture as ZoneShaderNodeRhi — shader baking,
 * UBO management, pipeline creation, partial uniform updates.
 */
class PLASMAZONES_RENDERING_EXPORT AnimationShaderNodeRhi : public QSGRenderNode
{
public:
    explicit AnimationShaderNodeRhi(QQuickItem* item);
    ~AnimationShaderNodeRhi() override;

    void invalidateItem();

    // QSGRenderNode overrides
    StateFlags changedStates() const override;
    RenderingFlags flags() const override;
    QRectF rect() const override;
    void prepare() override;
    void render(const RenderState* state) override;
    void releaseResources() override;

    // Animation properties
    void setProgress(float progress);
    void setDuration(float durationMs);
    void setStyleParam(float param);
    void setDirection(int direction);
    void setResolution(float width, float height);

    // Custom params (same 8 vec4 slots as zone shader)
    void setCustomParams(int index, const QVector4D& params);

    // Content texture from source item's layer
    void setSourceTexture(QSGTexture* texture);

    // Shader loading from disk (GLSL 450, uses detail::loadAndExpandShader)
    bool loadFragmentShader(const QString& path);
    bool isShaderReady() const;
    QString shaderError() const;

private:
    QRhi* safeRhi() const;
    bool ensurePipeline();
    void syncUniforms();
    void releaseRhiResources();

    QQuickItem* m_item = nullptr;
    std::atomic<bool> m_itemValid{true};

    // RHI resources
    std::unique_ptr<QRhiBuffer> m_vbo;
    std::unique_ptr<QRhiBuffer> m_ubo;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    std::unique_ptr<QRhiSampler> m_contentSampler;
    QShader m_vertexShader;
    QShader m_fragmentShader;
    QVector<quint32> m_renderPassFormat;

    // State
    bool m_initialized = false;
    bool m_vboUploaded = false;
    bool m_shaderReady = false;
    bool m_shaderDirty = true;
    bool m_uniformsDirty = true;
    bool m_progressDirty = true;
    bool m_didFullUploadOnce = false;
    QString m_shaderError;
    QString m_fragmentPath;
    QString m_fragmentShaderSource;
    qint64 m_fragmentMtime = 0;

    // Content texture (from source item's layer)
    QSGTexture* m_sourceTexture = nullptr;

    // Uniform data
    AnimationShaderUniforms m_uniforms = {};
    float m_progress = 0.0f;
    float m_duration = 150.0f;
    float m_styleParam = 0.0f;
    int m_direction = 0;
    float m_width = 0.0f;
    float m_height = 0.0f;
    std::array<QVector4D, 8> m_customParams;
};

} // namespace PlasmaZones
