// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The ONLY place in this tree that differs between Plasma 6.7 and 6.8. Everything
// else — every override body, every capture site — is written once against the
// 6.8 shape and compiles unchanged on both, because these wrappers give 6.7 the
// same signatures.
//
// Three things changed in 6.8 (KWin 6.7.90, Plasma 6.8 Beta 1):
//
//   1. Effect::paintScreen / paintWindow / drawWindow return bool instead of
//      void. False means the paint FAILED, in practice a GPU reset, and upstream's
//      contract is that the effect stops rendering immediately. 6.7 cannot report
//      failure at all, so the wrappers below answer true there: on 6.7 the
//      failure-handling arms in this tree are simply never taken, which is exactly
//      the behaviour 6.7 had before any of this existed.
//
//   2. RenderDevice is a NEW type. 6.7 has no render-device concept whatsoever —
//      neither the class nor RenderView::renderDevice() exists there — so on 6.7
//      renderDeviceOf() below is always null, and null therefore does not mean
//      "no device available to draw with". Callers must not test it themselves.
//
//   3. Scene::renderer() became renderer(RenderDevice*), because an ItemRenderer
//      is now per render device rather than per scene. 6.7 keeps exactly one, so
//      the wrapper ignores the device there.
//
// PLASMAZONES_KWIN_PAINT_RETURNS_BOOL is set by kwin-effect/CMakeLists.txt from
// the KWin version it found. See that file for the condition under which the 6.7
// half of this header is deleted.

#include <effect/effecthandler.h>
#include <effect/offscreeneffect.h>
#include <scene/scene.h>

#if !defined(PLASMAZONES_KWIN_PAINT_RETURNS_BOOL)
#error                                                                                                                 \
    "PLASMAZONES_KWIN_PAINT_RETURNS_BOOL is not defined - "                                                         \
    "kwin-effect/CMakeLists.txt sets it from the detected KWin version"
#endif

namespace KWin {
/// 6.8-only type: 6.7 has no render-device concept at all, so on 6.7 this stays an
/// incomplete type that only ever appears as a pointer nobody dereferences.
///
/// `class`, matching upstream's own `class KWIN_EXPORT RenderDevice : public QObject`
/// in core/renderdevice.h — a struct/class mismatch would be a warning on some
/// compilers. Three other headers in this tree (plasmazoneseffect.h,
/// pointer/pointerdecorationpass.h and transitions/transitionpasshelpers.h)
/// declare it identically rather than including this file for it, and that
/// duplication is deliberate: a header should not have to pull in the whole
/// compat layer, with its version-macro requirement, to name a pointer type.
class RenderDevice;
}

namespace PlasmaZones::KWinCompat {

/// The return type of this effect's own paint hooks: bool on 6.8, void on 6.7.
/// The overrides are thin adapters over *Impl() methods that always return bool,
/// so only the adapters mention this.
#if PLASMAZONES_KWIN_PAINT_RETURNS_BOOL
using PaintResult = bool;
#else
using PaintResult = void;
#endif

/// Hand an *Impl() result back through an override of PaintResult type. On 6.7
/// the value is dropped, since the API has nowhere to put it.
inline PaintResult paintResult([[maybe_unused]] bool ok)
{
#if PLASMAZONES_KWIN_PAINT_RETURNS_BOOL
    return ok;
#endif
}

/// effects->paintScreen, reporting whether the scene walk succeeded. Always true
/// on 6.7.
[[nodiscard]] inline bool paintScreenChecked(const KWin::RenderTarget& renderTarget,
                                             const KWin::RenderViewport& viewport, int mask,
                                             const KWin::Region& deviceRegion, KWin::LogicalOutput* screen)
{
#if PLASMAZONES_KWIN_PAINT_RETURNS_BOOL
    return KWin::effects->paintScreen(renderTarget, viewport, mask, deviceRegion, screen);
#else
    KWin::effects->paintScreen(renderTarget, viewport, mask, deviceRegion, screen);
    return true;
#endif
}

/// effects->paintWindow. Always true on 6.7.
[[nodiscard]] inline bool paintWindowChecked(const KWin::RenderTarget& renderTarget,
                                             const KWin::RenderViewport& viewport, KWin::EffectWindow* w, int mask,
                                             const KWin::Region& deviceRegion, KWin::WindowPaintData& data)
{
#if PLASMAZONES_KWIN_PAINT_RETURNS_BOOL
    return KWin::effects->paintWindow(renderTarget, viewport, w, mask, deviceRegion, data);
#else
    KWin::effects->paintWindow(renderTarget, viewport, w, mask, deviceRegion, data);
    return true;
#endif
}

/// effects->drawWindow. Always true on 6.7. Used by every offscreen capture site
/// as well as the on-screen draw arms.
[[nodiscard]] inline bool drawWindowChecked(const KWin::RenderTarget& renderTarget,
                                            const KWin::RenderViewport& viewport, KWin::EffectWindow* w, int mask,
                                            const KWin::Region& deviceRegion, KWin::WindowPaintData& data)
{
#if PLASMAZONES_KWIN_PAINT_RETURNS_BOOL
    return KWin::effects->drawWindow(renderTarget, viewport, w, mask, deviceRegion, data);
#else
    KWin::effects->drawWindow(renderTarget, viewport, w, mask, deviceRegion, data);
    return true;
#endif
}

/// The base-class chain call from inside our own drawWindow override, yielding
/// whether it succeeded — always true on 6.7.
///
/// A MACRO rather than a function, unlike everything else here, because
/// OffscreenEffect::drawWindow is PROTECTED: C++ checks access at the point the
/// call is written, so only a member of the derived class may make it, and a free
/// function taking the effect by reference cannot (the compiler says exactly that).
/// Keeping it here anyway is deliberate — every 6.7-vs-6.8 difference stays in this
/// one file, so deleting the file is all the eventual cleanup needs. Expands to a
/// bool expression; use it as `const bool drawn = PLASMAZONES_OFFSCREEN_DRAW_WINDOW(...)`.
///
/// Defined OUTSIDE the namespace below, with every other macro here: the
/// preprocessor has no notion of namespaces, so a #define inside one only misleads
/// a reader into thinking it is scoped.
} // namespace PlasmaZones::KWinCompat

#if PLASMAZONES_KWIN_PAINT_RETURNS_BOOL
#define PLASMAZONES_OFFSCREEN_DRAW_WINDOW(...) (KWin::OffscreenEffect::drawWindow(__VA_ARGS__))
#else
#define PLASMAZONES_OFFSCREEN_DRAW_WINDOW(...) ((KWin::OffscreenEffect::drawWindow(__VA_ARGS__)), true)
#endif

/// Whether a call returning KWinCompat::PaintResult succeeded. A MACRO for the same
/// reason PaintResult itself cannot simply be a bool: on 6.7 the expression's type
/// is `void`, which no function could accept as an argument. Use it to check any of
/// this effect's OWN paint hooks when one is called directly — the transition passes
/// drive paintWindow that way to composite a window into a capture.
///
/// Always true on 6.7, where the hooks report nothing.
#if PLASMAZONES_KWIN_PAINT_RETURNS_BOOL
#define PLASMAZONES_PAINT_OK(...) (__VA_ARGS__)
#else
#define PLASMAZONES_PAINT_OK(...) ((__VA_ARGS__), true)
#endif

namespace PlasmaZones::KWinCompat {

/// The render device @p view is painting on. 6.8-only: on 6.7 neither RenderDevice
/// nor RenderView::renderDevice() exists, so this is always nullptr there — which
/// sceneRenderer() below accepts, because 6.7 has a single renderer to return
/// regardless. Callers must therefore NOT treat a null device as "cannot draw".
[[nodiscard]] inline KWin::RenderDevice* renderDeviceOf([[maybe_unused]] KWin::RenderView* view)
{
#if PLASMAZONES_KWIN_PAINT_RETURNS_BOOL
    return view ? view->renderDevice() : nullptr;
#else
    return nullptr;
#endif
}

/// The scene's ItemRenderer for @p device. 6.7 has one renderer per scene and
/// ignores the device; 6.8 keys them by device, and passing the wrong one draws
/// with another GPU's renderer. Callers pass the device of the pass they are in.
///
/// The one place the two versions differ in BEHAVIOUR rather than just in
/// signature: on 6.8 a null device yields a null renderer, while on 6.7 the same
/// call always returns the scene's only renderer. A caller's null-renderer arm is
/// therefore reachable on 6.8 alone and a 6.7 build can never exercise it, so it
/// must fail loudly rather than quietly. This is not a contradiction of the file
/// header above: the SOURCE is written once, and it is the 6.7 half that cannot
/// take the arm.
[[nodiscard]] inline KWin::ItemRenderer* sceneRenderer(KWin::Scene* scene, [[maybe_unused]] KWin::RenderDevice* device)
{
#if PLASMAZONES_KWIN_PAINT_RETURNS_BOOL
    return device ? scene->renderer(device) : nullptr;
#else
    return scene->renderer();
#endif
}

} // namespace PlasmaZones::KWinCompat
