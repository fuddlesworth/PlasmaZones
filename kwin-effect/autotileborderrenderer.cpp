// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autotileborderrenderer.h"

#include <epoxy/gl.h>

#include <core/colorspace.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/glvertexbuffer.h>

#include <QList>
#include <QRectF>
#include <QVector2D>

namespace PlasmaZones {

void AutotileBorderRenderer::drawBorders(const KWin::RenderTarget& renderTarget,
                                         const KWin::RenderViewport& viewport,
                                         const QVector<QRect>& zoneGeometries,
                                         int borderWidth,
                                         const QColor& borderColor)
{
    if (borderWidth <= 0 || !borderColor.isValid() || borderColor.alpha() == 0
        || zoneGeometries.isEmpty()) {
        return;
    }

    const qreal scale = viewport.scale();

    // Build vertex data for all border rectangles across all zones.
    // Each zone produces up to 4 border strips (top, bottom, left, right).
    QList<QVector2D> verts;
    verts.reserve(zoneGeometries.size() * 4 * 6); // 4 strips × 6 verts (2 triangles)

    for (const QRect& zoneGeometry : zoneGeometries) {
        const QRectF zone(zoneGeometry);

        // Clamp effective border width to half the zone dimension to prevent
        // overlapping top/bottom or left/right strips (double-alpha artifacts).
        const qreal bw = qMin(static_cast<qreal>(borderWidth), qMin(zone.width(), zone.height()) / 2.0);
        if (bw <= 0) {
            continue;
        }

        // Build the 4 border rectangles in logical coordinates
        const QRectF top(zone.x(), zone.y(), zone.width(), bw);
        const QRectF bottom(zone.x(), zone.bottom() - bw, zone.width(), bw);
        const QRectF left(zone.x(), zone.y() + bw, bw, zone.height() - 2 * bw);
        const QRectF right(zone.right() - bw, zone.y() + bw, bw, zone.height() - 2 * bw);

        for (const QRectF& r : {top, bottom, left, right}) {
            if (r.width() <= 0 || r.height() <= 0) {
                continue;
            }
            // Scale logical coordinates to device coordinates for the GPU
            const float x1 = r.left() * scale;
            const float y1 = r.top() * scale;
            const float x2 = r.right() * scale;
            const float y2 = r.bottom() * scale;

            // Two triangles per rectangle
            verts.append(QVector2D(x1, y1));
            verts.append(QVector2D(x2, y1));
            verts.append(QVector2D(x1, y2));
            verts.append(QVector2D(x2, y1));
            verts.append(QVector2D(x2, y2));
            verts.append(QVector2D(x1, y2));
        }
    }

    if (verts.isEmpty()) {
        return;
    }

    // Save GL blend state so we restore it exactly as KWin expects.
    // KWin keeps GL_BLEND enabled with its own blend function throughout
    // the compositing pipeline. If we leave it disabled, subsequent frames
    // render incorrectly and the border disappears.
    const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    GLint prevSrcRGB, prevDstRGB, prevSrcAlpha, prevDstAlpha;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevDstAlpha);

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    KWin::ShaderBinder binder(KWin::ShaderTrait::UniformColor | KWin::ShaderTrait::TransformColorspace);
    binder.shader()->setUniform(KWin::GLShader::Mat4Uniform::ModelViewProjectionMatrix,
                                viewport.projectionMatrix());
    binder.shader()->setColorspaceUniforms(KWin::ColorDescription::sRGB,
                                           renderTarget.colorDescription(),
                                           KWin::RenderingIntent::Perceptual);
    binder.shader()->setUniform(KWin::GLShader::ColorUniform::Color, borderColor);

    auto* vbo = KWin::GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setVertices(verts);
    vbo->render(GL_TRIANGLES);

    // Restore previous blend state
    if (blendWasEnabled) {
        glBlendFuncSeparate(prevSrcRGB, prevDstRGB, prevSrcAlpha, prevDstAlpha);
    } else {
        glDisable(GL_BLEND);
    }
}

} // namespace PlasmaZones
