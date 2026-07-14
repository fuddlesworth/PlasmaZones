// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "desktoptransitionmanager.h"

#include "plasmazoneseffect.h"
#include "plasmazoneseffect/shader_internal.h"

#include <core/output.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <opengl/eglcontext.h>
#include <opengl/glframebuffer.h>
#include <opengl/gltexture.h>
#include <scene/windowitem.h>

#include <QPoint>
#include <QRectF>
#include <QScopeGuard>
#include <QSize>

#include <memory>

// The capture half of DesktopTransitionManager: how a desktop BECOMES a texture.
// desktoptransitionmanager.cpp keeps the lifecycle half (blend, settle,
// teardown) and desktoptransitionshader.cpp the assembly half (pack source →
// compiled GLShader). The capture paths and the texture allocation/format
// helpers they share only serve the first question, so they live here.
namespace PlasmaZones {

namespace {
// The internal format a desktop capture must use: whatever KWin is blending the
// output in.
//
// Hardcoding GL_RGBA8 (as this did) is wrong on an HDR / wide-gamut output. KWin
// blends there in the output's container colorimetry with 1.0 mapped onto the
// display's peak luminance, so 8-bit sRGB values written verbatim land dim and
// desaturated — a desktop switch flashed the wrong brightness. Inheriting the
// target's format is the idiom KWin's own blur and screen-transform use. Every
// format KWin maps a DRM format to carries alpha, so this never silently drops it.
//
// Reached through framebuffer() rather than RenderTarget::texture(): that
// accessor dereferences the framebuffer unconditionally, and it is null on an
// image-backed target. PlasmaZonesEffect::supported() now requires OpenGL
// compositing so that cannot happen, but this stays honest rather than resting on
// a guarantee made in another file.
GLenum captureFormatFor(const KWin::RenderTarget& outputTarget)
{
    const KWin::GLFramebuffer* const fb = outputTarget.framebuffer();
    const KWin::GLTexture* const targetTex = fb ? fb->colorAttachment() : nullptr;
    return targetTex ? targetTex->internalFormat() : GL_RGBA8;
}

// Allocate a capture texture of @p deviceSize in @p internalFormat (LINEAR
// filter, CLAMP_TO_EDGE) — the shared preamble of both desktop captures. Returns
// null when the size is empty or GL allocation fails.
std::unique_ptr<KWin::GLTexture> allocateOutputTexture(const QSize& deviceSize, GLenum internalFormat)
{
    if (deviceSize.isEmpty()) {
        return nullptr;
    }
    std::unique_ptr<KWin::GLTexture> tex = KWin::GLTexture::allocate(internalFormat, deviceSize);
    if (!tex) {
        return nullptr;
    }
    tex->setFilter(GL_LINEAR);
    tex->setWrapMode(GL_CLAMP_TO_EDGE);
    return tex;
}

} // namespace

std::unique_ptr<KWin::GLTexture> DesktopTransitionManager::captureDesktop(KWin::VirtualDesktop* desktop,
                                                                          KWin::LogicalOutput* screen,
                                                                          const KWin::RenderTarget& outputTarget,
                                                                          const KWin::RenderViewport& outputViewport)
{
    const qreal scale = screen->scale();
    const QRectF logicalGeometry = screen->geometryF();

    // Never leak the capture's GL state (blend/viewport/clear/active texture)
    // into the on-screen draw that follows in this same frame.
    const ShaderInternal::ScopedGlState glStateGuard;

    // Size from the OUTPUT viewport rather than re-deriving it: deviceSize() is
    // scaledRenderRect().size(), which is how KWin itself rounds. Rounding the
    // logical size per-component instead (the old deviceSizeForOutput) can differ
    // by a pixel on a fractional scale with a non-zero origin, which would leave
    // the capture texture and the blend quad disagreeing by that pixel.
    std::unique_ptr<KWin::GLTexture> tex =
        allocateOutputTexture(outputViewport.deviceSize(), captureFormatFor(outputTarget));
    if (!tex) {
        return nullptr;
    }
    KWin::GLFramebuffer fbo(tex.get());
    if (!fbo.valid()) {
        return nullptr;
    }

    // NOTE: m_capturingSnapshot is deliberately NOT set here.
    //
    // It used to be, and that is what stripped the borders. The flag means "we
    // are inside our own offscreen capture of ONE window — do not run per-window
    // processing on the nested draw", and it guards a real re-entrancy hazard for
    // the per-window sites (the composite fold's nested effects->drawWindow would
    // otherwise recurse into renderSurfaceChainComposite). Here it guarded
    // nothing: this loop drove windows through effects->drawWindow directly, so
    // paintWindow, the fold and the backdrop capture were never entered at all.
    // All the flag actually did was suppress the present-bind in drawWindow, so
    // the OUTGOING texture carried no decorations while the INCOMING one (captured
    // via effects->paintScreen, which re-enters our paintWindow) carried them —
    // and every border popped off the instant a switch began.
    //
    // It must also STAY false: the fold toggles the same bool internally for its
    // own raw capture and clears it unconditionally on the way out, so holding it
    // across this loop would have it silently cleared by the first folded window.
    {
        // Capture in the OUTPUT's colour space, not the sRGB default. The window
        // content is converted into whatever space this target declares, and the
        // blend later writes those values verbatim into KWin's output target — so
        // declaring sRGB here while the compositor blends in a wide-gamut float
        // space is exactly the mismatch that made HDR desktop switches flash the
        // wrong brightness. Matching the space makes the blend a pass-through.
        KWin::RenderTarget renderTarget(&fbo, outputTarget.colorDescription());
        KWin::RenderViewport viewport(logicalGeometry, scale, renderTarget, QPoint());
        KWin::GLFramebuffer::pushFramebuffer(&fbo);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Bottom-to-top: the wallpaper (an on-all-desktops window) lands first
        // and app windows composite over it. isHiddenByShowDesktop: a capture
        // taken while a peek is active must not bake the hidden windows into
        // the transition texture — the scene isn't painting them, so the
        // capture shouldn't either.
        // No isDeleted exclusion here, unlike the peek filter: a mid-close
        // window still visible on the outgoing desktop belongs in its capture
        // (matching KWin's close-animation semantics), while the peek filter
        // must never resurrect a deleted window off a lingering
        // hiddenByShowDesktop flag.
        compositeWindowsInto(renderTarget, viewport, logicalGeometry, [desktop](KWin::EffectWindow* w) {
            return !w->isMinimized() && !w->isHiddenByShowDesktop()
                && (w->isOnDesktop(desktop) || w->isOnAllDesktops());
        });

        KWin::GLFramebuffer::popFramebuffer();
    }

    return tex;
}

void DesktopTransitionManager::compositeWindowsInto(const KWin::RenderTarget& renderTarget,
                                                    const KWin::RenderViewport& viewport, const QRectF& logicalGeometry,
                                                    const std::function<bool(KWin::EffectWindow*)>& includeWindow)
{
    // Direct-drive mode for the loop below: paintWindow's tail must
    // terminate with a raw draw instead of continuing the paintWindow
    // chain (see m_directPaintCapture's doc — the chain iterator is at
    // begin() here, so chaining would re-enter paintWindow and drive
    // later effects without prePaintWindow). Scope-guarded so a throw
    // from the draw chain cannot leak the mode into live painting.
    m_effect->m_directPaintCapture = true;
    const auto directPaintGuard = qScopeGuard([this] {
        m_effect->m_directPaintCapture = false;
    });

    // Bottom-to-top: stackingOrder() is already bottom→top. Windows outside
    // this output are clipped by the viewport.
    const QList<KWin::EffectWindow*> stack = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : stack) {
        if (!w || !includeWindow(w)) {
            continue;
        }
        if (!w->expandedGeometry().intersects(logicalGeometry)) {
            continue;
        }
        KWin::ItemEffect keepRenderable(w->windowItem());
        KWin::WindowPaintData captureData;
        captureData.setOpacity(1.0);
        const int captureMask = KWin::Effect::PAINT_WINDOW_TRANSFORMED | KWin::Effect::PAINT_WINDOW_TRANSLUCENT;
        // Drive the window through OUR OWN per-window pipeline, exactly as the
        // live scene does for the incoming desktop (captureLiveScene →
        // effects->paintScreen → scene → our paintWindow). paintWindow builds
        // the decoration composite, and under m_directPaintCapture (set
        // above) its tail terminates with effects->drawWindow — the same
        // call this used to make directly — whose present branch then
        // binds that FRESH composite. Going straight to effects->drawWindow
        // skipped all of it, so the outgoing texture lost not just borders but
        // rule opacity, the animator's translate/scale, and any in-flight
        // transition's true progress.
        //
        // NOT effects->paintWindow (the whole chain): these windows were not in
        // this frame's scene walk, so they never got prePaintWindow, and a
        // third-party paintWindow hook that keys off that state would be driven
        // with none. Our paintWindow explicitly tolerates the missing
        // prePaintWindow (it falls back to a live opacity resolve).
        //
        // Stepping an in-flight transition's spring here is safe. Within a
        // frame it cannot double-step: paintWindow's dt comes from the frame
        // clock pinned in prePaintScreen, and the first step of a frame
        // stamps lastPaintTimeMs to that same pinned value, so a second call
        // sees dt = 0. Across frames an extra step is a no-op by
        // construction: Spring::step is an exact exponential integrator, so
        // step(a) then step(b) lands bit-for-bit where step(a+b) does.
        m_effect->paintWindow(renderTarget, viewport, w, captureMask, KWin::Region::infinite(), captureData);
    }
}

std::unique_ptr<KWin::GLTexture> DesktopTransitionManager::captureLiveScene(int mask, KWin::LogicalOutput* screen,
                                                                            const KWin::RenderTarget& outputTarget,
                                                                            const KWin::RenderViewport& outputViewport)
{
    const qreal scale = screen->scale();
    const QRectF logicalGeometry = screen->geometryF();

    const ShaderInternal::ScopedGlState glStateGuard;

    // Same size / format / colour space as the outgoing capture — see
    // captureDesktop. The two textures are blended against each other and then
    // written into the output target, so all three must agree.
    std::unique_ptr<KWin::GLTexture> tex =
        allocateOutputTexture(outputViewport.deviceSize(), captureFormatFor(outputTarget));
    if (!tex) {
        return nullptr;
    }
    KWin::GLFramebuffer fbo(tex.get());
    if (!fbo.valid()) {
        return nullptr;
    }

    {
        KWin::RenderTarget renderTarget(&fbo, outputTarget.colorDescription());
        KWin::RenderViewport viewport(logicalGeometry, scale, renderTarget, QPoint());
        KWin::GLFramebuffer::pushFramebuffer(&fbo);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        // Render the live scene (the now-current INCOMING desktop) into the FBO
        // via KWin's own composite. This is the downstream chain call (effects
        // below us + the scene), NOT a re-entry into our own paintScreen.
        //
        // The region parameter is DEVICE space, and this FBO's device space starts
        // at (0, 0) because the viewport above is built with a QPoint() render
        // offset. screen->geometry() is LOGICAL and output-positioned, so on a 2x
        // output it would cover half the FBO, and on any secondary monitor (origin
        // 1920,0) it would not intersect the FBO at all. Inert today only because
        // our prePaintScreen sets PAINT_SCREEN_TRANSFORMED, which routes the scene
        // through the generic infinite-region path — the moment that mask bit
        // changes, the second monitor's incoming capture goes black.
        KWin::effects->paintScreen(renderTarget, viewport, mask,
                                   KWin::Region(KWin::Rect(QPoint(), viewport.deviceSize())), screen);
        KWin::GLFramebuffer::popFramebuffer();
    }
    return tex;
}

std::unique_ptr<KWin::GLTexture>
DesktopTransitionManager::capturePeekWindowsScene(KWin::GLTexture* bareDesktop, int mask, KWin::LogicalOutput* screen,
                                                  const KWin::RenderTarget& outputTarget,
                                                  const KWin::RenderViewport& outputViewport)
{
    // The peek HIDE leg's "windows scene" endpoint. showingDesktopChanged fires
    // AFTER KWin marks the windows hiddenByShowDesktop and their WindowItems
    // invisible, and this frame's scene walk was already built in prePaint
    // WITHOUT them — so a live-scene capture cannot show the windows, no matter
    // what visibility refs are taken now (a ref changes NEXT frame's walk, not
    // this one's; the first attempt did exactly that and the transition's FROM
    // came out as the bare desktop, making the peek look like an instant hide
    // followed by a decorative sweep).
    //
    // Instead the windows scene is reconstructed in two layers:
    //   1. the bare desktop — wallpaper, docks, panels — blitted from the
    //      already-captured TO texture (raster copy: same size, format and
    //      colour space by construction), or re-rendered via the live scene
    //      when no blit is possible;
    //   2. every show-desktop-hidden window composited back on top through the
    //      direct per-window path, exactly how captureDesktop reconstructs the
    //      outgoing desktop's non-scene windows. Being out of the scene walk is
    //      the case that path is built for (the black-render problem only
    //      affects windows the live scene IS painting this frame).
    //
    // Known simplification: the hidden windows land ABOVE the docks/panels
    // baked into layer 1, so a window that overlapped a panel paints over it
    // for the transition's duration instead of under it. The packs start
    // shrinking/fading the windows layer immediately, so this reads correctly.
    const qreal scale = screen->scale();
    const QRectF logicalGeometry = screen->geometryF();

    const ShaderInternal::ScopedGlState glStateGuard;

    std::unique_ptr<KWin::GLTexture> tex =
        allocateOutputTexture(outputViewport.deviceSize(), captureFormatFor(outputTarget));
    if (!tex) {
        return nullptr;
    }
    KWin::GLFramebuffer fbo(tex.get());
    if (!fbo.valid()) {
        return nullptr;
    }

    {
        KWin::RenderTarget renderTarget(&fbo, outputTarget.colorDescription());
        KWin::RenderViewport viewport(logicalGeometry, scale, renderTarget, QPoint());
        KWin::GLFramebuffer::pushFramebuffer(&fbo);

        // Layer 1: the bare desktop. Prefer a straight framebuffer blit from
        // the caller's TO capture — a raster copy has no orientation or
        // colour-space semantics to get wrong and skips a second full scene
        // render. Fall back to re-rendering the live scene (identical output,
        // one extra scene pass) when there is no source texture or the
        // hardware cannot blit.
        // Blit capability lives on the GL context in this KWin (the
        // GLFramebuffer doc still names the old blitSupported() accessor);
        // desktop framebuffer blits need GL 3.0 / ARB_framebuffer_object,
        // which supportsBlits() reports.
        const KWin::EglContext* const glContext = KWin::EglContext::currentContext();
        bool baseCopied = false;
        if (bareDesktop && bareDesktop->size() == tex->size() && glContext && glContext->supportsBlits()) {
            KWin::GLFramebuffer srcFbo(bareDesktop);
            if (srcFbo.valid()) {
                // blitFromFramebuffer reads from the CURRENT framebuffer and
                // draws into the object it is called on.
                KWin::GLFramebuffer::pushFramebuffer(&srcFbo);
                fbo.blitFromFramebuffer(KWin::Rect(), KWin::Rect(), GL_NEAREST);
                KWin::GLFramebuffer::popFramebuffer();
                baseCopied = true;
            }
        }
        if (!baseCopied) {
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            // Same call and device-space region reasoning as captureLiveScene.
            KWin::effects->paintScreen(renderTarget, viewport, mask,
                                       KWin::Region(KWin::Rect(QPoint(), viewport.deviceSize())), screen);
        }

        // Layer 2: the hidden windows, bottom-to-top in stacking order through
        // the direct-drive per-window pipeline (see compositeWindowsInto for
        // why the tail must terminate with a raw draw here). isOnCurrentDesktop
        // already covers on-all-desktops windows, but no such window can be
        // hiddenByShowDesktop anyway (the wallpaper and docks are exempt from
        // show-desktop hiding).
        compositeWindowsInto(renderTarget, viewport, logicalGeometry, [](KWin::EffectWindow* w) {
            return w->isHiddenByShowDesktop() && !w->isDeleted() && !w->isMinimized() && w->isOnCurrentDesktop();
        });

        KWin::GLFramebuffer::popFramebuffer();
    }
    return tex;
}

} // namespace PlasmaZones
