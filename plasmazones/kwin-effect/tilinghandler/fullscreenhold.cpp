// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The own-fullscreen HOLD of a scrolling strip tile: the return that undoes
// it, the desktop-switch park that keeps it routable, and the record drops
// every teardown funnel shares. The enter branch itself lives beside KWin's
// fullscreen slot in signals.cpp; this file holds what that branch and the
// desktop-switch passes call. Part of TilingHandler — split from signals.cpp
// for SRP.

#include "tilinghandler.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "compositor/effectlogging.h"
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/ClientHelpers.h>

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QTimer>

namespace PlasmaZones {

void TilingHandler::dispatchFullscreenUnfloat(const QString& windowId, const QString& screenId, int attempt,
                                              quint64 generation)
{
    // Reply-checked, unlike the old fire-and-forget: the record is consumed
    // before the send, so a lost request had nothing left to re-drive it and
    // the window stayed floating while this side called it tiled. A D-Bus
    // error is retried on a short delay; a false reply is the engine saying
    // the hold is not its to undo (a user unfloat took it over, or the window
    // left the engine), which is final. The in-flight set lets a re-enter
    // inside the round trip take a fresh hold (see the enter branch), and the
    // generation stamp keeps a reply or retry from a SUPERSEDED request (an
    // older return whose reply lands after a re-enter and re-exit) from
    // acting on the newer one's record.
    constexpr int kFullscreenUnfloatRetries = 3;
    constexpr int kFullscreenUnfloatRetryMs = 250;
    if (!m_effect->m_daemonGate.serviceRegistered) {
        m_fullscreenUnfloatInFlight.remove(windowId);
        qCWarning(lcEffect) << "Fullscreen hold return declined for" << windowId << "(daemon gate closed)";
        return;
    }
    if (attempt == 0) {
        generation = ++m_fullscreenHoldRequestGeneration;
    }
    m_fullscreenHoldGeneration[windowId] = generation;
    m_fullscreenUnfloatInFlight.insert(windowId);
    auto* watcher =
        new QDBusPendingCallWatcher(PhosphorProtocol::ClientHelpers::asyncCall(
                                        PhosphorProtocol::Service::Interface::Scrolling,
                                        QStringLiteral("setWindowFullscreenFloat"), {windowId, screenId, false}),
                                    this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, windowId, screenId, attempt, generation](QDBusPendingCallWatcher* call) {
                call->deleteLater();
                if (!m_fullscreenUnfloatInFlight.contains(windowId)
                    || m_fullscreenHoldGeneration.value(windowId) != generation) {
                    return; // countermanded by close, a session drain, or a newer hold/return
                }
                const QDBusPendingReply<bool> reply = *call;
                if (reply.isError()) {
                    if (attempt + 1 < kFullscreenUnfloatRetries) {
                        qCWarning(lcEffect) << "Fullscreen hold return failed for" << windowId << ':'
                                            << reply.error().message() << "- retrying";
                        QTimer::singleShot(
                            kFullscreenUnfloatRetryMs, this, [this, windowId, screenId, attempt, generation] {
                                if (m_fullscreenUnfloatInFlight.contains(windowId)
                                    && m_fullscreenHoldGeneration.value(windowId) == generation) {
                                    dispatchFullscreenUnfloat(windowId, screenId, attempt + 1, generation);
                                }
                            });
                        return;
                    }
                    qCWarning(lcEffect) << "Fullscreen hold return gave up for" << windowId << ':'
                                        << reply.error().message();
                }
                m_fullscreenUnfloatInFlight.remove(windowId);
                if (reply.isError() || !reply.value()) {
                    // A false reply is ambiguous: the engine either still
                    // FLOATS the window under another owner (a user float took
                    // the hold over) or already TILES it (a re-announce or a
                    // user unfloat put it back). The cache decides: every
                    // engine float transition announces before the method
                    // reply on the same connection, so it is current here.
                    // Floating: undo the early tiled mark and pay the maximize
                    // claims, both no-ops for a non-member. Tiled: fall through
                    // to the reapply, which the first-attempt gate used to
                    // skip.
                    if (m_effect->isWindowFloating(windowId)) {
                        clearWindowTiledAllScreens(windowId);
                        unmaximizeMonocleWindow(windowId);
                        if (KWin::EffectWindow* fw = m_effect->findWindowByIdExact(windowId)) {
                            releaseMaximizedToEdges(windowId, fw);
                        }
                        m_effect->updateAllDecorations();
                        return;
                    }
                }
                // KWin restores the PRE-fullscreen rect a client round trip
                // later than the return's own batch, and the engine's
                // emit-on-change gate would not correct it unprompted.
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    m_effect, PhosphorProtocol::Service::Interface::Scrolling, QStringLiteral("reapplyWindowGeometry"),
                    {windowId}, QStringLiteral("reapplyWindowGeometry"));
            });
}

void TilingHandler::settleParkedFullscreenHold(KWin::EffectWindow* w, const QString& windowId, const QString& screenId)
{
    // A hold whose window left fullscreen while demoted for a desktop switch
    // kept its record (signals.cpp, parked arm): the screen record was gone,
    // so the return could not be routed. Now the window is re-tracked on a
    // managed screen, so send it if the window is indeed out of fullscreen.
    // Requested OR committed, the family's predicate: a window that
    // re-requested fullscreen while parked is still held. No decoration sweep
    // here: every caller sweeps once after its own pass.
    const KWin::Window* const kw = w ? w->window() : nullptr;
    if (!m_fullscreenFloatedWindows.contains(windowId) || !w || w->isFullScreen()
        || (kw && kw->isRequestedFullScreen())) {
        return;
    }
    m_fullscreenFloatedWindows.remove(windowId);
    if (!isScrollingScreen(screenId)) {
        qCInfo(lcEffect) << "Parked fullscreen hold for" << windowId << "returns on a non-scrolling screen" << screenId
                         << "- record dropped";
        return;
    }
    if (!m_effect->m_daemonGate.serviceRegistered) {
        qCInfo(lcEffect) << "Parked fullscreen hold for" << windowId << "cannot return, daemon gate closed";
        return;
    }
    qCInfo(lcEffect) << "Settling parked fullscreen hold for" << windowId << "on" << screenId;
    dispatchFullscreenUnfloat(windowId, screenId, 0);
    markWindowTiled(screenId, windowId);
}

bool TilingHandler::parkFullscreenHoldForDesktopSwitch(const QString& windowId)
{
    // ONE representation for every park site: record kept, id out of the
    // notified set and screen map, id IN the desktop-return set, so the exit
    // branch's parked arm and the desktop-return re-track both find it. A
    // hold whose passive announce has not landed yet is still ours, which is
    // why this runs before any floating or fullscreen test the caller makes.
    if (!m_fullscreenFloatedWindows.contains(windowId)) {
        return false;
    }
    if (m_notifiedWindows.remove(windowId)) {
        m_notifiedWindowScreens.remove(windowId);
    }
    m_savedNotifiedForDesktopReturn.insert(windowId);
    // The per-window companions a demotion sheds; the enter branch cleared
    // most of them already, so these are belts.
    m_tileTargetZones.remove(windowId);
    m_centeredWaylandZones.remove(windowId);
    m_effect->m_scrollCommandedRects.remove(windowId);
    m_effect->m_scrollOfferedColumn.remove(windowId);
    if (m_effect->m_scrollVisualDelta.remove(windowId) > 0 && KWin::effects) {
        KWin::effects->addRepaintFull();
    }
    clearWindowTiledAllScreens(windowId);
    qCInfo(lcEffect) << "Parked own-fullscreen hold for desktop switch:" << windowId;
    return true;
}

void TilingHandler::dropFullscreenHoldRecords(const QString& windowId)
{
    m_fullscreenFloatedWindows.remove(windowId);
    m_fullscreenUnfloatInFlight.remove(windowId);
    m_fullscreenHoldGeneration.remove(windowId);
}

} // namespace PlasmaZones
