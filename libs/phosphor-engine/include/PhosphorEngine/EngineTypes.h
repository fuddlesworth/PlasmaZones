// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QHashFunctions>
#include <QLatin1StringView>
#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>

namespace PhosphorEngine {

struct TilingStateKey
{
    QString screenId;
    int desktop = 1;
    QString activity;

    bool operator==(const TilingStateKey& other) const
    {
        return screenId == other.screenId && desktop == other.desktop && activity == other.activity;
    }
};

inline size_t qHash(const TilingStateKey& key, size_t seed = 0)
{
    return qHashMulti(seed, key.screenId, key.desktop, key.activity);
}

enum class SnapIntent {
    UserInitiated,
    AutoRestored,
};

/**
 * @brief Coarse classification of a window's KWin structural type.
 *
 * Carried alongside `PendingRestore` so the snap-restore consume path can
 * refuse to assign a saved-zone entry to a window that doesn't match the
 * kind the entry was recorded for. Discussion #461 follow-up: Steam (and
 * other Electron / CEF clients) reuse `windowClass()` between their main
 * client and short-lived popup surfaces; without a kind discriminator the
 * appId-keyed `pendingRestoreQueues` FIFO is consumed indiscriminately by
 * whichever window of that class opens first.
 *
 * `Unknown` is the migration / wire-default: legacy on-disk entries and
 * unmodified call sites carry it, and the consume path treats it as a
 * permissive match (the pre-fix behaviour). Once the effect and daemon
 * both classify reliably, every fresh entry carries a concrete kind and
 * the gate engages.
 */
enum class WindowKind : int {
    Unknown = 0, ///< Unset / legacy entry — does not gate restore.
    Normal = 1, ///< Plain top-level window the user manages directly.
    Transient = 2, ///< Popup / dialog / menu / tooltip / utility child surface.
};

struct ResnapEntry
{
    QString windowId;
    int zonePosition;
    QList<int> allZonePositions;
    QString screenId;
    int virtualDesktop = 0;
};

struct PendingRestore
{
    QStringList zoneIds;
    QString screenId;
    int virtualDesktop = 0;
    QString layoutId;
    QList<int> zoneNumbers;
    /// Structural kind of the window when this entry was recorded. The
    /// consume path (`SnapEngine::calculateRestoreFromSession`) compares
    /// the new window's kind against this and skips the restore when the
    /// two are concrete and different. `Unknown` is permissive on both
    /// sides — see WindowKind.
    WindowKind windowKind = WindowKind::Unknown;
};

struct SnapResult
{
    bool shouldSnap = false;
    QRect geometry;
    QString zoneId;
    QStringList zoneIds;
    QString screenId;

    bool isValid() const
    {
        return shouldSnap && geometry.isValid() && !zoneId.isEmpty();
    }

    static SnapResult noSnap()
    {
        return SnapResult{false, QRect(), QString(), QStringList(), QString()};
    }
};

struct UnfloatResult
{
    bool found = false;
    QStringList zoneIds;
    QRect geometry;
    QString screenId;
};

struct ZoneAssignmentEntry
{
    QString windowId{};
    QString sourceZoneId{};
    QString targetZoneId{};
    QStringList targetZoneIds{};
    QRect targetGeometry{};
    QString targetScreenId{};
};

enum class StickyWindowHandling {
    TreatAsNormal = 0,
    RestoreOnly = 1,
    IgnoreAll = 2
};

inline constexpr QLatin1StringView RestoreSentinel("__restore__");

} // namespace PhosphorEngine
