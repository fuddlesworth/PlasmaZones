// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dbushelpers.h"
#include "../core/screenmanagerservice.h"
#include "../core/utils.h"
#include <PhosphorScreens/VirtualScreen.h>
#include "../core/geometryutils.h"
#include <PhosphorZones/Layout.h>
#include <QGuiApplication>
#include <QScreen>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {
namespace DbusHelpers {

QString resolveScreenId(const QString& screenId)
{
    if (screenId.isEmpty()) {
        // Fall back to the primary screen's effective screen ID.
        // QCursor::pos() returns stale data for Wayland background daemons,
        // so we avoid it entirely and use the primary screen instead.
        auto* mgr = screenManager();
        QScreen* primary = QGuiApplication::primaryScreen();
        if (primary && mgr) {
            // If the primary screen has virtual subdivisions, returns the first
            // virtual screen ID. Otherwise returns the physical screen ID.
            const QStringList ids = mgr->virtualScreenIdsFor(Phosphor::Screens::ScreenIdentity::identifierFor(primary));
            return ids.isEmpty() ? Phosphor::Screens::ScreenIdentity::identifierFor(primary) : ids.first();
        }
        return primary ? Phosphor::Screens::ScreenIdentity::identifierFor(primary) : QString();
    }
    return PhosphorIdentity::VirtualScreenId::isVirtual(screenId)
        ? screenId
        : Phosphor::Screens::ScreenIdentity::idForName(screenId);
}

QRectF resolveScreenGeometry(PhosphorZones::Layout* layout, const QString& screenId)
{
    auto* mgr = screenManager();
    if (mgr) {
        const QRect geom = mgr->screenGeometry(screenId);
        if (geom.isValid()) {
            return GeometryUtils::effectiveScreenGeometry(layout, screenId);
        }
    }
    QScreen* screen = Phosphor::Screens::ScreenIdentity::findByIdOrName(
        PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId));
    if (!screen) {
        screen = resolvePhysicalScreen(screenId);
    }
    if (screen) {
        return GeometryUtils::effectiveScreenGeometry(layout, screen);
    }
    return {};
}

QScreen* resolvePhysicalQScreen(const QString& screenId)
{
    auto* mgr = screenManager();
    if (mgr) {
        QScreen* screen = mgr->physicalQScreenFor(screenId);
        if (screen) {
            return screen;
        }
    }
    return Phosphor::Screens::ScreenIdentity::findByIdOrName(
        PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId));
}

} // namespace DbusHelpers
} // namespace PlasmaZones
