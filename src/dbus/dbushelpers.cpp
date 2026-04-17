// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dbushelpers.h"
#include "../core/screenmanager.h"
#include "../core/utils.h"
#include "../core/virtualscreen.h"
#include "../core/geometryutils.h"
#include <PhosphorZones/Layout.h>
#include <QGuiApplication>
#include <QScreen>

namespace PlasmaZones {
namespace DbusHelpers {

QString resolveScreenId(const QString& screenId)
{
    if (screenId.isEmpty()) {
        // Fall back to the primary screen's effective screen ID.
        // QCursor::pos() returns stale data for Wayland background daemons,
        // so we avoid it entirely and use the primary screen instead.
        auto* mgr = ScreenManager::instance();
        QScreen* primary = QGuiApplication::primaryScreen();
        if (primary && mgr) {
            // If the primary screen has virtual subdivisions, returns the first
            // virtual screen ID. Otherwise returns the physical screen ID.
            const QStringList ids = mgr->effectiveIdsForPhysical(Utils::screenIdentifier(primary));
            return ids.isEmpty() ? Utils::screenIdentifier(primary) : ids.first();
        }
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
    return Utils::findScreenByIdOrName(VirtualScreenId::extractPhysicalId(screenId));
}

} // namespace DbusHelpers
} // namespace PlasmaZones
