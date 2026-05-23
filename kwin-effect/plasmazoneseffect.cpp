// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"

#include <QEvent>
#include <QKeyEvent>
#include <QLoggingCategory>

#include <effect/effect.h>

#include "dragtracker.h"
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
    // This effect is a compositor plugin that works in KWin on Wayland
    // Note: PlasmaZones daemon requires Wayland with layer-shell support
    return true;
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
}

bool PlasmaZonesEffect::isActive() const
{
    // Critical: include `!m_shaderManager.m_shaderTransitions.empty()` here. KWin calls
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
    return m_dragTracker->isDragging() || m_windowAnimator->hasActiveAnimations()
        || !m_shaderManager.m_shaderTransitions.empty();
}

void PlasmaZonesEffect::grabbedKeyboardEvent(QKeyEvent* e)
{
    if (e->type() == QEvent::KeyPress && e->key() == Qt::Key_Escape && m_dragTracker->isDragging()) {
        // The keyboard grab ensures this runs before KWin's MoveResizeFilter,
        // so Escape never reaches the interactive move handler. The daemon
        // hides the overlay and sets snapCancelled; the drag continues as
        // a plain window move without zone snapping.
        qCInfo(lcEffect) << "Drag escape: overlay hidden, drag continues";
        callCancelSnap();
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
