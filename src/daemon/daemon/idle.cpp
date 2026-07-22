// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// SESSION IDLE, one of the daemon's per-concern partitions.
//
// Why the DAEMON owns this at all: ext-idle-notify-v1 is a Wayland CLIENT protocol.
// The KWin effect lives inside the compositor and cannot consume it. So the daemon
// watches the seat and pushes the answer to the effect over D-Bus, which is the only
// reason any of this is here rather than beside the code that acts on it.

#include "../daemon.h"

#include "core/platform/logging.h"

// Settings must be COMPLETE here: m_settings is a unique_ptr<Settings>, daemon.h only
// forward-declares it, and this file connects to its signals and calls its accessors. It
// compiles without this only because the unity build happens to group this TU with one
// that includes settings.h — which is not a guarantee, it is a coincidence of chunking.
#include "../../config/settings.h"
#include "core/interfaces/isettings.h"
#include "dbus/compositorbridgeadaptor.h"
#include "dbus/settingsadaptor/settingsadaptor.h"

#include <PhosphorServiceIdle/IdleService.h>

#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace PlasmaZones {

void Daemon::setupIdleService()
{
    if (m_idleUnsupported) {
        // Established on the first probe and not worth repeating: start() re-arms on a null
        // m_idleService, and an unsupported compositor leaves it null forever, so without
        // this every stop()→start() cycle would rebuild the service, fail the same way, and
        // log the same notice again.
        return;
    }

    // start() re-arms this after a stop(), and stop() has already severed these — but sever
    // them again rather than trusting that. DISCONNECT, do not merely clear the list: a
    // cleared list leaves the connections themselves live, so a caller who reached here with
    // connections still standing would stack a second set on top and every settings write
    // would fire both.
    teardownIdleConnections();

    // No QObject parent: the unique_ptr owns it. Passing `this` as well would be
    // dual ownership (it survives only because member destruction runs before
    // ~QObject de-parents the children), and every other unique_ptr-held QObject
    // in this file is constructed with a null parent for the same reason.
    m_idleService = std::make_unique<PhosphorServiceIdle::IdleService>(nullptr);
    if (!m_idleService->isSupported()) {
        // No ext-idle-notify-v1 on this compositor. The decoration chain simply
        // keeps animating; PauseWhenIdle degrades to a no-op rather than guessing
        // at idleness from some other signal.
        qCInfo(lcDaemon) << "Idle notification unsupported by this compositor — "
                            "decoration PauseWhenIdle will not engage";
        m_idleUnsupported = true;
        m_idleService.reset();
        return;
    }

    // Both edges publish the SAME derived answer, and neither of them decides anything
    // on its own. sessionIdleNow() is the single place that knows what "the session is
    // idle, as far as this feature is concerned" means; every caller asks it.
    connect(m_idleService.get(), &PhosphorServiceIdle::IdleService::idled, this, [this](int /*stage*/) {
        publishSessionIdle(sessionIdleNow());
    });
    connect(m_idleService.get(), &PhosphorServiceIdle::IdleService::resumed, this, [this]() {
        publishSessionIdle(sessionIdleNow());
    });

    // BEFORE the first refreshIdleStages() below, which is not merely tidy: that call's
    // arm-retry branch starts THIS timer. Set up after it, the timer would be started
    // un-connected and not-yet-single-shot, and only the fact that both run before control
    // returns to the event loop would save it. The login race the retry exists for is
    // exactly when that ordering gets exercised.
    //
    // Single-shot and shared by two callers of refreshIdleStages: the settings debounce
    // (below) and the arm-retry (in refreshIdleStages). Each start() passes its own interval,
    // so a pending retry and a pending debounce coalesce into one refresh, and stop() in the
    // daemon teardown cancels either — a plain QTimer::singleShot for the retry would outlive
    // that teardown and could not be coalesced.
    m_idleStagesRefreshTimer.setSingleShot(true);
    m_idleConnections << connect(&m_idleStagesRefreshTimer, &QTimer::timeout, this, &Daemon::refreshIdleStages);

    refreshIdleStages();

    // The ladder is armed by the TIMEOUT and by nothing else. It is NOT torn down when
    // PauseWhenIdle goes off, and that is the whole design:
    //
    // The obvious implementation empties the ladder when the feature is off. But an
    // empty ladder pins the state machine at stage 0, so isIdle() reads FALSE for the
    // entire time the feature is off — and then, when the user turns it back ON while
    // sitting away from the machine, there is nothing to read and no compositor edge
    // coming (the seat went idle long ago). The effect would run unpaused on a session
    // that has been idle for an hour, until the next real timeout after the next real
    // input. Tearing the ladder down destroys the very fact the feature needs.
    //
    // So the ladder stays armed whenever the compositor supports it, the seat's idleness
    // is always known, and the SETTING is applied where it belongs: in sessionIdleNow(),
    // which every publisher goes through. The cost is one ext-idle-notify-v1 object we
    // sometimes ignore, which is nothing. What it buys is that turning the feature on
    // takes effect immediately rather than at some unpredictable point in the future.

    // DEBOUNCED, because a rebuild is not free and not silent: it destroys and recreates
    // the compositor's ext-idle-notify-v1 object, and if the session is currently idle it
    // announces a resume on the way (the machine re-arms from active). The "Idle after"
    // slider writes on every step of a drag, so an undebounced connect would do all of
    // that per pixel of travel.
    //
    // NOT guarded on m_settings. It is a unique_ptr built in the constructor's init list
    // and cannot be null here — m_settingsAdaptor was constructed FROM it already. A
    // guard would be worse than pointless: were it ever to trip, this connect would be
    // skipped and the ladder would sit pinned to its startup timeout for the process
    // lifetime, so the slider would become a silent no-op.
    m_idleConnections << connect(m_settings.get(), &ISettings::decorationIdleTimeoutSecChanged, this, [this] {
        // A new timeout is a FRESH arming attempt against a source the rebuild recreates,
        // not a continuation of an earlier burst — so give it the full retry budget back.
        // Without this, a startup that exhausted the budget (no seat yet) leaves the user's
        // later slider change going straight to the give-up branch, even though the rebuild
        // it triggers would very likely arm.
        //
        // Replenished HERE and not in refreshIdleStages: that function is what the retry
        // re-enters, so resetting the budget there would refill it on every retry and spin
        // the 1 Hz rebuild forever instead of bounding it.
        m_idleArmRetriesLeft = kIdleArmRetries;
        m_idleStagesRefreshTimer.start(kIdleStagesRefreshDebounceMs);
    });

    // The TOGGLE rebuilds nothing. It only changes the answer, so publish the new one at
    // once rather than waiting for an edge that may never come: turning the feature ON
    // while the seat is already idle has to pause the decorations NOW, and turning it OFF
    // has to release them NOW. Plain change-check, no force — the value really does move.
    m_idleConnections << connect(m_settings.get(), &ISettings::decorationPauseWhenIdleChanged, this, [this] {
        publishSessionIdle(sessionIdleNow());
    });

    // Push the CURRENT state whenever the KWin effect (re)registers. sessionIdleChanged
    // is edge-triggered, and a restarted daemon arms a fresh ext-idle-notify-v1
    // notification on a seat that may already be idle or already active — either way it
    // produces no edge, so an effect that restarted (or a daemon that did) would
    // otherwise run on a stale assumption until the next real idle/active flip.
    if (m_compositorBridge) {
        m_idleConnections << connect(m_compositorBridge, &CompositorBridgeAdaptor::bridgeRegistered, this,
                                     [this](const QString&, const QString&, const QStringList&) {
                                         // FORCED past the change check, and this is the one caller that needs
                                         // it. A newly registered effect starts from its OWN default (not
                                         // idle) and has heard nothing from us, so the question is what IT
                                         // believes, not what we last published. Suppressing a "redundant"
                                         // false would leave an effect that reconnected mid-idle running
                                         // unpaused for good.
                                         publishSessionIdle(sessionIdleNow(), /*force=*/true);
                                     });
    }
}

void Daemon::teardownIdleConnections()
{
    for (QMetaObject::Connection& c : m_idleConnections) {
        disconnect(c);
    }
    m_idleConnections.clear();
}

bool Daemon::sessionIdleNow() const
{
    // The seat being idle is a FACT; pausing on it is a CHOICE. The ladder always reports
    // the fact (see setupIdleService), and this is where the choice is applied — one
    // place, so the toggle cannot be honoured on one path and forgotten on another.
    return m_idleService && m_idleService->isIdle() && m_settings && m_settings->decorationPauseWhenIdle();
}

void Daemon::publishSessionIdle(bool idle, bool force)
{
    if (!m_settingsAdaptor) {
        return;
    }
    // Only on a real change, per the project's emit-on-change rule. The idle service can
    // report the same state twice (a ladder rebuild that lands where it already was, a
    // second stage on the same ladder), and while the effect does dedupe a same-value
    // signal at its own door, publishing one anyway is a D-Bus broadcast that says
    // nothing. @p force is for the one case where our last published value is not the
    // question at all: a client that has just connected and knows none of it.
    if (!force && idle == m_publishedSessionIdle) {
        return;
    }
    m_publishedSessionIdle = idle;
    Q_EMIT m_settingsAdaptor->sessionIdleChanged(idle);
}

void Daemon::refreshIdleStages()
{
    if (!m_idleService || !m_settings) {
        return;
    }
    // One stage, at the configured timeout, armed whenever the compositor supports idle
    // notification — regardless of the PauseWhenIdle toggle. See setupIdleService for
    // why the toggle must NOT tear this down.
    //
    // Setting the ladder it already has is a no-op inside IdleService (it does not
    // re-arm and does not resume), which is what makes this safe to call freely.
    const int timeoutMs = m_settings->decorationIdleTimeoutSec() * 1000;
    // A non-positive timeout can only arrive from a config hand-edited below the schema
    // floor (the slider's minimum is well above zero). toStages drops a <=0 stage, so the
    // ladder below would be empty, isArmed() would report false, and the retry path would
    // spend its whole budget rebuilding the same empty ladder before blaming "arming" for a
    // misconfiguration. Treat it as disabled: clear the ladder and keep the retry budget
    // intact so a later valid timeout arms cleanly.
    if (timeoutMs <= 0) {
        m_idleService->setStages({});
        m_idleArmRetriesLeft = kIdleArmRetries;
        return;
    }
    const QVariantList ladder{QVariantMap{
        {PhosphorServiceIdle::StageKey::Name, QStringLiteral("decorations")},
        {PhosphorServiceIdle::StageKey::TimeoutMs, timeoutMs},
    }};
    m_idleService->setStages(ladder);

    // Did it actually arm? Supporting the protocol and managing to USE it are two different
    // facts, and only the second one matters: arming needs a seat whose input devices the
    // compositor has advertised, and the daemon can win that race at login. Arming then does
    // nothing, isSupported() still says true, and idle detection is dead for the session
    // with a single warning to show for it. Nothing would ever rebuild it, because the only
    // rebuild trigger is a timeout change and setting the ladder it already has is a no-op.
    //
    // So retry, with the ladder cleared first — an empty ladder is a REAL change, so the
    // next set is not swallowed by that no-op rule. Bounded, because a compositor that
    // genuinely cannot arm should not be retried forever; when the budget runs out the
    // feature degrades to off, which is what it already did silently.
    if (m_idleService->isArmed()) {
        m_idleArmRetriesLeft = kIdleArmRetries;
        return;
    }
    if (m_idleArmRetriesLeft <= 0) {
        qCWarning(lcDaemon) << "Idle notification is supported but would not arm — decoration PauseWhenIdle is off "
                               "for this session";
        return;
    }
    --m_idleArmRetriesLeft;
    qCInfo(lcDaemon) << "Idle ladder did not arm (no seat yet?) — retrying in" << kIdleArmRetryDelayMs << "ms";
    m_idleService->setStages({});
    m_idleStagesRefreshTimer.start(kIdleArmRetryDelayMs);
}

} // namespace PlasmaZones
