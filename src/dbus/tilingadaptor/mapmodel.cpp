// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The placement-map surfaces of TilingAdaptor: the per-screen replay of the
// last tile batch (currentTilesJson) and the engine-managed focused window
// with its change signal (managedFocusedWindow / focusedWindowChanged).
// Split from tilingadaptor.cpp and relay.cpp by concern, both of which sit
// near the file-size ceiling; the lifecycle dispatch and the engine relays
// stay where they were and call the hooks defined here.

#include "tilingadaptor.h"

#include "core/platform/logging.h"

#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorProtocol/WindowMarshalling.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace PlasmaZones {

namespace {
/// The replay's keys, spelled to match what relayTileRequestsJson PARSES
/// (relay.cpp reads the same literals off the engines' batch JSON), so a
/// client can feed a replay back through the same reader. Kept beside the
/// serializer rather than in the protocol header because the batch JSON is
/// an in-process contract between the engines and this adaptor, not a wire
/// vocabulary the effect reads.
inline QLatin1String windowIdKey()
{
    return QLatin1String("windowId");
}
inline QLatin1String xKey()
{
    return QLatin1String("x");
}
inline QLatin1String yKey()
{
    return QLatin1String("y");
}
inline QLatin1String widthKey()
{
    return QLatin1String("width");
}
inline QLatin1String heightKey()
{
    return QLatin1String("height");
}
inline QLatin1String screenIdKey()
{
    return QLatin1String("screenId");
}
inline QLatin1String monocleKey()
{
    return QLatin1String("monocle");
}
inline QLatin1String floatingKey()
{
    return QLatin1String("floating");
}
inline QLatin1String zoneIdKey()
{
    return QLatin1String("zoneId");
}
} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// currentTilesJson
// ═══════════════════════════════════════════════════════════════════════════

void TilingAdaptor::recordTileBatch(const PhosphorProtocol::TileRequestList& requests)
{
    // One list per screen the batch names, REPLACED rather than merged: the
    // engines emit a screen's whole placement per batch (the emit-on-change
    // gate is per entry, but a batch that names a screen at all carries every
    // entry that moved on it), so the newest batch is the whole truth for
    // that screen and an older entry it does not repeat would be a stale
    // rect. Grouped in one pass so a batch naming two screens costs one walk.
    QHash<QString, PhosphorProtocol::TileRequestList> grouped;
    for (const PhosphorProtocol::TileRequestEntry& entry : requests) {
        grouped[entry.screenId].append(entry);
    }
    for (auto it = grouped.cbegin(); it != grouped.cend(); ++it) {
        m_lastTileBatchPerScreen.insert(it.key(), it.value());
    }
}

void TilingAdaptor::forgetTileEntriesForWindow(const QString& windowId)
{
    // Every screen's list, not just the one the window was last tiled on: a
    // window that moved across outputs can sit in an older screen's retained
    // batch as well as its current one, and the older entry would replay a
    // rect on a screen the window left.
    for (auto it = m_lastTileBatchPerScreen.begin(); it != m_lastTileBatchPerScreen.end();) {
        it.value().removeIf([&windowId](const PhosphorProtocol::TileRequestEntry& entry) {
            return entry.windowId == windowId;
        });
        // An emptied list is dropped rather than kept, so "no batch held" and
        // "every entry of the batch has since closed" read the same "[]".
        if (it.value().isEmpty()) {
            it = m_lastTileBatchPerScreen.erase(it);
        } else {
            ++it;
        }
    }
}

QString TilingAdaptor::currentTilesJson(const QString& screenId) const
{
    if (screenId.isEmpty()) {
        return QStringLiteral("[]");
    }
    const auto it = m_lastTileBatchPerScreen.constFind(screenId);
    if (it == m_lastTileBatchPerScreen.constEnd()) {
        return QStringLiteral("[]");
    }
    QJsonArray arr;
    for (const PhosphorProtocol::TileRequestEntry& entry : it.value()) {
        QJsonObject obj;
        obj[windowIdKey()] = entry.windowId;
        // A floating entry carries no rect: the parser skips the four
        // geometry keys for it, and the struct's defaults are what it holds.
        // Written as the zeros they are rather than omitted, so a reader
        // sees one shape per entry and branches on `floating` alone.
        obj[xKey()] = entry.x;
        obj[yKey()] = entry.y;
        obj[widthKey()] = entry.width;
        obj[heightKey()] = entry.height;
        obj[screenIdKey()] = entry.screenId;
        obj[monocleKey()] = entry.monocle;
        obj[floatingKey()] = entry.floating;
        // Reserved and always empty on the signal (relay.cpp never parses
        // it); written for shape parity with the reader's key set.
        obj[zoneIdKey()] = entry.zoneId;
        arr.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

// ═══════════════════════════════════════════════════════════════════════════
// managedFocusedWindow / focusedWindowChanged
// ═══════════════════════════════════════════════════════════════════════════

QString TilingAdaptor::ownerFocusedWindow(const QString& screenId) const
{
    // Strict claim, no primary fallback (the header says why). The first
    // engine whose live set claims the screen answers; the sets are disjoint
    // by construction (one mode per screen), so "first" is "only".
    for (const PhosphorEngine::IPlacementEngine* engine : m_lifecycleEngines) {
        if (engine->isActiveOnScreen(screenId)) {
            return engine->managedFocusedWindow(screenId);
        }
    }
    return {};
}

QString TilingAdaptor::trackedScreenForWindow(const QString& windowId) const
{
    for (const PhosphorEngine::IPlacementEngine* engine : m_lifecycleEngines) {
        if (engine->isWindowTracked(windowId)) {
            return engine->screenForTrackedWindow(windowId);
        }
    }
    return {};
}

QString TilingAdaptor::managedFocusedWindow(const QString& screenId) const
{
    // The LIVE answer, never the broadcast memory: the memory is the change
    // gate's, and it lags the engine between hooks by design.
    if (screenId.isEmpty()) {
        return {};
    }
    return ownerFocusedWindow(screenId);
}

void TilingAdaptor::refreshFocusedWindow(const QString& screenId)
{
    if (screenId.isEmpty()) {
        return;
    }
    const QString now = ownerFocusedWindow(screenId);
    // Absent and empty compare equal on purpose: a screen never broadcast
    // for whose answer is empty has nothing to announce, and a screen that
    // announced a window and then lost its owner announces the empty once.
    const QString last = m_lastFocusedBroadcast.value(screenId);
    if (now == last) {
        return;
    }
    if (now.isEmpty()) {
        m_lastFocusedBroadcast.remove(screenId);
    } else {
        m_lastFocusedBroadcast.insert(screenId, now);
    }
    qCDebug(lcDbusTiling) << "focusedWindowChanged:" << screenId << "->" << now;
    Q_EMIT focusedWindowChanged(screenId, now);
}

void TilingAdaptor::reconcileMapStateWithAnnounce(const QStringList& announced)
{
    // Screens that LEFT the managed set: their retained batch describes a
    // layout no engine owns any more, and their focus, if one was broadcast,
    // is nobody's. refreshFocusedWindow answers empty for an unowned screen,
    // so it is the one that announces the clear; the batch is dropped here.
    // Copied keys, since both maps are mutated in the walk.
    const QStringList held = m_lastTileBatchPerScreen.keys();
    for (const QString& screenId : held) {
        if (!announced.contains(screenId)) {
            m_lastTileBatchPerScreen.remove(screenId);
        }
    }
    const QStringList broadcast = m_lastFocusedBroadcast.keys();
    for (const QString& screenId : broadcast) {
        if (!announced.contains(screenId)) {
            refreshFocusedWindow(screenId);
        }
    }
    // Screens in the set are re-read: a mode flip moves a screen between
    // engines in one pass, and the new owner's focus is a different window
    // than the old owner's was, or the same one under a different engine.
    for (const QString& screenId : announced) {
        refreshFocusedWindow(screenId);
    }
}

} // namespace PlasmaZones
