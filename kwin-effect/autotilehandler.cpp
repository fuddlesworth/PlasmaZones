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
#include <QLoggingCategory>
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

QString AutotileHandler::findSavedGeometryKey(const QHash<QString, QRectF>& savedGeometries, const QString& windowId)
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

bool AutotileHandler::hasSavedGeometryForWindow(const QHash<QString, QRectF>& savedGeometries, const QString& windowId)
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

void AutotileHandler::restoreAllBorderless()
{
    if (m_border.borderlessWindows.isEmpty()) {
        return;
    }
    const QSet<QString> toRestore = m_border.borderlessWindows;
    for (const QString& windowId : toRestore) {
        KWin::EffectWindow* w = m_effect->findWindowById(windowId);
        if (w) {
            setWindowBorderless(w, windowId, false);
        }
    }
    // Clear any orphan entries where findWindowById returned null
    m_border.borderlessWindows.clear();
    m_border.zoneGeometries.clear();
}

std::optional<QRect> AutotileHandler::borderZoneGeometry(const QString& windowId) const
{
    auto it = m_border.zoneGeometries.constFind(windowId);
    if (it != m_border.zoneGeometries.constEnd()) {
        return it.value();
    }
    return std::nullopt;
}

QVector<QRect> AutotileHandler::allBorderZoneGeometries() const
{
    QVector<QRect> result;
    result.reserve(m_border.zoneGeometries.size());
    for (auto it = m_border.zoneGeometries.constBegin(); it != m_border.zoneGeometries.constEnd(); ++it) {
        result.append(it.value());
    }
    return result;
}

bool AutotileHandler::shouldApplyBorderInset(const QString& windowId) const
{
    return m_border.hideTitleBars && m_border.width > 0 && m_border.borderlessWindows.contains(windowId);
}

void AutotileHandler::updateHideTitleBarsSetting(bool enabled)
{
    const bool wasEnabled = m_border.hideTitleBars;
    m_border.hideTitleBars = enabled;
    if (wasEnabled && !enabled) {
        m_border.zoneGeometries.clear();
        const QSet<QString> toRestore = m_border.borderlessWindows;
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
    // Use EXACT windowId match only — NOT stableId fallback.
    // Multiple instances of the same app (e.g., 3 Dolphin windows) share a stableId.
    // hasSavedGeometryForWindow's stableId fallback would return true after the first
    // instance is saved, preventing all other instances from saving their own geometry.
    // On restore, all instances would get the first instance's geometry — scrambling
    // window positions on every autotile ↔ snapping toggle.
    if (screenGeometries.contains(windowId)) {
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
        if (!m_border.borderlessWindows.contains(windowId)) {
            kw->setNoBorder(true);
            m_border.borderlessWindows.insert(windowId);
            qCDebug(lcEffect) << "Autotile: hid title bar for" << windowId;
        }
    } else {
        if (m_border.borderlessWindows.remove(windowId)) {
            m_border.zoneGeometries.remove(windowId);
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

    // Clean up borderless, monocle-maximize, deferred-centering, border zone, and pre-autotile tracking
    m_border.borderlessWindows.remove(windowId);
    m_border.zoneGeometries.remove(windowId);
    m_monocleMaximizedWindows.remove(windowId);
    m_autotileTargetZones.remove(windowId);
    if (m_preAutotileGeometries.contains(screenName)) {
        m_preAutotileGeometries[screenName].remove(windowId);
    }

    // Notify autotile daemon
    if (m_autotileScreens.contains(screenName)) {
        m_effect->fireAndForgetDBusCall(DBus::Interface::Autotile, QStringLiteral("windowClosed"), {windowId},
                                        QStringLiteral("windowClosed"));
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

bool AutotileHandler::handleAutotileFloatToggle(KWin::EffectWindow* activeWindow, const QString& windowId,
                                                const QString& screenName)
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
            if (m_effect->ensureWindowTrackingReady("pre-save floating geometry")
                && m_effect->m_windowTrackingInterface) {
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
                this, SLOT(slotWindowFloatingChanged(QString, bool, QString)));

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

} // namespace PlasmaZones
