// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scrollhandler.h"
#include "plasmazoneseffect.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/WindowMarshalling.h>

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QLoggingCategory>
#include <QVariant>
#include <QtMath>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

ScrollHandler::ScrollHandler(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
{
}

namespace {
/// Discover a window's minimum size, ceil-rounded; 0×0 when unconstrained.
void discoverMinSize(KWin::EffectWindow* w, int& minWidth, int& minHeight)
{
    minWidth = 0;
    minHeight = 0;
    if (KWin::Window* kw = w->window()) {
        const QSizeF minSize = kw->minSize();
        if (minSize.isValid()) {
            minWidth = qCeil(minSize.width());
            minHeight = qCeil(minSize.height());
        }
    }
}
} // namespace

void ScrollHandler::notifyWindowAdded(KWin::EffectWindow* w)
{
    if (!m_effect->isEligibleForTilingNotify(w)) {
        return;
    }

    const QString screenId = m_effect->getWindowScreenId(w);
    if (!m_scrollScreens.contains(screenId)) {
        return; // not a scroll screen — autotile or snap owns this window
    }

    const QString windowId = m_effect->getWindowId(w);

    // Window was already closed before we could report the open — skip
    // (D-Bus ordering race; see m_pendingCloses).
    if (m_pendingCloses.remove(windowId)) {
        return;
    }
    if (m_notifiedWindows.contains(windowId)) {
        return;
    }
    m_notifiedWindows.insert(windowId);
    m_notifiedWindowScreens[windowId] = screenId;

    int minWidth = 0;
    int minHeight = 0;
    discoverMinSize(w, minWidth, minHeight);

    auto* watcher =
        new QDBusPendingCallWatcher(PhosphorProtocol::ClientHelpers::asyncCall(
                                        PhosphorProtocol::Service::Interface::Scroll, QStringLiteral("windowOpened"),
                                        {windowId, screenId, minWidth, minHeight}),
                                    this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, windowId](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (w->isError()) {
            qCWarning(lcEffect) << "scroll windowOpened D-Bus call failed for" << windowId << ":"
                                << w->error().message();
            m_notifiedWindows.remove(windowId);
            m_notifiedWindowScreens.remove(windowId);
        }
    });
    qCDebug(lcEffect) << "Notified scroll: windowOpened" << windowId << "on screen" << screenId
                      << "minSize:" << minWidth << "x" << minHeight;
}

void ScrollHandler::notifyWindowsAddedBatch(const QList<KWin::EffectWindow*>& windows, bool resetNotified)
{
    PhosphorProtocol::WindowOpenedList batchEntries;
    QStringList batchWindowIds; // for error rollback

    for (KWin::EffectWindow* w : windows) {
        if (!m_effect->isEligibleForTilingNotify(w)) {
            continue;
        }
        const QString screenId = m_effect->getWindowScreenId(w);
        if (!m_scrollScreens.contains(screenId)) {
            continue;
        }
        const QString windowId = m_effect->getWindowId(w);
        if (m_pendingCloses.remove(windowId)) {
            continue;
        }
        if (resetNotified) {
            m_notifiedWindows.remove(windowId);
        }
        if (m_notifiedWindows.contains(windowId)) {
            continue;
        }
        m_notifiedWindows.insert(windowId);
        m_notifiedWindowScreens[windowId] = screenId;

        int minWidth = 0;
        int minHeight = 0;
        discoverMinSize(w, minWidth, minHeight);

        PhosphorProtocol::WindowOpenedEntry entry;
        entry.windowId = windowId;
        entry.screenId = screenId;
        entry.minWidth = minWidth;
        entry.minHeight = minHeight;
        batchEntries.append(entry);
        batchWindowIds.append(windowId);
    }

    if (batchEntries.isEmpty()) {
        return;
    }

    auto* watcher =
        new QDBusPendingCallWatcher(PhosphorProtocol::ClientHelpers::asyncCall(
                                        PhosphorProtocol::Service::Interface::Scroll,
                                        QStringLiteral("windowsOpenedBatch"), {QVariant::fromValue(batchEntries)}),
                                    this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, batchWindowIds](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (w->isError()) {
            qCWarning(lcEffect) << "scroll windowsOpenedBatch D-Bus call failed:" << w->error().message();
            for (const QString& wid : batchWindowIds) {
                m_notifiedWindows.remove(wid);
                m_notifiedWindowScreens.remove(wid);
            }
        }
    });
    qCInfo(lcEffect) << "Notified scroll: windowsOpenedBatch with" << batchEntries.size() << "windows";
}

void ScrollHandler::onWindowClosed(const QString& windowId, const QString& screenId)
{
    // If we haven't reported this window open yet, record the close so a
    // late windowOpened (D-Bus ordering race) is suppressed.
    if (!m_notifiedWindows.contains(windowId) && m_scrollScreens.contains(screenId)) {
        m_pendingCloses.insert(windowId);
    }
    m_notifiedWindows.remove(windowId);
    m_notifiedWindowScreens.remove(windowId);

    if (m_scrollScreens.contains(screenId)) {
        PhosphorProtocol::ClientHelpers::fireAndForget(m_effect, PhosphorProtocol::Service::Interface::Scroll,
                                                       QStringLiteral("windowClosed"), {windowId},
                                                       QStringLiteral("windowClosed"));
        qCDebug(lcEffect) << "Notified scroll: windowClosed" << windowId << "on screen" << screenId;
    }
}

void ScrollHandler::notifyWindowFocused(const QString& windowId, const QString& screenId)
{
    if (!m_scrollScreens.contains(screenId)) {
        return;
    }
    PhosphorProtocol::ClientHelpers::fireAndForget(m_effect, PhosphorProtocol::Service::Interface::Scroll,
                                                   QStringLiteral("notifyWindowFocused"), {windowId, screenId},
                                                   QStringLiteral("notifyWindowFocused"));
}

void ScrollHandler::onDaemonReady()
{
    loadSettings();
    connectSignals();
    m_notifiedWindows.clear();
    m_notifiedWindowScreens.clear();
    m_pendingCloses.clear();
}

void ScrollHandler::connectSignals()
{
    QDBusConnection bus = QDBusConnection::sessionBus();

    // Disconnect first so daemon restarts don't accumulate duplicate match
    // rules — Qt registers the same handler twice if connect() is called
    // twice with identical args.
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Scroll, QStringLiteral("scrollScreensChanged"), this,
                   SLOT(slotScrollScreensChanged(QStringList)));
    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Scroll, QStringLiteral("scrollScreensChanged"), this,
                SLOT(slotScrollScreensChanged(QStringList)));

    qCInfo(lcEffect) << "Connected to scroll D-Bus signals";
}

void ScrollHandler::loadSettings()
{
    // Query the initial scroll-mode screen set from the daemon. Mirrors
    // AutotileHandler::loadSettings — the foreign Properties interface is
    // correct for D-Bus property reads; bound by SyncCallTimeoutMs so a
    // wedged daemon doesn't leak a watcher for Qt's default 25 s.
    QDBusMessage msg =
        QDBusMessage::createMethodCall(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                       QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << PhosphorProtocol::Service::Interface::Scroll << QStringLiteral("scrollScreens");

    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg, PhosphorProtocol::Service::SyncCallTimeoutMs);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QDBusVariant> reply = *w;
        if (reply.isValid()) {
            const QStringList screens = reply.value().variant().toStringList();
            m_scrollScreens = QSet<QString>(screens.cbegin(), screens.cend());
            qCInfo(lcEffect) << "Loaded scroll screens:" << m_scrollScreens;

            if (!m_scrollScreens.isEmpty()) {
                // Batch-notify all existing windows on scroll screens in one
                // D-Bus call instead of per-window windowOpened round-trips.
                notifyWindowsAddedBatch(KWin::effects->stackingOrder(), /*resetNotified=*/true);
            }
        } else {
            qCDebug(lcEffect) << "Scroll screens: query failed, daemon may not be running";
        }
    });
}

void ScrollHandler::slotScrollScreensChanged(const QStringList& screenIds)
{
    m_scrollScreens = QSet<QString>(screenIds.cbegin(), screenIds.cend());
    qCDebug(lcEffect) << "Scroll screens updated:" << m_scrollScreens;
}

} // namespace PlasmaZones
