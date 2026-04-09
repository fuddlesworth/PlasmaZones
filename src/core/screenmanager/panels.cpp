// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// KDE Plasma panel D-Bus queries and scheduling.
// Part of ScreenManager — split from screenmanager.cpp for SRP.

#include "../screenmanager.h"
#include "../logging.h"
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QScreen>
#include <QTimer>

namespace PlasmaZones {

static const QString s_plasmaShellService = QStringLiteral("org.kde.plasmashell");

void ScreenManager::scheduleDbusQuery()
{
    if (m_dbusQueryPending) {
        return;
    }

    m_dbusQueryPending = true;

    // Defer to next event loop pass to coalesce multiple createGeometrySensor
    // calls during start() into a single query.
    //
    // If org.kde.plasmashell is registered, the async D-Bus call in
    // queryKdePlasmaPanels succeeds and we get panel data.
    // If it's NOT registered, queryKdePlasmaPanels's !isValid() fallback
    // still runs calculateAvailableGeometry and emits panelGeometryReady —
    // essential for non-Plasma desktops and late-starting plasmashell.
    // The QDBusServiceWatcher (set up in start()) will re-query when
    // plasmashell eventually appears.
    QTimer::singleShot(0, this, [this]() {
        m_dbusQueryPending = false;
        queryKdePlasmaPanels();
    });
}

void ScreenManager::scheduleDelayedPanelRequery(int delayMs)
{
    if (delayMs <= 0) {
        return;
    }
    m_delayedPanelRequeryTimer.setInterval(delayMs);
    m_delayedPanelRequeryTimer.start();
}

void ScreenManager::queryKdePlasmaPanels(bool fromDelayedRequery)
{
    // Query KDE Plasma via D-Bus for panel information (ASYNC to avoid blocking)
    QDBusInterface* plasmaShell =
        new QDBusInterface(s_plasmaShellService, QStringLiteral("/PlasmaShell"), QStringLiteral("org.kde.PlasmaShell"),
                           QDBusConnection::sessionBus(), this);

    if (!plasmaShell->isValid()) {
        delete plasmaShell;
        // No Plasma shell - just recalculate with what we have
        for (auto* screen : m_trackedScreens) {
            calculateAvailableGeometry(screen);
        }
        // Still emit panelGeometryReady so components don't hang waiting
        if (!m_panelGeometryReceived) {
            m_panelGeometryReceived = true;
            qCInfo(lcScreen) << "Panel geometry: ready, no Plasma shell";
            Q_EMIT panelGeometryReady();
        }
        return;
    }

    // JavaScript to get panel information from Plasma Shell
    // We query the panel's actual geometry to calculate the real offset from the screen edge,
    // which includes both thickness and any floating gap the theme defines.
    // p.height is the panel thickness (perpendicular dimension) in Plasma's API
    // p.location is one of: "top", "bottom", "left", "right"
    // p.screen is the screen index (0-based) — NOTE: Plasma's screen ordering can differ
    //   from Qt's QGuiApplication::screens() ordering on multi-monitor setups.
    //   We include the screen geometry in the output so we can match by geometry.
    // p.floating is a boolean indicating if the panel is in floating mode (Plasma 6)
    // p.hiding indicates auto-hide mode, one of ("none", "autohide", "dodgewindows", "windowsgobelow")
    // screenGeometry(screenIndex) returns the screen's full geometry
    QString script = QStringLiteral(R"(
        panels().forEach(function(p,i){
            var thickness = Math.abs(p.height);
            var floating = p.floating ? 1 : 0;
            var hiding = p.hiding;
            var sg = screenGeometry(p.screen);
            var loc = p.location;
            var pg = p.geometry;
            // Calculate the actual offset from the screen edge based on panel geometry
            // This includes both the panel thickness AND any floating gap
            var offset = thickness;
            if (pg && sg) {
                if (loc === "top") {
                    offset = (pg.y + pg.height) - sg.y;
                } else if (loc === "bottom") {
                    offset = (sg.y + sg.height) - pg.y;
                } else if (loc === "left") {
                    offset = (pg.x + pg.width) - sg.x;
                } else if (loc === "right") {
                    offset = (sg.x + sg.width) - pg.x;
                }
            }
            // Include screen geometry so we can match by geometry instead of index
            // (Plasma and Qt can have different screen orderings)
            var sgStr = sg ? (sg.x + "," + sg.y + "," + sg.width + "," + sg.height) : "";
            print("PANEL:" + p.screen + ":" + loc + ":" + hiding + ":" + offset + ":" + floating + ":" + sgStr + "\n");
        });
    )");

    // Use ASYNC call to avoid blocking the main thread during startup
    QDBusPendingCall pendingCall = plasmaShell->asyncCall(QStringLiteral("evaluateScript"), script);
    QDBusPendingCallWatcher* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, plasmaShell, fromDelayedRequery](QDBusPendingCallWatcher* w) {
                QDBusPendingReply<QString> reply = *w;

                // Clear existing panel offsets before parsing new data
                m_panelOffsets.clear();

                if (reply.isValid()) {
                    QString output = reply.value();
                    qCDebug(lcScreen) << "queryKdePlasmaPanels D-Bus reply=" << output;

                    // Parse: PANEL:plasmaIndex:location:hiding:offset:floating:x,y,w,h
                    // Match panels to Qt screens by geometry (Plasma and Qt can have different screen orderings)
                    static QRegularExpression panelRegex(QStringLiteral(
                        "PANEL:(\\d+):(\\w+):(\\w+):(\\d+)(?::(\\d+))?(?::(\\d+),(\\d+),(\\d+),(\\d+))?"));
                    QRegularExpressionMatchIterator it = panelRegex.globalMatch(output);

                    const auto qtScreens = QGuiApplication::screens();

                    while (it.hasNext()) {
                        QRegularExpressionMatch match = it.next();
                        int plasmaIndex = match.captured(1).toInt();
                        QString location = match.captured(2);
                        QString hiding = match.captured(3);
                        int totalOffset = match.captured(4).toInt();
                        bool isFloating = !match.captured(5).isEmpty() && match.captured(5).toInt() != 0;

                        // Find the Qt screen matching this Plasma screen's geometry
                        QString connectorName;
                        if (!match.captured(6).isEmpty()) {
                            QRect plasmaGeom(match.captured(6).toInt(), match.captured(7).toInt(),
                                             match.captured(8).toInt(), match.captured(9).toInt());
                            for (auto* qs : qtScreens) {
                                if (qs->geometry() == plasmaGeom) {
                                    connectorName = qs->name();
                                    break;
                                }
                            }
                        }

                        if (connectorName.isEmpty()) {
                            qCWarning(lcScreen) << "Could not match Plasma screen" << plasmaIndex
                                                << "to any Qt screen by geometry - skipping panel";
                            continue;
                        }

                        qCDebug(lcScreen)
                            << "Parsed panel screen=" << connectorName << " (plasma idx" << plasmaIndex << ")"
                            << "location=" << location << "offset=" << totalOffset << "floating=" << isFloating
                            << "hiding=" << hiding;

                        bool panelAutoHides =
                            (hiding == QLatin1String("autohide") || hiding == QLatin1String("dodgewindows")
                             || hiding == QLatin1String("windowsgobelow"));

                        if (panelAutoHides) {
                            totalOffset = 0;
                        }

                        if (!m_panelOffsets.contains(connectorName)) {
                            m_panelOffsets.insert(connectorName, ScreenPanelOffsets{});
                        }

                        ScreenPanelOffsets& offsets = m_panelOffsets[connectorName];
                        if (location == QLatin1String("top")) {
                            offsets.top = qMax(offsets.top, totalOffset);
                        } else if (location == QLatin1String("bottom")) {
                            offsets.bottom = qMax(offsets.bottom, totalOffset);
                        } else if (location == QLatin1String("left")) {
                            offsets.left = qMax(offsets.left, totalOffset);
                        } else if (location == QLatin1String("right")) {
                            offsets.right = qMax(offsets.right, totalOffset);
                        }
                    }
                } else {
                    qCWarning(lcScreen) << "queryKdePlasmaPanels D-Bus query failed:" << reply.error().message();
                }

                // Log final panel offsets
                for (auto it = m_panelOffsets.constBegin(); it != m_panelOffsets.constEnd(); ++it) {
                    qCInfo(lcScreen) << "Screen" << it.key() << "panel offsets T=" << it.value().top
                                     << "B=" << it.value().bottom << "L=" << it.value().left
                                     << "R=" << it.value().right;
                }

                // Now recalculate geometry for all screens with updated panel data
                for (auto* screen : m_trackedScreens) {
                    calculateAvailableGeometry(screen);
                }

                // Emit panelGeometryReady on first successful query
                if (!m_panelGeometryReceived) {
                    m_panelGeometryReceived = true;
                    qCInfo(lcScreen) << "Panel geometry: ready";
                    Q_EMIT panelGeometryReady();
                }

                if (fromDelayedRequery) {
                    Q_EMIT delayedPanelRequeryCompleted();
                }

                // Cleanup
                plasmaShell->deleteLater();
                w->deleteLater();
            });
}

} // namespace PlasmaZones
