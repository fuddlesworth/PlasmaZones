// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "utils.h"
#include "logging.h"
#include "screenmanager.h"
#include "virtualscreen.h"

#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorIdentity/ScreenId.h>

#include <QGuiApplication>
#include <QHash>
#include <QScreen>
#include <QStringList>

namespace PlasmaZones {
namespace Utils {

// All screen-identity helpers below have moved to PhosphorScreens::ScreenIdentity
// (and PhosphorIdentity::ScreenId for the cross-process EDID primitives).
// The Utils:: API surface stays unchanged so the daemon's ~50 call sites
// keep compiling — these are thin trampolines into the lib.

QString readEdidHeaderSerial(const QString& connectorName)
{
    return PhosphorIdentity::ScreenId::readEdidHeaderSerial(connectorName);
}

void invalidateEdidCache(const QString& connectorName)
{
    Phosphor::Screens::ScreenIdentity::invalidateEdidCache(connectorName);
}

QString screenIdentifier(const QScreen* screen)
{
    return Phosphor::Screens::ScreenIdentity::identifierFor(screen);
}

QString screenIdForName(const QString& connectorName)
{
    return Phosphor::Screens::ScreenIdentity::idForName(connectorName);
}

QString screenNameForId(const QString& screenId)
{
    return Phosphor::Screens::ScreenIdentity::nameForId(screenId);
}

bool isConnectorName(const QString& identifier)
{
    return Phosphor::Screens::ScreenIdentity::isConnectorName(identifier);
}

QScreen* findScreenByIdOrName(const QString& identifier)
{
    return Phosphor::Screens::ScreenIdentity::findByIdOrName(identifier);
}

bool screensMatch(const QString& a, const QString& b)
{
    return Phosphor::Screens::ScreenIdentity::screensMatch(a, b);
}

bool belongsToPhysicalScreen(const QString& storedScreenId, const QString& physicalScreenId)
{
    if (storedScreenId.isEmpty() || physicalScreenId.isEmpty()) {
        return false;
    }
    // A virtual ID passed as the physical filter is a misuse — the function
    // is "stored belongs to PHYSICAL screen X". Return false rather than
    // silently mis-matching.
    if (VirtualScreenId::isVirtual(physicalScreenId)) {
        return false;
    }

    // Stored ID is virtual: extract its physical parent and compare via
    // screensMatch so connector-name ↔ EDID-ID equivalence is honored. A
    // raw `==` would miss legitimate matches when one side is a connector
    // name (e.g. "DP-2") and the other an EDID-based ID (e.g.
    // "Dell:U2722D:115107"); both forms can appear in stored window state
    // depending on which code path produced the ID.
    if (VirtualScreenId::isVirtual(storedScreenId)) {
        return screensMatch(VirtualScreenId::extractPhysicalId(storedScreenId), physicalScreenId);
    }

    // Stored ID is physical (or a connector name): defer to screensMatch
    // which handles connector-name ↔ screen-ID equivalence via QScreen lookup.
    return screensMatch(storedScreenId, physicalScreenId);
}

void warnDuplicateScreenIds()
{
    QHash<QString, QStringList> idToConnectors;
    for (QScreen* screen : QGuiApplication::screens()) {
        QString id = Phosphor::Screens::ScreenIdentity::baseIdentifierFor(screen);
        idToConnectors[id].append(screen->name());
    }
    for (auto it = idToConnectors.constBegin(); it != idToConnectors.constEnd(); ++it) {
        if (it.value().size() > 1) {
            qCInfo(lcScreen) << "Identical monitors detected for EDID ID" << it.key()
                             << "(connectors:" << it.value().join(QStringLiteral(", ")) << ")."
                             << "Using connector-disambiguated IDs for independent layout assignments.";
        }
    }
}

QString effectiveScreenIdAt(const QPoint& pos, QScreen* fallbackScreen)
{
    auto* mgr = screenManager();
    if (mgr) {
        QString id = mgr->effectiveScreenAt(pos);
        if (!id.isEmpty()) {
            return id;
        }
    }
    QScreen* screen = fallbackScreen;
    if (!screen) {
        screen = findScreenAtPosition(pos);
    }
    return screen ? screenIdentifier(screen) : QString();
}

qreal screenAspectRatio(const QString& screenNameOrId)
{
    // For virtual screen IDs, use ScreenManager geometry
    if (VirtualScreenId::isVirtual(screenNameOrId)) {
        auto* mgr = screenManager();
        if (mgr) {
            QRect geom = mgr->screenGeometry(screenNameOrId);
            if (geom.isValid() && geom.height() > 0) {
                return static_cast<qreal>(geom.width()) / geom.height();
            }
        }
    }

    // Fallback: physical screen lookup
    return screenAspectRatio(findScreenByIdOrName(screenNameOrId));
}

} // namespace Utils
} // namespace PlasmaZones
