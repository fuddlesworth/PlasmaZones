// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autotilehandler.h"
#include "plasmazoneseffect.h"
#include "windowanimator.h"
#include "navigationhandler.h"
#include "dbus_constants.h"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>
#include <workspace.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QTimer>
#include <QtMath>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

AutotileHandler::AutotileHandler(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
{
}

// ═══════════════════════════════════════════════════════════════════════════════
// Static utility methods
// ═══════════════════════════════════════════════════════════════════════════════

QString AutotileHandler::findSavedGeometryKey(const QHash<QString, QRectF>& savedGeometries,
                                               const QString& windowId)
{
    auto it = savedGeometries.constFind(windowId);
    if (it != savedGeometries.constEnd()) {
        return it.key();
    }
    const QString windowStableId = PlasmaZonesEffect::extractStableId(windowId);
    if (windowStableId.isEmpty()) {
        return QString();
    }
    QString matchKey;
    for (auto i = savedGeometries.constBegin(); i != savedGeometries.constEnd(); ++i) {
        if (PlasmaZonesEffect::extractStableId(i.key()) == windowStableId) {
            if (!matchKey.isEmpty()) {
                return QString(); // Multiple matches - ambiguous
            }
            matchKey = i.key();
        }
    }
    return matchKey;
}

bool AutotileHandler::hasSavedGeometryForWindow(const QHash<QString, QRectF>& savedGeometries,
                                                  const QString& windowId)
{
    return !findSavedGeometryKey(savedGeometries, windowId).isEmpty();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Monocle helpers
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileHandler::unmaximizeMonocleWindow(const QString& windowId)
{
    if (!m_monocleMaximizedWindows.remove(windowId)) {
        return;
    }
    KWin::EffectWindow* w = m_effect->findWindowById(windowId);
    if (!w) {
        return;
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        return;
    }
    ++m_suppressMaximizeChanged;
    kw->maximize(KWin::MaximizeRestore);
    --m_suppressMaximizeChanged;
}

void AutotileHandler::restoreAllMonocleMaximized()
{
    if (m_monocleMaximizedWindows.isEmpty()) {
        return;
    }
    ++m_suppressMaximizeChanged;
    for (const QString& wid : std::as_const(m_monocleMaximizedWindows)) {
        KWin::EffectWindow* w = m_effect->findWindowById(wid);
        if (w) {
            KWin::Window* kw = w->window();
            if (kw) {
                kw->maximize(KWin::MaximizeRestore);
            }
        }
    }
    --m_suppressMaximizeChanged;
    m_monocleMaximizedWindows.clear();
}

void AutotileHandler::updateHideTitleBarsSetting(bool enabled)
{
    const bool wasEnabled = m_autotileHideTitleBars;
    m_autotileHideTitleBars = enabled;
    if (wasEnabled && !enabled) {
        const QSet<QString> toRestore = m_borderlessWindows;
        for (const QString& windowId : toRestore) {
            KWin::EffectWindow* win = m_effect->findWindowById(windowId);
            if (win) {
                setWindowBorderless(win, windowId, false);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Geometry persistence
// ═══════════════════════════════════════════════════════════════════════════════

bool AutotileHandler::saveAndRecordPreAutotileGeometry(const QString& windowId, const QString& screenName,
                                                        const QRectF& frame)
{
    if (windowId.isEmpty() || screenName.isEmpty()) {
        return false;
    }
    if (!frame.isValid() || frame.width() <= 0 || frame.height() <= 0) {
        return false;
    }
    auto& screenGeometries = m_preAutotileGeometries[screenName];
    if (hasSavedGeometryForWindow(screenGeometries, windowId)) {
        return false;
    }
    screenGeometries[windowId] = frame;
    qCDebug(lcEffect) << "Saved pre-autotile geometry for" << windowId << "on" << screenName << ":" << frame;
    if (m_effect->m_daemonServiceRegistered) {
        m_effect->fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("recordPreAutotileGeometry"),
                                        {windowId, screenName, static_cast<int>(frame.x()), static_cast<int>(frame.y()),
                                         static_cast<int>(frame.width()), static_cast<int>(frame.height())},
                                        QStringLiteral("recordPreAutotileGeometry"));
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Borderless / title bar management
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileHandler::setWindowBorderless(KWin::EffectWindow* w, const QString& windowId, bool borderless)
{
    if (!w) {
        return;
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        return;
    }
    if (borderless) {
        // Skip CSD windows (GTK/Electron) — hasDecoration() is false for them.
        if (!w->hasDecoration()) {
            return;
        }
        if (!m_borderlessWindows.contains(windowId)) {
            kw->setNoBorder(true);
            m_borderlessWindows.insert(windowId);
            qCDebug(lcEffect) << "Autotile: hid title bar for" << windowId;
        }
    } else {
        if (m_borderlessWindows.remove(windowId)) {
            kw->setNoBorder(false);
            qCDebug(lcEffect) << "Autotile: restored title bar for" << windowId;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Screen accessors
// ═══════════════════════════════════════════════════════════════════════════════

bool AutotileHandler::isAutotileScreen(const QString& screenName) const
{
    return m_autotileScreens.contains(screenName);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration points
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileHandler::notifyWindowAdded(KWin::EffectWindow* w)
{
    if (!w || !m_effect->shouldHandleWindow(w)) {
        return;
    }

    if (!m_effect->isTileableWindow(w)) {
        return;
    }

    if (w->isMinimized()) {
        return;
    }

    const QString windowId = m_effect->getWindowId(w);

    // Window was already closed before we could notify open — skip (D-Bus ordering race)
    if (m_pendingCloses.remove(windowId)) {
        return;
    }

    if (m_notifiedWindows.contains(windowId)) {
        return;
    }
    m_notifiedWindows.insert(windowId);

    QString screenName = m_effect->getWindowScreenName(w);

    // Only notify autotile daemon for windows on autotile screens
    if (m_autotileScreens.contains(screenName)) {
        int minWidth = 0;
        int minHeight = 0;
        KWin::Window* kw = w->window();
        if (kw) {
            const QSizeF minSize = kw->minSize();
            if (minSize.isValid()) {
                minWidth = qCeil(minSize.width());
                minHeight = qCeil(minSize.height());
            }
        }

        QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath,
                                                          DBus::Interface::Autotile, QStringLiteral("windowOpened"));
        msg << windowId;
        msg << screenName;
        msg << minWidth;
        msg << minHeight;

        QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg);
        auto* watcher = new QDBusPendingCallWatcher(pending, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, windowId](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            if (w->isError()) {
                qCWarning(lcEffect) << "windowOpened D-Bus call failed for" << windowId << ":" << w->error().message();
                m_notifiedWindows.remove(windowId);
            }
        });
        qCDebug(lcEffect) << "Notified autotile: windowOpened" << windowId << "on screen" << screenName
                          << "minSize:" << minWidth << "x" << minHeight;
    }
}

void AutotileHandler::onWindowClosed(const QString& windowId, const QString& screenName)
{
    // If we haven't notified the daemon about this window yet, record the close
    // so we can suppress the open if it arrives late (D-Bus ordering race)
    if (!m_notifiedWindows.contains(windowId) && m_autotileScreens.contains(screenName)) {
        m_pendingCloses.insert(windowId);
    }

    // Remove from autotile tracking sets so re-opened windows get re-notified.
    m_notifiedWindows.remove(windowId);
    m_minimizeFloatedWindows.remove(windowId);

    // Clean up borderless, monocle-maximize, and deferred-centering tracking
    m_borderlessWindows.remove(windowId);
    m_monocleMaximizedWindows.remove(windowId);
    m_autotileTargetZones.remove(windowId);

    // Notify autotile daemon
    if (m_autotileScreens.contains(screenName)) {
        m_effect->fireAndForgetDBusCall(DBus::Interface::Autotile, QStringLiteral("windowClosed"),
                                        {windowId}, QStringLiteral("windowClosed"));
        qCDebug(lcEffect) << "Notified autotile: windowClosed" << windowId << "on screen" << screenName;
    }
}

void AutotileHandler::onDaemonReady()
{
    loadSettings();
    connectSignals();
    m_notifiedWindows.clear();
    m_pendingCloses.clear();
}

bool AutotileHandler::handleAutotileFloatToggle(KWin::EffectWindow* activeWindow,
                                                 const QString& windowId, const QString& screenName)
{
    // Check whether the window has pre-autotile geometry on ANY screen
    bool isAutotileWindow = false;
    for (auto it = m_preAutotileGeometries.constBegin(); it != m_preAutotileGeometries.constEnd(); ++it) {
        if (hasSavedGeometryForWindow(it.value(), windowId)) {
            isAutotileWindow = true;
            break;
        }
    }
    if (!isAutotileWindow) {
        return false;
    }

    // If the window is currently floating, capture its geometry now before
    // the D-Bus call. The engine's retile emits windowsTileRequested before
    // windowFloatingChanged on D-Bus, so reading frameGeometry in the unfloat
    // handler would yield the already-tiled geometry, not the floating position.
    if (m_effect->isWindowFloating(windowId)) {
        const QRectF frame = activeWindow->frameGeometry();
        if (frame.isValid() && frame.width() > 0 && frame.height() > 0) {
            auto& screenGeometries = m_preAutotileGeometries[screenName];
            const QString savedKey = findSavedGeometryKey(screenGeometries, windowId);
            const QString key = savedKey.isEmpty() ? windowId : savedKey;
            screenGeometries[key] = frame;
            qCDebug(lcEffect) << "Pre-saved floating geometry before unfloat:" << windowId << frame;
            if (m_effect->ensureWindowTrackingReady("pre-save floating geometry") && m_effect->m_windowTrackingInterface) {
                m_effect->m_windowTrackingInterface->asyncCall(
                    QStringLiteral("recordPreAutotileGeometry"), windowId, screenName, static_cast<int>(frame.x()),
                    static_cast<int>(frame.y()), static_cast<int>(frame.width()), static_cast<int>(frame.height()));
            }
        }
    }
    m_effect->fireAndForgetDBusCall(DBus::Interface::Autotile, QStringLiteral("toggleWindowFloat"),
                                    {windowId, screenName}, QStringLiteral("toggleWindowFloat"));
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// D-Bus signal connections and settings
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileHandler::connectSignals()
{
    QDBusConnection bus = QDBusConnection::sessionBus();

    bus.connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Autotile, QStringLiteral("windowsTileRequested"),
                this, SLOT(slotWindowsTileRequested(QString)));

    bus.connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Autotile, QStringLiteral("focusWindowRequested"),
                this, SLOT(slotFocusWindowRequested(QString)));

    bus.connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Autotile, QStringLiteral("enabledChanged"), this,
                SLOT(slotEnabledChanged(bool)));

    bus.connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Autotile,
                QStringLiteral("autotileScreensChanged"), this, SLOT(slotScreensChanged(QStringList)));

    bus.connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Autotile, QStringLiteral("windowFloatingChanged"),
                this, SLOT(slotWindowFloatingChanged(QString,bool,QString)));

    qCInfo(lcEffect) << "Connected to autotile D-Bus signals";
}

void AutotileHandler::loadSettings()
{
    // Query initial autotile screen set from daemon asynchronously.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        DBus::ServiceName, DBus::ObjectPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << DBus::Interface::Autotile << QStringLiteral("autotileScreens");

    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QDBusVariant> reply = *w;
        if (reply.isValid()) {
            QStringList screens = reply.value().variant().toStringList();
            const QSet<QString> added(screens.begin(), screens.end());
            m_autotileScreens = added;
            qCInfo(lcEffect) << "Loaded autotile screens:" << m_autotileScreens;

            if (!added.isEmpty()) {
                const auto windows = KWin::effects->stackingOrder();
                for (KWin::EffectWindow* win : windows) {
                    if (win && m_effect->shouldHandleWindow(win)) {
                        const QString screenName = m_effect->getWindowScreenName(win);
                        if (added.contains(screenName)) {
                            const QString windowId = m_effect->getWindowId(win);
                            saveAndRecordPreAutotileGeometry(windowId, screenName, win->frameGeometry());
                            if (!win->isMinimized()) {
                                m_notifiedWindows.remove(windowId);
                                notifyWindowAdded(win);
                            }
                        }
                    }
                }
            }
        } else {
            qCDebug(lcEffect) << "Could not query autotile screens - daemon may not be running";
        }
    });
}

// ═══════════════════════════════════════════════════════════════════════════════
// D-Bus signal slot handlers
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileHandler::slotEnabledChanged(bool enabled)
{
    qCInfo(lcEffect) << "Autotile enabled state changed:" << enabled;
    if (!enabled) {
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
            if (m_borderlessWindows.contains(windowId)) {
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
        ++m_autotileStaggerGeneration;
        m_autotileTargetZones.clear();
        const uint64_t gen = m_autotileStaggerGeneration;
        m_effect->applyStaggeredOrImmediate(toRestore.size(), [this, toRestore, gen](int i) {
            if (m_autotileStaggerGeneration != gen) {
                return;
            }
            const RestoreEntry& e = toRestore[i];
            if (e.window && !e.window->isDeleted() && m_effect->shouldHandleWindow(e.window)) {
                qCInfo(lcEffect) << "Restoring pre-autotile geometry for" << m_effect->getWindowId(e.window) << "to" << e.geometry;
                m_effect->applySnapGeometry(e.window, e.geometry);
            }
        });
    }

    m_autotileScreens = newScreens;

    if (!added.isEmpty()) {
        // Pre-populate from daemon's persisted pre-autotile geometries.
        QDBusMessage fetchMsg = QDBusMessage::createMethodCall(
            DBus::ServiceName, DBus::ObjectPath,
            DBus::Interface::WindowTracking, QStringLiteral("getPreAutotileGeometriesJson"));
        QDBusMessage fetchReply = QDBusConnection::sessionBus().call(fetchMsg, QDBus::Block, 500);
        if (fetchReply.type() == QDBusMessage::ReplyMessage && !fetchReply.arguments().isEmpty()) {
            const QString json = fetchReply.arguments().first().toString();
            QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
            if (doc.isObject()) {
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
                    for (KWin::EffectWindow* w : windows) {
                        if (!w || !m_effect->shouldHandleWindow(w)) continue;
                        if (PlasmaZonesEffect::extractStableId(m_effect->getWindowId(w)) == stableId) {
                            const QString scr = m_effect->getWindowScreenName(w);
                            if (added.contains(scr)) {
                                auto& screenGeometries = m_preAutotileGeometries[scr];
                                if (!hasSavedGeometryForWindow(screenGeometries, m_effect->getWindowId(w))) {
                                    screenGeometries[m_effect->getWindowId(w)] = geom;
                                    qCDebug(lcEffect) << "Pre-populated pre-autotile geometry from daemon for"
                                                      << stableId << "on" << scr << ":" << geom;
                                }
                            }
                            break;
                        }
                    }
                }
            }
        }

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
        if (m_borderlessWindows.contains(windowId)) {
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
    if (m_monocleMaximizedWindows.remove(windowId)) {
        qCInfo(lcEffect) << "Monocle window went fullscreen:" << windowId << "— removed from tracking";
    }
}

void AutotileHandler::slotWindowsTileRequested(const QString& tileRequestsJson)
{
    if (tileRequestsJson.isEmpty()) {
        return;
    }

    ++m_autotileStaggerGeneration;
    m_autotileTargetZones.clear();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(tileRequestsJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        qCWarning(lcEffect) << "Autotile windowsTileRequested: invalid JSON:" << parseError.errorString();
        return;
    }

    struct Entry
    {
        QString windowId;
        QRect geometry;
        KWin::EffectWindow* window = nullptr;
        QVector<KWin::EffectWindow*> candidates;
        bool isMonocle = false;
    };
    QVector<Entry> entries;

    const QJsonArray arr = doc.array();
    for (const QJsonValue& val : arr) {
        QJsonObject obj = val.toObject();
        QString windowId = obj.value(QLatin1String("windowId")).toString();
        QRect geo(obj.value(QLatin1String("x")).toInt(), obj.value(QLatin1String("y")).toInt(),
                  obj.value(QLatin1String("width")).toInt(), obj.value(QLatin1String("height")).toInt());
        QRect normalizedGeometry = geo.normalized();

        if (normalizedGeometry.width() <= 0 || normalizedGeometry.height() <= 0) {
            qCWarning(lcEffect) << "Autotile tile request: invalid geometry for" << windowId << normalizedGeometry;
            continue;
        }

        QVector<KWin::EffectWindow*> candidates = m_effect->findAllWindowsById(windowId);
        if (candidates.isEmpty()) {
            qCDebug(lcEffect) << "Autotile: window not found:" << windowId;
            continue;
        }
        KWin::EffectWindow* w = nullptr;
        if (candidates.size() == 1) {
            w = candidates.first();
        }
        Entry entry;
        entry.windowId = windowId;
        entry.geometry = normalizedGeometry;
        entry.window = w;
        entry.isMonocle = obj.value(QLatin1String("monocle")).toBool(false);
        if (candidates.size() > 1) {
            entry.candidates = candidates;
        }
        entries.append(entry);
    }

    // Disambiguate entries with multiple candidates (same stableId)
    QHash<QString, QVector<int>> stableIdToEntryIndices;
    for (int i = 0; i < entries.size(); ++i) {
        if (!entries[i].candidates.isEmpty()) {
            stableIdToEntryIndices[PlasmaZonesEffect::extractStableId(entries[i].windowId)].append(i);
        }
    }
    for (const QVector<int>& indices : std::as_const(stableIdToEntryIndices)) {
        if (indices.size() <= 1) {
            if (indices.size() == 1 && entries[indices[0]].candidates.size() > 1) {
                Entry& e = entries[indices[0]];
                QPoint targetCenter = e.geometry.center();
                KWin::EffectWindow* best = nullptr;
                qreal bestDist = 1e9;
                for (KWin::EffectWindow* c : std::as_const(e.candidates)) {
                    QPointF cf = c->frameGeometry().center();
                    qreal d = QPointF(targetCenter - cf).manhattanLength();
                    if (d < bestDist) {
                        bestDist = d;
                        best = c;
                    }
                }
                e.window = best;
            }
            continue;
        }
        QVector<KWin::EffectWindow*> candidates = entries[indices[0]].candidates;
        if (candidates.size() != indices.size()) {
            qCDebug(lcEffect) << "Autotile: stableId has" << indices.size() << "entries and" << candidates.size()
                              << "candidates; assigning by position";
        }
        QVector<int> sortedIndices = indices;
        std::sort(sortedIndices.begin(), sortedIndices.end(), [&entries](int a, int b) {
            return entries[a].geometry.x() < entries[b].geometry.x();
        });
        std::sort(candidates.begin(), candidates.end(), [](KWin::EffectWindow* a, KWin::EffectWindow* b) {
            return a->frameGeometry().x() < b->frameGeometry().x();
        });
        const int n = qMin(sortedIndices.size(), candidates.size());
        for (int i = 0; i < n; ++i) {
            entries[sortedIndices[i]].window = candidates[i];
        }
    }

    // Build snapshot with QPointer for safe deferred access
    struct TileSnap {
        QPointer<KWin::EffectWindow> window;
        QRect geometry;
        QString windowId;
        QString screenName;
        bool isMonocle = false;
    };
    QVector<TileSnap> toApply;
    QSet<QString> tiledWindowIds;
    QString tileScreenName;
    for (Entry& e : entries) {
        if (!e.window) {
            continue;
        }
        tiledWindowIds.insert(e.windowId);
        QString screenName = m_effect->getWindowScreenName(e.window);
        if (tileScreenName.isEmpty()) {
            tileScreenName = screenName;
        }
        toApply.append({QPointer<KWin::EffectWindow>(e.window), e.geometry, e.windowId, screenName, e.isMonocle});
    }

    const uint64_t gen = m_autotileStaggerGeneration;

    auto onComplete = [this, toApply, tiledWindowIds, tileScreenName, gen]() {
        if (m_autotileStaggerGeneration != gen) {
            return;
        }
        if (m_autotileHideTitleBars && !m_borderlessWindows.isEmpty()) {
            const QSet<QString> toRestore = m_borderlessWindows - tiledWindowIds;
            for (const QString& wid : toRestore) {
                KWin::EffectWindow* win = m_effect->findWindowById(wid);
                if (win && m_effect->getWindowScreenName(win) == tileScreenName && !win->isMinimized()) {
                    setWindowBorderless(win, wid, false);
                }
            }
        }
        auto* ws = KWin::Workspace::self();
        if (ws) {
            for (int i = toApply.size() - 1; i >= 0; --i) {
                const TileSnap& snap = toApply[i];
                if (!snap.window) {
                    continue;
                }
                KWin::Window* kw = snap.window->window();
                if (kw) {
                    ws->raiseWindow(kw);
                }
            }
            if (!m_pendingAutotileFocusWindowId.isEmpty()) {
                KWin::EffectWindow* focusWin = m_effect->findWindowById(m_pendingAutotileFocusWindowId);
                m_pendingAutotileFocusWindowId.clear();
                if (focusWin) {
                    KWin::Window* kw = focusWin->window();
                    if (kw) {
                        ws->raiseWindow(kw);
                    }
                }
            }
        }

        if (!m_autotileTargetZones.isEmpty()) {
            QTimer::singleShot(150, this, [this, gen]() {
                if (m_autotileStaggerGeneration != gen) {
                    return;
                }
                centerUndersizedAutotileWindows();
                if (!m_autotileTargetZones.isEmpty()) {
                    QTimer::singleShot(250, this, [this, gen]() {
                        if (m_autotileStaggerGeneration != gen) {
                            return;
                        }
                        centerUndersizedAutotileWindows();
                    });
                }
            });
        }
    };

    m_effect->applyStaggeredOrImmediate(toApply.size(), [this, toApply, gen](int i) {
        if (m_autotileStaggerGeneration != gen) {
            return;
        }
        const TileSnap& snap = toApply[i];
        if (!snap.window || snap.window->isDeleted()) {
            return;
        }
        saveAndRecordPreAutotileGeometry(snap.windowId, snap.screenName, snap.window->frameGeometry());
        qCInfo(lcEffect) << "Autotile tile request:" << snap.windowId << "QRect=" << snap.geometry;
        if (m_autotileHideTitleBars) {
            setWindowBorderless(snap.window, snap.windowId, true);
        }

        if (snap.isMonocle) {
            KWin::Window* kw = snap.window->window();
            if (kw) {
                const bool wasAlreadyMaximized = (kw->maximizeMode() == KWin::MaximizeFull);
                ++m_suppressMaximizeChanged;
                kw->maximize(KWin::MaximizeFull);
                if (!wasAlreadyMaximized) {
                    m_monocleMaximizedWindows.insert(snap.windowId);
                }
                m_effect->applySnapGeometry(snap.window, snap.geometry);
                --m_suppressMaximizeChanged;
            }
        } else {
            unmaximizeMonocleWindow(snap.windowId);
            m_effect->applySnapGeometry(snap.window, snap.geometry);
        }

        if (!snap.isMonocle && snap.window->isWaylandClient()) {
            m_autotileTargetZones[snap.windowId] = snap.geometry;
        }
    }, onComplete);
}

void AutotileHandler::centerUndersizedAutotileWindows()
{
    QHash<QString, QRect> targets;
    targets.swap(m_autotileTargetZones);

    constexpr qreal MinCenteringDelta = 3.0;
    constexpr qreal MaxCenteringDelta = 64.0;

    for (auto it = targets.constBegin(); it != targets.constEnd(); ++it) {
        const QString& windowId = it.key();
        const QRect& targetZone = it.value();

        KWin::EffectWindow* w = m_effect->findWindowById(windowId);
        if (!w || w->isDeleted() || w->isFullScreen()) {
            continue;
        }

        const QRectF actual = w->frameGeometry();
        const qreal dw = targetZone.width() - actual.width();
        const qreal dh = targetZone.height() - actual.height();

        if (qAbs(dw) <= MinCenteringDelta && qAbs(dh) <= MinCenteringDelta) {
            m_autotileTargetZones[windowId] = targetZone;
            continue;
        }

        if ((dw > MinCenteringDelta || dh > MinCenteringDelta)
            && qAbs(dw) < MaxCenteringDelta && qAbs(dh) < MaxCenteringDelta) {
            const qreal dx = qMax(0.0, dw) / 2.0;
            const qreal dy = qMax(0.0, dh) / 2.0;
            const QRectF centered(targetZone.x() + dx, targetZone.y() + dy,
                                  actual.width(), actual.height());

            KWin::Window* kw = w->window();
            if (kw) {
                qCInfo(lcEffect) << "Centering undersized autotile window" << windowId
                                 << "actual=" << actual.size() << "zone=" << targetZone.size()
                                 << "offset=(" << dx << "," << dy << ")";
                m_effect->m_windowAnimator->removeAnimation(w);
                kw->moveResize(centered);
            }
        }
    }
}

void AutotileHandler::slotFocusWindowRequested(const QString& windowId)
{
    KWin::EffectWindow* w = m_effect->findWindowById(windowId);
    if (!w) {
        qCDebug(lcEffect) << "Autotile: window not found for focus request:" << windowId;
        return;
    }

    m_pendingAutotileFocusWindowId = windowId;
    KWin::effects->activateWindow(w);
}

} // namespace PlasmaZones
