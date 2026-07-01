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
    // `m_shaderManager.hasOpacityRules()` is included for the same
    // chain-exclusion reason, but the failure mode is the opposite of a
    // one-shot transition: a SetOpacity rule is a PERSISTENT per-window
    // appearance change that prePaintWindow/paintWindow must apply on
    // every frame the matched window is painted. The transient triggers
    // above only hold isActive() true while a drag/animation/transition
    // is in flight; the instant they settle KWin drops the effect from
    // the chain and `data.setOpacity()` stops running, so the window
    // snaps back to full opacity until the next interaction spins a
    // transition back up. Gating on hasOpacityRules() keeps the effect
    // in the chain for as long as any enabled opacity rule exists, so
    // the dim survives idle. This does not force continuous repaints —
    // KWin still only composites on damage; isActive() merely keeps the
    // effect consulted (and the window composited rather than
    // direct-scanned-out) when a frame is produced.
    //
    // `!m_windowBorders.isEmpty()` is the SAME persistent case as opacity
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
        || m_shaderManager.hasOpacityRules() || !m_windowBorders.isEmpty();
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
