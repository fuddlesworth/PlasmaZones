// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorRendering/phosphorrendering_export.h>

#include <PhosphorShaders/ShaderEntryPoint.h>

#include <QColor>
#include <QImage>
#include <QMutex>
#include <QPointF>
#include <QQuickItem>
#include <QSizeF>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QVector4D>

#include <QPointer>
#include <array>
#include <atomic>
#include <memory>

QT_BEGIN_NAMESPACE
class QSGNode;
QT_END_NAMESPACE

namespace PhosphorShaders {
class IUniformExtension;
}

namespace PhosphorRendering {

class ShaderNodeRhi;

/// Public mirror of `ShaderNodeRhi::kMaxUserTextures` (4 user-texture slots
/// at SRB bindings 7..10). Re-declared here so member-array sizes can use
/// the named constant without pulling in the heavy rhi/qrhi.h transitive
/// chain that ShaderNodeRhi.h carries. Kept in sync via a `static_assert`
/// in shadereffect.cpp.
inline constexpr int kMaxUserTextureSlots = 4;

/**
 * @brief QQuickItem that renders a fullscreen fragment shader via Qt RHI.
 *
 * Shadertoy-compatible: iTime, iTimeDelta, iFrame, iResolution, iMouse,
 * multipass buffers, custom parameters, and custom colors.
 *
 * Extend with IUniformExtension to append application-specific uniform data
 * after the base UBO layout.
 *
 * Consumers that want this type available in QML must register it themselves
 * with qmlRegisterType() under their own module URI. The library does not
 * ship a QML module — doing so would force every consumer onto one module
 * name and duplicate registration with subclasses (e.g. ZoneShaderItem).
 */
class PHOSPHORRENDERING_EXPORT ShaderEffect : public QQuickItem
{
    Q_OBJECT

    // ── Shadertoy-compatible animation uniforms ──────────────────────
    Q_PROPERTY(qreal iTime READ iTime WRITE setITime NOTIFY iTimeChanged FINAL)
    Q_PROPERTY(qreal iTimeDelta READ iTimeDelta WRITE setITimeDelta NOTIFY iTimeDeltaChanged FINAL)
    Q_PROPERTY(int iFrame READ iFrame WRITE setIFrame NOTIFY iFrameChanged FINAL)
    Q_PROPERTY(QSizeF iResolution READ iResolution WRITE setIResolution NOTIFY iResolutionChanged FINAL)
    Q_PROPERTY(QPointF iMouse READ iMouse WRITE setIMouse NOTIFY iMouseChanged FINAL)
    Q_PROPERTY(bool isReversed READ isReversed WRITE setIsReversed NOTIFY isReversedChanged FINAL)

    /// Auto-tick mode. When true, this item hooks the host QQuickWindow's
    /// per-frame signal and advances iTime / iTimeDelta / iFrame on every
    /// rendered frame. The Shadertoy-style uniforms then "just work" for
    /// QML callers — no FrameAnimation / Timer plumbing required.
    ///
    /// When false (default) the iTime/iTimeDelta/iFrame Q_PROPERTYs stay
    /// fully under the caller's control. This is what kwin-effect's
    /// SurfaceAnimator and phosphor-animation's transition shaders use:
    /// they drive iTime by hand to map the animation curve through the
    /// shader without committing to wall-clock pacing.
    ///
    /// Toggling at runtime is supported. While true, the manual setters
    /// for iTime/iTimeDelta/iFrame are additive: the next frame's tick
    /// adds the wall-clock delta to whatever value is currently stored,
    /// so a manual write is preserved as a baseline rather than
    /// overwritten. iFrame is incremented by 1 per tick.
    Q_PROPERTY(bool playing READ isPlaying WRITE setPlaying NOTIFY playingChanged FINAL)

    // ── Shader source ────────────────────────────────────────────────
    Q_PROPERTY(QUrl shaderSource READ shaderSource WRITE setShaderSource NOTIFY shaderSourceChanged FINAL)
    Q_PROPERTY(QUrl vertexShaderUrl READ vertexShaderUrl WRITE setVertexShaderUrl NOTIFY vertexShaderUrlChanged FINAL)
    Q_PROPERTY(QVariantMap shaderParams READ shaderParams WRITE setShaderParams NOTIFY shaderParamsChanged)
    Q_PROPERTY(QString paramPreamble READ paramPreamble WRITE setParamPreamble NOTIFY paramPreambleChanged)
    Q_PROPERTY(QQuickItem* sourceItem READ sourceItem WRITE setSourceItem NOTIFY sourceItemChanged FINAL)

    // ── Multipass ────────────────────────────────────────────────────
    Q_PROPERTY(
        QString bufferShaderPath READ bufferShaderPath WRITE setBufferShaderPath NOTIFY bufferShaderPathChanged FINAL)
    Q_PROPERTY(QStringList bufferShaderPaths READ bufferShaderPaths WRITE setBufferShaderPaths NOTIFY
                   bufferShaderPathsChanged FINAL)
    Q_PROPERTY(bool bufferFeedback READ bufferFeedback WRITE setBufferFeedback NOTIFY bufferFeedbackChanged FINAL)
    Q_PROPERTY(qreal bufferScale READ bufferScale WRITE setBufferScale NOTIFY bufferScaleChanged FINAL)
    Q_PROPERTY(QString bufferWrap READ bufferWrap WRITE setBufferWrap NOTIFY bufferWrapChanged FINAL)
    Q_PROPERTY(QStringList bufferWraps READ bufferWraps WRITE setBufferWraps NOTIFY bufferWrapsChanged FINAL)
    Q_PROPERTY(QString bufferFilter READ bufferFilter WRITE setBufferFilter NOTIFY bufferFilterChanged FINAL)
    Q_PROPERTY(QStringList bufferFilters READ bufferFilters WRITE setBufferFilters NOTIFY bufferFiltersChanged FINAL)

    // ── Custom parameters (32 floats in 8 vec4s) ────────────────────
    Q_PROPERTY(QVector4D customParams1 READ customParams1 WRITE setCustomParams1 NOTIFY customParamsChanged FINAL)
    Q_PROPERTY(QVector4D customParams2 READ customParams2 WRITE setCustomParams2 NOTIFY customParamsChanged FINAL)
    Q_PROPERTY(QVector4D customParams3 READ customParams3 WRITE setCustomParams3 NOTIFY customParamsChanged FINAL)
    Q_PROPERTY(QVector4D customParams4 READ customParams4 WRITE setCustomParams4 NOTIFY customParamsChanged FINAL)
    Q_PROPERTY(QVector4D customParams5 READ customParams5 WRITE setCustomParams5 NOTIFY customParamsChanged FINAL)
    Q_PROPERTY(QVector4D customParams6 READ customParams6 WRITE setCustomParams6 NOTIFY customParamsChanged FINAL)
    Q_PROPERTY(QVector4D customParams7 READ customParams7 WRITE setCustomParams7 NOTIFY customParamsChanged FINAL)
    Q_PROPERTY(QVector4D customParams8 READ customParams8 WRITE setCustomParams8 NOTIFY customParamsChanged FINAL)

    // ── Custom colors (16 color slots) ───────────────────────────────
    Q_PROPERTY(QColor customColor1 READ customColor1 WRITE setCustomColor1 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QColor customColor2 READ customColor2 WRITE setCustomColor2 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QColor customColor3 READ customColor3 WRITE setCustomColor3 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QColor customColor4 READ customColor4 WRITE setCustomColor4 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QColor customColor5 READ customColor5 WRITE setCustomColor5 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QColor customColor6 READ customColor6 WRITE setCustomColor6 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QColor customColor7 READ customColor7 WRITE setCustomColor7 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QColor customColor8 READ customColor8 WRITE setCustomColor8 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QColor customColor9 READ customColor9 WRITE setCustomColor9 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QColor customColor10 READ customColor10 WRITE setCustomColor10 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QColor customColor11 READ customColor11 WRITE setCustomColor11 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QColor customColor12 READ customColor12 WRITE setCustomColor12 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QColor customColor13 READ customColor13 WRITE setCustomColor13 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QColor customColor14 READ customColor14 WRITE setCustomColor14 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QColor customColor15 READ customColor15 WRITE setCustomColor15 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QColor customColor16 READ customColor16 WRITE setCustomColor16 NOTIFY customColorsChanged FINAL)

    // ── Textures ─────────────────────────────────────────────────────
    Q_PROPERTY(QVariant audioSpectrum READ audioSpectrumVariant WRITE setAudioSpectrumVariant NOTIFY
                   audioSpectrumChanged FINAL)
    Q_PROPERTY(
        QImage wallpaperTexture READ wallpaperTexture WRITE setWallpaperTexture NOTIFY wallpaperTextureChanged FINAL)
    Q_PROPERTY(bool useWallpaper READ useWallpaper WRITE setUseWallpaper NOTIFY useWallpaperChanged FINAL)
    Q_PROPERTY(bool useDepthBuffer READ useDepthBuffer WRITE setUseDepthBuffer NOTIFY useDepthBufferChanged FINAL)

    // ── Status ───────────────────────────────────────────────────────
    Q_PROPERTY(Status status READ status NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString errorLog READ errorLog NOTIFY errorLogChanged FINAL)

public:
    /**
     * @brief Shader loading and compilation status
     */
    enum class Status {
        Null, ///< No shader loaded
        Loading, ///< Shader is being loaded/compiled
        Ready, ///< Shader compiled successfully
        Error, ///< Shader compilation failed
    };
    Q_ENUM(Status)

    /// Hard ceiling on the per-axis SVG rasterisation dimension applied by
    /// `setShaderParams` to the `uTexture<N>_svgSize` parameter. Exposed so
    /// consumers (UI sliders, validators, tests) can mirror the clamp range
    /// without hardcoding the value. The setter performs
    /// `qBound(64, requested, kMaxSvgDimension)`; values outside the range
    /// are silently clamped, not rejected.
    static constexpr int kMaxSvgDimension = 2048;

    /// Hard cap on the byte budget for a single rasterised SVG (16 MiB —
    /// matches RGBA8 at `kMaxSvgDimension` × `kMaxSvgDimension`). When a
    /// near-square doc would exceed this even after the per-axis clamp,
    /// the rasterisation is downscaled proportionally and a warning is
    /// logged. Exposed for the same mirror-the-cap reason as
    /// `kMaxSvgDimension`.
    static constexpr qint64 kMaxSvgPixelBytes = 16LL * 1024 * 1024;

    explicit ShaderEffect(QQuickItem* parent = nullptr);
    ~ShaderEffect() override;

    // ── Uniform extension ────────────────────────────────────────────

    /**
     * @brief Attach a uniform extension that appends custom data after BaseUniforms.
     *
     * Stored on this item and pushed down to the render node in
     * syncBasePropertiesToNode(). `virtual` so a subclass that owns its own
     * extension (e.g. ZoneShaderItem) can intercept stray assignments and
     * preserve its internal extension contract instead of having the call
     * silently no-op.
     */
    virtual void setUniformExtension(std::shared_ptr<PhosphorShaders::IUniformExtension> extension);
    std::shared_ptr<PhosphorShaders::IUniformExtension> uniformExtension() const;

    // ── Shadertoy uniforms ───────────────────────────────────────────

    qreal iTime() const
    {
        return m_iTime;
    }
    void setITime(qreal time);

    qreal iTimeDelta() const
    {
        return m_iTimeDelta;
    }
    void setITimeDelta(qreal delta);

    int iFrame() const
    {
        return m_iFrame;
    }
    void setIFrame(int frame);

    bool isPlaying() const
    {
        return m_playing;
    }
    void setPlaying(bool playing);

    bool isReversed() const
    {
        return m_isReversed;
    }
    /// Direction signal forwarded to ShaderNodeRhi → BaseUniforms::iIsReversed.
    /// SurfaceAnimator pushes this on every leg attach (false for show, true
    /// for hide). Symmetric shaders ignore the value; asymmetric shaders
    /// branch on it (see canonical animation_uniforms.glsl docs).
    void setIsReversed(bool reverse);

    QSizeF iResolution() const
    {
        return m_iResolution;
    }
    void setIResolution(const QSizeF& resolution);

    QPointF iMouse() const
    {
        return m_iMouse;
    }
    void setIMouse(const QPointF& mouse);

    // ── Shader source ────────────────────────────────────────────────

    QUrl shaderSource() const
    {
        return m_shaderSource;
    }
    void setShaderSource(const QUrl& source);

    QUrl vertexShaderUrl() const
    {
        return m_vertexShaderUrl;
    }
    void setVertexShaderUrl(const QUrl& source);

    QVariantMap shaderParams() const
    {
        return m_shaderParams;
    }
    /// Push a parameter map onto the shader. Recognised keys:
    ///   • `customParams<N>_<x|y|z|w>` (1-based) — float vec4 sub-slots
    ///   • `customColor<N>` (1-based) — color vec4 slots
    ///   • `uTexture<N>` (0-based, 0..3) — file path for the user-texture
    ///     sampler at SRB binding 7..10 / GLSL `uTexture<N>`
    ///   • `uTexture<N>_wrap` — wrap mode string ("clamp" / "repeat" /
    ///     "mirror"); ignored if no companion `uTexture<N>` resolves
    ///   • `uTexture<N>_svgSize` — SVG rasterise max-axis dimension
    ///     (clamped 64..2048; ignored for bitmap formats). The cap is
    ///     exposed as `ShaderEffect::kMaxSvgDimension` for callers that
    ///     want to mirror the clamp without hardcoding the value, and
    ///     the per-rasterisation byte budget is exposed as
    ///     `ShaderEffect::kMaxSvgPixelBytes` (a near-square doc whose
    ///     RGBA8 size would exceed the budget is downscaled
    ///     proportionally with a warning).
    ///
    /// **Trust boundary.** `uTexture<N>` paths are passed verbatim to
    /// `QImage::load` / `QSvgRenderer::load`. This class does NOT
    /// sanitise traversal segments or enforce absolute-path-only — the
    /// caller is the trust boundary.
    /// `AnimationShaderRegistry::translateAnimationParams` already
    /// resolves and traversal-checks pack-default paths at scan time;
    /// the kwin-effect's `m_textureCache` lookup keys absolute paths.
    /// A direct caller (e.g. tests, custom QML embedding) that
    /// forwards untrusted strings into `params["uTexture<N>"]` MUST
    /// pre-resolve and traversal-check before calling.
    ///
    /// **SVG rasterisation cost.** When the params map contains a
    /// `uTexture<N>` whose path resolves to an `.svg` / `.svgz` file
    /// (and the path or `uTexture<N>_svgSize` differs from the cached
    /// state for that slot), this method calls `QSvgRenderer` and
    /// `QPainter` synchronously on the calling thread to rasterise the
    /// document up to the clamped dimension. The cost scales with
    /// `kMaxSvgPixelBytes` in the worst case. Hot-path callers (e.g.
    /// per-frame parameter pushes) should pre-warm via
    /// `loadUserTextureFile(path, svgMaxDim)` from a worker thread (it's
    /// thread-safe per Qt docs: each call constructs its own
    /// QSvgRenderer / QImage instance with no shared mutable state)
    /// and call `setUserTexture(slot, image)` on the GUI thread to
    /// install the result. Bitmap formats are loaded via `QImage` and
    /// carry the same synchronous-IO caveat but no rasterisation cost.
    ///
    /// **Subclass contract.** Overrides MUST chain to
    /// `ShaderEffect::setShaderParams(params)` (or replicate the full
    /// trust-boundary parse, including uTexture* / *_wrap /
    /// *_svgSize). Skipping the base call leaves user-texture paths
    /// uninterpreted and pinned to whatever the previous parse set —
    /// silent stale samplers across reloads.
    virtual void setShaderParams(const QVariantMap& params);

    /// Static helper that loads a user-texture file (PNG/JPG/etc. via
    /// QImage; SVG/SVGZ via QSvgRenderer rasterised at @p svgMaxDim
    /// max-axis with the same byte-budget guard as `setShaderParams`).
    /// Thread-safe: each invocation constructs its own QSvgRenderer /
    /// QImage instance — callers may invoke from a worker thread (e.g.
    /// `QtConcurrent::run`) to off-load the cost from the GUI thread.
    /// Returns a null QImage on load failure (file missing, parse
    /// error, OOM); callers should keep their prior image in that case.
    /// The result is in `QImage::Format_RGBA8888`, ready for
    /// `setUserTexture(slot, image)` upload on the GUI thread.
    static QImage loadUserTextureFile(const QString& path, int svgMaxDim);

    /// @brief Live texture-provider source bound to SRB binding 7
    ///        (`uTexture0`).
    ///
    /// When set to a non-null QQuickItem, the shader samples that item's
    /// rendered visual every frame instead of whatever QImage was uploaded
    /// via `setUserTexture(0, ...)`. This is what the SurfaceAnimator
    /// transition shaders (pixelate / dissolve / glitch) use to operate
    /// on the rendered surface — no async grab, no first-show gap, no
    /// stale snapshot.
    ///
    /// The item is forced to `layer.enabled = true` on the QML side so
    /// `QQuickItem::textureProvider()` returns the layer's provider; the
    /// scene graph allocates an FBO and re-renders the item each frame
    /// the consumer dirties it. The shader effect's parent must NOT be
    /// the source item (or any of its descendants) — sampling within the
    /// same layer creates a feedback loop where the shader's own output
    /// is captured into the next frame's texture. Park the shader effect
    /// as a sibling of the source item instead.
    ///
    /// Setting the property to nullptr unbinds the texture; subsequent
    /// frames fall back to the user-texture-0 QImage path (or the
    /// transparent fallback if neither has been set).
    QQuickItem* sourceItem() const
    {
        return m_sourceItem.data();
    }
    void setSourceItem(QQuickItem* item);

    // ── Multipass ────────────────────────────────────────────────────

    QString bufferShaderPath() const
    {
        return m_bufferShaderPath;
    }
    void setBufferShaderPath(const QString& path);

    QStringList bufferShaderPaths() const
    {
        return m_bufferShaderPaths;
    }
    void setBufferShaderPaths(const QStringList& paths);

    bool bufferFeedback() const
    {
        return m_bufferFeedback;
    }
    void setBufferFeedback(bool enable);

    qreal bufferScale() const
    {
        return m_bufferScale;
    }
    void setBufferScale(qreal scale);

    QString bufferWrap() const
    {
        return m_bufferWrap;
    }
    void setBufferWrap(const QString& wrap);

    QStringList bufferWraps() const
    {
        return m_bufferWraps;
    }
    void setBufferWraps(const QStringList& wraps);

    QString bufferFilter() const
    {
        return m_bufferFilter;
    }
    void setBufferFilter(const QString& filter);

    QStringList bufferFilters() const
    {
        return m_bufferFilters;
    }
    void setBufferFilters(const QStringList& filters);

    // ── Custom parameters (32 float slots in 8 vec4s) ────────────────

    QVector4D customParams1() const
    {
        return m_customParams[0];
    }
    void setCustomParams1(const QVector4D& params);
    QVector4D customParams2() const
    {
        return m_customParams[1];
    }
    void setCustomParams2(const QVector4D& params);
    QVector4D customParams3() const
    {
        return m_customParams[2];
    }
    void setCustomParams3(const QVector4D& params);
    QVector4D customParams4() const
    {
        return m_customParams[3];
    }
    void setCustomParams4(const QVector4D& params);
    QVector4D customParams5() const
    {
        return m_customParams[4];
    }
    void setCustomParams5(const QVector4D& params);
    QVector4D customParams6() const
    {
        return m_customParams[5];
    }
    void setCustomParams6(const QVector4D& params);
    QVector4D customParams7() const
    {
        return m_customParams[6];
    }
    void setCustomParams7(const QVector4D& params);
    QVector4D customParams8() const
    {
        return m_customParams[7];
    }
    void setCustomParams8(const QVector4D& params);

    // ── Custom colors (16 color slots) ───────────────────────────────

    QColor customColor1() const
    {
        return m_customColors[0];
    }
    void setCustomColor1(const QColor& color);
    QColor customColor2() const
    {
        return m_customColors[1];
    }
    void setCustomColor2(const QColor& color);
    QColor customColor3() const
    {
        return m_customColors[2];
    }
    void setCustomColor3(const QColor& color);
    QColor customColor4() const
    {
        return m_customColors[3];
    }
    void setCustomColor4(const QColor& color);
    QColor customColor5() const
    {
        return m_customColors[4];
    }
    void setCustomColor5(const QColor& color);
    QColor customColor6() const
    {
        return m_customColors[5];
    }
    void setCustomColor6(const QColor& color);
    QColor customColor7() const
    {
        return m_customColors[6];
    }
    void setCustomColor7(const QColor& color);
    QColor customColor8() const
    {
        return m_customColors[7];
    }
    void setCustomColor8(const QColor& color);
    QColor customColor9() const
    {
        return m_customColors[8];
    }
    void setCustomColor9(const QColor& color);
    QColor customColor10() const
    {
        return m_customColors[9];
    }
    void setCustomColor10(const QColor& color);
    QColor customColor11() const
    {
        return m_customColors[10];
    }
    void setCustomColor11(const QColor& color);
    QColor customColor12() const
    {
        return m_customColors[11];
    }
    void setCustomColor12(const QColor& color);
    QColor customColor13() const
    {
        return m_customColors[12];
    }
    void setCustomColor13(const QColor& color);
    QColor customColor14() const
    {
        return m_customColors[13];
    }
    void setCustomColor14(const QColor& color);
    QColor customColor15() const
    {
        return m_customColors[14];
    }
    void setCustomColor15(const QColor& color);
    QColor customColor16() const
    {
        return m_customColors[15];
    }
    void setCustomColor16(const QColor& color);

    // ── Indexed accessors (avoid per-slot function-pointer tables at call sites) ──
    /**
     * @brief Read a customParams slot by index.
     * @param index [0, 8). Returns a zero vector for out-of-range indices.
     */
    QVector4D customParamAt(int index) const;
    /**
     * @brief Write a customParams slot by index.
     * @param index [0, 8). Out-of-range indices are ignored.
     *
     * Emits customParamsChanged() and schedules an update only when the value
     * actually differs from the stored slot.
     */
    void setCustomParamAt(int index, const QVector4D& params);

    /**
     * @brief Read a customColor slot by index.
     * @param index [0, 16). Returns an invalid QColor for out-of-range indices.
     */
    QColor customColorAt(int index) const;
    /**
     * @brief Write a customColor slot by index.
     * @param index [0, 16). Out-of-range indices are ignored.
     */
    void setCustomColorAt(int index, const QColor& color);

    // ── Textures ─────────────────────────────────────────────────────

    QVariant audioSpectrumVariant() const;
    void setAudioSpectrumVariant(const QVariant& spectrum);
    /** Direct setter from C++ avoiding QVariantList round-trip. */
    void setAudioSpectrum(const QVector<float>& spectrum);

    /**
     * Set a user texture (slots 0-3, bindings 7-10) directly from a QImage,
     * bypassing the path-driven loader.
     *
     * Clears the per-slot cached path and resets the companion svgSize / wrap
     * settings so a subsequent params-driven load starts from a clean slot.
     *
     * Mixing-with-`setShaderParams` contract: a subsequent
     * `setShaderParams(p)` will reload the texture from `p["uTextureN"]`
     * even when `p` is byte-equal to the cached params map. The
     * intervening direct-push sets a private dirty flag that suppresses
     * the equality short-circuit on the very next call, so the cleared
     * path is honoured and the on-disk image replaces the directly-set
     * QImage. After that one re-parse, the flag is cleared and the
     * usual fast-path resumes.
     */
    void setUserTexture(int slot, const QImage& image);
    /** Set user texture wrap mode (slots 0-3). "clamp", "repeat", or "mirror". */
    void setUserTextureWrap(int slot, const QString& wrap);

    QImage wallpaperTexture() const;
    void setWallpaperTexture(const QImage& image);

    bool useWallpaper() const
    {
        return m_useWallpaper;
    }
    void setUseWallpaper(bool use);

    bool useDepthBuffer() const
    {
        return m_useDepthBuffer;
    }
    void setUseDepthBuffer(bool use);

    // ── Shader include paths ─────────────────────────────────────────

    /** Set directories to search for #include directives in shaders. */
    void setShaderIncludePaths(const QStringList& paths);
    QStringList shaderIncludePaths() const
    {
        return m_shaderIncludePaths;
    }

    // ── Parameter preamble (T1.1: generated `#define p_<id> ...`) ─────

    /** Set the generated named-parameter preamble spliced after `#version`
     *  into the fragment source at load time, so authors read a parameter by
     *  name (`p_speed`) instead of a `customParams[N].xyzw` lane. Empty (the
     *  default) is a no-op. Forwarded to the render node and folded into its
     *  bake-cache key; a change forces a reload so the new defines take. */
    void setParamPreamble(const QString& preamble);
    QString paramPreamble() const
    {
        return m_paramPreamble;
    }

    // ── Entry-point scaffold (T1.4: harness-generated `main()`) ────────

    /** Install the entry-point scaffold forwarded to the render node: when the
     *  fragment source defines no `main()`, the node prepends @p prologue and
     *  appends the generated `main()` of the first @p candidates entry function
     *  the source defines. Empty/empty (the default) disables assembly. Forces a
     *  reload so the new scaffold takes; folded into the node's bake-cache key. */
    void setEntryScaffold(const QString& prologue, const QList<PhosphorShaders::EntryCandidate>& candidates);

    // ── Status ───────────────────────────────────────────────────────

    Status status() const
    {
        return m_status.load(std::memory_order_acquire);
    }
    QString errorLog() const
    {
        return m_errorLog;
    }

    // ── Actions ──────────────────────────────────────────────────────

    /** Force reload of shader from source (callable from QML). */
    Q_INVOKABLE void reloadShader();

Q_SIGNALS:
    void iTimeChanged();
    void iTimeDeltaChanged();
    void iFrameChanged();
    void iResolutionChanged();
    void iMouseChanged();
    void isReversedChanged();
    void playingChanged();
    void shaderSourceChanged();
    void vertexShaderUrlChanged();
    void shaderParamsChanged();
    void paramPreambleChanged();
    void sourceItemChanged();
    void bufferShaderPathChanged();
    void bufferShaderPathsChanged();
    void bufferFeedbackChanged();
    void bufferScaleChanged();
    void bufferWrapChanged();
    void bufferWrapsChanged();
    void bufferFilterChanged();
    void bufferFiltersChanged();
    void customParamsChanged();
    void customColorsChanged();
    void audioSpectrumChanged();
    void wallpaperTextureChanged();
    void useWallpaperChanged();
    void useDepthBufferChanged();
    /// Emitted when status() transitions. May be raised on the render
    /// thread under Qt's threaded render loop (setStatus is called from
    /// updatePaintNode). Connect with Qt::AutoConnection or
    /// Qt::QueuedConnection only — Qt::DirectConnection from a slot on
    /// another thread will run that slot on the render thread, which is
    /// almost always wrong (V4 / QML JS / most app code is GUI-thread-
    /// only).
    void statusChanged();
    void errorLogChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void componentComplete() override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;

    /**
     * @brief Sync base properties (time, params, colors, audio, multipass, depth, wallpaper) to a render node.
     *
     * Does NOT sync user textures, uniform extension, or shader source — these differ
     * between ShaderEffect and subclasses (e.g. ZoneShaderItem).
     *
     * Called from updatePaintNode(); subclasses that override updatePaintNode should call
     * this instead of duplicating the property sync.
     */
    void syncBasePropertiesToNode(ShaderNodeRhi* node);

    /**
     * @brief Atomically read-and-clear the shader-dirty flag.
     *
     * Subclasses that override updatePaintNode() must call this to observe
     * pending reload requests from setShaderSource / setShaderIncludePaths /
     * reloadShader(). Without consuming the flag, runtime include-path
     * changes never reach the render node.
     */
    bool consumeShaderDirty()
    {
        return m_shaderDirty.exchange(false);
    }

    /**
     * @brief Factory hook for the render node updatePaintNode() will create.
     *
     * Default returns a plain ShaderNodeRhi. Subclasses that need an enriched
     * node (e.g. ZoneShaderNodeRhi for zone-aware shaders with a labels
     * texture binding) override this to return their own subclass — base
     * updatePaintNode() will then drive that node through the same
     * sync/render flow without subclasses having to duplicate it.
     *
     * Called only when oldNode is null. Ownership transfers to the scene graph.
     */
    virtual ShaderNodeRhi* createShaderNode();

    void setError(const QString& error);
    void setStatus(Status newStatus);

private:
    // ── Auto-tick (playing=true) plumbing ────────────────────────────
    /// Subscribe / unsubscribe to the host QQuickWindow's per-frame
    /// signal. Called from setPlaying and from itemChange when the item
    /// moves between windows (or is removed from any window).
    void updatePlayingConnection();
    /// Per-frame slot when playing=true. Computes the wall-clock delta
    /// from the previous frame and advances iTime / iTimeDelta / iFrame.
    void onPlayingTick();

    // ── Animation state ──────────────────────────────────────────────
    // Field order minimises padding: 8-byte (qreal/QSizeF/QPointF) members
    // grouped together, followed by 4-byte (int), trailing 1-byte bool.
    // qreal-bool-int interleaving wastes 7 bytes via alignment padding.
    qreal m_iTime = 0.0;
    qreal m_iTimeDelta = 0.0;
    qreal m_playingLastFrameSeconds = 0.0; ///< Wall-clock time of the previous tick
    QSizeF m_iResolution;
    QPointF m_iMouse;
    int m_iFrame = 0;
    bool m_isReversed = false;
    bool m_playing = false;
    QMetaObject::Connection m_playingConnection;

    // ── Shader source ────────────────────────────────────────────────
    QUrl m_shaderSource;
    QUrl m_vertexShaderUrl;
    QVariantMap m_shaderParams;
    QStringList m_shaderIncludePaths;
    QString m_paramPreamble; ///< Generated `#define p_<id> ...` block (T1.1); empty = none.
    QString m_entryPrologue; ///< T1.4 entry-point prologue forwarded to the node; empty = none.
    QList<PhosphorShaders::EntryCandidate> m_entryCandidates; ///< T1.4 entry candidates; empty = no assembly.

    // QPointer because the source item is owned by the QML scene, not
    // the ShaderEffect — a torn-down source must auto-null rather than
    // leave a dangling pointer that the per-frame textureProvider()
    // lookup would dereference.
    QPointer<QQuickItem> m_sourceItem;
    /// Single-shot warning latch for `setSourceItem(this)` rejection. A QML
    /// binding mistakenly wired to `this` would otherwise spam the journal at
    /// 60 Hz; mirrors the pattern used by `m_warnedForeignRhi` on the node.
    bool m_warnedSelfSourceItem = false;

    // ── Multipass ────────────────────────────────────────────────────
    QString m_bufferShaderPath;
    QStringList m_bufferShaderPaths;
    bool m_bufferFeedback = false;
    qreal m_bufferScale = 1.0;
    QString m_bufferWrap = QStringLiteral("clamp");
    QStringList m_bufferWraps;
    QString m_bufferFilter = QStringLiteral("linear");
    QStringList m_bufferFilters;

    // ── Custom parameters (8 vec4s, initialized to -1.0 "unset" sentinel) ──
    std::array<QVector4D, 8> m_customParams = {{
        QVector4D(-1.0f, -1.0f, -1.0f, -1.0f),
        QVector4D(-1.0f, -1.0f, -1.0f, -1.0f),
        QVector4D(-1.0f, -1.0f, -1.0f, -1.0f),
        QVector4D(-1.0f, -1.0f, -1.0f, -1.0f),
        QVector4D(-1.0f, -1.0f, -1.0f, -1.0f),
        QVector4D(-1.0f, -1.0f, -1.0f, -1.0f),
        QVector4D(-1.0f, -1.0f, -1.0f, -1.0f),
        QVector4D(-1.0f, -1.0f, -1.0f, -1.0f),
    }};

    // ── Custom colors (16 colors) ────────────────────────────────────
    std::array<QColor, 16> m_customColors = {{
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
    }};

    // ── Textures ─────────────────────────────────────────────────────
    QVector<float> m_audioSpectrum;
    std::array<QImage, kMaxUserTextureSlots> m_userTextureImages;
    std::array<QString, kMaxUserTextureSlots> m_userTextureWraps;
    /// Last-resolved file path per user-texture slot. Tracked here so
    /// `setShaderParams` can detect path changes (load on transition,
    /// not re-load on every params write) and so the SVG rasterise size
    /// can re-render the same SVG path when only the size changes.
    /// Empty when the slot has never been assigned a path.
    std::array<QString, kMaxUserTextureSlots> m_userTexturePaths;
    /// Per-slot SVG rasterise dimension (logical pixels, max-axis). Only
    /// consulted on `.svg` / `.svgz` paths; bitmap formats ignore this.
    /// Default 1024 carries forward the pre-unification ZoneShaderItem
    /// behaviour — sized to be sharp at the typical zone-icon scale
    /// without the 4× cost a 4096 default would impose on the common
    /// case (a 200×200 px logo doesn't need a 16 MP rasterisation).
    std::array<int, kMaxUserTextureSlots> m_userTextureSvgSizes = {1024, 1024, 1024, 1024};
    /// Set by `setUserTexture` to flag that a directly-pushed QImage now
    /// occupies one of the user-texture slots (path cache cleared). Honoured
    /// by `setShaderParams`: when the incoming params map is byte-equal to
    /// `m_shaderParams`, the early-return is normally fine, but if a direct
    /// push intervened the cleared path needs to be reloaded from the
    /// unchanged-on-the-wire `uTextureN` entry. Bypassing the early-return
    /// in that one case lets the texture pipeline re-parse and reload.
    /// Reset at the end of the parse branch so the cost is paid exactly
    /// once per intervening direct push.
    bool m_userTexturesDirectlyOverridden = false;
    QImage m_wallpaperTexture;
    mutable QMutex m_wallpaperTextureMutex;
    bool m_useWallpaper = false;
    bool m_useDepthBuffer = false;

    // ── Uniform extension ────────────────────────────────────────────
    std::shared_ptr<PhosphorShaders::IUniformExtension> m_uniformExtension;

    // ── Status ───────────────────────────────────────────────────────
    // Atomic because setStatus is called from updatePaintNode (render
    // thread under threaded render loop) while onPlayingTick reads it on
    // the GUI thread. A torn read would only matter at status
    // transitions, but the gate at `m_status != Status::Ready` would
    // skip-or-tick spuriously around the boundary. Acquire/release
    // semantics suffice — there's no associated data we need to publish
    // alongside.
    std::atomic<Status> m_status{Status::Null};
    QString m_errorLog;

    // ── Render node tracking ─────────────────────────────────────────
    ShaderNodeRhi* m_renderNode = nullptr;
    // QPointer defends against reparent/teardown storms where the window is
    // destroyed out from under us before windowChanged(nullptr) fires.
    QPointer<QQuickWindow> m_connectedWindow;

    // ── Thread-safe dirty flags for main -> render thread sync ───────
    std::atomic<bool> m_shaderDirty{false};
};

} // namespace PhosphorRendering
