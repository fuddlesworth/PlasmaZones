// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "zoneshadernodebase.h"
#include <QQuickItem>
#include <QMatrix4x4>
#include <QVector4D>
#include <QString>
#include <QStringList>
#include <QVector>
#include <array>
#include <atomic>
#include <memory>

#include <plasmazones_rendering_export.h>
#include <rhi/qrhi.h>

namespace PlasmaZones {

/**
 * @brief QSGRenderNode for zone overlay rendering via Qt RHI (Vulkan / OpenGL)
 *
 * Uses QRhi and QShaderBaker (runtime SPIR-V + GLSL 330 bake). Requires Qt 6.6+
 * (commandBuffer(), renderTarget()).
 */
class ZoneShaderNodeRhi : public ZoneShaderNodeBase
{
public:
    explicit ZoneShaderNodeRhi(QQuickItem* item);
    ~ZoneShaderNodeRhi() override;

    /**
     * @brief Notify the render node that its owning item is being destroyed.
     *
     * Called from ZoneShaderItem::~ZoneShaderItem() on the GUI thread.
     * After this call, the render node will no longer dereference m_item.
     * Thread-safe: uses an atomic flag checked by prepare()/render().
     */
    void invalidateItem();

    // QSGRenderNode
    QSGRenderNode::StateFlags changedStates() const override;
    QSGRenderNode::RenderingFlags flags() const override;
    QRectF rect() const override;
    void prepare() override;
    void render(const RenderState* state) override;
    void releaseResources() override;

    // ZoneShaderNodeBase
    void setZones(const QVector<ZoneData>& zones) override;
    void setZone(int index, const ZoneData& data) override;
    void setZoneCount(int count) override;
    void setHighlightedZones(const QVector<int>& indices) override;
    void clearHighlights() override;
    void setTime(float time) override;
    void setTimeDelta(float delta) override;
    void setFrame(int frame) override;
    void setResolution(float width, float height) override;
    void setMousePosition(const QPointF& pos) override;
    void setCustomParams1(const QVector4D& params) override;
    void setCustomParams2(const QVector4D& params) override;
    void setCustomParams3(const QVector4D& params) override;
    void setCustomParams4(const QVector4D& params) override;
    void setCustomParams5(const QVector4D& params) override;
    void setCustomParams6(const QVector4D& params) override;
    void setCustomParams7(const QVector4D& params) override;
    void setCustomParams8(const QVector4D& params) override;
    void setCustomColor1(const QColor& color) override;
    void setCustomColor2(const QColor& color) override;
    void setCustomColor3(const QColor& color) override;
    void setCustomColor4(const QColor& color) override;
    void setCustomColor5(const QColor& color) override;
    void setCustomColor6(const QColor& color) override;
    void setCustomColor7(const QColor& color) override;
    void setCustomColor8(const QColor& color) override;
    void setCustomColor9(const QColor& color) override;
    void setCustomColor10(const QColor& color) override;
    void setCustomColor11(const QColor& color) override;
    void setCustomColor12(const QColor& color) override;
    void setCustomColor13(const QColor& color) override;
    void setCustomColor14(const QColor& color) override;
    void setCustomColor15(const QColor& color) override;
    void setCustomColor16(const QColor& color) override;
    void setLabelsTexture(const QImage& image) override;
    void setAudioSpectrum(const QVector<float>& spectrum) override;
    void setUserTexture(int slot, const QImage& image) override;
    void setUserTextureWrap(int slot, const QString& wrap) override;
    void setWallpaperTexture(const QImage& image) override;
    void setUseWallpaper(bool use) override;
    void setBufferShaderPath(const QString& path) override;
    void setBufferShaderPaths(const QStringList& paths) override;
    void setBufferFeedback(bool enable) override;
    void setBufferScale(qreal scale) override;
    void setBufferWrap(const QString& wrap) override;
    void setBufferWraps(const QStringList& wraps) override;
    void setUseDepthBuffer(bool use) override;
    bool loadVertexShader(const QString& path) override;
    bool loadFragmentShader(const QString& path) override;
    void setVertexShaderSource(const QString& source) override;
    void setFragmentShaderSource(const QString& source) override;
    bool isShaderReady() const override;
    QString shaderError() const override;
    void invalidateShader() override;
    void invalidateUniforms() override;

private:
    bool ensurePipeline();
    bool ensureBufferPipeline();
    bool ensureBufferTarget();
    void syncUniformsFromData();
    void uploadDirtyTextures(QRhi* rhi, QRhiCommandBuffer* cb);
    void releaseRhiResources();
    void appendUserTextureBindings(QVector<QRhiShaderResourceBinding>& bindings) const;
    void appendWallpaperBinding(QVector<QRhiShaderResourceBinding>& bindings) const;
    void appendDepthBinding(QVector<QRhiShaderResourceBinding>& bindings) const;
    void resetAllSrbs();
    void bakeBufferShaders();

    QQuickItem* m_item = nullptr;
    std::atomic<bool> m_itemValid{true}; // Set to false by invalidateItem(); checked before dereferencing m_item

    std::unique_ptr<QRhiBuffer> m_vbo;
    std::unique_ptr<QRhiBuffer> m_ubo;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    QShader m_vertexShader;
    QShader m_fragmentShader;
    QVector<quint32> m_renderPassFormat;

    // Multi-pass: buffer pass(es) (optional). Up to 4 paths; when size==1 feedback may use ping-pong.
    static constexpr int kMaxBufferPasses = 4;
    QString m_bufferPath;
    QStringList m_bufferPaths;
    bool m_bufferFeedback = false;
    qreal m_bufferScale = 1.0;
    std::array<QString, kMaxBufferPasses> m_bufferWraps = {QStringLiteral("clamp"), QStringLiteral("clamp"),
                                                           QStringLiteral("clamp"), QStringLiteral("clamp")};
    QString m_bufferWrapDefault = QStringLiteral("clamp");
    QString m_bufferFragmentShaderSource;
    QShader m_bufferFragmentShader;
    qint64 m_bufferMtime = 0;
    bool m_bufferShaderReady = false;
    bool m_bufferShaderDirty = true;
    std::unique_ptr<QRhiTexture> m_bufferTexture;
    std::unique_ptr<QRhiRenderPassDescriptor> m_bufferRenderPassDescriptor;
    std::unique_ptr<QRhiTextureRenderTarget> m_bufferRenderTarget;
    std::array<std::unique_ptr<QRhiSampler>, kMaxBufferPasses> m_bufferSamplers;
    std::unique_ptr<QRhiShaderResourceBindings> m_bufferSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_bufferPipeline;
    QVector<quint32> m_bufferRenderPassFormat;
    // Ping-pong (bufferFeedback): second texture/RT/SRB for buffer pass; image pass has two SRBs
    std::unique_ptr<QRhiTexture> m_bufferTextureB;
    std::unique_ptr<QRhiRenderPassDescriptor> m_bufferRenderPassDescriptorB;
    std::unique_ptr<QRhiTextureRenderTarget> m_bufferRenderTargetB;
    std::unique_ptr<QRhiShaderResourceBindings> m_bufferSrbB;
    std::unique_ptr<QRhiShaderResourceBindings> m_srbB; // image pass SRB with binding 2 = texture B
    bool m_bufferFeedbackCleared = false; // one-time clear of both buffers when feedback starts

    // Multi-buffer mode (2–4 passes): per-pass resources; only used when m_bufferPaths.size() > 1
    std::array<std::unique_ptr<QRhiTexture>, kMaxBufferPasses> m_multiBufferTextures = {};
    std::array<std::unique_ptr<QRhiTextureRenderTarget>, kMaxBufferPasses> m_multiBufferRenderTargets = {};
    std::array<std::unique_ptr<QRhiRenderPassDescriptor>, kMaxBufferPasses> m_multiBufferRenderPassDescriptors = {};
    std::array<std::unique_ptr<QRhiGraphicsPipeline>, kMaxBufferPasses> m_multiBufferPipelines = {};
    std::array<std::unique_ptr<QRhiShaderResourceBindings>, kMaxBufferPasses> m_multiBufferSrbs = {};
    std::array<QShader, kMaxBufferPasses> m_multiBufferFragmentShaders = {};
    std::array<QString, kMaxBufferPasses> m_multiBufferFragmentShaderSources = {};
    std::array<qint64, kMaxBufferPasses> m_multiBufferMtimes = {};
    bool m_multiBufferShadersReady = false;
    bool m_multiBufferShaderDirty = true;
    // Dummy 1x1 texture for iChannel0 when multipass is set but buffer not yet created (e.g. zero size)
    std::unique_ptr<QRhiTexture> m_dummyChannelTexture;
    std::unique_ptr<QRhiSampler> m_dummyChannelSampler;
    bool m_dummyChannelTextureNeedsUpload = false;

    QString m_vertexShaderSource;
    QString m_fragmentShaderSource;
    QString m_vertexPath;
    QString m_fragmentPath;
    qint64 m_vertexMtime = 0;
    qint64 m_fragmentMtime = 0;
    QString m_shaderError;
    bool m_initialized = false;
    bool m_vboUploaded = false;
    bool m_shaderReady = false;
    bool m_shaderDirty = true;
    bool m_uniformsDirty = true;
    bool m_timeDirty = true;
    bool m_zoneDataDirty = true;
    bool m_didFullUploadOnce = false;

    ZoneShaderUniforms m_uniforms = {};
    QVector<ZoneData> m_zones;
    QVector<int> m_highlightedIndices;

    float m_time = 0.0f;
    float m_timeDelta = 0.0f;
    int m_frame = 0;
    float m_width = 0.0f;
    float m_height = 0.0f;
    QPointF m_mousePosition;

    QVector4D m_customParams1;
    QVector4D m_customParams2;
    QVector4D m_customParams3;
    QVector4D m_customParams4;
    QVector4D m_customParams5;
    QVector4D m_customParams6;
    QVector4D m_customParams7;
    QVector4D m_customParams8;
    QColor m_customColor1 = Qt::white;
    QColor m_customColor2 = Qt::white;
    QColor m_customColor3 = Qt::white;
    QColor m_customColor4 = Qt::white;
    QColor m_customColor5 = Qt::white;
    QColor m_customColor6 = Qt::white;
    QColor m_customColor7 = Qt::white;
    QColor m_customColor8 = Qt::white;
    QColor m_customColor9 = Qt::white;
    QColor m_customColor10 = Qt::white;
    QColor m_customColor11 = Qt::white;
    QColor m_customColor12 = Qt::white;
    QColor m_customColor13 = Qt::white;
    QColor m_customColor14 = Qt::white;
    QColor m_customColor15 = Qt::white;
    QColor m_customColor16 = Qt::white;

    // Labels texture (binding 1)
    QImage m_labelsImage;
    QImage m_transparentFallbackImage;
    std::unique_ptr<QRhiTexture> m_labelsTexture;
    std::unique_ptr<QRhiSampler> m_labelsSampler;
    bool m_labelsTextureDirty = false;

    // Audio spectrum texture (binding 6)
    QVector<float> m_audioSpectrum;
    std::unique_ptr<QRhiTexture> m_audioSpectrumTexture;
    std::unique_ptr<QRhiSampler> m_audioSpectrumSampler;
    bool m_audioSpectrumDirty = false;

    // User texture slots (bindings 7-10)
    static constexpr int kMaxUserTextures = 4;
    std::array<QImage, kMaxUserTextures> m_userTextureImages;
    std::array<std::unique_ptr<QRhiTexture>, kMaxUserTextures> m_userTextures;
    std::array<std::unique_ptr<QRhiSampler>, kMaxUserTextures> m_userTextureSamplers;
    std::array<QString, kMaxUserTextures> m_userTextureWraps;
    std::array<bool, kMaxUserTextures> m_userTextureDirty = {};

    // Depth buffer (binding 12) — opt-in via metadata "depthBuffer": true
    bool m_useDepthBuffer = false;
    std::unique_ptr<QRhiTexture> m_depthTexture; // R32F, readable at binding 12
    std::unique_ptr<QRhiSampler> m_depthSampler;

    // Desktop wallpaper texture (binding 11) — opt-in via metadata "wallpaper": true
    bool m_useWallpaper = false;
    QImage m_wallpaperImage;
    std::unique_ptr<QRhiTexture> m_wallpaperTexture;
    std::unique_ptr<QRhiSampler> m_wallpaperSampler;
    bool m_wallpaperDirty = false;
};

/** Result of warmShaderBakeCacheForPaths for reporting to UI (e.g. shaderCompilationFinished). */
struct WarmShaderBakeResult
{
    bool success = false;
    QString errorMessage;
};

/**
 * Pre-load cache warming: load, bake, and insert shaders for the given paths into the
 * shared bake cache. Safe to call from any thread (e.g. after ShaderRegistry::refresh()).
 * @return success and error message (e.g. from QShaderBaker) for UI reporting
 */
PLASMAZONES_RENDERING_EXPORT WarmShaderBakeResult warmShaderBakeCacheForPaths(const QString& vertexPath,
                                                                              const QString& fragmentPath);

} // namespace PlasmaZones
