// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "utils.h"
#include "logging.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>

#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorIdentity/ScreenId.h>

#include <QGuiApplication>
#include <QHash>
#include <QScreen>
#include <QStringList>

namespace PlasmaZones {
namespace Utils {

bool belongsToPhysicalScreen(const QString& storedScreenId, const QString& physicalScreenId)
{
    if (storedScreenId.isEmpty() || physicalScreenId.isEmpty()) {
        return false;
    }
    // A virtual ID passed as the physical filter is a misuse — the function
    // is "stored belongs to PHYSICAL screen X". Return false rather than
    // silently mis-matching.
    if (PhosphorIdentity::VirtualScreenId::isVirtual(physicalScreenId)) {
        return false;
    }

    // Stored ID is virtual: extract its physical parent and compare via
    // screensMatch so connector-name ↔ EDID-ID equivalence is honored. A
    // raw `==` would miss legitimate matches when one side is a connector
    // name (e.g. "DP-2") and the other an EDID-based ID (e.g.
    // "Dell:U2722D:115107"); both forms can appear in stored window state
    // depending on which code path produced the ID.
    if (PhosphorIdentity::VirtualScreenId::isVirtual(storedScreenId)) {
        return Phosphor::Screens::ScreenIdentity::screensMatch(
            PhosphorIdentity::VirtualScreenId::extractPhysicalId(storedScreenId), physicalScreenId);
    }

    // Stored ID is physical (or a connector name): defer to screensMatch
    // which handles connector-name ↔ screen-ID equivalence via QScreen lookup.
    return Phosphor::Screens::ScreenIdentity::screensMatch(storedScreenId, physicalScreenId);
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

QString effectiveScreenIdAt(Phosphor::Screens::ScreenManager* mgr, const QPoint& pos, QScreen* fallbackScreen)
{
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
    return screen ? Phosphor::Screens::ScreenIdentity::identifierFor(screen) : QString();
}

qreal screenAspectRatio(Phosphor::Screens::ScreenManager* mgr, const QString& screenNameOrId)
{
    // For virtual screen IDs, use the injected ScreenManager for VS geometry
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenNameOrId) && mgr) {
        QRect geom = mgr->screenGeometry(screenNameOrId);
        if (geom.isValid() && geom.height() > 0) {
            return static_cast<qreal>(geom.width()) / geom.height();
        }
    }

    // Fallback: physical screen lookup
    return screenAspectRatio(Phosphor::Screens::ScreenIdentity::findByIdOrName(screenNameOrId));
}

} // namespace Utils
} // namespace PlasmaZones
