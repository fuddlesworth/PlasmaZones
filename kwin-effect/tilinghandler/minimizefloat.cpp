// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TilingHandler's minimize/unminimize float state machine.
//
// Split out of signals.cpp, which had grown past the 1150-line hard ceiling.
// This is a self-contained concern: a minimize takes a tiled window out of the
// engine's layout by floating it (debounced against KWin's spurious
// minimize/unminimize pairs), and an unminimize hands it back — with a
// deferred, revalidated commit so the stock minimize animation is not torn
// mid-flight, plus a bounded retry for the window where the daemon has not yet
// claimed the screen. The rest of signals.cpp is the D-Bus slot surface.

#include "tilinghandler.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "handlers/navigationhandler.h"
#include "compositor/effectlogging.h"
#include "handlers/snaphandler.h" // cross-mode minimize-float adoption
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorIdentity/WindowId.h>

#include <effect/effect.h> // Effect::animationTime, the deferred-unfloat grace
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>
#include <workspace.h>

#include <QLoggingCategory>
#include <QPointer>
#include <QTimer>

#include <chrono> // std::chrono::milliseconds for Effect::animationTime

namespace PlasmaZones {

static constexpr int kAutotileUnfloatRetryDelayMs = 250;
static constexpr int kAutotileMaxUnfloatRetries = 3;

// Debounce window for minimize→float commits. KWin can transiently flip a
// tiled window's isMinimized() state to true and back within a few
// milliseconds when a plasmashell notification popup rearranges stacking.
// Deferring the D-Bus float for this long coalesces spurious cycles: if the
// matching unminimize arrives inside the window, no float is issued and the
// autotile layout never sees the transient 1-window state that causes the
// master to balloon to the full screen. Real user minimizes always last
// longer than this, so they commit normally.
// Pinned to the shared spurious-pair window (plasmazoneseffect.h) so this
// debounce and the minimize shader-event suppression can never desync.
static constexpr int kMinimizeFloatDebounceMs = kSpuriousMinimizePairMs;

void TilingHandler::cancelPendingMinimizeFloat(const QString& windowId)
{
    m_pendingMinimizeFloat.cancel(windowId);
}

void TilingHandler::cancelPendingUnminimizeUnfloat(const QString& windowId)
{
    m_pendingUnminimizeUnfloat.cancel(windowId);
}

void TilingHandler::clearAllPendingMinimizeFloats()
{
    m_pendingMinimizeFloat.cancelAll();
    // The restore-side timers must not fire against a disabled engine or a
    // torn-down handler either.
    m_pendingUnminimizeUnfloat.cancelAll();
}

bool TilingHandler::beginUnminimizeUnfloat(const QString& windowId)
{
    if (!m_minimizeFloatedWindows.contains(windowId) || !m_effect->m_daemonGate.serviceRegistered) {
        return false;
    }
    m_minimizeFloatedWindows.remove(windowId);
    m_unfloatInFlight.insert(windowId, ++m_unfloatRequestGeneration);
    return true;
}

bool TilingHandler::dispatchUnminimizeUnfloat(const QString& windowId, const QString& screenId)
{
    if (!beginUnminimizeUnfloat(windowId)) {
        // Either the window is no longer ours (benign — a concurrent path
        // consumed it) or the daemon is not registered. In the latter case
        // the window STAYS minimize-floated with the unminimize edge spent;
        // onDaemonReady's re-sync is the recovery path, but the strand must
        // be diagnosable rather than silent, and callers must not announce a
        // window the daemon still considers floating.
        qCWarning(lcEffect) << "Autotile: unfloat dispatch declined for" << windowId
                            << "(owned:" << m_minimizeFloatedWindows.contains(windowId)
                            << "daemon:" << m_effect->m_daemonGate.serviceRegistered << ")";
        return false;
    }
    const quint64 requestGeneration = m_unfloatInFlight.value(windowId);
    auto* watcher =
        new QDBusPendingCallWatcher(PhosphorProtocol::ClientHelpers::asyncCall(
                                        PhosphorProtocol::Service::Interface::WindowTracking,
                                        QStringLiteral("setWindowFloatingForScreen"), {windowId, screenId, false}),
                                    this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, windowId, requestGeneration](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                const auto inFlight = m_unfloatInFlight.constFind(windowId);
                if (inFlight == m_unfloatInFlight.constEnd() || inFlight.value() != requestGeneration) {
                    return;
                }
                if (w->isError()) {
                    m_unfloatInFlight.remove(windowId);
                    m_minimizeFloatedWindows.insert(windowId);
                    qCWarning(lcEffect) << "Autotile: unfloat request failed for" << windowId << ':'
                                        << w->error().message();
                    scheduleUnminimizeUnfloatRetry(windowId);
                    return;
                }

                auto* stateWatcher = new QDBusPendingCallWatcher(
                    PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::WindowTracking,
                                                               QStringLiteral("queryWindowFloating"), {windowId}),
                    this);
                connect(stateWatcher, &QDBusPendingCallWatcher::finished, this,
                        [this, windowId, requestGeneration](QDBusPendingCallWatcher* stateCall) {
                            stateCall->deleteLater();
                            const auto current = m_unfloatInFlight.constFind(windowId);
                            if (current == m_unfloatInFlight.constEnd() || current.value() != requestGeneration) {
                                return;
                            }
                            QDBusPendingReply<bool> reply = *stateCall;
                            if (!reply.isValid()) {
                                m_unfloatInFlight.remove(windowId);
                                m_minimizeFloatedWindows.insert(windowId);
                                scheduleUnminimizeUnfloatRetry(windowId);
                                return;
                            }
                            if (reply.value()) {
                                m_unfloatInFlight.remove(windowId);
                                m_minimizeFloatedWindows.insert(windowId);
                                scheduleUnminimizeUnfloatRetry(windowId);
                                return;
                            }
                            m_unfloatInFlight.remove(windowId);
                            m_minimizeFloatedWindows.remove(windowId);
                            m_untiledMinimizeFloats.remove(windowId);
                            m_unfloatRetryAttempts.remove(windowId);
                            m_effect->slotWindowFloatingChanged(windowId, false, QString());
                        });
            });
    return true;
}

void TilingHandler::scheduleUnminimizeUnfloatRetry(const QString& windowId)
{
    if (!m_effect->m_daemonGate.serviceRegistered || m_pendingUnminimizeUnfloat.contains(windowId)
        || m_unfloatRetryAttempts.value(windowId) >= kAutotileMaxUnfloatRetries) {
        return;
    }
    KWin::EffectWindow* window = m_effect->findWindowById(windowId);
    if (!window || window->isDeleted() || window->isMinimized()) {
        return;
    }
    QPointer<KWin::EffectWindow> safeWindow = window;
    ++m_unfloatRetryAttempts[windowId];
    m_pendingUnminimizeUnfloat.schedule(windowId, kAutotileUnfloatRetryDelayMs, [this, windowId, safeWindow]() {
        if (!safeWindow || safeWindow->isDeleted() || safeWindow->isMinimized()) {
            return;
        }
        const QString screenId = m_effect->getWindowScreenId(safeWindow.data());
        if (!m_minimizeFloatedWindows.contains(windowId)) {
            return;
        }
        if (!m_managedScreens.contains(screenId)) {
            SnapHandler* snap = m_effect->snapHandler();
            if (!snap || !snap->offerMinimizeEdge(safeWindow.data(), windowId, screenId)) {
                // Refused (screen flipped again mid-transfer) — ownership
                // stayed here, so re-arm rather than spending the edge.
                scheduleUnminimizeUnfloatRetry(windowId);
            }
            return;
        }
        if (dispatchUnminimizeUnfloat(windowId, screenId)) {
            notifyWindowAdded(safeWindow.data(), /*knownFreeFloating=*/false);
        }
    });
}

void TilingHandler::claimAlreadyMinimizedAsFloated(KWin::EffectWindow* w, const QString& windowId,
                                                   const QSet<QString>& screenFilter, bool enteringAutotile,
                                                   const QString& resolvedScreenId)
{
    const QString screenId = resolvedScreenId.isEmpty() ? m_effect->getWindowScreenId(w) : resolvedScreenId;
    if (!screenFilter.isEmpty() && !screenFilter.contains(screenId)) {
        return;
    }
    if (!m_managedScreens.contains(screenId)) {
        return;
    }
    // Same skip as the runtime minimize path: an already-floating window
    // (user float, or another mode's minimize-float record) keeps its float
    // and its owner — claiming it would make our unminimize path force-tile
    // a window whose float we did not create.
    // isMinimizeFloated, not the raw set: it also covers m_unfloatInFlight,
    // during which this handler still owns the float (tilinghandler.h states
    // the rule). A window that re-minimized while its screen was outside the
    // union leaves its id in m_unfloatInFlight, and testing the raw set let a
    // later batch announce insert it into m_minimizeFloatedWindows as well —
    // dual-held, with the in-flight completion fighting the fresh claim.
    if (isMinimizeFloated(windowId)) {
        if (enteringAutotile) {
            m_untiledMinimizeFloats.insert(windowId);
            if (m_effect->m_daemonGate.serviceRegistered) {
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    m_effect, PhosphorProtocol::Service::Interface::WindowTracking,
                    QStringLiteral("setWindowFloatingForScreen"), {windowId, screenId, true},
                    QStringLiteral("setWindowFloatingForScreen"));
            }
        }
        return;
    }
    // A snap-owned MINIMIZE-float is adoptable, not foreign: on a mode swap
    // the float's owner must follow the screen's current mode, and the snap
    // handler's own unminimize path bails on autotile screens — without the
    // adoption nobody claims the window, the daemon keeps (or re-seeds) a
    // tiling slot for it, and the effect never corrects it (the empty-
    // tile-spot defect). The re-asserted per-screen float below tells the
    // daemon's freshly seeded state this window floats in THIS mode too.
    if (enteringAutotile) {
        if (SnapHandler* snap = m_effect->snapHandler(); snap && snap->isMinimizeFloated(windowId)) {
            // Budget survives the hop (see seedUnfloatRetryBudget).
            const int usedBudget = snap->unfloatRetryBudgetUsed(windowId);
            snap->removeMinimizeFloated(windowId);
            seedUnfloatRetryBudget(windowId, usedBudget);
            qCInfo(lcEffect) << "Autotile: adopting snap-mode minimize-float at announce:" << windowId << "on"
                             << screenId;
            m_minimizeFloatedWindows.insert(windowId);
            m_untiledMinimizeFloats.insert(windowId);
            applyFloatCleanup(windowId);
            if (m_effect->m_daemonGate.serviceRegistered) {
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    m_effect, PhosphorProtocol::Service::Interface::WindowTracking,
                    QStringLiteral("setWindowFloatingForScreen"), {windowId, screenId, true},
                    QStringLiteral("setWindowFloatingForScreen"));
            }
            return;
        }
    }
    // Genuine foreign float (user float, or another mode's float we must not
    // own): keep the float and its owner — claiming it would make our
    // unminimize path force-tile a window whose float we did not create.
    if (m_effect->isWindowFloating(windowId)) {
        return;
    }
    m_minimizeFloatedWindows.insert(windowId);
    if (enteringAutotile) {
        // The current rect belongs to the prior mode. Place it in autotile at
        // the visibility edge instead of after the animation grace.
        m_untiledMinimizeFloats.insert(windowId);
    }
    qCInfo(lcEffect) << "Autotile: window already minimized at announce, claiming as minimize-floated:" << windowId
                     << "on" << screenId;
    // Full float reconcile, not just the D-Bus call: the claim must clear
    // stale tiled-membership, centering targets, and the decoration claim the
    // same way every runtime float path does, or the next frameGeometryChanged
    // on unminimize can consume a stale centering entry and snap the window
    // into an old zone rect.
    applyFloatCleanup(windowId);
    if (m_effect->m_daemonGate.serviceRegistered) {
        PhosphorProtocol::ClientHelpers::fireAndForget(m_effect, PhosphorProtocol::Service::Interface::WindowTracking,
                                                       QStringLiteral("setWindowFloatingForScreen"),
                                                       {windowId, screenId, true},
                                                       QStringLiteral("setWindowFloatingForScreen"));
    }
}

bool TilingHandler::offerMinimizeEdge(KWin::EffectWindow* w)
{
    // Mirror of slotWindowMinimizedChanged's entry gates. See the header doc:
    // a transfer the gates would refuse must be reported to the sender, not
    // silently dropped.
    if (!w || w->isDeleted() || !m_effect->shouldHandleWindow(w) || !m_effect->isTileableWindow(w)
        || !m_managedScreens.contains(m_effect->getWindowScreenId(w))) {
        return false;
    }
    slotWindowMinimizedChanged(w);
    return true;
}

void TilingHandler::slotWindowMinimizedChanged(KWin::EffectWindow* w)
{
    // isDeleted: the edge can race close teardown (Deleted shell), and
    // proceeding would repollute the scrubbed id caches and arm a debounce
    // for a corpse.
    if (!w || w->isDeleted() || !m_effect->shouldHandleWindow(w) || !m_effect->isTileableWindow(w)) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    const QString screenId = m_effect->getWindowScreenId(w);

    if (!m_managedScreens.contains(screenId)) {
        return;
    }

    const bool minimized = w->isMinimized();

    if (minimized) {
        // A re-minimize during the deferred unfloat grace cancels the pending
        // commit: the window never unfloated, so it is still minimize-floated
        // and the isWindowFloating skip below covers the rest.
        cancelPendingUnminimizeUnfloat(windowId);
        m_unfloatRetryAttempts.remove(windowId);
        if (m_unfloatInFlight.remove(windowId)) {
            m_minimizeFloatedWindows.insert(windowId);
            qCInfo(lcEffect) << "Autotile: re-minimize countermanding in-flight unfloat:" << windowId;
            if (m_effect->m_daemonGate.serviceRegistered) {
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    m_effect, PhosphorProtocol::Service::Interface::WindowTracking,
                    QStringLiteral("setWindowFloatingForScreen"), {windowId, screenId, true},
                    QStringLiteral("setWindowFloatingForScreen"));
            }
            return;
        }
        if (m_effect->isWindowFloating(windowId)) {
            qCDebug(lcEffect) << "Autotile: minimized already-floating window, skipping floatWindow:" << windowId;
            return;
        }
        // A commit is already pending for this window — nothing new to do.
        if (m_pendingMinimizeFloat.contains(windowId)) {
            return;
        }

        // Debounce the float. KWin emits spurious minimizedChanged(true) events
        // on tiled windows when plasmashell notification popups change stacking;
        // the matching unminimize arrives ~1-2ms later. If we commit the float
        // immediately, the autotile engine recalculates with N-1 windows, the
        // surviving master gets tiled to the full screen, and the user sees a
        // "100%" flash on every notification. By deferring we give the
        // unminimize path a chance to cancel the float entirely.
        QPointer<KWin::EffectWindow> wPtr(w);
        m_pendingMinimizeFloat.schedule(windowId, kMinimizeFloatDebounceMs, [this, windowId, screenId, wPtr]() {
            // Re-validate the full entry predicate: the window may have been
            // destroyed, unminimized, moved, or had autotile disabled on its
            // screen during the debounce window. Mirrors the checks at entry
            // to slotWindowMinimizedChanged so the deferred path can't commit
            // a float the entry path would have rejected.
            if (!wPtr) {
                qCDebug(lcEffect) << "Autotile: debounced minimize window destroyed, skipping float:" << windowId;
                return;
            }
            KWin::EffectWindow* fw = wPtr.data();
            if (!m_effect->shouldHandleWindow(fw) || !m_effect->isTileableWindow(fw)) {
                qCDebug(lcEffect) << "Autotile: debounced minimize no longer handleable, skipping float:" << windowId;
                return;
            }
            if (!fw->isMinimized()) {
                qCDebug(lcEffect) << "Autotile: debounced minimize reverted, skipping float:" << windowId;
                return;
            }
            if (m_effect->isWindowFloating(windowId)) {
                return;
            }
            // Screen membership must still hold, and the window must still
            // be on the same screen we captured — a mid-debounce output
            // change invalidates the deferred commit.
            const QString currentScreenId = m_effect->getWindowScreenId(fw);
            if (currentScreenId != screenId || !m_managedScreens.contains(currentScreenId)) {
                qCDebug(lcEffect) << "Autotile: debounced minimize screen changed or no longer autotiled, skipping:"
                                  << windowId << "was=" << screenId << "now=" << currentScreenId;
                return;
            }

            m_minimizeFloatedWindows.insert(windowId);

            qCInfo(lcEffect) << "Autotile: window minimized (after debounce), floating:" << windowId << "on"
                             << screenId;

            if (m_effect->m_daemonGate.serviceRegistered) {
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    m_effect, PhosphorProtocol::Service::Interface::WindowTracking,
                    QStringLiteral("setWindowFloatingForScreen"), {windowId, screenId, true},
                    QStringLiteral("setWindowFloatingForScreen"));
            }
        });
        return;
    }

    // Unminimized path.
    //
    // If a debounced float is still pending, cancel it: the minimize was
    // spurious (a transient plasmashell-induced flicker). No D-Bus traffic,
    // no retile, no balloon-to-100% flash.
    if (m_pendingMinimizeFloat.contains(windowId)) {
        cancelPendingMinimizeFloat(windowId);
        qCDebug(lcEffect) << "Autotile: coalesced spurious minimize/unminimize cycle for" << windowId;
        notifyWindowAdded(w, /*knownFreeFloating=*/false);
        return;
    }

    if (!m_minimizeFloatedWindows.contains(windowId)) {
        // Adopt a minimize-float created by the SNAP handler before this
        // screen swapped to autotile. Ownership must follow the screen's
        // current mode: the snap handler's own unminimize path bails on
        // autotile screens (and its deferred commit bails when the screen
        // flipped mid-grace), so without the adoption NOBODY unfloats the
        // window — it unminimizes into a permanent floating limbo until the
        // next mode toggle.
        //
        // Committed IMMEDIATELY, not through the deferred grace below. The
        // grace exists so the retile's moveResize doesn't cancel the stock
        // unminimize animation, and it is invisible in the normal cycle only
        // because a window we minimize-floated ourselves still sits at its
        // tiled rect — the deferral merely shifts retile timing. An adopted
        // window sits at its snap-mode free-float rect: deferring parks it
        // there for the whole animation and then visibly hops it into the
        // tile. Unfloating at the edge lets the tile decision land while the
        // restore is still starting, so the window goes straight to its tile
        // (trading a cancelled restore animation for the correct position).
        SnapHandler* snap = m_effect->snapHandler();
        const int snapBudgetUsed = snap ? snap->unfloatRetryBudgetUsed(windowId) : 0;
        if (snap && snap->removeMinimizeFloated(windowId)) {
            m_minimizeFloatedWindows.insert(windowId);
            m_untiledMinimizeFloats.insert(windowId);
            // Budget survives the hop (see seedUnfloatRetryBudget).
            seedUnfloatRetryBudget(windowId, snapBudgetUsed);
            qCInfo(lcEffect) << "Autotile: adopted snap-mode minimize-float, unfloating immediately:" << windowId
                             << "on" << screenId;
            if (dispatchUnminimizeUnfloat(windowId, screenId)) {
                // The frame still belongs to the prior mode; withhold paints
                // until the tile geometry lands (or the suppression's own
                // deadline) so the restore appears once, in its tile.
                m_effect->beginRestoreSuppression(w);
                notifyWindowAdded(w, /*knownFreeFloating=*/false);
            }
            return;
        }
        qCDebug(lcEffect) << "Autotile: unminimized window was not minimize-floated, skipping unfloatWindow:"
                          << windowId;
        notifyWindowAdded(w, /*knownFreeFloating=*/false);
        return;
    }
    // A window claimed at batch-announce time was never tiled by us — it
    // sits at its free-floating rect, so the deferred grace below would park
    // it there for the whole restore animation and then visibly hop it into
    // its tile. Commit the unfloat immediately instead, same rationale as
    // the snap-mode adoption above: the tile decision lands while the
    // restore is still starting and the window goes straight to its tile.
    if (m_untiledMinimizeFloats.contains(windowId)) {
        qCInfo(lcEffect) << "Autotile: window unminimized (claimed at announce), unfloating immediately:" << windowId
                         << "on" << screenId;
        if (dispatchUnminimizeUnfloat(windowId, screenId)) {
            // Same stale-frame rationale as the adoption branch above: the
            // rect belongs to the prior mode, so withhold paints until the
            // tile geometry lands.
            m_effect->beginRestoreSuppression(w);
            notifyWindowAdded(w, /*knownFreeFloating=*/false);
        }
        return;
    }
    // Supersede any surviving pending entry rather than skipping the edge:
    // the shared queue also carries RETRY timers (the twin of snap's
    // scheduleUnminimizeUnfloatRetry), and a genuine unminimize must never be
    // swallowed by whichever stale timer slipped through — the fresh edge is
    // the authoritative signal, so the grace re-arms from it.
    cancelPendingUnminimizeUnfloat(windowId);
    // No pre-autotile geometry capture here: a minimize-floated window cannot
    // move while minimized, so its frame is always the tiled rect — recording
    // it would poison the local float-back cache for windows with no prior
    // entry (snap→autotile transitions), and a genuine entry already exists
    // from the original capture.

    // Defer the unfloat commit past KWin's unminimize animation. Committing
    // immediately retiles the screen, and the retile's applyWindowGeometry
    // moveResize lands mid-flight and CANCELS the stock animation (magic
    // lamp / squash; kwin's maximize script likewise self-cancels on a
    // mid-flight resize) — the discussion #816 restore stutter. There is no
    // cross-effect API to observe the animation, so the grace is
    // animationTime(400ms): stock minimize animations are 250ms base scaled
    // by the user's global animation-speed factor, and animationTime applies
    // that same factor, so the deferral tracks the user's actual animation
    // length with margin. The window sits at its tiled rect meanwhile (it
    // could not move while minimized), so the deferral only shifts the
    // retile timing. Ownership moves to m_unfloatInFlight at COMMIT, so a
    // re-minimize can countermand the asynchronous unfloat before its echo.
    const int graceMs = int(KWin::Effect::animationTime(std::chrono::milliseconds(400)).count());
    QPointer<KWin::EffectWindow> wPtr(w);
    m_pendingUnminimizeUnfloat.schedule(windowId, graceMs, [this, windowId, wPtr]() {
        // Re-validate the entry predicate at commit time: the window may
        // have died, re-minimized (belt — the minimize path also cancels),
        // moved off an autotiled screen, or become unhandleable during the
        // grace. Every bail leaves the window minimize-floated, which the
        // next unminimize edge picks up again.
        if (!wPtr) {
            qCDebug(lcEffect) << "Autotile: deferred unfloat window destroyed, skipping:" << windowId;
            return;
        }
        KWin::EffectWindow* fw = wPtr.data();
        if (fw->isMinimized()) {
            qCDebug(lcEffect) << "Autotile: deferred unfloat window re-minimized, skipping:" << windowId;
            return;
        }
        if (!m_effect->shouldHandleWindow(fw) || !m_effect->isTileableWindow(fw)) {
            qCDebug(lcEffect) << "Autotile: deferred unfloat no longer handleable, skipping:" << windowId;
            return;
        }
        const QString currentScreenId = m_effect->getWindowScreenId(fw);
        if (!m_managedScreens.contains(currentScreenId)) {
            // The unminimize edge already happened. Hand the pending commit to
            // snap now instead of retaining ownership for an edge that will
            // never be emitted again.
            SnapHandler* snap = m_effect->snapHandler();
            qCInfo(lcEffect) << "Autotile: deferred unfloat screen became snap, transferring:" << windowId;
            if (!snap || !snap->offerMinimizeEdge(fw, windowId, currentScreenId)) {
                // Refused (screen flipped again mid-transfer) — ownership
                // stayed here, so re-arm rather than spending the edge.
                qCInfo(lcEffect) << "Autotile: snap refused transferred edge, re-arming retry:" << windowId;
                scheduleUnminimizeUnfloatRetry(windowId);
            }
            return;
        }
        if (!m_minimizeFloatedWindows.contains(windowId)) {
            return; // State moved under us (e.g. bulk cleanup); nothing to commit.
        }

        qCInfo(lcEffect) << "Autotile: window unminimized, unfloating (after animation grace):" << windowId << "on"
                         << currentScreenId;

        if (dispatchUnminimizeUnfloat(windowId, currentScreenId)) {
            notifyWindowAdded(fw, /*knownFreeFloating=*/false);
        }
    });
}

} // namespace PlasmaZones
