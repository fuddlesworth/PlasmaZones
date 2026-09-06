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

#include <algorithm>
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
        entry.textureDeviceOrigin = QPoint();
        entry.textureRenderOrigin = QPointF();
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
        entry->textureDeviceOrigin = QPoint();
        entry->textureRenderOrigin = QPointF();
        entry->textureScale = 0.0;
        entry->dirty = true;
        entry->hoverDirtyRects.clear();
        return false;
    }

    const qreal scale = viewport.scale() > 0.0 ? viewport.scale() : 1.0;
    // Every device-grid calculation below is anchored HERE, at the viewport's
    // own render rect, not at the absolute logical origin. The device grid
    // this pass draws on is the render target's, and the target's first
    // column is renderRect's top-left — so "on the grid" means a whole number
    // of device pixels FROM THAT CORNER, and anywhere else only agrees with
    // it by luck.
    //
    // It is not luck on the primary output, which is why an absolute
    // floor(x * scale) looked right: renderRect starts at (0, 0) there and
    // the two anchors coincide. They part company on any other output.
    //
    // Both halves of that, in KWin's own terms. The damage box is
    // Scene::addLogicalRepaint -> RenderView::mapToDeviceCoordinatesAligned,
    // which is (logical - viewport().topLeft()) * scale + renderOffset, then
    // roundedOut() — output-relative, floored on the origin and ceiled on the
    // far edge. The quad's space is RenderViewport's ortho over
    // m_scaledRenderRect, which is renderRect.scaled(scale).ROUNDED, so an
    // ortho coordinate c lands on framebuffer column renderOffset.x() + (c -
    // scaledRenderRect.x()). WorkspaceScene::paint builds that viewport from
    // the same delegate viewport(), scale() and offset the damage used, so
    // measuring from renderRect makes the two expressions identical.
    //
    // An ABSOLUTE floor() does not survive the round: a second monitor at
    // logical x=1670 on a 1.15 output has its ortho origin at 1921 covering
    // 1920.5, so the quad lands a fraction off the target grid and can
    // disagree with the damage box by a whole column — the defect this
    // alignment exists to close, reappearing everywhere but the primary.
    const QPointF renderOrigin = viewport.renderRect().topLeft();
    const bool geometryChanged = entry->textureBounds != entry->bounds || entry->textureScale != scale
        || entry->textureRenderOrigin != renderOrigin;
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
        // The texture covers the band's DEVICE-ALIGNED box, measured from the
        // render rect's corner (see the anchor note above): the origin floored
        // and the far edge ceiled onto the device grid. That is the same box
        // KWin's damage alignment produces from the logical band the caller
        // hands addRepaint (which takes a logical region — the parameter is
        // named so in effecthandler.h), so the quad below lands exactly on the
        // pixels this pass is allowed to touch, and no border column can fall
        // outside one box while inside the other.
        //
        // The band used to be rasterised at ceil(w*scale) and blitted at
        // round(x*scale), which is exact only when both scaled edges are whole.
        // At a fractional scale they generally are not, and the two boxes then
        // disagree by a device column at each end: at scale 1.15 a band at
        // x=618 w=201 damages [710,942) while the quad covered [711,943). The
        // left column was damaged but never painted by the pill (so the
        // window underneath showed through the hairline border), and the right
        // column was painted but scissored away — except on frames whose
        // damage happened to be wider, where it appeared. A border column that
        // comes and goes with the shape of unrelated damage is a flicker, and
        // it needs a fractionally-scaled output to happen at all.
        const QPointF localBounds = QPointF(entry->bounds.topLeft()) - renderOrigin;
        const QPoint deviceOrigin(int(std::floor(localBounds.x() * scale)), int(std::floor(localBounds.y() * scale)));
        const int deviceW = int(std::ceil((localBounds.x() + entry->bounds.width()) * scale)) - deviceOrigin.x();
        const int deviceH = int(std::ceil((localBounds.y() + entry->bounds.height()) * scale)) - deviceOrigin.y();
        if (m_maxTextureSize > 0 && (deviceW > m_maxTextureSize || deviceH > m_maxTextureSize)) {
            // Larger than the GPU can hold in one texture (a very wide
            // multi-column span at high scale with a large in-flight view
            // delta). Better to skip the pills than to spin on a raster
            // that cannot upload. The previous texture goes with the other
            // failure arms: it is never blitted again while the latch holds.
            entry->texture.reset();
            entry->textureBounds = QRect();
            entry->textureDeviceOrigin = QPoint();
            entry->textureRenderOrigin = QPointF();
            entry->textureScale = 0.0;
            entry->hoverDirtyRects.clear();
            entry->failedBounds = entry->bounds;
            entry->failedScale = scale;
            entry->dirty = false;
            return false;
        }
        // Rasterised through the device-addressed form, because the aligned
        // origin sits BETWEEN logical pixels whenever the floor moved it.
        const QImage image = ScrollTabRaster::rasterisePatch(entry->indicators, entry->style,
                                                             renderOrigin + QPointF(deviceOrigin) / scale,
                                                             QSize(deviceW, deviceH), scale, entry->hoveredWindowId);
        entry->texture.reset();
        entry->textureBounds = QRect();
        entry->textureScale = 0.0;
        entry->textureDeviceOrigin = QPoint();
        entry->textureRenderOrigin = QPointF();
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
        entry->textureDeviceOrigin = deviceOrigin;
        entry->textureRenderOrigin = renderOrigin;
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
            // rasterised for textureBounds at textureScale, and the patch
            // lands at the scaled delta between the two origins. That delta
            // is only whole at integral scales; at a fractional scale an
            // indicator origin generally does not fall on the device grid,
            // and glTexSubImage2D addresses whole device pixels only.
            //
            // So the PATCH is snapped to the grid rather than the offset
            // being rounded: the destination box is floored outward on the
            // origin and ceiled outward on the far edge, and the raster is
            // then asked for exactly that box in device pixels (its logical
            // origin sits between logical pixels, which is the point). The
            // patch is therefore pixel-identical to the same region of a full
            // raster, with no sub-pixel jitter to avoid.
            //
            // This used to fall back to a full re-raster whenever the delta
            // was fractional, which on a fractionally-scaled output is nearly
            // every hover: at scale 1.15 the delta is whole only every 20
            // logical pixels, on BOTH axes. That turned each hover enter and
            // leave into a band-wide QPainter pass plus a full texture
            // re-upload — exactly the cost this branch exists to avoid, and
            // silently so on the laptop iGPUs least able to absorb it.
            // Measured from the texture's own DEVICE origin, which is the
            // band's origin floored onto the grid from the render rect's
            // corner rather than the scaled logical one, so the clipped rect
            // is taken relative to that same corner first — see the alignment
            // note in the full-raster arm.
            const QPointF localClipped = QPointF(clipped.topLeft()) - renderOrigin;
            const int devX0 = int(std::floor(localClipped.x() * scale)) - entry->textureDeviceOrigin.x();
            const int devY0 = int(std::floor(localClipped.y() * scale)) - entry->textureDeviceOrigin.y();
            const int devX1 =
                int(std::ceil((localClipped.x() + clipped.width()) * scale)) - entry->textureDeviceOrigin.x();
            const int devY1 =
                int(std::ceil((localClipped.y() + clipped.height()) * scale)) - entry->textureDeviceOrigin.y();
            // Clamped to the live texture: `clipped` is inside `bounds`, so
            // the ceiled far edge can only overrun by the same rounding the
            // texture's own ceiled size already absorbed, but a sub-upload
            // that ran past the edge would be dropped whole by GL.
            const QSize textureSize = entry->texture->size();
            const int devW = std::min(devX1, textureSize.width()) - devX0;
            const int devH = std::min(devY1, textureSize.height()) - devY0;
            if (devX0 < 0 || devY0 < 0 || devW <= 0 || devH <= 0) {
                entry->dirty = true;
                break;
            }
            // The logical point the patch's top-left device pixel looks at.
            // Fractional by construction whenever the grid snap moved it.
            const QPointF patchOrigin =
                renderOrigin + QPointF(entry->textureDeviceOrigin + QPoint(devX0, devY0)) / scale;
            const QImage patch = ScrollTabRaster::rasterisePatch(entry->indicators, entry->style, patchOrigin,
                                                                 QSize(devW, devH), scale, entry->hoveredWindowId);
            if (patch.isNull()) {
                // Allocation failure on the patch: redo the frame as a full
                // raster rather than leave this indicator's old hover pixels.
                entry->dirty = true;
                break;
            }
            const QPoint offset{devX0, devY0};
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
    // AT REST the origin is the render rect's own scaled corner plus the
    // texture's device origin, verbatim: the texture was rasterised for the
    // band's device-aligned box measured from that corner, so this is a whole
    // device pixel of the render target by construction and the quad covers
    // exactly that box. scaledRenderRect() is the integer corner the ortho
    // this matrix carries is built over, which is why the offset is added to
    // it rather than to renderRect() * scale — the two differ by up to half a
    // device pixel on a fractionally-scaled output away from the origin, and
    // it is the ortho's corner that decides where column zero lands.
    // No rounding step is needed or wanted here — the alignment happened at
    // raster time, where the pixels could be drawn to match it, rather than
    // being applied to a texture already rasterised for somewhere else.
    //
    // MID-LEG the view offset is added unrounded. Snapping it would quantise
    // the view spring to whole device pixels and make a smooth scroll step,
    // and a leg damages the whole output every frame anyway (the spring's own
    // repaint pump), so the damage-box agreement the alignment buys is not at
    // stake while one is in flight. isNull() is the right at-rest test because
    // offsetFor() returns a default-constructed QPointF on every path that is
    // not animating, including an animation's last frame — so the strip ends
    // up aligned when it settles.
    QPointF destDevice(viewport.scaledRenderRect().topLeft() + entry->textureDeviceOrigin);
    if (!viewOffset.isNull()) {
        destDevice += QPointF(viewOffset.x() * scale, viewOffset.y() * scale);
    }
    // Draw the quad at the texture's own device size: it IS the aligned box's
    // size, and a quad any other size would resample the whole strip.
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
