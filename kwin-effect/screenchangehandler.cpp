// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenchangehandler.h"
#include "autotilehandler.h"
#include "plasmazoneseffect.h"
#include "dbus_constants.h"

#include <effect/effecthandler.h>

#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
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
        qCDebug(lcScreenChange) << "Virtual screen size unchanged, skipping window repositioning";
        return;
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
    QDBusPendingCall pendingCall = m_effect->asyncMethodCall(PlasmaZones::DBus::Interface::WindowTracking,
                                                             QStringLiteral("getUpdatedWindowGeometries"));
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);
    QPointer<ScreenChangeHandler> self(this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [self](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (!self) {
            return;
        }
        self->m_reapplyInProgress = false;
        QDBusPendingReply<QString> reply = *w;
        if (!reply.isValid()) {
            qCDebug(lcScreenChange) << "No window geometries to update";
        } else {
            self->applyWindowGeometriesFromJson(reply.value());
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

void ScreenChangeHandler::applyWindowGeometriesFromJson(const QString& geometriesJson)
{
    if (geometriesJson.isEmpty() || geometriesJson == QLatin1String("[]")) {
        qCDebug(lcScreenChange) << "Empty geometries list from daemon";
        return;
    }
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(geometriesJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcScreenChange) << "Failed to parse window geometries:" << parseError.errorString();
        return;
    }
    if (!doc.isArray()) {
        qCWarning(lcScreenChange) << "Window geometries root is not an array";
        return;
    }
    QJsonArray geometries = doc.array();
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
        QString appId = PlasmaZonesEffect::extractAppId(fullId);
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

    for (const QJsonValue& value : geometries) {
        if (!value.isObject()) {
            qCDebug(lcScreenChange) << "Skipping non-object geometry entry";
            continue;
        }
        QJsonObject obj = value.toObject();
        QString windowId = obj[QLatin1String("windowId")].toString();
        if (windowId.isEmpty()) {
            qCDebug(lcScreenChange) << "Skipping geometry entry with empty windowId";
            continue;
        }
        int width = obj[QLatin1String("width")].toInt();
        int height = obj[QLatin1String("height")].toInt();
        if (width <= 0 || height <= 0) {
            qCDebug(lcScreenChange) << "Skipping geometry entry with invalid size for" << windowId;
            continue;
        }
        int x = obj[QLatin1String("x")].toInt();
        int y = obj[QLatin1String("y")].toInt();

        KWin::EffectWindow* window = windowByFullId.value(windowId);
        if (!window) {
            window = windowByAppId.value(PlasmaZonesEffect::extractAppId(windowId));
        }
        if (window && m_effect->shouldHandleWindow(window)) {
            const QString winScreenId = m_effect->getWindowScreenId(window);
            if (m_effect->m_autotileHandler->isAutotileScreen(winScreenId)) {
                qCDebug(lcScreenChange) << "Skipping autotile-managed window" << windowId << "on screen" << winScreenId;
                continue;
            }
            QRect newGeometry(x, y, width, height);
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
