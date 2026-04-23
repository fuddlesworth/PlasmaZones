// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenchangehandler.h"
#include "autotilehandler.h"
#include "plasmazoneseffect.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/WireTypes.h>
#include <PhosphorIdentity/WindowId.h>

#include <effect/effecthandler.h>

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
        if (!w || !m_effect->shouldHandleWindow(w)) {
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
            qCInfo(lcScreenChange) << "Repositioning window" << m_effect->getWindowId(e.window) << "to" << e.geometry;
            m_effect->applySnapGeometry(e.window, e.geometry);
        }
    });
}

} // namespace PlasmaZones
