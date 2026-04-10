// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "animationshadercommon.h"

#include <plasmazones_rendering_export.h>
#include <QQuickItem>
#include <QUrl>
#include <QVector4D>
#include <QVariantMap>
#include <atomic>

namespace PlasmaZones {

class AnimationShaderNodeRhi;

/**
 * @brief QQuickItem for applying animation shader transitions to overlay content
 *
 * Renders the source item's content through a transition shader driven by
 * an animation progress value. The source item must have `layer.enabled: true`
 * so its content is available as a texture.
 *
 * Shaders are loaded from disk via the AnimationShaderRegistry — the same
 * GLSL 450 fragment shaders used by zone overlays and (in future) KWin.
 *
 * Usage in QML:
 * @code
 * AnimationShaderItem {
 *     anchors.fill: contentWrapper
 *     source: contentWrapper      // must have layer.enabled: true
 *     shaderSource: "file:///usr/share/plasmazones/animations/dissolve/effect.frag"
 *     progress: 0.5               // 0→1 driven by NumberAnimation
 *     direction: 0                // 0=show, 1=hide
 * }
 * @endcode
 */
class PLASMAZONES_RENDERING_EXPORT AnimationShaderItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    /// Source item whose content is rendered through the shader.
    /// The source item MUST have layer.enabled: true.
    Q_PROPERTY(QQuickItem* source READ source WRITE setSource NOTIFY sourceChanged FINAL)

    /// Fragment shader source (file:// URL from AnimationShaderRegistry).
    /// Setting this triggers shader loading and compilation.
    Q_PROPERTY(QUrl shaderSource READ shaderSource WRITE setShaderSource NOTIFY shaderSourceChanged FINAL)

    /// Animation progress [0.0-1.0]. Drive this with a NumberAnimation.
    Q_PROPERTY(qreal progress READ progress WRITE setProgress NOTIFY progressChanged FINAL)

    /// Total animation duration in milliseconds (informational for the shader).
    Q_PROPERTY(qreal duration READ duration WRITE setDuration NOTIFY durationChanged FINAL)

    /// Style-specific parameter (e.g., pixel size for pixelate).
    Q_PROPERTY(qreal styleParam READ styleParam WRITE setStyleParam NOTIFY styleParamChanged FINAL)

    /// Direction: 0=show (in), 1=hide (out).
    Q_PROPERTY(int direction READ direction WRITE setDirection NOTIFY directionChanged FINAL)

    /// Custom shader parameters (same format as zone shader customParams).
    /// Keys: "customParams1_x", "customParams1_y", etc. (mapped from metadata.json slots).
    Q_PROPERTY(QVariantMap shaderParams READ shaderParams WRITE setShaderParams NOTIFY shaderParamsChanged FINAL)

    /// Shader status.
    Q_PROPERTY(Status status READ status NOTIFY statusChanged FINAL)

    /// Shader compilation error message (empty when status != Error).
    Q_PROPERTY(QString errorLog READ errorLog NOTIFY errorLogChanged FINAL)

public:
    enum Status {
        Null,
        Loading,
        Ready,
        Error
    };
    Q_ENUM(Status)

    explicit AnimationShaderItem(QQuickItem* parent = nullptr);
    ~AnimationShaderItem() override;

    QQuickItem* source() const;
    void setSource(QQuickItem* source);

    QUrl shaderSource() const;
    void setShaderSource(const QUrl& source);

    qreal progress() const;
    void setProgress(qreal progress);

    qreal duration() const;
    void setDuration(qreal duration);

    qreal styleParam() const;
    void setStyleParam(qreal param);

    int direction() const;
    void setDirection(int direction);

    QVariantMap shaderParams() const;
    void setShaderParams(const QVariantMap& params);

    Status status() const;
    QString errorLog() const;

Q_SIGNALS:
    void sourceChanged();
    void shaderSourceChanged();
    void progressChanged();
    void durationChanged();
    void styleParamChanged();
    void directionChanged();
    void shaderParamsChanged();
    void statusChanged();
    void errorLogChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;

private:
    void setStatus(Status status);
    void setError(const QString& error);

    QQuickItem* m_source = nullptr;
    QUrl m_shaderSource;
    qreal m_progress = 0.0;
    qreal m_duration = 150.0;
    qreal m_styleParam = 0.0;
    int m_direction = 0;
    QVariantMap m_shaderParams;
    Status m_status = Null;
    QString m_errorLog;
    AnimationShaderNodeRhi* m_renderNode = nullptr;
    std::atomic<bool> m_shaderDirty{false};

    // Custom params extracted from shaderParams map
    std::array<QVector4D, 8> m_customParams;
};

} // namespace PlasmaZones
