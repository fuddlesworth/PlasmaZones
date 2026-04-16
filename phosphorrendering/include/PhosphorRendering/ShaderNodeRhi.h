// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorRendering/phosphorrendering_export.h>

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
#include <map>
#include <memory>

#include <rhi/qrhi.h>

namespace PhosphorRendering {

// Forward declare constants used in member declarations
static constexpr int kMaxBufferPasses = 4;
static constexpr int kMaxUserTextures = 4;
static constexpr int kMaxCustomParams = 8;
static constexpr int kMaxCustomColors = 16;

// ── Consumer binding range (for setExtraBinding) ────────────────────────
// Library-managed bindings: 0 (UBO), 2-5 (multipass buffers), 6 (audio),
// 7-10 (user textures), 11 (wallpaper), 12 (depth). Assigning any of those
// via setExtraBinding() would duplicate SRB entries and is rejected at
// runtime. Binding 1 is the one "free-in-the-gap" slot; 13..31 are free as
// well.
//
// PlasmaZones uses binding 1 for its zone-labels texture by convention.
// Other consumers that interoperate with PlasmaZones should pick from
// 13..kMaxConsumerBinding to avoid a per-instance overwrite.
constexpr int kFirstFreeConsumerBinding = 1; ///< First slot usable via setExtraBinding()
/// Highest portable SRB binding. 31 matches Qt RHI's minimum guarantee
/// (minMaxShaderResourceBindingCount) across all backends — Vulkan/D3D11/
/// Metal/OpenGL all advertise at least 32 bindings. Going higher risks
/// pipeline-creation failure on conservative drivers.
constexpr int kMaxConsumerBinding = 31;
constexpr int kReservedBindingRangeStart = 2; ///< First library-managed binding above 0
constexpr int kReservedBindingRangeEnd = 12; ///< Last library-managed binding

/// @return true if @p binding is usable by consumers via setExtraBinding().
constexpr bool isConsumerBinding(int binding) noexcept
{
    return binding >= kFirstFreeConsumerBinding && binding <= kMaxConsumerBinding
        && (binding < kReservedBindingRangeStart || binding > kReservedBindingRangeEnd);
}

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
 *
 * @par Threading contract
 * Setters on **this class** (ShaderNodeRhi — setTime, setResolution, setCustomParams,
 * setExtraBinding, etc.) must be called from QQuickItem::updatePaintNode() during
 * the scene graph sync phase — the GUI thread is blocked and the render thread is
 * idle at that point. Calling these setters outside updatePaintNode() is a data
 * race with prepare()/render() on the render thread. Only invalidateItem() is
 * safe to call from the GUI thread outside the sync phase (it is the only flag
 * exposed as std::atomic).
 *
 * (Setters on the sibling ShaderEffect class are a different story — those run
 * on the GUI thread and stage their changes into ShaderEffect's own members, to
 * be pushed down to this node during the next sync phase.)
 *
 * @par Extra binding lifetime (CRITICAL)
 * The consumer-owned QRhiTexture/QRhiSampler pointers passed to setExtraBinding()
 * are stored as raw pointers and re-bound on every prepare() until
 * removeExtraBinding() is called. If the consumer drops the texture/sampler
 * (e.g. by calling unique_ptr::reset() or letting it fall out of scope) without
 * first calling removeExtraBinding(), the next prepare() will dereference a
 * dangling pointer — undefined behaviour, typically a crash inside the RHI
 * backend. ALWAYS call removeExtraBinding(binding) before destroying the
 * underlying QRhiTexture/QRhiSampler.
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
    /** Access the currently-installed uniform extension (may be nullptr). */
    std::shared_ptr<PhosphorShell::IUniformExtension> uniformExtension() const
    {
        return m_uniformExtension;
    }

    // ── Timing ─────────────────────────────────────────────────────────
    void setTime(double time);
    void setTimeDelta(float delta);
    void setFrame(int frame);
    void setResolution(float width, float height);
    void setMousePosition(const QPointF& pos);

    // ── Custom Parameters (indexed API) ────────────────────────────────
    void setCustomParams(int index, const QVector4D& params);
    void setCustomColor(int index, const QColor& color);

    // ── App Fields (consumer escape hatch in BaseUniforms) ─────────────
    /**
     * @brief Write the consumer's two int slots inside BaseUniforms (offsets 88, 92).
     *
     * See PhosphorShell::BaseUniforms for the full escape-hatch rationale.
     * Updating these fields is cheap: the library uploads only the 8-byte
     * K_APP_FIELDS region rather than the full scene header.
     */
    void setAppField0(int value);
    void setAppField1(int value);

    // ── Extra Bindings (consumer-managed texture bindings) ─────────────
    /**
     * @brief Bind a consumer-owned texture/sampler at the given binding number.
     *
     * @warning The texture and sampler are stored as raw pointers and reused
     * across frames. The consumer MUST call removeExtraBinding(binding) before
     * destroying the underlying QRhiTexture/QRhiSampler — failing to do so
     * leaves a dangling pointer that will be dereferenced inside the RHI on
     * the next prepare() (UB, typically a crash).
     *
     * @return true on success; false if `binding` collides with a library-
     * managed slot (0, 2-12) or falls outside the supported range. A `true`
     * return for an identical (binding, texture, sampler) triple is a no-op —
     * the SRB/pipeline is NOT rebuilt when nothing actually changed.
     */
    bool setExtraBinding(int binding, QRhiTexture* texture, QRhiSampler* sampler);
    /// @return true if a binding existed at that slot and was removed.
    bool removeExtraBinding(int binding);

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

    /// Normalize wrap mode string to "clamp" or "repeat" (static helper,
    /// safe to call from any thread — operates on its arguments only).
    static QString normalizeWrapMode(const QString& wrap);
    /// Normalize filter mode string to "nearest", "linear", or "mipmap".
    static QString normalizeFilterMode(const QString& filter);

protected:
    /**
     * @brief Thread-safe QRhi accessor.
     * @return The active QRhi for the owning window, or nullptr when the item
     * has been invalidated or has no window. Subclasses that override prepare()
     * should route through this helper rather than `commandBuffer()->rhi()` so
     * the post-invalidateItem() guard stays uniform.
     */
    QRhi* safeRhi() const;

private:
    bool ensurePipeline();
    bool ensureBufferPipeline();
    bool ensureBufferTarget();
    bool ensureDummyChannelResources(QRhi* rhi);
    bool ensureBufferSampler(QRhi* rhi, int index);
    void syncBaseUniforms();
    void uploadDirtyTextures(QRhi* rhi, QRhiCommandBuffer* cb);
    /**
     * Append the extension region to a resource update batch.
     * @pre m_uniformExtension && m_uniformExtension->extensionSize() > 0
     * Reuses m_extensionStaging to avoid per-frame render-thread allocations.
     */
    void uploadExtensionToUbo(QRhiResourceUpdateBatch* batch);
    void releaseRhiResources();
    void appendUserTextureBindings(QVector<QRhiShaderResourceBinding>& bindings) const;
    void appendWallpaperBinding(QVector<QRhiShaderResourceBinding>& bindings) const;
    void appendDepthBinding(QVector<QRhiShaderResourceBinding>& bindings) const;
    void appendExtraBindings(QVector<QRhiShaderResourceBinding>& bindings) const;
    void appendAudioBinding(QVector<QRhiShaderResourceBinding>& bindings) const;
    /// UBO at binding 0 + consumer-managed extra bindings (e.g. binding 1).
    void appendUboAndExtraBindings(QVector<QRhiShaderResourceBinding>& bindings) const;
    /// Bindings 6 (audio), 7-10 (user textures), 11 (wallpaper), 12 (depth) —
    /// shared trailer between buffer-pass and image-pass SRBs.
    void appendCommonTrailerBindings(QVector<QRhiShaderResourceBinding>& bindings) const;
    void resetAllBindingsAndPipelines();
    void bakeBufferShaders();
    QString loadAndExpandShader(const QString& path, QString* outError);

    QQuickItem* m_item = nullptr;
    std::atomic<bool> m_itemValid{true};

    // ── Uniform Extension ──────────────────────────────────────────────
    std::shared_ptr<PhosphorShell::IUniformExtension> m_uniformExtension;
    /// Reused staging buffer for extension uploads — resized only when the
    /// extension's reported size changes (avoids a render-thread QByteArray
    /// allocation every frame that the extension is dirty).
    QByteArray m_extensionStaging;

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
    bool m_appFieldsDirty = false; ///< Only appField0/appField1 changed (8-byte upload, not full scene header)
    bool m_didFullUploadOnce = false;
    /// Epoch-ms of the last iDate recomputation. Throttles
    /// QDateTime::currentDateTime() to once per second during mouse-driven
    /// scene-header churn (iDate only advances at 1 Hz anyway).
    qint64 m_lastDateRefreshMs = 0;

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
    // std::map (ordered) so SRB construction produces a deterministic binding
    // order regardless of insertion/erasure history. An unordered_map can
    // produce different iteration orders after erasures, which Qt RHI
    // backends may hash into different pipeline layout signatures.
    std::map<int, ExtraBinding> m_extraBindings;
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
