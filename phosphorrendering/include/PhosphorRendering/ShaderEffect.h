// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorrendering_export.h>

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

#include <array>
#include <atomic>
#include <memory>

QT_BEGIN_NAMESPACE
class QSGNode;
QT_END_NAMESPACE

namespace PhosphorShell {
class IUniformExtension;
}

namespace PhosphorRendering {

class ShaderNodeRhi;

/**
 * @brief QQuickItem that renders a fullscreen fragment shader via Qt RHI.
 *
 * Shadertoy-compatible: iTime, iTimeDelta, iFrame, iResolution, iMouse,
 * multipass buffers, custom parameters, and custom colors.
 *
 * Extend with IUniformExtension to append application-specific uniform data
 * after the base UBO layout.
 *
 * QML usage:
 * @code
 * import PhosphorRendering
 *
 * ShaderEffect {
 *     anchors.fill: parent
 *     shaderSource: "file:///path/to/effect.frag"
 *     customParams1: Qt.vector4d(0.1, 0.5, 0.0, 1.0)
 *     customColor1: "#ff8800"
 * }
 * @endcode
 */
class PHOSPHORRENDERING_EXPORT ShaderEffect : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    // ── Shadertoy-compatible animation uniforms ──────────────────────
    Q_PROPERTY(qreal iTime READ iTime WRITE setITime NOTIFY iTimeChanged FINAL)
    Q_PROPERTY(qreal iTimeDelta READ iTimeDelta WRITE setITimeDelta NOTIFY iTimeDeltaChanged FINAL)
    Q_PROPERTY(int iFrame READ iFrame WRITE setIFrame NOTIFY iFrameChanged FINAL)
    Q_PROPERTY(QSizeF iResolution READ iResolution WRITE setIResolution NOTIFY iResolutionChanged FINAL)
    Q_PROPERTY(QPointF iMouse READ iMouse WRITE setIMouse NOTIFY iMouseChanged FINAL)

    // ── Shader source ────────────────────────────────────────────────
    Q_PROPERTY(QUrl shaderSource READ shaderSource WRITE setShaderSource NOTIFY shaderSourceChanged FINAL)
    Q_PROPERTY(QVariantMap shaderParams READ shaderParams WRITE setShaderParams NOTIFY shaderParamsChanged FINAL)

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

    explicit ShaderEffect(QQuickItem* parent = nullptr);
    ~ShaderEffect() override;

    // ── Uniform extension ────────────────────────────────────────────

    /// Attach a uniform extension that appends custom data after BaseUniforms.
    void setUniformExtension(std::shared_ptr<PhosphorShell::IUniformExtension> extension);
    std::shared_ptr<PhosphorShell::IUniformExtension> uniformExtension() const;

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

    QVariantMap shaderParams() const
    {
        return m_shaderParams;
    }
    virtual void setShaderParams(const QVariantMap& params);

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

    // ── Textures ─────────────────────────────────────────────────────

    QVariant audioSpectrumVariant() const;
    void setAudioSpectrumVariant(const QVariant& spectrum);
    /** Direct setter from C++ avoiding QVariantList round-trip. */
    void setAudioSpectrum(const QVector<float>& spectrum);

    /** Set a user texture (slots 0-3, bindings 7-10). */
    void setUserTexture(int slot, const QImage& image);
    /** Set user texture wrap mode (slots 0-3). "clamp" or "repeat". */
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

    // ── Status ───────────────────────────────────────────────────────

    Status status() const
    {
        return m_status;
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
    void shaderSourceChanged();
    void shaderParamsChanged();
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

    void setError(const QString& error);
    void setStatus(Status newStatus);

private:
    // ── Animation state ──────────────────────────────────────────────
    qreal m_iTime = 0.0;
    qreal m_iTimeDelta = 0.0;
    int m_iFrame = 0;
    QSizeF m_iResolution;
    QPointF m_iMouse;

    // ── Shader source ────────────────────────────────────────────────
    QUrl m_shaderSource;
    QVariantMap m_shaderParams;
    QStringList m_shaderIncludePaths;

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
        QColor::fromRgbF(1.0f, 0.5f, 0.0f, 1.0f), // Default orange highlight
        QColor::fromRgbF(0.2f, 0.2f, 0.2f, 0.8f), // Default gray inactive
        QColor::fromRgbF(1.0f, 1.0f, 1.0f, 0.0f), // Default white, alpha 0 = not set
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
    std::array<QImage, 4> m_userTextureImages;
    std::array<QString, 4> m_userTextureWraps;
    QImage m_wallpaperTexture;
    mutable QMutex m_wallpaperTextureMutex;
    bool m_useWallpaper = false;
    bool m_useDepthBuffer = false;

    // ── Uniform extension ────────────────────────────────────────────
    std::shared_ptr<PhosphorShell::IUniformExtension> m_uniformExtension;

    // ── Status ───────────────────────────────────────────────────────
    Status m_status = Status::Null;
    QString m_errorLog;

    // ── Render node tracking ─────────────────────────────────────────
    ShaderNodeRhi* m_renderNode = nullptr;
    QQuickWindow* m_connectedWindow = nullptr;

    // ── Thread-safe dirty flags for main -> render thread sync ───────
    std::atomic<bool> m_shaderDirty{false};
};

} // namespace PhosphorRendering
