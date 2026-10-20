// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The screen of a window that fills a split output. KWin sizes a maximized or
// fullscreen window to the whole physical output on every axis it fills, so
// its frame centre is the output's centre there and names the same virtual
// screen for every such window: a window maximized anywhere else crossed into
// that one and back on the restore. Its screen is the one it belongs to
// instead. A window maximized on one axis only is read at its frame on the
// free axis and its restore rect on the other (F1057). Otherwise the engine's
// screen for a tiling-tracked window comes first (a monocle tile moved between
// two monocle virtual screens keeps the restore rect of the one it left,
// because the monocle arm skips maximize() for a window already MaximizeFull),
// then the virtual screen holding its restore rect, then its last stamp. Its
// own translation unit because screens.cpp is at the file-size ceiling.

#include "plasmazoneseffect.h"
#include "windowscreendecisions.h"

#include <core/output.h>
#include <effect/effecthandler.h>
#include <window.h>

#include "tilinghandler/tilinghandler.h"

namespace PlasmaZones {

QString PlasmaZonesEffect::fillingWindowScreenId(KWin::EffectWindow* w, const QString& windowId,
                                                 KWin::LogicalOutput* output) const
{
    KWin::Window* const kw = w ? w->window() : nullptr;
    if (!kw || !output || !KWin::effects) {
        return {};
    }
    // Committed OR requested: in the Wayland release gap the request is
    // already Restore while the frame is still the whole output.
    const bool fullscreen = kw->isFullScreen() || kw->isRequestedFullScreen();
    const int maximizeMode = int(kw->maximizeMode()) | int(kw->requestedMaximizeMode());
    if (!fullscreen && maximizeMode == KWin::MaximizeRestore) {
        return {};
    }
    const QString physId = outputScreenId(output);
    const auto defs = m_virtualScreenDefs.constFind(physId);
    if (defs == m_virtualScreenDefs.constEnd() || defs->isEmpty()) {
        return {};
    }
    WindowScreenDecisions::FillingWindow in;
    for (const EffectVirtualScreenDef& vs : *defs) {
        in.outputScreens.append(vs.id);
    }
    const QString id = windowId.isEmpty() ? getWindowId(w) : windowId;
    if (m_tilingHandler->isTrackedWindow(id)) {
        in.engineScreen = m_tilingHandler->notifiedScreenFor(id);
    }
    // A maximized window's own restore rect, also under a fullscreen on top:
    // that one's fullscreen restore rect is the maximized frame, the whole
    // output, and a re-statement seats the placement in the maximize's (F1073).
    QRectF restore =
        fullscreen && maximizeMode == KWin::MaximizeRestore ? QRectF(kw->fullscreenGeometryRestore()) : QRectF();
    if (restore.width() <= 0 || restore.height() <= 0) {
        restore = QRectF(kw->geometryRestore());
    }
    const bool hasRestore = restore.width() > 0 && restore.height() > 0;
    if (!fullscreen && maximizeMode != KWin::MaximizeFull && hasRestore) {
        // The frame KWin was last asked for, where the window is going, when
        // it is on this output (the pending read's output), else the committed
        // one.
        const QRectF requested = QRectF(kw->moveResizeGeometry());
        const QRectF frame =
            KWin::effects->screenAt(requested.center().toPoint()) == output ? requested : QRectF(w->frameGeometry());
        const QPointF probe = WindowScreenDecisions::axisMaximizedProbe(
            frame, restore, QRectF(QRect(output->geometry())), maximizeMode == KWin::MaximizeVertical);
        in.axisScreen = resolveEffectiveScreenId(probe.toPoint(), physId);
    }
    if (hasRestore) {
        const QPoint centre = restore.center().toPoint();
        if (KWin::effects->screenAt(centre) == output) {
            in.restoreScreen = resolveEffectiveScreenId(centre, physId);
        }
    }
    in.trackedScreen = m_trackedScreenPerWindow.value(w);
    return WindowScreenDecisions::resolveFilling(in);
}

} // namespace PlasmaZones
