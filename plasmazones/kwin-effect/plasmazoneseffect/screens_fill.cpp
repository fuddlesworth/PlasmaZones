// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The screen of a window that fills a split output. KWin sizes a maximized
// (on any axis) or fullscreen window to the whole physical output, so its
// frame centre is the output's centre and names the same virtual screen for
// every such window: a window maximized anywhere else crossed into that one
// and back on the restore. Its screen is the one it belongs to instead: the
// engine's for a tiling-tracked window first (a monocle tile moved between
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
    const bool maximized =
        kw->maximizeMode() != KWin::MaximizeRestore || kw->requestedMaximizeMode() != KWin::MaximizeRestore;
    if (!fullscreen && !maximized) {
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
    QRectF restore = fullscreen ? QRectF(kw->fullscreenGeometryRestore()) : QRectF();
    if (restore.width() <= 0 || restore.height() <= 0) {
        restore = QRectF(kw->geometryRestore());
    }
    if (restore.width() > 0 && restore.height() > 0) {
        const QPoint centre = restore.center().toPoint();
        if (KWin::effects->screenAt(centre) == output) {
            in.restoreScreen = resolveEffectiveScreenId(centre, physId);
        }
    }
    in.trackedScreen = m_trackedScreenPerWindow.value(w);
    return WindowScreenDecisions::resolveFilling(in);
}

} // namespace PlasmaZones
