// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scrolltabindicatorpainter.h"

#include "plasmazoneseffect/shader_internal.h"

// epoxy MUST precede any other GL header so it can interpose the function
// pointers. shader_internal.h above pulls it in first for the same reason;
// naming it here as well keeps the ordering true if that header ever stops.
#include <epoxy/gl.h>

#include <core/rect.h>
#include <core/region.h>
#include <core/renderviewport.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>

#include <QMatrix4x4>
#include <QRectF>
#include <QSizeF>

// State, hit-testing and the GPU blit for the scrolling strip's tab
// indicators. The layout maths and the QPainter rasterisation live in
// scrolltabindicatorpainter_raster.cpp; this file owns the per-output model,
// the dirty tracking and the one texture per output.
namespace PlasmaZones {

ScrollTabIndicatorPainter::ScrollTabIndicatorPainter() = default;

ScrollTabIndicatorPainter::~ScrollTabIndicatorPainter() = default;

ScrollTabIndicatorPainter::PerOutput* ScrollTabIndicatorPainter::find(KWin::LogicalOutput* output)
{
    const auto it = m_outputs.find(output);
    return it == m_outputs.end() ? nullptr : &it->second;
}

const ScrollTabIndicatorPainter::PerOutput* ScrollTabIndicatorPainter::find(KWin::LogicalOutput* output) const
{
    const auto it = m_outputs.find(output);
    return it == m_outputs.end() ? nullptr : &it->second;
}

void ScrollTabIndicatorPainter::rebuildLayout(PerOutput& entry)
{
    entry.hits.clear();
    entry.bounds = QRect();
    for (const ScrollTabIndicator& indicator : entry.indicators) {
        if (indicator.rect.isEmpty() || indicator.tabs.isEmpty()) {
            continue;
        }
        entry.bounds = entry.bounds.isNull() ? indicator.rect : entry.bounds.united(indicator.rect);
        entry.hits.append(ScrollTabRaster::layoutPills(indicator, entry.style));
    }
    if (entry.hoveredWindowId.isEmpty()) {
        return;
    }
    // A hover survives a relayout only while its window is still on the
    // strip. Dropping it otherwise keeps a closed or moved-away window from
    // tinting whichever tab inherits its slot.
    for (const ScrollTabHitRect& hit : entry.hits) {
        if (hit.windowId == entry.hoveredWindowId) {
            return;
        }
    }
    entry.hoveredWindowId.clear();
}

void ScrollTabIndicatorPainter::setIndicators(KWin::LogicalOutput* output,
                                              const QVector<ScrollTabIndicator>& indicators,
                                              const ScrollTabIndicatorStyle& style)
{
    if (!output) {
        return;
    }
    if (indicators.isEmpty() && !find(output)) {
        return; // nothing to draw and nothing to forget — don't default-insert
    }
    PerOutput& entry = m_outputs[output];
    // The whole point of the equality check: this is called every frame with
    // the same data while a strip sits still, and rasterising is a QPainter
    // pass plus a texture upload.
    if (entry.indicators == indicators && entry.style == style) {
        return;
    }
    entry.indicators = indicators;
    entry.style = style;
    rebuildLayout(entry);
    entry.dirty = true;
}

bool ScrollTabIndicatorPainter::setHover(KWin::LogicalOutput* output, const QPointF& pos, const QPointF& viewOffset)
{
    PerOutput* const entry = find(output);
    if (!entry) {
        return false;
    }
    const QString hit = pillAt(output, pos, viewOffset);
    if (hit == entry->hoveredWindowId) {
        return false;
    }
    entry->hoveredWindowId = hit;
    entry->dirty = true;
    return true;
}

QString ScrollTabIndicatorPainter::pillAt(KWin::LogicalOutput* output, const QPointF& pos,
                                          const QPointF& viewOffset) const
{
    const PerOutput* const entry = find(output);
    if (!entry) {
        return {};
    }
    // The hit rects are stored where the model put them; the blit shifts them
    // by the view offset, so undo that shift on the pointer rather than
    // re-laying the model out every frame of a scroll.
    const QPointF local = pos - viewOffset;
    for (const ScrollTabHitRect& hit : entry->hits) {
        if (QRectF(hit.rect).contains(local)) {
            return hit.windowId;
        }
    }
    return {};
}

QRect ScrollTabIndicatorPainter::boundsFor(KWin::LogicalOutput* output) const
{
    const PerOutput* const entry = find(output);
    return entry ? entry->bounds : QRect();
}

bool ScrollTabIndicatorPainter::hasIndicators(KWin::LogicalOutput* output) const
{
    const PerOutput* const entry = find(output);
    return entry && !entry->bounds.isEmpty();
}

void ScrollTabIndicatorPainter::clearOutput(KWin::LogicalOutput* output)
{
    m_outputs.erase(output);
}

void ScrollTabIndicatorPainter::clearAll()
{
    m_outputs.clear();
}

void ScrollTabIndicatorPainter::releaseGl()
{
    // Models are kept: an output whose context went away and came back
    // rasterises again on its next paint, because dropping the texture also
    // sets the entry dirty.
    for (auto& [output, entry] : m_outputs) {
        entry.texture.reset();
        entry.textureBounds = QRect();
        entry.textureScale = 0.0;
        entry.dirty = true;
    }
}

void ScrollTabIndicatorPainter::paint(KWin::LogicalOutput* output, const KWin::RenderTarget& renderTarget,
                                      const KWin::RenderViewport& viewport, const KWin::Region& deviceRegion,
                                      const QPointF& viewOffset)
{
    // The target is part of the call signature because the blit belongs to a
    // specific target, but everything this pass needs from it (the projection
    // and the device mapping) is already on the viewport, which is
    // constructed against that target.
    Q_UNUSED(renderTarget)

    PerOutput* const entry = find(output);
    if (!entry) {
        return;
    }
    if (entry->bounds.isEmpty()) {
        // Nothing to draw: release the texture rather than leaving VRAM held
        // by a strip that no longer has tabbed columns.
        entry->texture.reset();
        entry->textureBounds = QRect();
        entry->textureScale = 0.0;
        entry->dirty = true;
        return;
    }

    const qreal scale = viewport.scale() > 0.0 ? viewport.scale() : 1.0;
    if (entry->dirty || !entry->texture || entry->textureBounds != entry->bounds || entry->textureScale != scale) {
        const QImage image =
            ScrollTabRaster::rasterise(entry->indicators, entry->style, entry->bounds, scale, entry->hoveredWindowId);
        entry->texture.reset();
        entry->textureBounds = QRect();
        entry->textureScale = 0.0;
        if (image.isNull()) {
            entry->dirty = false; // nothing rasterisable; retry when the model changes
            return;
        }
        entry->texture = KWin::GLTexture::upload(image);
        if (!entry->texture) {
            return; // stays dirty, so the next frame tries again
        }
        // A fresh upload defaults to GL_REPEAT with whatever filter the
        // driver picked. The quad samples right up to the texture's edge, so
        // REPEAT wraps the opposite edge's antialiased pixels into the border
        // — set both explicitly rather than inheriting.
        entry->texture->setFilter(GL_LINEAR);
        entry->texture->setWrapMode(GL_CLAMP_TO_EDGE);
        entry->textureBounds = entry->bounds;
        entry->textureScale = scale;
        entry->dirty = false;
    }
    if (!entry->texture) {
        return;
    }

    // Where the pills are ON SCREEN this frame: the model's rects plus the
    // strip view spring's offset. The offset is never baked into the model
    // (it changes every frame of a scroll; the model does not), so it is
    // applied here and, identically, in pillAt().
    const QRectF destLogical = QRectF(entry->bounds).translated(viewOffset);
    const KWin::RectF dest = viewport.mapToRenderTarget(KWin::RectF(destLogical));
    if (dest.isEmpty()) {
        return;
    }

    // Hand blend/viewport/scissor/active-unit back exactly as found: this
    // runs inside KWin's scene walk, and the effect's own convention is that
    // no pass leaks GL state into the next one.
    const ShaderInternal::ScopedGlState glStateGuard;
    // The rasterised image is ARGB32_PREMULTIPLIED, so the source colour is
    // already multiplied by its alpha and the correct blend is
    // (ONE, ONE_MINUS_SRC_ALPHA). Using (SRC_ALPHA, ONE_MINUS_SRC_ALPHA)
    // against premultiplied data double-darkens every translucent pixel —
    // the pill background and the hairline border are exactly that.
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    // The quad is drawn in the viewport's DEVICE coordinate space and
    // projected by KWin's own matrix, which encodes the output transform and
    // the render offset. GLTexture::render() emits the quad at the origin, so
    // the destination position rides on the matrix.
    QMatrix4x4 mvp = viewport.projectionMatrix();
    mvp.translate(float(dest.x()), float(dest.y()));

    KWin::ShaderBinder binder(KWin::ShaderTrait::MapTexture);
    if (KWin::GLShader* const shader = binder.shader()) {
        shader->setUniform(KWin::GLShader::Mat4Uniform::ModelViewProjectionMatrix, mvp);
    }
    glActiveTexture(GL_TEXTURE0);
    entry->texture->bind();
    // render() rather than a hand-rolled quad: it applies the texture's own
    // content transform, and an image upload is Y-flipped relative to GL's
    // origin. Hardware-clipped to the caller's DEVICE region: the scissor is
    // in framebuffer pixels, which is exactly the space KWin's per-window
    // damage region is in, so the region is passed through untranslated —
    // the quad's own placement rides the MVP and the scissor is independent
    // of it. Pixels outside the region must not be painted (see the header).
    entry->texture->render(deviceRegion, QSizeF(dest.width(), dest.height()), /*hardwareClipping=*/true);
    // ScopedGlState restores the active-unit ENUM, not the BINDINGS: a name
    // still bound when the texture is later deleted survives as a dangling
    // reference.
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace PlasmaZones
