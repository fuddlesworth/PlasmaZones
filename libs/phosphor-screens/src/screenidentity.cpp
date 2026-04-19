// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorScreens/ScreenIdentity.h"

#include "screenslogging.h"

#include <PhosphorIdentity/ScreenId.h>
#include <PhosphorIdentity/VirtualScreenId.h>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QHash>
#include <QPointer>
#include <QScreen>
#include <QThread>

namespace Phosphor::Screens::ScreenIdentity {

namespace {

/// Identifier cache entry. Held by pointer value (stable inside Qt's
/// QScreen lifetime) + name fingerprint (so stale entries can be swept
/// safely without dereferencing a possibly-dangling QScreen*).
struct IdentifierCacheEntry
{
    QString connectorName; ///< screen->name() at insert time
    QString identifier; ///< full, disambiguation-aware screen ID
};

// QScreen* → cached identifier (full, with disambiguation).
QHash<const QScreen*, IdentifierCacheEntry>& identifierCache()
{
    static QHash<const QScreen*, IdentifierCacheEntry> s_cache;
    return s_cache;
}

// Reverse cache: ID → QScreen* for findByIdOrName slow path.
QHash<QString, QPointer<QScreen>>& reverseCache()
{
    static QHash<QString, QPointer<QScreen>> s_cache;
    return s_cache;
}

/// GUI-thread guard for the unsynchronised static caches. The header
/// documents the threading contract but nothing enforced it at runtime,
/// so a worker-thread misuse would silently corrupt the caches. In
/// debug builds this fires immediately at the misuse site; release
/// builds compile it out (matching the cache's lock-free posture).
#define PS_SCREEN_IDENTITY_ASSERT_GUI_THREAD()                                                                         \
    Q_ASSERT_X(QCoreApplication::instance() != nullptr                                                                 \
                   && QThread::currentThread() == QCoreApplication::instance()->thread(),                              \
               Q_FUNC_INFO, "ScreenIdentity helpers must be called on the GUI thread")

} // namespace

void invalidateEdidCache(const QString& connectorName)
{
    PS_SCREEN_IDENTITY_ASSERT_GUI_THREAD();
    // Cascade through the cross-process EDID cache in PhosphorIdentity
    // first so subsequent identifier rebuilds see the fresh hardware
    // state.
    PhosphorIdentity::ScreenId::invalidateEdidCache(connectorName);

    if (connectorName.isEmpty()) {
        identifierCache().clear();
        reverseCache().clear();
        return;
    }

    // Sweep ALL entries whose cached connector matches — raw pointer keys
    // may be stale so we match by the stored connector name, not by
    // dereferencing the QScreen*. The connector name is captured at insert
    // time so a later `it.key()->name()` would UB-deref a dangling pointer.
    auto& idCache = identifierCache();
    for (auto it = idCache.begin(); it != idCache.end();) {
        if (it.value().connectorName == connectorName) {
            it = idCache.erase(it);
        } else {
            ++it;
        }
    }

    auto& byIdCache = reverseCache();
    for (auto it = byIdCache.begin(); it != byIdCache.end();) {
        // value() is a QPointer — guards against dangling, safely returns null.
        if (it.value().isNull() || it.value()->name() == connectorName) {
            it = byIdCache.erase(it);
        } else {
            ++it;
        }
    }
}

QString baseIdentifierFor(const QScreen* screen)
{
    PS_SCREEN_IDENTITY_ASSERT_GUI_THREAD();
    if (!screen) {
        return QString();
    }
    // Recompute from EDID fields. Cheap because the EDID-serial sysfs
    // read is cached one level down in PhosphorIdentity::ScreenId.
    return PhosphorIdentity::ScreenId::buildScreenBaseId(screen->manufacturer(), screen->model(),
                                                         screen->serialNumber(), screen->name());
}

QString identifierFor(const QScreen* screen)
{
    PS_SCREEN_IDENTITY_ASSERT_GUI_THREAD();
    if (!screen) {
        return QString();
    }

    auto& cache = identifierCache();
    auto cacheIt = cache.constFind(screen);
    if (cacheIt != cache.constEnd()) {
        return cacheIt.value().identifier;
    }

    const QString baseId = baseIdentifierFor(screen);
    QString result = baseId;

    // Disambiguate identical monitors via "/CONNECTOR" suffix.
    for (const QScreen* other : QGuiApplication::screens()) {
        if (other != screen && baseIdentifierFor(other) == baseId) {
            result = baseId + QLatin1Char('/') + screen->name();
            break;
        }
    }

    cache.insert(screen, IdentifierCacheEntry{screen->name(), result});
    return result;
}

QString idForName(const QString& connectorName)
{
    PS_SCREEN_IDENTITY_ASSERT_GUI_THREAD();
    if (connectorName.isEmpty()) {
        return connectorName;
    }
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() == connectorName) {
            return identifierFor(screen);
        }
    }
    return connectorName;
}

QString nameForId(const QString& screenId)
{
    PS_SCREEN_IDENTITY_ASSERT_GUI_THREAD();
    if (screenId.isEmpty()) {
        return QString();
    }
    QScreen* screen = findByIdOrName(screenId);
    return screen ? screen->name() : QString();
}

QScreen* findByIdOrName(const QString& identifier)
{
    PS_SCREEN_IDENTITY_ASSERT_GUI_THREAD();
    if (identifier.isEmpty()) {
        return QGuiApplication::primaryScreen();
    }

    // Virtual screen IDs ("EDID/vs:N") resolve to the physical parent.
    const QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(identifier);

    // Fast path: connector name match.
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() == physId) {
            return screen;
        }
    }

    auto& cache = reverseCache();
    auto cacheIt = cache.constFind(physId);
    if (cacheIt != cache.constEnd()) {
        QScreen* cached = cacheIt.value().data();
        if (cached && QGuiApplication::screens().contains(cached)) {
            return cached;
        }
        cache.remove(physId);
    }

    // Exact screen ID match (only if it looks like an EDID-style ID).
    if (physId.contains(QLatin1Char(':'))) {
        for (QScreen* screen : QGuiApplication::screens()) {
            if (identifierFor(screen) == physId) {
                cache.insert(physId, screen);
                return screen;
            }
        }
    }

    // Disambiguated form: "Manuf:Model:Serial/CONNECTOR".
    // Skip for virtual screen IDs (the slash there is the VS separator).
    int slashPos = physId.lastIndexOf(QLatin1Char('/'));
    if (slashPos > 0 && physId == identifier) {
        const QString connectorPart = identifier.mid(slashPos + 1);
        const QString basePart = identifier.left(slashPos);
        for (QScreen* screen : QGuiApplication::screens()) {
            if (screen->name() == connectorPart && baseIdentifierFor(screen) == basePart) {
                cache.insert(identifier, screen);
                return screen;
            }
        }
        for (QScreen* screen : QGuiApplication::screens()) {
            if (identifierFor(screen) == basePart) {
                cache.insert(identifier, screen);
                return screen;
            }
        }
    }

    // Reverse fallback: stored config has bare base ID without suffix,
    // but currently-connected monitors are duplicates (so identifierFor
    // adds suffix). Match by base part of current screens.
    if (identifier.contains(QLatin1Char(':')) && !identifier.contains(QLatin1Char('/'))) {
        for (QScreen* screen : QGuiApplication::screens()) {
            if (baseIdentifierFor(screen) == identifier) {
                return screen;
            }
        }
    }

    return nullptr;
}

bool screensMatch(const QString& a, const QString& b)
{
    PS_SCREEN_IDENTITY_ASSERT_GUI_THREAD();
    if (a == b) {
        return true;
    }
    if (a.isEmpty() || b.isEmpty()) {
        return false;
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(a) || PhosphorIdentity::VirtualScreenId::isVirtual(b)) {
        return false;
    }
    QScreen* sa = findByIdOrName(a);
    QScreen* sb = findByIdOrName(b);
    return sa && sb && sa == sb;
}

} // namespace Phosphor::Screens::ScreenIdentity
