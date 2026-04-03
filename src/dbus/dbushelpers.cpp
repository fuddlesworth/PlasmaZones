// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dbushelpers.h"
#include "../core/screenmanager.h"
#include "../core/utils.h"
#include "../core/virtualscreen.h"
#include "../core/geometryutils.h"
#include "../core/layout.h"
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>

namespace PlasmaZones {
namespace DbusHelpers {

QString resolveScreenId(const QString& screenId)
{
    if (screenId.isEmpty()) {
        auto* mgr = ScreenManager::instance();
        if (mgr) {
            const QString cursorScreen = mgr->effectiveScreenAt(QCursor::pos());
            if (!cursorScreen.isEmpty()) {
                return cursorScreen;
            }
        }
        QScreen* primary = QGuiApplication::primaryScreen();
        return primary ? Utils::screenIdentifier(primary) : QString();
    }
    return VirtualScreenId::isVirtual(screenId) ? screenId : Utils::screenIdForName(screenId);
}

QRectF resolveScreenGeometry(Layout* layout, const QString& screenId)
{
    auto* mgr = ScreenManager::instance();
    if (mgr) {
        const QRect geom = mgr->screenGeometry(screenId);
        if (geom.isValid()) {
            return GeometryUtils::effectiveScreenGeometry(layout, screenId);
        }
    }
    QScreen* screen = Utils::findScreenByIdOrName(VirtualScreenId::extractPhysicalId(screenId));
    if (!screen) {
        screen = ScreenManager::resolvePhysicalScreen(screenId);
    }
    if (screen) {
        return GeometryUtils::effectiveScreenGeometry(layout, screen);
    }
    return {};
}

QScreen* resolvePhysicalQScreen(const QString& screenId)
{
    auto* mgr = ScreenManager::instance();
    if (mgr) {
        QScreen* screen = mgr->physicalQScreenFor(screenId);
        if (screen) {
            return screen;
        }
    }
    return Utils::findScreenByIdOrName(screenId);
}

} // namespace DbusHelpers
} // namespace PlasmaZones
