// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorshell_export.h>

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QQuickItem>
#include <QSizeF>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QVector4D>
#include <memory>

namespace PhosphorShell {

class IUniformExtension;

/// QQuickItem that renders a fullscreen fragment shader via Qt RHI.
///
/// Shadertoy-compatible: iTime, iMouse, iResolution, multipass, custom params.
/// Extend with IUniformExtension to append app-specific uniform data.
///
/// QML usage:
/// @code
/// import PhosphorShell
///
/// ShaderEffect {
///     anchors.fill: parent
///     shaderSource: "file:///path/to/effect.frag"
///     customParams1: Qt.vector4d(0.1, 0.5, 0.0, 1.0)
/// }
/// @endcode
class PHOSPHORSHELL_EXPORT ShaderEffect : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    // Shadertoy-compatible animation uniforms
    Q_PROPERTY(qreal iTime READ iTime WRITE setITime NOTIFY iTimeChanged FINAL)
    Q_PROPERTY(qreal iTimeDelta READ iTimeDelta WRITE setITimeDelta NOTIFY iTimeDeltaChanged FINAL)
    Q_PROPERTY(int iFrame READ iFrame WRITE setIFrame NOTIFY iFrameChanged FINAL)
    Q_PROPERTY(QSizeF iResolution READ iResolution WRITE setIResolution NOTIFY iResolutionChanged FINAL)
    Q_PROPERTY(QPointF iMouse READ iMouse WRITE setIMouse NOTIFY iMouseChanged FINAL)

    // Shader source
    Q_PROPERTY(QUrl shaderSource READ shaderSource WRITE setShaderSource NOTIFY shaderSourceChanged FINAL)
    Q_PROPERTY(QVariantMap shaderParams READ shaderParams WRITE setShaderParams NOTIFY shaderParamsChanged FINAL)

    // Multipass
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

    // Custom parameters (32 floats in 8 vec4s)
    Q_PROPERTY(QVector4D customParams1 READ customParams1 WRITE setCustomParams1 NOTIFY customParamsChanged FINAL)
    Q_PROPERTY(QVector4D customParams2 READ customParams2 WRITE setCustomParams2 NOTIFY customParamsChanged FINAL)
    Q_PROPERTY(QVector4D customParams3 READ customParams3 WRITE setCustomParams3 NOTIFY customParamsChanged FINAL)
    Q_PROPERTY(QVector4D customParams4 READ customParams4 WRITE setCustomParams4 NOTIFY customParamsChanged FINAL)
    Q_PROPERTY(QVector4D customParams5 READ customParams5 WRITE setCustomParams5 NOTIFY customParamsChanged FINAL)
    Q_PROPERTY(QVector4D customParams6 READ customParams6 WRITE setCustomParams6 NOTIFY customParamsChanged FINAL)
    Q_PROPERTY(QVector4D customParams7 READ customParams7 WRITE setCustomParams7 NOTIFY customParamsChanged FINAL)
    Q_PROPERTY(QVector4D customParams8 READ customParams8 WRITE setCustomParams8 NOTIFY customParamsChanged FINAL)

    // Custom colors (16 colors)
    Q_PROPERTY(QVector4D customColor1 READ customColor1 WRITE setCustomColor1 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QVector4D customColor2 READ customColor2 WRITE setCustomColor2 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QVector4D customColor3 READ customColor3 WRITE setCustomColor3 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QVector4D customColor4 READ customColor4 WRITE setCustomColor4 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QVector4D customColor5 READ customColor5 WRITE setCustomColor5 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QVector4D customColor6 READ customColor6 WRITE setCustomColor6 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QVector4D customColor7 READ customColor7 WRITE setCustomColor7 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QVector4D customColor8 READ customColor8 WRITE setCustomColor8 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QVector4D customColor9 READ customColor9 WRITE setCustomColor9 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QVector4D customColor10 READ customColor10 WRITE setCustomColor10 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QVector4D customColor11 READ customColor11 WRITE setCustomColor11 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QVector4D customColor12 READ customColor12 WRITE setCustomColor12 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QVector4D customColor13 READ customColor13 WRITE setCustomColor13 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QVector4D customColor14 READ customColor14 WRITE setCustomColor14 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QVector4D customColor15 READ customColor15 WRITE setCustomColor15 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QVector4D customColor16 READ customColor16 WRITE setCustomColor16 NOTIFY customColorsChanged FINAL)

    // Textures
    Q_PROPERTY(
        QImage wallpaperTexture READ wallpaperTexture WRITE setWallpaperTexture NOTIFY wallpaperTextureChanged FINAL)
    Q_PROPERTY(bool useWallpaper READ useWallpaper WRITE setUseWallpaper NOTIFY useWallpaperChanged FINAL)
    Q_PROPERTY(bool useDepthBuffer READ useDepthBuffer WRITE setUseDepthBuffer NOTIFY useDepthBufferChanged FINAL)

    // Status
    Q_PROPERTY(Status status READ status NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString errorLog READ errorLog NOTIFY errorLogChanged FINAL)

public:
    enum class Status {
        Null,
        Loading,
        Ready,
        Error,
    };
    Q_ENUM(Status)

    explicit ShaderEffect(QQuickItem* parent = nullptr);
    ~ShaderEffect() override;

    // ── Uniform extension ─────────────────────────────────────────────
    /// Attach a uniform extension that appends custom data after BaseUniforms.
    void setUniformExtension(std::shared_ptr<IUniformExtension> extension);
    std::shared_ptr<IUniformExtension> uniformExtension() const;

    // ── Shadertoy uniforms ────────────────────────────────────────────
    qreal iTime() const;
    void setITime(qreal time);

    qreal iTimeDelta() const;
    void setITimeDelta(qreal delta);

    int iFrame() const;
    void setIFrame(int frame);

    QSizeF iResolution() const;
    void setIResolution(const QSizeF& resolution);

    QPointF iMouse() const;
    void setIMouse(const QPointF& mouse);

    // ── Shader source ─────────────────────────────────────────────────
    QUrl shaderSource() const;
    void setShaderSource(const QUrl& source);

    QVariantMap shaderParams() const;
    void setShaderParams(const QVariantMap& params);

    // ── Multipass ─────────────────────────────────────────────────────
    QString bufferShaderPath() const;
    void setBufferShaderPath(const QString& path);

    QStringList bufferShaderPaths() const;
    void setBufferShaderPaths(const QStringList& paths);

    bool bufferFeedback() const;
    void setBufferFeedback(bool enable);

    qreal bufferScale() const;
    void setBufferScale(qreal scale);

    QString bufferWrap() const;
    void setBufferWrap(const QString& wrap);

    QStringList bufferWraps() const;
    void setBufferWraps(const QStringList& wraps);

    QString bufferFilter() const;
    void setBufferFilter(const QString& filter);

    QStringList bufferFilters() const;
    void setBufferFilters(const QStringList& filters);

    // ── Custom parameters (32 float slots) ────────────────────────────
    QVector4D customParams1() const;
    void setCustomParams1(const QVector4D& params);
    QVector4D customParams2() const;
    void setCustomParams2(const QVector4D& params);
    QVector4D customParams3() const;
    void setCustomParams3(const QVector4D& params);
    QVector4D customParams4() const;
    void setCustomParams4(const QVector4D& params);
    QVector4D customParams5() const;
    void setCustomParams5(const QVector4D& params);
    QVector4D customParams6() const;
    void setCustomParams6(const QVector4D& params);
    QVector4D customParams7() const;
    void setCustomParams7(const QVector4D& params);
    QVector4D customParams8() const;
    void setCustomParams8(const QVector4D& params);

    // ── Custom colors (16 color slots) ────────────────────────────────
    QVector4D customColor1() const;
    void setCustomColor1(const QVector4D& color);
    QVector4D customColor2() const;
    void setCustomColor2(const QVector4D& color);
    QVector4D customColor3() const;
    void setCustomColor3(const QVector4D& color);
    QVector4D customColor4() const;
    void setCustomColor4(const QVector4D& color);
    QVector4D customColor5() const;
    void setCustomColor5(const QVector4D& color);
    QVector4D customColor6() const;
    void setCustomColor6(const QVector4D& color);
    QVector4D customColor7() const;
    void setCustomColor7(const QVector4D& color);
    QVector4D customColor8() const;
    void setCustomColor8(const QVector4D& color);
    QVector4D customColor9() const;
    void setCustomColor9(const QVector4D& color);
    QVector4D customColor10() const;
    void setCustomColor10(const QVector4D& color);
    QVector4D customColor11() const;
    void setCustomColor11(const QVector4D& color);
    QVector4D customColor12() const;
    void setCustomColor12(const QVector4D& color);
    QVector4D customColor13() const;
    void setCustomColor13(const QVector4D& color);
    QVector4D customColor14() const;
    void setCustomColor14(const QVector4D& color);
    QVector4D customColor15() const;
    void setCustomColor15(const QVector4D& color);
    QVector4D customColor16() const;
    void setCustomColor16(const QVector4D& color);

    // ── Textures ──────────────────────────────────────────────────────
    void setAudioSpectrum(const QVector<float>& spectrum);

    void setUserTexture(int slot, const QImage& image);
    void setUserTextureWrap(int slot, const QString& wrap);

    QImage wallpaperTexture() const;
    void setWallpaperTexture(const QImage& image);

    bool useWallpaper() const;
    void setUseWallpaper(bool use);

    bool useDepthBuffer() const;
    void setUseDepthBuffer(bool use);

    // ── Status ────────────────────────────────────────────────────────
    Status status() const;
    QString errorLog() const;

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
    void wallpaperTextureChanged();
    void useWallpaperChanged();
    void useDepthBufferChanged();
    void statusChanged();
    void errorLogChanged();
    void audioSpectrumChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorShell
