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

void reset()
{
    PS_SCREEN_IDENTITY_ASSERT_GUI_THREAD();
    // Full-cache wipe hook for tests and for hosts that tear down and
    // recreate QGuiApplication within one process. Distinct from
    // invalidateEdidCache(empty) only in intent: that one is the hotplug
    // path, this one is explicitly "start from nothing". Cascades through
    // PhosphorIdentity so the EDID-serial cache also resets.
    PhosphorIdentity::ScreenId::invalidateEdidCache(QString());
    identifierCache().clear();
    reverseCache().clear();
}

void invalidateComputedIdentifiers()
{
    PS_SCREEN_IDENTITY_ASSERT_GUI_THREAD();
    // Hotplug invalidation for the computed-identifier caches only.
    // Disambiguation (the "/CONNECTOR" suffix) is a function of the
    // full screen set — any add/remove can flip a screen's identifier
    // between bare and disambiguated forms even though that screen's
    // own EDID is unchanged. Leave the EDID serial cache alone; it's
    // keyed on connector and only invalidates on real monitor swap.
    identifierCache().clear();
    reverseCache().clear();
}

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
    // Operate on `physId` — which is always the physical identifier with
    // any "/vs:N" suffix already stripped — so this branch runs for BOTH
    // purely physical inputs AND virtual IDs whose physical part carries
    // a "/CONNECTOR" disambiguator. Without this, a persisted virtual ID
    // captured while duplicate monitors were connected (e.g.
    // "Dell:U2722D:115107/HDMI-1/vs:2") fails to resolve after one of
    // the duplicates is unplugged: identifierFor() no longer returns the
    // suffixed form, the exact-match loop above misses, and this block
    // was previously gated out for virtual IDs.
    //
    // Cache inserts key on `physId` (not the raw `identifier`) so a later
    // lookup with any /vs:N-suffixed form of the same physical id hits
    // the reverse-cache fast path — extractPhysicalId strips the suffix
    // before the lookup at the top of this function, so keying on the
    // raw identifier would leave dead entries that never got read.
    int slashPos = physId.lastIndexOf(QLatin1Char('/'));
    if (slashPos > 0) {
        const QString connectorPart = physId.mid(slashPos + 1);
        const QString basePart = physId.left(slashPos);
        for (QScreen* screen : QGuiApplication::screens()) {
            if (screen->name() == connectorPart && baseIdentifierFor(screen) == basePart) {
                cache.insert(physId, screen);
                return screen;
            }
        }
        for (QScreen* screen : QGuiApplication::screens()) {
            if (identifierFor(screen) == basePart) {
                cache.insert(physId, screen);
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
    // Fast paths (string equal, one empty, either virtual) return BEFORE
    // any QScreen lookup so the VS-vs-physical case doesn't pay two walks
    // over QGuiApplication::screens() only to reject on the isVirtual
    // check at the end.
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
    if (!sa) {
        return false;
    }
    QScreen* sb = findByIdOrName(b);
    return sb && sa == sb;
}

bool belongsToPhysicalScreen(const QString& storedScreenId, const QString& physicalScreenId)
{
    if (storedScreenId.isEmpty() || physicalScreenId.isEmpty()) {
        return false;
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(physicalScreenId)) {
        return false;
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(storedScreenId)) {
        return screensMatch(PhosphorIdentity::VirtualScreenId::extractPhysicalId(storedScreenId), physicalScreenId);
    }
    return screensMatch(storedScreenId, physicalScreenId);
}

} // namespace Phosphor::Screens::ScreenIdentity
