// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "zoneshadercommon.h"
#include <QSGRenderNode>
#include <QColor>
#include <QImage>
#include <QStringList>
#include <QVector4D>
#include <QString>

namespace PlasmaZones {

/**
 * @brief Abstract base for zone shader render nodes (RHI backend)
 *
 * ZoneShaderItem uses this interface to drive ZoneShaderNodeRhi (QRhi/OpenGL).
 */
class ZoneShaderNodeBase : public QSGRenderNode
{
public:
    explicit ZoneShaderNodeBase() = default;
    ~ZoneShaderNodeBase() override = default;

    // Zone data
    virtual void setZones(const QVector<ZoneData>& zones) = 0;
    virtual void setZone(int index, const ZoneData& data) = 0;
    virtual void setZoneCount(int count) = 0;
    virtual void setHighlightedZones(const QVector<int>& indices) = 0;
    virtual void clearHighlights() = 0;

    // Timing
    virtual void setTime(float time) = 0;
    virtual void setTimeDelta(float delta) = 0;
    virtual void setFrame(int frame) = 0;
    virtual void setResolution(float width, float height) = 0;
    virtual void setMousePosition(const QPointF& pos) = 0;

    // Custom params and colors
    virtual void setCustomParams1(const QVector4D& params) = 0;
    virtual void setCustomParams2(const QVector4D& params) = 0;
    virtual void setCustomParams3(const QVector4D& params) = 0;
    virtual void setCustomParams4(const QVector4D& params) = 0;
    virtual void setCustomColor1(const QColor& color) = 0;
    virtual void setCustomColor2(const QColor& color) = 0;
    virtual void setCustomColor3(const QColor& color) = 0;
    virtual void setCustomColor4(const QColor& color) = 0;
    virtual void setCustomColor5(const QColor& color) = 0;
    virtual void setCustomColor6(const QColor& color) = 0;
    virtual void setCustomColor7(const QColor& color) = 0;
    virtual void setCustomColor8(const QColor& color) = 0;

    /** Labels texture (pre-rendered zone numbers). Default no-op for backends that don't support it. */
    virtual void setLabelsTexture(const QImage& image)
    {
        Q_UNUSED(image)
    }

    /** Multi-pass: optional buffer pass fragment shader path. No-op if backend does not support multipass. */
    virtual void setBufferShaderPath(const QString& path) { Q_UNUSED(path) }
    /** Multi-pass: up to 4 buffer pass fragment shader paths (A→B→C→D). Overrides single path when non-empty. */
    virtual void setBufferShaderPaths(const QStringList& paths) { Q_UNUSED(paths) }
    /** When true, buffer pass uses ping-pong (two textures, samples previous frame as iChannel0). Default no-op. */
    virtual void setBufferFeedback(bool enable) { Q_UNUSED(enable) }
    /** Buffer resolution scale (e.g. 0.5 = half size). Default no-op. */
    virtual void setBufferScale(qreal scale) { Q_UNUSED(scale) }
    /** Buffer channel wrap: "clamp" or "repeat". Default no-op. */
    virtual void setBufferWrap(const QString& wrap) { Q_UNUSED(wrap) }

    // Shader loading (paths; RHI node bakes GLSL 330 at runtime)
    virtual bool loadVertexShader(const QString& path) = 0;
    virtual bool loadFragmentShader(const QString& path) = 0;
    virtual void setVertexShaderSource(const QString& source) = 0;
    virtual void setFragmentShaderSource(const QString& source) = 0;

    virtual bool isShaderReady() const = 0;
    virtual QString shaderError() const = 0;
    virtual void invalidateShader() = 0;
    virtual void invalidateUniforms() = 0;
};

} // namespace PlasmaZones
