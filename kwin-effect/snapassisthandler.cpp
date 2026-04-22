// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapassisthandler.h"
#include "plasmazoneseffect.h"
#include "autotilehandler.h"
#include "kwin_compositor_bridge.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/WireTypes.h>
#include <snap_assist_filter.h>

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

Q_LOGGING_CATEGORY(lcSnapAssist, "kwin.effect.plasmazones.snapassist", QtWarningMsg)

namespace PlasmaZones {

SnapAssistHandler::SnapAssistHandler(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
{
}

void SnapAssistHandler::showContinuationIfNeeded(const QString& screenId)
{
    if (screenId.isEmpty()) {
        qCInfo(lcSnapAssist) << "Snap assist continuation skipped: empty screen name";
        return;
    }
    if (!m_snapAssistEnabled) {
        qCInfo(lcSnapAssist) << "Snap assist continuation skipped: snapAssistEnabled is false";
        return;
    }
    if (!m_effect->isDaemonReady("snap assist continuation")) {
        return;
    }
    qCInfo(lcSnapAssist) << "Snap assist continuation: querying empty zones for screen" << screenId;
    QDBusPendingCall emptyCall = PhosphorProtocol::ClientHelpers::asyncCall(
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("getEmptyZones"), {screenId});
    auto* watcher = new QDBusPendingCallWatcher(emptyCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, screenId](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<PhosphorProtocol::EmptyZoneList> reply = *w;
        if (!reply.isValid() || reply.value().isEmpty()) {
            qCInfo(lcSnapAssist) << "Snap assist continuation: no empty zones"
                                 << (reply.isValid() ? QStringLiteral("(empty list)")
                                                     : QStringLiteral("(invalid reply)"));
            return;
        }
        asyncShow(QString(), screenId, reply.value());
    });
}

void SnapAssistHandler::asyncShow(const QString& excludeWindowId, const QString& screenId,
                                  const PhosphorProtocol::EmptyZoneList& emptyZones)
{
    if (!m_effect->isDaemonReady("snap assist snapped windows")) {
        return;
    }
    QDBusPendingCall snapCall = PhosphorProtocol::ClientHelpers::asyncCall(
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("getSnappedWindows"));
    auto* snapWatcher = new QDBusPendingCallWatcher(snapCall, this);
    connect(snapWatcher, &QDBusPendingCallWatcher::finished, this,
            [this, excludeWindowId, screenId, emptyZones](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                QDBusPendingReply<QStringList> reply = *w;
                QSet<QString> snappedWindowIds;
                if (reply.isValid()) {
                    for (const QString& id : reply.value()) {
                        snappedWindowIds.insert(id);
                    }
                }
                PhosphorProtocol::SnapAssistCandidateList candidates =
                    buildCandidates(excludeWindowId, screenId, snappedWindowIds);
                if (candidates.isEmpty()) {
                    qCInfo(lcSnapAssist) << "Snap assist skipped: no unsnapped candidate windows on" << screenId;
                    return;
                }
                if (!m_effect->isDaemonReady("snap assist show")) {
                    return;
                }
                QDBusMessage msg = QDBusMessage::createMethodCall(
                    PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                    PhosphorProtocol::Service::Interface::Overlay, QStringLiteral("showSnapAssist"));
                msg << screenId << QVariant::fromValue(emptyZones) << QVariant::fromValue(candidates);
                auto* callWatcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), m_effect);
                connect(callWatcher, &QDBusPendingCallWatcher::finished, m_effect, [](QDBusPendingCallWatcher* cw) {
                    if (cw->isError()) {
                        qCWarning(lcSnapAssist) << "showSnapAssist D-Bus call failed:" << cw->error().message();
                    }
                    cw->deleteLater();
                });
                qCInfo(lcSnapAssist) << "Snap Assist shown with" << candidates.size() << "candidates";
            });
}

PhosphorProtocol::SnapAssistCandidateList
SnapAssistHandler::buildCandidates(const QString& excludeWindowId, const QString& screenId,
                                   const QSet<QString>& snappedWindowIds) const
{
    PhosphorProtocol::SnapAssistCandidateList candidates =
        SnapAssistFilter::buildCandidates(m_effect->compositorBridge(), excludeWindowId, screenId, snappedWindowIds);

    // KWin-specific: fill compositorHandle (internal UUID) for overlay window identification.
    // Also apply KWin-specific filters the shared SnapAssistFilter can't know about:
    //  - exclude windows managed by the autotile engine
    //  - for virtual screens on the same physical monitor, include sibling VS candidates
    //    but still drop autotile-managed ones (already handled above).
    auto* autotile = m_effect->autotileHandler();
    for (auto it = candidates.begin(); it != candidates.end();) {
        if (autotile && autotile->isTrackedWindow(it->windowId)) {
            it = candidates.erase(it);
            continue;
        }
        auto* ew = KWinCompositorBridge::toEffectWindow(m_effect->compositorBridge()->findWindowById(it->windowId));
        if (ew) {
            it->compositorHandle = ew->internalId().toString();
        }
        ++it;
    }
    return candidates;
}

} // namespace PlasmaZones
