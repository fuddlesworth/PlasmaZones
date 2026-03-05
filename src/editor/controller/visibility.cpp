// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../EditorController.h"
#include "../undo/UndoController.h"
#include "../undo/commands/UpdateVisibilityCommand.h"
#include "../../core/constants.h"
#include "../../core/logging.h"
#include "../../core/utils.h"

#include <QGuiApplication>
#include <QScreen>

namespace PlasmaZones {

QVariantList EditorController::allowedDesktops() const
{
    QVariantList result;
    for (int d : m_allowedDesktopsInt) {
        result.append(d);
    }
    return result;
}

void EditorController::setAllowedScreens(const QStringList& screens)
{
    if (m_allowedScreens != screens) {
        auto* cmd = new UpdateVisibilityCommand(this, m_allowedScreens, screens, m_allowedDesktopsInt,
                                                m_allowedDesktopsInt, m_allowedActivities, m_allowedActivities);
        m_undoController->push(cmd);
    }
}

void EditorController::setAllowedDesktops(const QVariantList& desktops)
{
    QList<int> intList;
    for (const QVariant& v : desktops) {
        intList.append(v.toInt());
    }
    if (m_allowedDesktopsInt != intList) {
        auto* cmd = new UpdateVisibilityCommand(this, m_allowedScreens, m_allowedScreens, m_allowedDesktopsInt, intList,
                                                m_allowedActivities, m_allowedActivities);
        m_undoController->push(cmd);
    }
}

void EditorController::setAllowedActivities(const QStringList& activities)
{
    if (m_allowedActivities != activities) {
        auto* cmd = new UpdateVisibilityCommand(this, m_allowedScreens, m_allowedScreens, m_allowedDesktopsInt,
                                                m_allowedDesktopsInt, m_allowedActivities, activities);
        m_undoController->push(cmd);
    }
}

void EditorController::setAllowedScreensDirect(const QStringList& screens)
{
    if (m_allowedScreens != screens) {
        m_allowedScreens = screens;
        markUnsaved();
        Q_EMIT allowedScreensChanged();
    }
}

void EditorController::setAllowedDesktopsDirect(const QList<int>& desktops)
{
    if (m_allowedDesktopsInt != desktops) {
        m_allowedDesktopsInt = desktops;
        markUnsaved();
        Q_EMIT allowedDesktopsChanged();
    }
}

void EditorController::setAllowedActivitiesDirect(const QStringList& activities)
{
    if (m_allowedActivities != activities) {
        m_allowedActivities = activities;
        markUnsaved();
        Q_EMIT allowedActivitiesChanged();
    }
}

void EditorController::toggleScreenAllowed(const QString& screenName)
{
    QStringList screens = m_allowedScreens;

    if (screens.isEmpty()) {
        // Currently "all screens" - populate with all except this one
        for (const QString& s : m_availableScreenIds) {
            if (s != screenName) {
                screens.append(s);
            }
        }
        // If result is empty (single screen), nothing to restrict
        if (screens.isEmpty()) {
            return;
        }
    } else if (screens.contains(screenName)) {
        screens.removeAll(screenName);
        // If removing last screen, clear to mean "all screens"
        if (screens.isEmpty()) {
            screens.clear(); // explicit: empty = visible everywhere
        }
    } else {
        screens.append(screenName);
        // If all screens are now in the list, clear it (= visible everywhere)
        if (screens.size() >= m_availableScreenIds.size()) {
            screens.clear();
        }
    }

    setAllowedScreens(screens);
}

QString EditorController::screenDisplayName(const QString& screenIdOrName) const
{
    for (QScreen* screen : QGuiApplication::screens()) {
        if (Utils::screenIdentifier(screen) == screenIdOrName || screen->name() == screenIdOrName) {
            QString connectorName = screen->name();
            QString mfr = screen->manufacturer();
            QString mdl = screen->model();
            QString displayInfo;
            if (!mfr.isEmpty() && !mdl.isEmpty()) {
                displayInfo = mfr + QLatin1Char(' ') + mdl;
            } else if (!mfr.isEmpty()) {
                displayInfo = mfr;
            } else if (!mdl.isEmpty()) {
                displayInfo = mdl;
            }
            if (!displayInfo.isEmpty()) {
                return connectorName + QStringLiteral(" — ") + displayInfo;
            }
            return connectorName;
        }
    }
    // Screen not currently connected. If the identifier is a screen ID (contains colons),
    // try to make it more human-readable: "manufacturer:model:serial" → "manufacturer model"
    if (screenIdOrName.contains(QLatin1Char(':'))) {
        QStringList parts = screenIdOrName.split(QLatin1Char(':'));
        QStringList displayParts;
        // parts[0] = manufacturer, parts[1] = model, parts[2..] = serial etc.
        for (int i = 0; i < qMin(2, parts.size()); ++i) {
            if (!parts[i].isEmpty()) {
                displayParts.append(parts[i]);
            }
        }
        if (!displayParts.isEmpty()) {
            return displayParts.join(QLatin1Char(' '));
        }
    }
    return screenIdOrName;
}

void EditorController::toggleDesktopAllowed(int desktop)
{
    QList<int> desktops = m_allowedDesktopsInt;

    if (desktops.isEmpty()) {
        // Currently "all desktops" - populate with all except this one
        for (int i = 1; i <= m_virtualDesktopCount; ++i) {
            if (i != desktop) {
                desktops.append(i);
            }
        }
        // If result is empty (single desktop), nothing to restrict
        if (desktops.isEmpty()) {
            return;
        }
    } else if (desktops.contains(desktop)) {
        desktops.removeAll(desktop);
    } else {
        desktops.append(desktop);
        // If all desktops are now in the list, clear it (= visible everywhere)
        if (desktops.size() >= m_virtualDesktopCount) {
            desktops.clear();
        }
    }

    QVariantList varList;
    for (int d : desktops) {
        varList.append(d);
    }
    setAllowedDesktops(varList);
}

void EditorController::toggleActivityAllowed(const QString& activityId)
{
    QStringList activities = m_allowedActivities;

    if (activities.isEmpty()) {
        // Currently "all activities" - populate with all except this one
        for (const QVariant& v : m_availableActivities) {
            QVariantMap actMap = v.toMap();
            QString id = actMap.value(QLatin1String("id")).toString();
            if (id != activityId) {
                activities.append(id);
            }
        }
        // If result is empty (single activity), nothing to restrict
        if (activities.isEmpty()) {
            return;
        }
    } else if (activities.contains(activityId)) {
        activities.removeAll(activityId);
    } else {
        activities.append(activityId);
        // If all activities are now in the list, clear it (= visible everywhere)
        if (activities.size() >= m_availableActivities.size()) {
            activities.clear();
        }
    }

    setAllowedActivities(activities);
}

} // namespace PlasmaZones
