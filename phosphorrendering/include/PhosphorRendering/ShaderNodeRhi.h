// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorrendering_export.h>

#include <PhosphorShell/BaseUniforms.h>
#include <PhosphorShell/IUniformExtension.h>

#include <QColor>
#include <QImage>
#include <QMatrix4x4>
#include <QPointF>
#include <QQuickItem>
#include <QSGRenderNode>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector4D>

#include <array>
#include <atomic>
#include <memory>
#include <unordered_map>

#include <rhi/qrhi.h>

namespace PhosphorRendering {

// Forward declare constants used in member declarations
static constexpr int kMaxBufferPasses = 4;
static constexpr int kMaxUserTextures = 4;
static constexpr int kMaxCustomParams = 8;
static constexpr int kMaxCustomColors = 16;

/**
 * @brief QSGRenderNode for fullscreen-quad shader rendering via Qt RHI (Vulkan / OpenGL)
 *
 * Generalized render node extracted from PlasmaZones' ZoneShaderNodeRhi.
 * Manages a Shadertoy-compatible UBO (BaseUniforms), multipass buffer system,
 * texture bindings (audio, user, wallpaper, depth), and shader baking.
 *
 * Application-specific UBO data is appended via IUniformExtension.
 * Application-specific texture bindings use setExtraBinding() / removeExtraBinding().
 *
 * Uses QRhi and QShaderBaker (runtime SPIR-V + GLSL 330 bake). Requires Qt 6.6+
 * (commandBuffer(), renderTarget()).
 */
class PHOSPHORRENDERING_EXPORT ShaderNodeRhi : public QSGRenderNode
{
public:
    explicit ShaderNodeRhi(QQuickItem* item);
    ~ShaderNodeRhi() override;

    /**
     * @brief Notify the render node that its owning item is being destroyed.
     *
     * Called from the owning QQuickItem destructor on the GUI thread.
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

    // ── Uniform Extension ──────────────────────────────────────────────
    void setUniformExtension(std::shared_ptr<PhosphorShell::IUniformExtension> extension);

    // ── Timing ─────────────────────────────────────────────────────────
    void setTime(double time);
    void setTimeDelta(float delta);
    void setFrame(int frame);
    void setResolution(float width, float height);
    void setMousePosition(const QPointF& pos);

    // ── Custom Parameters (indexed API) ────────────────────────────────
    void setCustomParams(int index, const QVector4D& params);
    void setCustomColor(int index, const QColor& color);

    // ── App Fields (consumer-defined integers in BaseUniforms) ─────────
    void setAppField0(int value);
    void setAppField1(int value);

    // ── Extra Bindings (consumer-managed texture bindings) ─────────────
    void setExtraBinding(int binding, QRhiTexture* texture, QRhiSampler* sampler);
    void removeExtraBinding(int binding);

    // ── Textures ───────────────────────────────────────────────────────
    void setAudioSpectrum(const QVector<float>& spectrum);
    void setUserTexture(int slot, const QImage& image);
    void setUserTextureWrap(int slot, const QString& wrap);
    void setWallpaperTexture(const QImage& image);
    void setUseWallpaper(bool use);
    void setUseDepthBuffer(bool use);

    // ── Multi-pass Buffers ─────────────────────────────────────────────
    void setBufferShaderPath(const QString& path);
    void setBufferShaderPaths(const QStringList& paths);
    void setBufferFeedback(bool enable);
    void setBufferScale(qreal scale);
    void setBufferWrap(const QString& wrap);
    void setBufferWraps(const QStringList& wraps);
    void setBufferFilter(const QString& filter);
    void setBufferFilters(const QStringList& filters);

    // ── Shader Loading ─────────────────────────────────────────────────
    bool loadVertexShader(const QString& path);
    bool loadFragmentShader(const QString& path);
    void setVertexShaderSource(const QString& source);
    void setFragmentShaderSource(const QString& source);
    bool isShaderReady() const;
    QString shaderError() const;
    void invalidateShader();
    void invalidateUniforms();

    // ── Include Paths ──────────────────────────────────────────────────
    void setShaderIncludePaths(const QStringList& paths);

private:
    /** Thread-safe QRhi accessor: returns nullptr when m_item is invalidated or has no window. */
    QRhi* safeRhi() const;

    bool ensurePipeline();
    bool ensureBufferPipeline();
    bool ensureBufferTarget();
    bool ensureDummyChannelResources(QRhi* rhi);
    bool ensureBufferSampler(QRhi* rhi, int index);
    void syncBaseUniforms();
    void uploadDirtyTextures(QRhi* rhi, QRhiCommandBuffer* cb);
    void releaseRhiResources();
    void appendUserTextureBindings(QVector<QRhiShaderResourceBinding>& bindings) const;
    void appendWallpaperBinding(QVector<QRhiShaderResourceBinding>& bindings) const;
    void appendDepthBinding(QVector<QRhiShaderResourceBinding>& bindings) const;
    void appendExtraBindings(QVector<QRhiShaderResourceBinding>& bindings) const;
    void resetAllBindingsAndPipelines();
    void bakeBufferShaders();
    QString loadAndExpandShader(const QString& path, QString* outError);

    /// Normalize wrap/filter mode strings
    static QString normalizeWrapMode(const QString& wrap);
    static QString normalizeFilterMode(const QString& filter);

    QQuickItem* m_item = nullptr;
    std::atomic<bool> m_itemValid{true};

    // ── Uniform Extension ──────────────────────────────────────────────
    std::shared_ptr<PhosphorShell::IUniformExtension> m_uniformExtension;

    // ── Shader Include Paths ───────────────────────────────────────────
    QStringList m_shaderIncludePaths;

    // ── RHI Core Resources ─────────────────────────────────────────────
    std::unique_ptr<QRhiBuffer> m_vbo;
    std::unique_ptr<QRhiBuffer> m_ubo;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    QShader m_vertexShader;
    QShader m_fragmentShader;
    QVector<quint32> m_renderPassFormat;

    // ── Multi-pass: buffer pass(es) (optional). Up to 4 paths. ────────
    QString m_bufferPath;
    QStringList m_bufferPaths;
    bool m_bufferFeedback = false;
    qreal m_bufferScale = 1.0;
    std::array<QString, kMaxBufferPasses> m_bufferWraps = {QStringLiteral("clamp"), QStringLiteral("clamp"),
                                                           QStringLiteral("clamp"), QStringLiteral("clamp")};
    QString m_bufferWrapDefault = QStringLiteral("clamp");
    std::array<QString, kMaxBufferPasses> m_bufferFilters = {QStringLiteral("linear"), QStringLiteral("linear"),
                                                             QStringLiteral("linear"), QStringLiteral("linear")};
    QString m_bufferFilterDefault = QStringLiteral("linear");
    QString m_bufferFragmentShaderSource;
    QShader m_bufferFragmentShader;
    qint64 m_bufferMtime = 0;
    bool m_bufferShaderReady = false;
    bool m_bufferShaderDirty = true;
    int m_bufferShaderRetries = 0;
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
    bool m_bufferFeedbackCleared = false;

    // Multi-buffer mode (2-4 passes): per-pass resources
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
    int m_multiBufferShaderRetries = 0;
    // Dummy 1x1 texture for iChannel0 when multipass is set but buffer not yet created
    std::unique_ptr<QRhiTexture> m_dummyChannelTexture;
    std::unique_ptr<QRhiSampler> m_dummyChannelSampler;
    bool m_dummyChannelTextureNeedsUpload = false;

    // ── Shader Sources ─────────────────────────────────────────────────
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
    bool m_timeHiDirty = true; ///< iTimeHi wrap offset changed (rare)
    bool m_extensionDirty = true; ///< Extension UBO data changed (checked via IUniformExtension::isDirty())
    bool m_sceneDataDirty = true; ///< Scene header (resolution, mouse, date, params) changed
    bool m_didFullUploadOnce = false;

    // ── Base Uniforms ──────────────────────────────────────────────────
    PhosphorShell::BaseUniforms m_baseUniforms = {};

    // Full-precision elapsed seconds (double). Split into iTime (wrapped lo) +
    // iTimeHi (wrap offset) at upload.
    double m_time = 0.0;
    float m_timeDelta = 0.0f;
    int m_frame = 0;
    float m_timeHi = 0.0f; // Cached iTimeHi for wrap-offset change detection
    float m_width = 0.0f;
    float m_height = 0.0f;
    QPointF m_mousePosition;

    // ── Custom Parameters (indexed) ────────────────────────────────────
    std::array<QVector4D, kMaxCustomParams> m_customParams;
    std::array<QColor, kMaxCustomColors> m_customColors;

    // ── Extra Bindings (consumer-managed) ──────────────────────────────
    struct ExtraBinding
    {
        QRhiTexture* texture = nullptr;
        QRhiSampler* sampler = nullptr;
    };
    std::unordered_map<int, ExtraBinding> m_extraBindings;
    bool m_extraBindingsDirty = false;

    // ── 1x1 Transparent Fallback ───────────────────────────────────────
    QImage m_transparentFallbackImage;

    // ── Audio spectrum texture (binding 6) ─────────────────────────────
    QVector<float> m_audioSpectrum;
    std::unique_ptr<QRhiTexture> m_audioSpectrumTexture;
    std::unique_ptr<QRhiSampler> m_audioSpectrumSampler;
    bool m_audioSpectrumDirty = false;

    // ── User texture slots (bindings 7-10) ─────────────────────────────
    std::array<QImage, kMaxUserTextures> m_userTextureImages;
    std::array<std::unique_ptr<QRhiTexture>, kMaxUserTextures> m_userTextures;
    std::array<std::unique_ptr<QRhiSampler>, kMaxUserTextures> m_userTextureSamplers;
    std::array<QString, kMaxUserTextures> m_userTextureWraps;
    std::array<bool, kMaxUserTextures> m_userTextureDirty = {};

    // ── Depth buffer (binding 12) ──────────────────────────────────────
    bool m_useDepthBuffer = false;
    bool m_depthMultiBufferWarned = false;
    std::unique_ptr<QRhiTexture> m_depthTexture;
    std::unique_ptr<QRhiSampler> m_depthSampler;

    // ── Desktop wallpaper texture (binding 11) ─────────────────────────
    bool m_useWallpaper = false;
    QImage m_wallpaperImage;
    std::unique_ptr<QRhiTexture> m_wallpaperTexture;
    std::unique_ptr<QRhiSampler> m_wallpaperSampler;
    bool m_wallpaperDirty = false;
};

/** Result of warmShaderBakeCacheForPaths for reporting to UI. */
struct WarmShaderBakeResult
{
    bool success = false;
    QString errorMessage;
};

/**
 * Pre-load cache warming: load, bake, and insert shaders for the given paths into the
 * shared bake cache. Safe to call from any thread.
 * @param vertexPath    Path to the vertex shader file
 * @param fragmentPath  Path to the fragment shader file
 * @param includePaths  Directories to search for #include directives
 * @return success and error message for UI reporting
 */
PHOSPHORRENDERING_EXPORT WarmShaderBakeResult warmShaderBakeCacheForPaths(const QString& vertexPath,
                                                                          const QString& fragmentPath,
                                                                          const QStringList& includePaths = {});

} // namespace PhosphorRendering
