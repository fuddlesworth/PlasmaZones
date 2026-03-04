// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// D-Bus signal slot handlers for AutotileHandler.
// Part of AutotileHandler — split from autotilehandler.cpp for SRP.

#include "../autotilehandler.h"
#include "../plasmazoneseffect.h"
#include "../navigationhandler.h"
#include "../dbus_constants.h"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>
#include <workspace.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

// ═══════════════════════════════════════════════════════════════════════════════
// D-Bus signal slot handlers
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileHandler::slotEnabledChanged(bool enabled)
{
    qCInfo(lcEffect) << "Autotile enabled state changed:" << enabled;
    if (!enabled) {
        restoreAllBorderless();
        restoreAllMonocleMaximized();
    }
}

void AutotileHandler::slotScreensChanged(const QStringList& screenNames)
{
    const QSet<QString> newScreens(screenNames.begin(), screenNames.end());
    const QSet<QString> removed = m_autotileScreens - newScreens;
    const QSet<QString> added = newScreens - m_autotileScreens;

    const auto windows = KWin::effects->stackingOrder();

    if (!removed.isEmpty()) {
        QSet<QString> windowsOnRemovedScreens;
        for (KWin::EffectWindow* w : windows) {
            if (w && removed.contains(m_effect->getWindowScreenName(w))) {
                windowsOnRemovedScreens.insert(m_effect->getWindowId(w));
            }
        }
        m_notifiedWindows -= windowsOnRemovedScreens;

        // Restore title bars for windows on removed screens
        for (const QString& windowId : std::as_const(windowsOnRemovedScreens)) {
            if (m_border.borderlessWindows.contains(windowId)) {
                KWin::EffectWindow* w = m_effect->findWindowById(windowId);
                if (w) {
                    setWindowBorderless(w, windowId, false);
                }
            }
        }

        // Restore pre-autotile geometries for windows on removed screens.
        struct RestoreEntry {
            QPointer<KWin::EffectWindow> window;
            QRect geometry;
        };
        QVector<RestoreEntry> toRestore;
        for (const QString& screenName : removed) {
            const auto savedIt = m_preAutotileGeometries.constFind(screenName);
            if (savedIt == m_preAutotileGeometries.constEnd()) {
                continue;
            }
            const QHash<QString, QRectF>& savedGeometries = savedIt.value();
            for (KWin::EffectWindow* w : windows) {
                if (!w || !m_effect->shouldHandleWindow(w) || m_effect->getWindowScreenName(w) != screenName) {
                    continue;
                }
                const QString windowId = m_effect->getWindowId(w);

                if (m_effect->isWindowFloating(windowId)) {
                    qCDebug(lcEffect) << "Skipping pre-autotile restore for floating window:" << windowId;
                    continue;
                }

                unmaximizeMonocleWindow(windowId);

                const QString savedKey = findSavedGeometryKey(savedGeometries, windowId);
                if (savedKey.isEmpty()) {
                    continue;
                }
                const QRectF& savedGeo = savedGeometries.value(savedKey);
                if (savedGeo.isValid()) {
                    const int l = qRound(savedGeo.x());
                    const int t = qRound(savedGeo.y());
                    const int r = qRound(savedGeo.x() + savedGeo.width());
                    const int b = qRound(savedGeo.y() + savedGeo.height());
                    toRestore.append({QPointer<KWin::EffectWindow>(w), QRect(l, t, std::max(0, r - l), std::max(0, b - t))});
                }
            }
            // Clear daemon-side pre-autotile geometries for this screen
            if (m_effect->m_daemonServiceRegistered) {
                for (KWin::EffectWindow* w : windows) {
                    if (!w || !m_effect->shouldHandleWindow(w) || m_effect->getWindowScreenName(w) != screenName) {
                        continue;
                    }
                    const QString windowId = m_effect->getWindowId(w);
                    m_effect->fireAndForgetDBusCall(DBus::Interface::WindowTracking,
                                                    QStringLiteral("clearPreAutotileGeometry"),
                                                    {windowId}, QStringLiteral("clearPreAutotileGeometry"));
                }
            }
            m_preAutotileGeometries.remove(screenName);
        }
        // Invalidate any pending stagger timers from prior autotile operations.
        ++m_autotileStaggerGeneration;
        m_autotileTargetZones.clear();
        // Apply pre-autotile geometry restore IMMEDIATELY (not staggered).
        // The daemon sends resnapCurrentAssignments() shortly after this handler
        // returns, which arrives as moveWindowToZone D-Bus calls. If we used
        // staggered restore here, the stagger timers would fire AFTER resnap
        // and override the correct zone positions with stale pre-autotile
        // positions — causing windows to end up at wrong locations.
        // Immediate restore ensures zone-assigned windows get briefly restored
        // then overridden by resnap, while orphaned windows (no zone assignment,
        // no resnap) keep their pre-autotile positions.
        for (const RestoreEntry& e : toRestore) {
            if (e.window && !e.window->isDeleted() && m_effect->shouldHandleWindow(e.window)) {
                qCInfo(lcEffect) << "Restoring pre-autotile geometry for" << m_effect->getWindowId(e.window) << "to" << e.geometry;
                m_effect->applySnapGeometry(e.window, e.geometry);
            }
        }
    }

    m_autotileScreens = newScreens;

    if (!added.isEmpty()) {
        // Save current frame geometries and notify windows IMMEDIATELY (non-blocking).
        // The window's current position IS the pre-autotile geometry we want to save.
        // slotWindowsTileRequested also saves pre-autotile geometry (before tiling), so
        // this is a belt-and-suspenders save for windows on newly-added autotile screens.
        for (KWin::EffectWindow* w : windows) {
            if (!w || !m_effect->shouldHandleWindow(w)) {
                continue;
            }
            const QString screenName = m_effect->getWindowScreenName(w);
            if (!added.contains(screenName)) {
                continue;
            }
            const QString windowId = m_effect->getWindowId(w);
            saveAndRecordPreAutotileGeometry(windowId, screenName, w->frameGeometry());

            if (!w->isMinimized()) {
                m_notifiedWindows.remove(windowId);
                notifyWindowAdded(w);
            }
        }
        qCInfo(lcEffect) << "Saved pre-autotile geometries for screens:" << added;

        // Async fetch of daemon's persisted pre-autotile geometries from previous session.
        // These may be more accurate than the current frame for windows that were resnapped
        // to zones in manual mode (current frame = zone position, daemon value = original).
        // Non-blocking: the old synchronous QDBus::Block call (500ms timeout) froze the
        // compositor thread, causing jerky first-retile animations since QElapsedTimer
        // kept advancing while no frames were rendered.
        QDBusMessage fetchMsg = QDBusMessage::createMethodCall(
            DBus::ServiceName, DBus::ObjectPath,
            DBus::Interface::WindowTracking, QStringLiteral("getPreAutotileGeometriesJson"));
        auto* watcher = new QDBusPendingCallWatcher(
            QDBusConnection::sessionBus().asyncCall(fetchMsg), this);
        // Capture expected screen set for staleness detection — if the user
        // rapidly toggles autotile, a stale reply must not overwrite fresh data.
        const QSet<QString> expectedScreens = newScreens;
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, added, expectedScreens](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            QDBusPendingReply<QString> reply = *w;
            if (!reply.isValid()) {
                return;
            }
            // Bail if the autotile screen set changed while we were waiting
            if (m_autotileScreens != expectedScreens) {
                qCDebug(lcEffect) << "Stale async pre-autotile geometry reply, screen set changed";
                return;
            }
            const QString json = reply.value();
            QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
            if (!doc.isObject()) {
                return;
            }
            const auto allWindows = KWin::effects->stackingOrder();
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                const QString stableId = it.key();
                if (!it.value().isObject()) continue;
                QJsonObject geomObj = it.value().toObject();
                QRectF geom(geomObj[QLatin1String("x")].toInt(),
                            geomObj[QLatin1String("y")].toInt(),
                            geomObj[QLatin1String("width")].toInt(),
                            geomObj[QLatin1String("height")].toInt());
                if (geom.width() <= 0 || geom.height() <= 0) continue;
                // Find all windows on added screens matching this stableId.
                // If multiple windows share the same stableId (e.g., 3 Dolphin instances),
                // the daemon's single geometry is ambiguous — skip the override entirely.
                KWin::EffectWindow* matchedWindow = nullptr;
                bool ambiguous = false;
                for (KWin::EffectWindow* ew : allWindows) {
                    if (!ew || !m_effect->shouldHandleWindow(ew)) continue;
                    if (PlasmaZonesEffect::extractStableId(m_effect->getWindowId(ew)) != stableId) continue;
                    if (!added.contains(m_effect->getWindowScreenName(ew))) continue;
                    if (matchedWindow) {
                        ambiguous = true;
                        break;
                    }
                    matchedWindow = ew;
                }
                if (ambiguous || !matchedWindow) {
                    if (ambiguous) {
                        qCDebug(lcEffect) << "Skipping daemon geometry override for ambiguous stableId"
                                          << stableId << "(multiple live windows match)";
                    }
                    continue;
                }
                {
                    const QString scr = m_effect->getWindowScreenName(matchedWindow);
                    auto& screenGeometries = m_preAutotileGeometries[scr];
                    const QString wId = m_effect->getWindowId(matchedWindow);
                    // Only pre-populate if no entry yet (saveAndRecordPreAutotileGeometry
                    // already ran for windows on these screens). If the entry matches the
                    // window's current frame (i.e., it was zone-snapped), prefer the daemon's
                    // stored value which is the original pre-autotile position.
                    auto existingIt = screenGeometries.find(wId);
                    if (existingIt == screenGeometries.end()) {
                        screenGeometries[wId] = geom;
                        qCDebug(lcEffect) << "Pre-populated pre-autotile geometry from daemon for"
                                          << stableId << "on" << scr << ":" << geom;
                    } else if (existingIt.value().toRect() != geom.toRect()) {
                        // Daemon stored a different geometry (likely from before the window
                        // was resnapped to a zone). Prefer the daemon's version as it's the
                        // true pre-autotile position.
                        qCDebug(lcEffect) << "Updated pre-autotile geometry from daemon for"
                                          << stableId << "on" << scr << ":"
                                          << existingIt.value() << "->" << geom;
                        existingIt.value() = geom;
                    }
                }
            }
        });
    }

    qCInfo(lcEffect) << "Autotile screens changed:" << m_autotileScreens;
}

void AutotileHandler::slotWindowFloatingChanged(const QString& windowId, bool isFloating,
                                                 const QString& screenName)
{
    Q_UNUSED(screenName)
    qCInfo(lcEffect) << "Autotile floating changed:" << windowId << "isFloating:" << isFloating
                     << "screen:" << screenName;

    m_effect->m_navigationHandler->setWindowFloating(windowId, isFloating);

    if (!isFloating) {
        KWin::EffectWindow* unfloatWin = m_effect->findWindowById(windowId);
        if (unfloatWin && unfloatWin == KWin::effects->activeWindow()) {
            m_pendingAutotileFocusWindowId = windowId;
            KWin::effects->activateWindow(unfloatWin);
        }
    }

    if (isFloating) {
        if (m_border.borderlessWindows.contains(windowId)) {
            KWin::EffectWindow* w = m_effect->findWindowById(windowId);
            if (w) {
                setWindowBorderless(w, windowId, false);
            }
        }
        unmaximizeMonocleWindow(windowId);

        KWin::EffectWindow* floatWin = m_effect->findWindowById(windowId);
        if (!floatWin) {
            qCDebug(lcEffect) << "Autotile: window not found for float raise:" << windowId;
        } else if (floatWin == KWin::effects->activeWindow()) {
            m_pendingAutotileFocusWindowId = windowId;
            auto* ws = KWin::Workspace::self();
            if (ws) {
                KWin::Window* kw = floatWin->window();
                if (kw) {
                    ws->raiseWindow(kw);
                }
            }
            KWin::effects->activateWindow(floatWin);
        }
    }
}

void AutotileHandler::slotWindowMinimizedChanged(KWin::EffectWindow* w)
{
    if (!w || !m_effect->shouldHandleWindow(w) || !m_effect->isTileableWindow(w)) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    const QString screenName = m_effect->getWindowScreenName(w);

    if (!m_autotileScreens.contains(screenName)) {
        return;
    }

    const bool minimized = w->isMinimized();

    if (minimized) {
        if (m_effect->isWindowFloating(windowId)) {
            qCDebug(lcEffect) << "Autotile: minimized already-floating window, skipping floatWindow:" << windowId;
            return;
        }
        m_minimizeFloatedWindows.insert(windowId);
    } else {
        if (!m_minimizeFloatedWindows.remove(windowId)) {
            qCDebug(lcEffect) << "Autotile: unminimized window was not minimize-floated, skipping unfloatWindow:" << windowId;
            notifyWindowAdded(w);
            return;
        }
        saveAndRecordPreAutotileGeometry(windowId, screenName, w->frameGeometry());
    }

    const QString method = minimized ? QStringLiteral("floatWindow") : QStringLiteral("unfloatWindow");
    qCInfo(lcEffect) << "Autotile: window" << (minimized ? "minimized, floating:" : "unminimized, unfloating:")
                     << windowId << "on" << screenName;

    if (m_effect->m_daemonServiceRegistered) {
        m_effect->fireAndForgetDBusCall(DBus::Interface::Autotile, method, {windowId}, method);
    }

    if (!minimized) {
        notifyWindowAdded(w);
    }
}

void AutotileHandler::slotWindowMaximizedStateChanged(KWin::EffectWindow* w, bool horizontal, bool vertical)
{
    if (m_suppressMaximizeChanged || !w) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    if (!m_monocleMaximizedWindows.contains(windowId)) {
        return;
    }
    if (horizontal || vertical) {
        return;
    }
    m_monocleMaximizedWindows.remove(windowId);
    qCInfo(lcEffect) << "Monocle window manually unmaximized:" << windowId << "— floating";

    if (m_effect->m_daemonServiceRegistered) {
        m_effect->fireAndForgetDBusCall(DBus::Interface::Autotile, QStringLiteral("floatWindow"),
                                        {windowId}, QStringLiteral("floatWindow"));
    }
}

void AutotileHandler::slotWindowFullScreenChanged(KWin::EffectWindow* w)
{
    if (!w || !w->isFullScreen()) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    // Clear border and borderless tracking so borders are not drawn over fullscreen content
    m_border.zoneGeometries.remove(windowId);
    if (m_border.borderlessWindows.remove(windowId)) {
        KWin::Window* kw = w->window();
        if (kw) {
            kw->setNoBorder(false);
        }
    }
    if (m_monocleMaximizedWindows.remove(windowId)) {
        qCInfo(lcEffect) << "Monocle window went fullscreen:" << windowId << "— removed from tracking";
    }
}

} // namespace PlasmaZones
