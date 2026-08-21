// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scrolltabindicatorpainter.h"

#include "plasmazoneseffect/shader_internal.h"

// epoxy MUST precede any other GL header so it can interpose the function
// pointers. shader_internal.h above pulls it in first for the same reason;
// naming it here as well keeps the ordering true if that header ever stops.
#include <epoxy/gl.h>

#include <core/colorspace.h>
#include <core/rect.h>
#include <core/region.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>

#include <QMatrix4x4>
#include <QRectF>
#include <QSizeF>

#include <cmath>
#include <utility>

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
        entry.bounds = entry.bounds.united(indicator.rect);
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

QRect ScrollTabIndicatorPainter::indicatorRectFor(const PerOutput& entry, const QString& windowId)
{
    if (windowId.isEmpty()) {
        return QRect();
    }
    for (const ScrollTabIndicator& indicator : entry.indicators) {
        for (const ScrollTabPill& pill : indicator.tabs) {
            if (pill.windowId == windowId) {
                return indicator.rect;
            }
        }
    }
    return QRect();
}

bool ScrollTabIndicatorPainter::setIndicators(KWin::LogicalOutput* output,
                                              const QVector<ScrollTabIndicator>& indicators,
                                              const ScrollTabIndicatorStyle& style)
{
    if (!output) {
        return false;
    }
    if (indicators.isEmpty() && !find(output)) {
        return false; // nothing to draw and nothing to forget — don't default-insert
    }
    PerOutput& entry = m_outputs[output];
    // The whole point of the equality check: this is called on every caption
    // tick, colour reply and settings edge with mostly unchanged data, and
    // rasterising is a QPainter pass plus a texture upload. A false here
    // tells the caller not to damage either.
    if (entry.indicators == indicators && entry.style == style) {
        return false;
    }
    entry.indicators = indicators;
    entry.style = style;
    rebuildLayout(entry);
    entry.dirty = true;
    entry.hoverDirtyRects.clear();
    entry.failedBounds = QRect();
    entry.failedScale = 0.0;
    return true;
}

bool ScrollTabIndicatorPainter::setHover(KWin::LogicalOutput* output, const QPointF& pos, const QPointF& viewOffset,
                                         QString* hitWindowId)
{
    PerOutput* const entry = find(output);
    const QString hit = entry ? pillAt(output, pos, viewOffset) : QString();
    if (hitWindowId) {
        *hitWindowId = hit;
    }
    if (!entry || hit == entry->hoveredWindowId) {
        return false;
    }
    // Only the indicators that lost and gained the hover change pixels, so
    // only those rects need re-rasterising (see paint()). The full raster
    // path still covers them when `dirty` is set for another reason.
    const QRect lost = indicatorRectFor(*entry, entry->hoveredWindowId);
    const QRect gained = indicatorRectFor(*entry, hit);
    entry->hoveredWindowId = hit;
    if (lost.isValid() && !entry->hoverDirtyRects.contains(lost)) {
        entry->hoverDirtyRects.append(lost);
    }
    if (gained.isValid() && gained != lost && !entry->hoverDirtyRects.contains(gained)) {
        entry->hoverDirtyRects.append(gained);
    }
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
    // Reverse order: the raster draws the hits in model order, so the LAST
    // one containing the point is the one on top. Indicator rects do not
    // overlap in practice (one per column), but the rule should match the
    // pixels if they ever do.
    for (auto it = entry->hits.crbegin(); it != entry->hits.crend(); ++it) {
        if (QRectF(it->rect).contains(local)) {
            return it->windowId;
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

bool ScrollTabIndicatorPainter::hasAnyIndicators() const
{
    for (const auto& [output, entry] : m_outputs) {
        if (!entry.bounds.isEmpty()) {
            return true;
        }
    }
    return false;
}

bool ScrollTabIndicatorPainter::paintedLastPass(KWin::LogicalOutput* output) const
{
    const PerOutput* const entry = find(output);
    return entry && entry->paintedLastPass;
}

void ScrollTabIndicatorPainter::notePassOutcome(KWin::LogicalOutput* output, bool painted)
{
    if (PerOutput* const entry = find(output)) {
        entry->paintedLastPass = painted;
    }
}

void ScrollTabIndicatorPainter::retireTexture(PerOutput& entry)
{
    // Both callers erase the entry right after this, so only the texture's
    // ownership matters here.
    if (entry.texture) {
        m_retiredTextures.push_back(std::move(entry.texture));
    }
}

void ScrollTabIndicatorPainter::drainRetired()
{
    // Caller guarantees a current context: this is the only place a retired
    // texture's glDeleteTextures runs.
    m_retiredTextures.clear();
}

void ScrollTabIndicatorPainter::drainRetiredTextures()
{
    drainRetired();
}

void ScrollTabIndicatorPainter::clearOutput(KWin::LogicalOutput* output)
{
    const auto it = m_outputs.find(output);
    if (it == m_outputs.end()) {
        return;
    }
    retireTexture(it->second);
    m_outputs.erase(it);
}

void ScrollTabIndicatorPainter::clearAll()
{
    for (auto& [output, entry] : m_outputs) {
        retireTexture(entry);
    }
    m_outputs.clear();
}

void ScrollTabIndicatorPainter::releaseGl()
{
    // Models are kept: an output whose context went away and came back
    // rasterises again on its next paint, because dropping the texture also
    // sets the entry dirty. The graveyard is drained here too, so a clear
    // that ran off-context before teardown still frees its name under this
    // context.
    for (auto& [output, entry] : m_outputs) {
        entry.texture.reset();
        entry.textureBounds = QRect();
        entry.textureScale = 0.0;
        entry.dirty = true;
        entry.hoverDirtyRects.clear();
        // A failure latched under the old context (an upload or allocation
        // that failed there) says nothing about the next one.
        entry.failedBounds = QRect();
        entry.failedScale = 0.0;
    }
    m_maxTextureSize = 0;
    drainRetired();
}

bool ScrollTabIndicatorPainter::paint(KWin::LogicalOutput* output, const KWin::RenderTarget& renderTarget,
                                      const KWin::RenderViewport& viewport, const KWin::Region& clipRegion,
                                      const QPointF& viewOffset)
{
    // GL-current point: whatever a clear retired since the last paint is
    // deleted here, before this output's own work.
    drainRetired();

    PerOutput* const entry = find(output);
    if (!entry) {
        return false;
    }
    if (entry->bounds.isEmpty()) {
        // Nothing to draw: release the texture rather than leaving VRAM held
        // by a strip that no longer has tabbed columns.
        entry->texture.reset();
        entry->textureBounds = QRect();
        entry->textureScale = 0.0;
        entry->dirty = true;
        entry->hoverDirtyRects.clear();
        return false;
    }

    const qreal scale = viewport.scale() > 0.0 ? viewport.scale() : 1.0;
    const bool geometryChanged = entry->textureBounds != entry->bounds || entry->textureScale != scale;
    if (entry->dirty || !entry->texture || geometryChanged) {
        if (entry->failedBounds == entry->bounds && entry->failedScale == scale) {
            // A previous attempt at exactly this bounds/scale pair failed to
            // rasterise or upload. Retrying every frame would be a full
            // union-sized QPainter pass per frame for as long as the failure
            // persists; the latch is cleared by setIndicators (model or
            // style change) and by a scale change, which are the only things
            // that can make the attempt different.
            return false;
        }
        if (m_maxTextureSize <= 0) {
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTextureSize);
        }
        const int deviceW = int(std::ceil(entry->bounds.width() * scale));
        const int deviceH = int(std::ceil(entry->bounds.height() * scale));
        if (m_maxTextureSize > 0 && (deviceW > m_maxTextureSize || deviceH > m_maxTextureSize)) {
            // Larger than the GPU can hold in one texture (a very wide
            // multi-column span at high scale with a large in-flight view
            // delta). Better to skip the pills than to spin on a raster
            // that cannot upload. The previous texture goes with the other
            // failure arms: it is never blitted again while the latch holds.
            entry->texture.reset();
            entry->textureBounds = QRect();
            entry->textureScale = 0.0;
            entry->hoverDirtyRects.clear();
            entry->failedBounds = entry->bounds;
            entry->failedScale = scale;
            entry->dirty = false;
            return false;
        }
        const QImage image =
            ScrollTabRaster::rasterise(entry->indicators, entry->style, entry->bounds, scale, entry->hoveredWindowId);
        entry->texture.reset();
        entry->textureBounds = QRect();
        entry->textureScale = 0.0;
        entry->hoverDirtyRects.clear();
        if (image.isNull()) {
            // QImage allocation failure (the empty-bounds case returned
            // above). Latch, so the next frame does not redo the pass.
            entry->failedBounds = entry->bounds;
            entry->failedScale = scale;
            entry->dirty = false;
            return false;
        }
        entry->texture = KWin::GLTexture::upload(image);
        if (!entry->texture) {
            entry->failedBounds = entry->bounds;
            entry->failedScale = scale;
            entry->dirty = false;
            return false;
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
    } else if (!entry->hoverDirtyRects.isEmpty()) {
        // Hover moved and nothing else did: re-rasterise only the indicator
        // rects that changed and sub-update the texture in place. A hover
        // enter/leave is the commonest reason this texture changes, and the
        // union can span most of an output for a wide strip, so the full
        // re-raster it used to cost showed up as a hitch on plain pointer
        // motion.
        for (const QRect& rect : std::as_const(entry->hoverDirtyRects)) {
            const QRect clipped = rect.intersected(entry->bounds);
            if (clipped.isEmpty()) {
                continue;
            }
            // Offset in the texture's DEVICE pixels: the texture was
            // rasterised for textureBounds at textureScale, and a sub-rect
            // rasterised for `clipped` at the same scale lands at the scaled
            // delta between the two origins. That delta is exact at integral
            // scales; at a fractional scale with an indicator origin that
            // does not land on the device grid it is not, and a patch
            // uploaded at the floored offset would sit up to one device pixel
            // off the full raster's placement until the next full pass. Fall
            // back to the full raster for that case rather than jitter.
            const qreal dx = (clipped.x() - entry->textureBounds.x()) * scale;
            const qreal dy = (clipped.y() - entry->textureBounds.y()) * scale;
            if (dx != std::floor(dx) || dy != std::floor(dy)) {
                entry->dirty = true;
                break;
            }
            const QImage patch =
                ScrollTabRaster::rasterise(entry->indicators, entry->style, clipped, scale, entry->hoveredWindowId);
            if (patch.isNull()) {
                // Allocation failure on the patch: redo the frame as a full
                // raster rather than leave this indicator's old hover pixels.
                entry->dirty = true;
                break;
            }
            const QPoint offset{int(dx), int(dy)};
            entry->texture->update(patch, KWin::Region(KWin::Rect(QPoint(), patch.size())), offset);
        }
        entry->hoverDirtyRects.clear();
        if (entry->dirty) {
            // Fractional-grid fallback: redo this frame as a full raster.
            return paint(output, renderTarget, viewport, clipRegion, viewOffset);
        }
    }
    if (!entry->texture) {
        return false;
    }

    // Where the pills are ON SCREEN this frame: the model's rects plus the
    // strip view spring's offset. The offset is never baked into the model
    // (it changes every frame of a scroll; the model does not), so it is
    // applied here and, identically, in pillAt().
    //
    // The quad is positioned in the viewport's ABSOLUTE scaled-logical space,
    // which is the space KWin's projectionMatrix() is an ortho over
    // (RenderViewport builds it from scaledRenderRect, the output's logical
    // rect scaled, NOT from a 0-based device rect). mapToRenderTarget() is
    // deliberately not used for the position: it subtracts the render rect's
    // origin and applies the output transform, both of which the matrix
    // already carries, so using it displaced the pills by the output's origin
    // on every non-primary monitor. Same convention as
    // TransitionPass::drawOutputQuad, which feeds scaledRenderRect to the
    // same matrix.
    const QRectF destLogical = QRectF(entry->bounds).translated(viewOffset);
    QPointF destDevice(destLogical.x() * scale, destLogical.y() * scale);
    // At rest, snap that origin to whole device pixels. `bounds` is integral in
    // LOGICAL space, so on a fractional-scale output the scaled origin lands
    // between device pixels and GL_LINEAR resamples the entire label texture —
    // and the chip raster fits its label to within a couple of pixels, so half
    // a pixel of resampling is a large share of the stroke. Conditional on
    // purpose: snapping every frame would quantise the view spring to whole
    // device pixels and make a smooth scroll step. isNull() is the right
    // at-rest test because offsetFor() returns a default-constructed QPointF on
    // every path that is not animating, and the last frame of an animation
    // lands with a null offset, so the strip ends up snapped. std::round rather
    // than std::floor: floor would bias the whole strip up and to the left by
    // up to a device pixel, where rounding moves it by at most half of one.
    if (viewOffset.isNull()) {
        destDevice = QPointF(std::round(destDevice.x()), std::round(destDevice.y()));
    }
    // Draw the quad at the texture's own device size rather than the
    // unrounded scaled bounds: the raster ceils its size, and a quad a
    // fraction narrower would resample the whole strip by up to a pixel.
    const QSizeF quadSize(entry->texture->size());
    if (quadSize.isEmpty()) {
        return false;
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

    QMatrix4x4 mvp = viewport.projectionMatrix();
    mvp.translate(float(destDevice.x()), float(destDevice.y()));

    // MapTexture | TransformColorspace: the raster is sRGB (a QPainter image)
    // and the target may not be. KWin's own present path converts every
    // sRGB texture into the target's colour description through this trait
    // plus setColorspaceUniforms; without it the pills are written verbatim
    // into the blending space and read at the wrong brightness/saturation
    // on an HDR or wide-gamut output (the same bug class the decoration and
    // shader-transition present shaders already carry the fix for). On an
    // SDR sRGB target the conversion is the identity.
    KWin::ShaderBinder binder(KWin::ShaderTrait::MapTexture | KWin::ShaderTrait::TransformColorspace);
    KWin::GLShader* const shader = binder.shader();
    if (!shader) {
        return false; // no program bound: a quad drawn now would land nowhere sane
    }
    shader->setUniform(KWin::GLShader::Mat4Uniform::ModelViewProjectionMatrix, mvp);
    shader->setColorspaceUniforms(KWin::ColorDescription::sRGB, renderTarget.colorDescription(),
                                  KWin::RenderingIntent::Perceptual);
    glActiveTexture(GL_TEXTURE0);
    // render() rather than a hand-rolled quad: it applies the texture's own
    // content transform, and an image upload is Y-flipped relative to GL's
    // origin. Hardware-clipped to the walk's DEVICE region: KWin's scissor
    // path takes framebuffer-space rects (it flips Y itself against the
    // current framebuffer height), which is exactly the space the per-window
    // damage region is in, so the region is passed through untranslated —
    // the quad's own placement rides the MVP and the scissor is independent
    // of it. Pixels outside the region must not be painted (see the header).
    //
    // GLVertexBuffer's hardwareClipping arm sets the scissor BOX per rect and
    // draws once per rect, and leaves enabling GL_SCISSOR_TEST to the caller
    // (glvertexbuffer.h says so). With the test off the quad would land
    // unclipped AND be blended once per region rect, progressively darkening
    // every translucent pixel. Enabled here; the ScopedGlState guard above
    // restores the prior enable bit and box on exit. render() binds and
    // unbinds the texture itself.
    glEnable(GL_SCISSOR_TEST);
    entry->texture->render(clipRegion, quadSize, /*hardwareClipping=*/true);
    return true;
}

} // namespace PlasmaZones
