// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenmanagerservice.h"

#include "utils.h"

#include <PhosphorScreens/ScreenIdentity.h>

#include <QGuiApplication>
#include <QPoint>
#include <QPointer>
#include <QScreen>

namespace PlasmaZones {

namespace {
// Process-global pointer to the active ScreenManager. Main-thread-only
// access — Qt GUI thread constraint, no synchronisation needed.
QPointer<Phosphor::Screens::ScreenManager>& globalManager()
{
    static QPointer<Phosphor::Screens::ScreenManager> s_manager;
    return s_manager;
}
} // namespace

void setScreenManager(Phosphor::Screens::ScreenManager* manager)
{
    globalManager() = manager;
}

Phosphor::Screens::ScreenManager* screenManager()
{
    return globalManager().data();
}

QRect actualAvailableGeometry(QScreen* screen)
{
    if (auto* mgr = screenManager()) {
        return mgr->actualAvailableGeometry(screen);
    }
    if (!screen) {
        return QRect();
    }
    const QRect avail = screen->availableGeometry();
    return avail.isValid() ? avail : screen->geometry();
}

bool isPanelGeometryReady()
{
    auto* mgr = screenManager();
    return mgr && mgr->isPanelGeometryReady();
}

QScreen* resolvePhysicalScreen(const QString& screenId)
{
    auto* mgr = screenManager();
    QScreen* screen = mgr ? mgr->physicalQScreenFor(screenId) : nullptr;
    if (!screen) {
        screen =
            screenId.isEmpty() ? Utils::primaryScreen() : Phosphor::Screens::ScreenIdentity::findByIdOrName(screenId);
    }
    if (!screen) {
        screen = Utils::primaryScreen();
    }
    return screen;
}

QStringList effectiveScreenIdsWithFallback()
{
    if (auto* mgr = screenManager()) {
        QStringList ids = mgr->effectiveScreenIds();
        if (!ids.isEmpty()) {
            return ids;
        }
    }
    QStringList result;
    for (const auto* screen : Utils::allScreens()) {
        result.append(Phosphor::Screens::ScreenIdentity::identifierFor(screen));
    }
    return result;
}

} // namespace PlasmaZones
