// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "utils.h"
#include "logging.h"
#include "screenmanager.h"
#include "virtualscreen.h"

#include <screen_id.h>

#include <QGuiApplication>
#include <QScreen>
#include <QHash>
#include <QPointer>

namespace PlasmaZones {
namespace Utils {

// Sysfs EDID lookups are delegated to ScreenIdUtils (compositor-common).
// The QScreen*-keyed caches below are daemon-side only — they wrap the shared
// ScreenIdUtils::buildScreenBaseId call and keep findScreenByIdOrName fast.

// Cache: QScreen* → EDID-based identifier (never changes while screen is connected)
static QHash<const QScreen*, QString>& screenIdentifierCache()
{
    static QHash<const QScreen*, QString> s_cache;
    return s_cache;
}

// Reverse cache: EDID identifier → QScreen* (for findScreenByIdOrName slow path)
static QHash<QString, QPointer<QScreen>>& screenByIdCache()
{
    static QHash<QString, QPointer<QScreen>> s_cache;
    return s_cache;
}

QString readEdidHeaderSerial(const QString& connectorName)
{
    return ScreenIdUtils::readEdidHeaderSerial(connectorName);
}

void invalidateEdidCache(const QString& connectorName)
{
    ScreenIdUtils::invalidateEdidCache(connectorName);
    if (connectorName.isEmpty()) {
        screenIdentifierCache().clear();
        screenByIdCache().clear();
    } else {
        // Remove the cached identifier for this connector's QScreen
        auto& idCache = screenIdentifierCache();
        for (auto it = idCache.begin(); it != idCache.end(); ++it) {
            if (it.key() && it.key()->name() == connectorName) {
                idCache.erase(it);
                break;
            }
        }
        // Remove reverse cache entries pointing to this connector's screen
        auto& byIdCache = screenByIdCache();
        for (auto it = byIdCache.begin(); it != byIdCache.end();) {
            if (it.value().isNull() || it.value()->name() == connectorName) {
                it = byIdCache.erase(it);
            } else {
                ++it;
            }
        }
    }
}

// EDID-based identifier without duplicate disambiguation.
// Delegates hex normalization and sysfs fallback to ScreenIdUtils.
static QString screenBaseIdentifier(const QScreen* screen)
{
    if (!screen) {
        return QString();
    }

    auto& cache = screenIdentifierCache();
    auto cacheIt = cache.constFind(screen);
    if (cacheIt != cache.constEnd()) {
        return cacheIt.value();
    }

    QString result = ScreenIdUtils::buildScreenBaseId(screen->manufacturer(), screen->model(), screen->serialNumber(),
                                                      screen->name());
    cache.insert(screen, result);
    return result;
}

QString screenIdentifier(const QScreen* screen)
{
    if (!screen) {
        return QString();
    }

    const QString baseId = screenBaseIdentifier(screen);

    // Check if another connected screen produces the same base ID (identical monitors).
    // When duplicates exist, append "/ConnectorName" to disambiguate.
    // This mirrors KWin's OutputConfigurationStore strategy: EDID primary, connector fallback.
    for (const QScreen* other : QGuiApplication::screens()) {
        if (other != screen && screenBaseIdentifier(other) == baseId) {
            return baseId + QLatin1Char('/') + screen->name();
        }
    }

    return baseId;
}

QString screenIdForName(const QString& connectorName)
{
    if (connectorName.isEmpty()) {
        return connectorName;
    }
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() == connectorName) {
            return screenIdentifier(screen);
        }
    }
    return connectorName;
}

QString screenNameForId(const QString& screenId)
{
    if (screenId.isEmpty()) {
        return QString();
    }

    QScreen* screen = findScreenByIdOrName(screenId);
    return screen ? screen->name() : QString();
}

bool isConnectorName(const QString& identifier)
{
    return !identifier.isEmpty() && !identifier.contains(QLatin1Char(':'));
}

QScreen* findScreenByIdOrName(const QString& identifier)
{
    if (identifier.isEmpty()) {
        return QGuiApplication::primaryScreen();
    }

    // Virtual screen IDs (e.g. "LG:Model:Serial/vs:0") resolve to their
    // backing physical QScreen* — strip the "/vs:N" suffix and look up
    // the physical parent.
    const QString physId = VirtualScreenId::extractPhysicalId(identifier);

    // Fast path: try connector name match first
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() == physId) {
            return screen;
        }
    }

    // Check reverse cache
    auto& cache = screenByIdCache();
    auto cacheIt = cache.constFind(physId);
    if (cacheIt != cache.constEnd()) {
        QScreen* cached = cacheIt.value().data();
        if (cached && QGuiApplication::screens().contains(cached)) {
            return cached;
        }
        cache.remove(physId);
    }

    // Try exact screen ID match (only if it looks like a screen ID)
    if (physId.contains(QLatin1Char(':'))) {
        for (QScreen* screen : QGuiApplication::screens()) {
            if (screenIdentifier(screen) == physId) {
                cache.insert(physId, screen); // cache for next time
                return screen;
            }
        }
    }
    // Fallback for connector-disambiguated IDs ("Manufacturer:Model:Serial/DP-3"):
    // if the identifier contains '/', try matching the connector suffix directly,
    // then fall back to the base ID (for when a previously-duplicate monitor is
    // now the only one connected, so its ID no longer has the suffix).
    // Skip this for virtual screen IDs (physId/vs:N) — the slash in those is
    // the VirtualScreenId separator, not a connector disambiguator.
    int slashPos = physId.lastIndexOf(QLatin1Char('/'));
    if (slashPos > 0 && physId == identifier) {
        const QString connectorPart = identifier.mid(slashPos + 1);
        const QString basePart = identifier.left(slashPos);
        // Try connector name from the suffix, but verify the EDID base matches
        // (prevents returning a different monitor that was plugged into the same port)
        for (QScreen* screen : QGuiApplication::screens()) {
            if (screen->name() == connectorPart && screenBaseIdentifier(screen) == basePart) {
                cache.insert(identifier, screen);
                return screen;
            }
        }
        // Try base ID match (monitor is now unique, no suffix needed)
        for (QScreen* screen : QGuiApplication::screens()) {
            if (screenIdentifier(screen) == basePart) {
                cache.insert(identifier, screen);
                return screen;
            }
        }
    }
    // Reverse fallback: saved config has base ID without suffix, but currently
    // connected monitors are duplicates (so screenIdentifier adds suffix).
    // Match by base part of the current screen IDs.
    if (identifier.contains(QLatin1Char(':')) && !identifier.contains(QLatin1Char('/'))) {
        for (QScreen* screen : QGuiApplication::screens()) {
            if (screenBaseIdentifier(screen) == identifier) {
                return screen; // returns first match — acceptable for legacy config
            }
        }
    }
    return nullptr;
}

bool screensMatch(const QString& a, const QString& b)
{
    if (a == b) {
        return true; // fast path: identical strings
    }
    if (a.isEmpty() || b.isEmpty()) {
        return false;
    }

    // Virtual screen IDs: exact string match was already handled above (a == b).
    // Different virtual IDs are never equivalent — even if they share the same
    // physical parent (e.g. "A/vs:0" vs "A/vs:1" are distinct screens).
    // A physical ID vs a virtual ID is also not a match (the physical screen
    // was subdivided; the physical ID no longer represents a usable screen).
    // Note: callers should pass normalized IDs. If a connector name is passed
    // for one argument and a virtual screen ID for the other, this will return
    // false — which is the correct behavior since they represent different things.
    if (VirtualScreenId::isVirtual(a) || VirtualScreenId::isVirtual(b)) {
        return false;
    }

    // Resolve both to QScreen* — handles connector names and screen IDs transparently
    QScreen* sa = findScreenByIdOrName(a);
    QScreen* sb = findScreenByIdOrName(b);
    return sa && sb && sa == sb;
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
        QString id = screenBaseIdentifier(screen);
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
    auto* mgr = ScreenManager::instance();
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
        auto* mgr = ScreenManager::instance();
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
