// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"

#include <QEvent>
#include <QKeyEvent>
#include <QLoggingCategory>

#include <effect/effect.h>

#include "dragtracker.h"
#include "snaphandler.h"
#include "windowanimator.h"

namespace PlasmaZones {

// `lcEffect` is the canonical logging category for this plugin. The storage is
// emitted here; sibling translation units in `plasmazoneseffect/*.cpp`
// re-declare the category via `Q_DECLARE_LOGGING_CATEGORY(lcEffect)` in their
// own namespace scope so they can log under the same name without each TU
// emitting its own definition.
Q_LOGGING_CATEGORY(lcEffect, "plasmazones.effect", QtInfoMsg)

// Opt-in window-classification diagnostics. Defaults to QtWarningMsg so the
// per-window property dump (logWindowDiagnostics) is silent unless explicitly
// enabled with QT_LOGGING_RULES="plasmazones.effect.diag.debug=true" — keeps
// the journal clean by default while still letting users reproduce Steam/CEF
// mis-classification on request.
Q_LOGGING_CATEGORY(lcEffectDiag, "plasmazones.effect.diag", QtWarningMsg)

bool PlasmaZonesEffect::supported()
{
    // OpenGL compositing is a hard requirement, not a preference: every render
    // path in this effect is GL (GLShader / GLFramebuffer / GLTexture, the
    // decoration composite fold, the transition shaders, the desktop blend).
    // Under QPainter compositing KWin hands effects an image-backed RenderTarget
    // whose framebuffer() is null, so the first thing that reaches for it —
    // RenderTarget::texture(), which dereferences it — would crash. Mirrors the
    // guard KWin's own GL-only effects (blur, screen transform) carry.
    //
    // The daemon additionally requires Wayland with layer-shell support.
    return KWin::effects && KWin::effects->isOpenGLCompositing();
}

bool PlasmaZonesEffect::enabledByDefault()
{
    return true;
}

void PlasmaZonesEffect::reconfigure(ReconfigureFlags flags)
{
    Q_UNUSED(flags)
    // Called when KWin wants effects to reload or when daemon notifies of settings change
    qCDebug(lcEffect) << "reconfigure() called";
    // A KWin effects reconfigure (any Desktop Effects KCM apply) reconciles
    // the loaded-effects list against kwinrc, which RE-LOADS windowaperture /
    // eyeonscreen — the suppression never writes kwinrc, so this is the one
    // path that undoes the unload without any of the other sync triggers
    // (tree load, registry commit, animations toggle) firing. Re-assert.
    syncShowDesktopEffectSuppression();
}

bool PlasmaZonesEffect::isActive() const
{
    // Critical: include `!m_shaderManager.empty()` here. KWin calls
    // isActive() before each paint cycle and EXCLUDES the effect from
    // the chain when it returns false — meaning prePaintScreen and
    // paintWindow are never called, so a shader transition installed
    // via beginShaderTransition would never get a frame to advance on.
    //
    // Without this clause, the only paths that wake the chain are
    // (a) interactive drag (`m_dragTracker->isDragging()`) and
    // (b) zone-snap reflow animations (`m_windowAnimator`).
    // window.move works through (a) because the drag holds isActive()
    // true; every other lifecycle event (focus/open/close/minimize/
    // maximize/resize) installs a shader transition only — without this
    // clause those events would resolve cleanly, redirect the window,
    // and then sit unrendered until the timer-driven teardown fired.
    //
    // `hasOpacityRules()` is deliberately NOT an activation clause.
    // SetOpacity is layer-backed: a persistent per-window dim exists only
    // as the opacity-tint layer's folded pack param, and any window
    // carrying that layer sits in m_windowDecorations — the clause below
    // already holds the effect active for it. The only per-frame rule
    // consumer left is prePaintWindow's frame-opacity cache, which feeds
    // the shader-transition draw's bare-uTexture0 fallback; a transition
    // in flight holds isActive() true via `!m_shaderManager.empty()`. A
    // SetOpacity rule with no layered window and no transition has no
    // paint-path consumer at all (the rule is inert by design), so it
    // must not keep the effect in the chain.
    //
    // `!m_windowDecorations.isEmpty()` is the SAME persistent case as opacity
    // rules: a per-window border is rendered passively in drawWindow by
    // re-blitting the redirected window through the border shader on every
    // composite (the KDE-Rounded-Corners / LightlyShaders model). Those
    // effects keep isActive() true whenever they manage a window; without
    // this clause an idle bordered window (no drag/animation/transition)
    // drops the effect from the chain, drawWindow is never called, and the
    // border only appears while some OTHER trigger (an animation) holds the
    // effect active. Gating on a non-empty border set keeps the effect
    // consulted for as long as any window has a border, so the border
    // survives idle. O(1) — a QHash emptiness check, safe in this per-frame
    // hot path.
    return m_dragTracker->isDragging() || m_windowAnimator->hasActiveAnimations() || !m_shaderManager.empty()
        || !m_windowDecorations.isEmpty() || m_desktopTransition.isRunning();
}

void PlasmaZonesEffect::grabbedKeyboardEvent(QKeyEvent* e)
{
    if (e->type() == QEvent::KeyPress && e->key() == Qt::Key_Escape && m_dragTracker->isDragging()) {
        // The keyboard grab ensures this runs before KWin's MoveResizeFilter,
        // so Escape never reaches the interactive move handler. The daemon
        // hides the overlay and sets snapCancelled; the drag continues as
        // a plain window move without zone snapping.
        qCInfo(lcEffect) << "Drag escape: overlay hidden, drag continues";
        m_snapHandler->callCancelSnap();
    }
    // All other keys are silently consumed by the grab. Modifier state is
    // unaffected because mouseChanged reads xkb state directly.
}

} // namespace PlasmaZones

// KWin Effect Factory - creates the plugin

namespace KWin {

KWIN_EFFECT_FACTORY_SUPPORTED(PlasmaZones::PlasmaZonesEffect, "metadata.json",
                              return PlasmaZones::PlasmaZonesEffect::supported();)

} // namespace KWin

// MOC include - REQUIRED for the Q_OBJECT in KWIN_EFFECT_FACTORY_SUPPORTED
#include "plasmazoneseffect.moc"
