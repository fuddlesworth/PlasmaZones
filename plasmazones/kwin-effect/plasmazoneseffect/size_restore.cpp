// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The size-only half of slotApplyGeometryRequested (#1106): a free size the
// daemon gives back to a window where it stands. Split from daemon_apply.cpp
// by concern (and to keep that file under the size ceiling).

#include "plasmazoneseffect.h"
#include "compositor/effectlogging.h"

#include <PhosphorAnimation/ProfilePaths.h>

#include <effect/effecthandler.h>
#include <window.h>

#include <QLoggingCategory>

#include "tilinghandler/tilinghandler.h"
#include "handlers/snaphandler.h"

namespace PlasmaZones {

void PlasmaZonesEffect::applySizeOnlyRestore(KWin::EffectWindow* w, const QString& liveWindowId,
                                             const QString& screenId, const QSize& size, bool freshOpen)
{
    // Guarded like every window entry point here; the deref below is first.
    if (!w || w->isDeleted()) {
        return;
    }
    // Integer-aligned like every other frame compare in this file: on a
    // fractional output the qreal frame carries sub-pixel residue.
    const QRect frameInt = w->frameGeometry().toRect();
    // Which producer this is decides where the restored size lands, and it
    // is read from the effect's own bookkeeping, not inferred from the
    // suppression entry: paintWindow erases a past-deadline entry on the
    // first paint that reaches the window, so a desktop-arrival re-drive
    // raced its own first paint for the answer. The open-path producer
    // (a first-placement resolve or a tiling announce in flight) keeps the
    // CENTRE: KWin placed the larger, app-inherited size, and a fresh window
    // whose top-left is kept ends up parked in the upper-left corner of the
    // spot KWin chose for it. The drag-out unsnap keeps the top-left: the
    // user just dropped the window there and the shrink must not walk it
    // away from the pointer.
    const bool openPath =
        m_snapHandler->hasOpenResolveInFlight(liveWindowId) || m_tilingHandler->announceInFlight(liveWindowId);
    QRect geo(frameInt.topLeft(), size);
    if (openPath) {
        geo.moveCenter(frameInt.center());
        // Clamped into the work area of the output the daemon named (its
        // authoritative answer), else the output under the window's centre;
        // never KWin's own w->screen(), which can name the wrong one of two
        // identical outputs (Discussion #724). A window placed against an
        // edge does not recentre off-screen.
        KWin::LogicalOutput* out = outputForScreenId(screenId);
        if (!out) {
            out = windowOutput(w);
        }
        QRect work;
        if (out) {
            work = KWin::effects->clientArea(KWin::MaximizeArea, out).toRect();
        }
        if (work.isValid()) {
            geo.moveLeft(qMax(work.left(), qMin(geo.left(), work.right() - size.width() + 1)));
            geo.moveTop(qMax(work.top(), qMin(geo.top(), work.bottom() - size.height() + 1)));
        }
    }
    qCInfo(lcEffect) << "slotApplyGeometryRequested: size-only restore for" << liveWindowId << "requested" << size
                     << "at" << geo.topLeft() << (openPath ? "(open path)" : "(drag-out)");
    if (freshOpen && frameInt.size() == geo.size()) {
        // Explicit, rather than applyWindowGeometry's own at-target bail:
        // that bail releases the suppression, which the float signal that
        // follows this apply, or the resolve reply, releases in order. (The
        // hold matters on Wayland, where the configure is asynchronous; an
        // XWayland moveResize settles synchronously inside the apply.)
        qCDebug(lcEffect) << "slotApplyGeometryRequested: size-only restore already at size for" << liveWindowId;
        return;
    }
    if (w->isMinimized()) {
        // Same reason as the float-restore path: a moveResize while
        // minimized poisons what KWin restores to on unminimize.
        qCDebug(lcEffect) << "slotApplyGeometryRequested: size-only restore skipped, window minimized:" << liveWindowId;
        return;
    }
    // A window that mapped maximized (or is going fullscreen) keeps its
    // frame whatever size is asked: the restored size, placed the way this
    // path places it, is seated as the rect it returns to, so the free size
    // lands when the user restores it (F575). Like the two skips above, the
    // first-frame suppression is left to its owners (resolve reply, announce
    // reply, paint deadline).
    if (KWin::Window* kw = w->window();
        kw && (kw->requestedMaximizeMode() != KWin::MaximizeRestore || kw->isRequestedFullScreen())) {
        if (kw->requestedMaximizeMode() != KWin::MaximizeRestore) {
            kw->setGeometryRestore(KWin::RectF(geo));
        } else {
            kw->setFullscreenGeometryRestore(KWin::RectF(geo));
        }
        qCDebug(lcEffect) << "slotApplyGeometryRequested: size-only restore seated as the restore rect of"
                          << liveWindowId;
        return;
    }
    // Pre-seed the tracked screen from the daemon's authoritative answer, as
    // the full-rect arm does: the configure's frame change is asynchronous
    // and the bracket below covers only the synchronous one.
    if (!screenId.isEmpty()) {
        m_trackedScreenPerWindow[w] = screenId;
        m_tilingHandler->updateNotifiedScreen(liveWindowId, screenId);
    }
    const auto applyGuard = geometryApplyScope();
    applyWindowGeometry(w, geo, /*allowDuringDrag=*/false, /*skipAnimation=*/freshOpen,
                        PhosphorAnimation::ProfilePaths::WindowPlaceOut);
    // The entry may be past its deadline (a window parked off-desktop keeps
    // its entry on purpose), and paintWindow erases such an entry on the next
    // paint: re-arm so the teleport just stamped is held to its settle, not
    // painted at the spawn frame until the client acks.
    if (freshOpen) {
        refreshRestoreSuppressionDeadline(w);
    }
}

// slotToggleWindowFloatRequested removed — the daemon now handles float-toggle
// locally against its active-window + frame-geometry shadow and emits
// applyGeometryRequested directly. See SnapAdaptor::toggleFloatForWindow.

} // namespace PlasmaZones
