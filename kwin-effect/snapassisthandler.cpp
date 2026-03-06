// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapassisthandler.h"
#include "plasmazoneseffect.h"

#include <effect/effecthandler.h>

#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonDocument>
#include <QJsonObject>

Q_LOGGING_CATEGORY(lcSnapAssist, "kwin.effect.plasmazones.snapassist", QtWarningMsg)

namespace PlasmaZones {

SnapAssistHandler::SnapAssistHandler(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
{
}

void SnapAssistHandler::showContinuationIfNeeded(const QString& screenName)
{
    if (screenName.isEmpty()) {
        qCInfo(lcSnapAssist) << "Snap assist continuation skipped: empty screen name";
        return;
    }
    if (!m_snapAssistEnabled) {
        qCInfo(lcSnapAssist) << "Snap assist continuation skipped: snapAssistEnabled is false";
        return;
    }
    if (!m_effect->ensureWindowTrackingReady("snap assist continuation")) {
        return;
    }
    qCInfo(lcSnapAssist) << "Snap assist continuation: querying empty zones for screen" << screenName;
    auto* iface = m_effect->windowTrackingInterface();
    QDBusPendingCall emptyCall = iface->asyncCall(QStringLiteral("getEmptyZonesJson"), screenName);
    auto* watcher = new QDBusPendingCallWatcher(emptyCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, screenName](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QString> reply = *w;
        if (!reply.isValid() || reply.value().isEmpty() || reply.value() == QLatin1String("[]")) {
            qCInfo(lcSnapAssist) << "Snap assist continuation: no empty zones"
                                 << (reply.isValid() ? reply.value() : QStringLiteral("(invalid reply)"));
            return;
        }
        asyncShow(QString(), screenName, reply.value());
    });
}

void SnapAssistHandler::asyncShow(const QString& excludeWindowId, const QString& screenName,
                                  const QString& emptyZonesJson)
{
    if (!m_effect->ensureWindowTrackingReady("snap assist snapped windows")) {
        return;
    }
    auto* iface = m_effect->windowTrackingInterface();
    QDBusPendingCall snapCall = iface->asyncCall(QStringLiteral("getSnappedWindows"));
    auto* snapWatcher = new QDBusPendingCallWatcher(snapCall, this);
    connect(snapWatcher, &QDBusPendingCallWatcher::finished, this,
            [this, excludeWindowId, screenName, emptyZonesJson](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                QDBusPendingReply<QStringList> reply = *w;
                QSet<QString> snappedWindowIds;
                if (reply.isValid()) {
                    for (const QString& id : reply.value()) {
                        snappedWindowIds.insert(id);
                    }
                }
                QJsonArray candidates = buildCandidates(excludeWindowId, screenName, snappedWindowIds);
                if (candidates.isEmpty()) {
                    qCInfo(lcSnapAssist) << "Snap assist skipped: no unsnapped candidate windows on" << screenName;
                    return;
                }
                if (!m_effect->ensureOverlayInterface("snap assist show")) {
                    return;
                }
                m_effect->m_overlayInterface->asyncCall(
                    QStringLiteral("showSnapAssist"), screenName, emptyZonesJson,
                    QString::fromUtf8(QJsonDocument(candidates).toJson(QJsonDocument::Compact)));
                qCInfo(lcSnapAssist) << "Snap Assist shown with" << candidates.size() << "candidates";
            });
}

QJsonArray SnapAssistHandler::buildCandidates(const QString& excludeWindowId, const QString& screenName,
                                              const QSet<QString>& snappedWindowIds) const
{
    QJsonArray candidates;
    const auto windows = KWin::effects->stackingOrder();

    for (KWin::EffectWindow* w : windows) {
        if (!w || !m_effect->shouldHandleWindow(w) || w->isMinimized() || !w->isOnCurrentDesktop()
            || !w->isOnCurrentActivity()) {
            continue;
        }

        QString windowId = m_effect->getWindowId(w);
        if (windowId == excludeWindowId) {
            continue;
        }
        if (snappedWindowIds.contains(windowId)) {
            continue;
        }
        QString appId = PlasmaZonesEffect::extractAppId(windowId);
        bool snappedByAppId = false;
        for (const QString& snappedId : snappedWindowIds) {
            if (PlasmaZonesEffect::extractAppId(snappedId) == appId) {
                snappedByAppId = true;
                break;
            }
        }
        if (snappedByAppId) {
            int sameAppCount = 0;
            for (KWin::EffectWindow* other : windows) {
                if (other && m_effect->shouldHandleWindow(other)
                    && PlasmaZonesEffect::extractAppId(m_effect->getWindowId(other)) == appId) {
                    ++sameAppCount;
                }
            }
            if (sameAppCount <= 1) {
                continue;
            }
        }

        QString winScreenName = m_effect->getWindowScreenName(w);
        if (!screenName.isEmpty() && winScreenName != screenName) {
            continue;
        }

        QString windowClass = w->windowClass();
        QString iconName = m_effect->deriveShortNameFromWindowClass(windowClass);
        if (iconName.isEmpty()) {
            iconName = QStringLiteral("application-x-executable");
        }

        QJsonObject obj;
        obj[QLatin1String("windowId")] = windowId;
        obj[QLatin1String("kwinHandle")] = w->internalId().toString();
        obj[QLatin1String("icon")] = iconName;
        obj[QLatin1String("caption")] = w->caption();

        const QString dataUrl = PlasmaZonesEffect::iconToDataUrl(w->icon(), 64);
        if (!dataUrl.isEmpty()) {
            obj[QLatin1String("iconPng")] = dataUrl;
        }

        candidates.append(obj);
    }
    return candidates;
}

} // namespace PlasmaZones
