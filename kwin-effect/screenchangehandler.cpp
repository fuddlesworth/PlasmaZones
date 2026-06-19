// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenchangehandler.h"
#include "autotilehandler.h"
#include "plasmazoneseffect.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/WindowMarshalling.h>
#include <PhosphorIdentity/WindowId.h>

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>

#include <QDBusArgument>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QPointer>

Q_LOGGING_CATEGORY(lcScreenChange, "kwin.effect.plasmazones.screenchange", QtWarningMsg)

namespace PlasmaZones {

namespace {
// Debounce interval for virtualScreenGeometryChanged signals. KWin fires this
// multiple times per screen connect/disconnect/resolution change. 500ms lets
// all signals from a single user action settle before we process.
constexpr int ScreenChangeDebounceMs = 500;
} // namespace

ScreenChangeHandler::ScreenChangeHandler(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
{
    m_screenChangeDebounce.setSingleShot(true);
    m_screenChangeDebounce.setInterval(ScreenChangeDebounceMs);
    connect(&m_screenChangeDebounce, &QTimer::timeout, this, &ScreenChangeHandler::applyScreenGeometryChange);

    m_lastVirtualScreenGeometry = KWin::effects->virtualScreenGeometry();
}

void ScreenChangeHandler::stop()
{
    m_screenChangeDebounce.stop();
    // Suppress any client-area report still queued for a later event-loop
    // turn — without this guard it would fire a stray D-Bus call between
    // stop() and this handler's destruction.
    m_stopped = true;
}

void ScreenChangeHandler::slotScreenGeometryChanged()
{
    // Debounce screen geometry changes to prevent rapid-fire updates.
    // virtualScreenGeometryChanged can fire multiple times for monitor
    // connect/disconnect, arrangement changes, resolution changes, etc.
    QRect currentGeometry = KWin::effects->virtualScreenGeometry();
    qCInfo(lcScreenChange) << "virtualScreenGeometryChanged fired"
                           << "- current:" << currentGeometry << "- previous:" << m_lastVirtualScreenGeometry
                           << "- pending:" << m_pendingScreenChange;

    if (currentGeometry == m_lastVirtualScreenGeometry && !m_pendingScreenChange) {
        qCDebug(lcScreenChange) << "Screen geometry unchanged, ignoring signal";
        return;
    }

    m_pendingScreenChange = true;
    m_screenChangeDebounce.start(); // Restart timer (debounce)

    // A resolution / arrangement change moves panels and resizes the work
    // area — re-push KWin's clientArea so the daemon's available geometry
    // tracks the new layout instead of a stale compositor override.
    scheduleClientAreaReport();
}

void ScreenChangeHandler::applyScreenGeometryChange()
{
    if (!m_pendingScreenChange) {
        return;
    }

    QRect currentGeometry = KWin::effects->virtualScreenGeometry();
    qCInfo(lcScreenChange) << "Applying debounced screen geometry change"
                           << "- previous:" << m_lastVirtualScreenGeometry << "- current:" << currentGeometry;

    m_pendingScreenChange = false;

    // Only reposition windows when the virtual screen SIZE changed.
    // Position-only changes (e.g. exiting KDE panel edit mode) should not move windows.
    const QSize previousSize = m_lastVirtualScreenGeometry.size();
    const QSize currentSize = currentGeometry.size();
    const bool sizeChanged = (previousSize != currentSize);

    m_lastVirtualScreenGeometry = currentGeometry;

    // Always refresh virtual screen definitions — physical screen geometry
    // (including position) changed, so virtual screen absolute coordinates
    // need recalculation from the daemon.
    if (m_effect->m_daemonServiceRegistered) {
        m_effect->fetchAllVirtualScreenConfigs();
    }

    if (!sizeChanged) {
        // Even when physical size is unchanged, virtual screen split ratio changes
        // require window repositioning. Only proceed if VS configs exist; otherwise
        // there's nothing to reposition.
        if (!m_effect->m_daemonServiceRegistered || m_effect->m_virtualScreenDefs.isEmpty()) {
            qCDebug(lcScreenChange) << "Screen size unchanged, no VS configs — skipping window repositioning";
            return;
        }
        qCDebug(lcScreenChange) << "Screen size unchanged but VS configs present — repositioning windows";
    }

    if (m_reapplyInProgress) {
        m_reapplyPending = true;
        return;
    }
    fetchAndApplyWindowGeometries();
}

void ScreenChangeHandler::slotScreenLayoutChanged()
{
    // KWin emits screenAdded / screenRemoved before per-window outputChanged
    // signals for windows it reassigns when an output appears or disappears.
    // virtualScreenGeometryChanged — which the geometry-debounce slot above
    // listens for — fires later in the same cascade, leaving a window where
    // outputChanged can be processed with no screen-change flag set. The
    // guard in PlasmaZonesEffect's outputChanged lambda (window_lifecycle.cpp)
    // depends on isScreenChangeInProgress() to recognize involuntary moves
    // when oldScreenStillConnected is true (DPMS-wake layout shift onto a
    // monitor that simply moved to a new x-offset rather than disappearing),
    // so latch the flag at the earliest point KWin tells us anything is
    // happening to the output set.
    m_pendingScreenChange = true;
    m_screenChangeDebounce.start();

    // A real layout change also moves panels — re-push the work area for the
    // same reason slotScreenGeometryChanged does. Coalesces via the queued
    // continuation, so a burst of screenAdded / screenRemoved + a trailing
    // virtualScreenGeometryChanged collapse into one client-area report.
    scheduleClientAreaReport();
}

void ScreenChangeHandler::slotReapplyWindowGeometriesRequested()
{
    qCInfo(lcScreenChange) << "Daemon requested re-apply of window geometries (e.g. after panel editor close)";
    if (m_reapplyInProgress) {
        m_reapplyPending = true;
        return;
    }
    fetchAndApplyWindowGeometries();
}

void ScreenChangeHandler::fetchAndApplyWindowGeometries()
{
    if (!m_effect->isDaemonReady("get updated window geometries")) {
        return;
    }
    m_reapplyInProgress = true;
    QDBusPendingCall pendingCall = PhosphorProtocol::ClientHelpers::asyncCall(
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("getUpdatedWindowGeometries"));
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);
    QPointer<ScreenChangeHandler> self(this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [self](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (!self) {
            return;
        }
        self->m_reapplyInProgress = false;
        QDBusPendingReply<PhosphorProtocol::WindowGeometryList> reply = *w;
        if (!reply.isValid()) {
            qCDebug(lcScreenChange) << "No window geometries to update";
        } else {
            self->applyWindowGeometries(reply.value());
        }
        if (self->m_reapplyPending) {
            self->m_reapplyPending = false;
            QTimer::singleShot(0, self, [self]() {
                if (self) {
                    self->fetchAndApplyWindowGeometries();
                }
            });
        }
    });
}

void ScreenChangeHandler::applyWindowGeometries(const PhosphorProtocol::WindowGeometryList& geometries)
{
    if (geometries.isEmpty()) {
        qCDebug(lcScreenChange) << "Empty geometries list from daemon";
        return;
    }
    qCInfo(lcScreenChange) << "Applying geometries to" << geometries.size() << "windows";

    // Single pass: map by full window ID and by appId for fallback
    QHash<QString, KWin::EffectWindow*> windowByFullId;
    QHash<QString, KWin::EffectWindow*> windowByAppId;
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        // isDeleted: a dying sibling must not claim the insert-if-absent
        // appId slot — the apply loop's own isDeleted guard would then
        // silently drop the live window's reapply.
        if (!w || w->isDeleted() || !m_effect->shouldHandleWindow(w)) {
            continue;
        }
        QString fullId = m_effect->getWindowId(w);
        QString appId = ::PhosphorIdentity::WindowId::extractAppId(fullId);
        windowByFullId.insert(fullId, w);
        if (!windowByAppId.contains(appId)) {
            windowByAppId.insert(appId, w);
        }
    }

    struct ApplyEntry
    {
        QPointer<KWin::EffectWindow> window;
        QRect geometry;
    };
    QVector<ApplyEntry> toApply;

    for (const auto& entry : geometries) {
        if (entry.windowId.isEmpty()) {
            qCDebug(lcScreenChange) << "Skipping geometry entry with empty windowId";
            continue;
        }
        if (entry.width <= 0 || entry.height <= 0) {
            qCDebug(lcScreenChange) << "Skipping geometry entry with invalid size for" << entry.windowId;
            continue;
        }

        KWin::EffectWindow* window = windowByFullId.value(entry.windowId);
        if (!window) {
            window = windowByAppId.value(::PhosphorIdentity::WindowId::extractAppId(entry.windowId));
        }
        if (window && m_effect->shouldHandleWindow(window)) {
            const QString winScreenId = m_effect->getWindowScreenId(window);
            if (m_effect->m_autotileHandler->isAutotileScreen(winScreenId)) {
                qCDebug(lcScreenChange) << "Skipping autotile-managed window" << entry.windowId << "on screen"
                                        << winScreenId;
                continue;
            }
            QRect newGeometry = entry.toRect();
            QRectF currentWindowGeometry = window->frameGeometry();
            if (QRect(currentWindowGeometry.toRect()) != newGeometry) {
                toApply.append({QPointer<KWin::EffectWindow>(window), newGeometry});
            }
        }
    }

    m_effect->applyStaggeredOrImmediate(toApply.size(), [this, toApply](int i) {
        const ApplyEntry& e = toApply[i];
        if (e.window && !e.window->isDeleted() && m_effect->shouldHandleWindow(e.window)) {
            // Re-check at apply time (mirrors the build-time guard above): the
            // window's screen can flip to autotile during the stagger interval,
            // and a snap-path applyWindowGeometry would then fight the autotile
            // engine for placement.
            if (m_effect->m_autotileHandler->isAutotileScreen(m_effect->getWindowScreenId(e.window))) {
                return;
            }
            qCInfo(lcScreenChange) << "Repositioning window" << m_effect->getWindowId(e.window) << "to" << e.geometry;
            m_effect->applyWindowGeometry(e.window, e.geometry);
        }
    });
}

void ScreenChangeHandler::trackDockWindow(KWin::EffectWindow* w)
{
    if (!w || !w->isDock()) {
        return;
    }
    // A panel mapped (or was already mapped at effect startup). KWin reserves
    // and adjusts its strut asynchronously, and the panel itself may resize
    // during its own lifetime (config changes, content-driven thickness).
    // Hook frame-geometry changes so every strut adjustment re-pushes the
    // work area. `this` as the connection context auto-disconnects the lambda
    // when either the dock's EffectWindow or this handler is destroyed.
    connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, this, [this]() {
        scheduleClientAreaReport();
    });
    scheduleClientAreaReport();
}

void ScreenChangeHandler::onWindowClosed(KWin::EffectWindow* w)
{
    if (!w || !w->isDock()) {
        return;
    }
    // A panel closed — its strut is freed and KWin grows the work area.
    scheduleClientAreaReport();
}

void ScreenChangeHandler::scheduleClientAreaReport()
{
    if (m_stopped) {
        return; // Handler is shutting down — no further reports.
    }
    if (m_clientAreaReportQueued) {
        return; // A report is already queued for this event-loop turn.
    }
    m_clientAreaReportQueued = true;
    // Queued, not timed: every scheduleClientAreaReport() in the current
    // event-loop turn collapses onto this one continuation, which Qt
    // dispatches after the turn's synchronous work — including KWin's own
    // strut recompute for the dock signal that triggered us — has run. No
    // arbitrary delay, and `this` as the context object means the call is
    // dropped if the handler is destroyed before it fires.
    QMetaObject::invokeMethod(
        this,
        [this]() {
            m_clientAreaReportQueued = false;
            if (m_stopped) {
                return; // stop() ran after this report was queued.
            }
            reportClientArea();
        },
        Qt::QueuedConnection);
}

void ScreenChangeHandler::reportClientArea()
{
    if (!m_effect->m_daemonServiceRegistered) {
        // Bridge not registered yet — continueDaemonReadySetup() re-schedules
        // a report once it is, so dropping this one loses nothing.
        return;
    }

    // clientArea(MaximizeArea) is the exact panel-excluded work area KWin
    // reserves for maximized windows: correct per-edge strut attribution,
    // and auto-hide panels (no strut) correctly excluded. KWin 6.7 dropped the
    // per-desktop overload (clientArea is now resolved against the current
    // desktop internally); a desktopChanged signal still re-schedules a report
    // so per-desktop panels stay tracked.
    const auto outputs = KWin::effects->screens();
    for (KWin::LogicalOutput* output : outputs) {
        if (!output) {
            continue;
        }
        const QRect work = KWin::effects->clientArea(KWin::MaximizeArea, output).toRect();
        // isValid() is the exact negation of isEmpty() for QRect — a degenerate
        // (zero/negative-size) work area is skipped by this single check.
        if (!work.isValid()) {
            continue;
        }
        qCDebug(lcScreenChange) << "Reporting KWin work area for" << output->name() << "=" << work;
        PhosphorProtocol::ClientHelpers::fireAndForget(
            this, PhosphorProtocol::Service::Interface::Screen, QStringLiteral("setAvailableGeometryFromKWin"),
            {output->name(), work.x(), work.y(), work.width(), work.height()},
            QStringLiteral("setAvailableGeometryFromKWin"));
    }
}

} // namespace PlasmaZones
