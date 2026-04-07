// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "controladaptor.h"
#include "windowtrackingadaptor.h"
#include "layoutadaptor.h"
#include "../core/layoutmanager.h"
#include "../core/layout.h"
#include "../core/zone.h"
#include "../core/logging.h"
#include "../core/geometryutils.h"
#include "../core/screenmanager.h"
#include "../core/supportreport.h"
#include "../autotile/AutotileEngine.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace PlasmaZones {

ControlAdaptor::ControlAdaptor(WindowTrackingAdaptor* wta, LayoutAdaptor* layoutAdaptor, LayoutManager* layoutManager,
                               AutotileEngine* autotileEngine, ScreenManager* screenManager, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_wta(wta)
    , m_layoutAdaptor(layoutAdaptor)
    , m_layoutManager(layoutManager)
    , m_autotileEngine(autotileEngine)
    , m_screenManager(screenManager)
{
}

void ControlAdaptor::snapWindowToZone(const QString& windowId, int zoneNumber, const QString& screenId)
{
    if (windowId.isEmpty() || zoneNumber < 1 || zoneNumber > 9) {
        qCWarning(lcDbusWindow) << "snapWindowToZone: invalid args windowId=" << windowId
                                << "zoneNumber=" << zoneNumber;
        return;
    }

    // Resolve zone from screen's current layout
    Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
    if (!layout) {
        qCWarning(lcDbusWindow) << "snapWindowToZone: no layout for screen" << screenId;
        return;
    }

    Zone* zone = layout->zoneByNumber(zoneNumber);
    if (!zone) {
        qCWarning(lcDbusWindow) << "snapWindowToZone: zone" << zoneNumber << "not found in layout" << layout->name();
        return;
    }

    // Delegate to WTA's moveWindowToZone convenience method
    if (m_wta) {
        m_wta->moveWindowToZone(windowId, zone->id().toString());
    }
}

void ControlAdaptor::toggleAutotileForScreen(const QString& screenId)
{
    if (screenId.isEmpty()) {
        qCWarning(lcDbusWindow) << "toggleAutotileForScreen: empty screenId";
        return;
    }

    // Determine current mode and toggle
    bool isAutotile = m_autotileEngine && m_autotileEngine->isAutotileScreen(screenId);
    int newMode = isAutotile ? 0 : 1; // 0=Snapping, 1=Autotile

    // Use the LayoutAdaptor's assignment system to toggle mode
    // This is a simplified toggle — uses current desktop/activity context
    qCInfo(lcDbusWindow) << "toggleAutotileForScreen:" << screenId << "from" << (isAutotile ? "autotile" : "snapping")
                         << "to" << (newMode == 1 ? "autotile" : "snapping");

    if (m_layoutAdaptor) {
        // setAssignmentEntry(screenId, desktop=0 (current), activity="" (current), mode, layout, algorithm)
        m_layoutAdaptor->setAssignmentEntry(screenId, 0, QString(), newMode, QString(), QString());
        m_layoutAdaptor->applyAssignmentChanges();
    } else {
        qCWarning(lcDbusWindow) << "toggleAutotileForScreen: LayoutAdaptor not available";
    }
}

QString ControlAdaptor::getFullState()
{
    QJsonObject state;

    // Layouts
    if (m_layoutManager) {
        QJsonArray layoutArray;
        for (Layout* layout : m_layoutManager->layouts()) {
            QJsonObject lo;
            lo[QLatin1String("id")] = layout->id().toString();
            lo[QLatin1String("name")] = layout->name();
            lo[QLatin1String("zoneCount")] = layout->zoneCount();
            layoutArray.append(lo);
        }
        state[QLatin1String("layouts")] = layoutArray;

        Layout* active = m_layoutManager->activeLayout();
        if (active) {
            state[QLatin1String("activeLayoutId")] = active->id().toString();
            state[QLatin1String("activeLayoutName")] = active->name();
        }
    }

    // Window states (via WTA)
    if (m_wta) {
        state[QLatin1String("windows")] = QJsonDocument::fromJson(m_wta->getAllWindowStates().toUtf8()).array();
    }

    // Autotile state
    if (m_autotileEngine) {
        QJsonObject autotile;
        autotile[QLatin1String("enabled")] = m_autotileEngine->isEnabled();
        QJsonArray screens;
        for (const QString& s : m_autotileEngine->autotileScreens()) {
            screens.append(s);
        }
        autotile[QLatin1String("screens")] = screens;
        state[QLatin1String("autotile")] = autotile;
    }

    return QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact));
}

QString ControlAdaptor::generateSupportReport(int sinceMinutes)
{
    if (sinceMinutes <= 0)
        sinceMinutes = 30;

    qCInfo(lcDbus) << "generateSupportReport: sinceMinutes=" << sinceMinutes;
    return SupportReport::generate(m_screenManager, m_layoutManager, m_autotileEngine, sinceMinutes);
}

} // namespace PlasmaZones
