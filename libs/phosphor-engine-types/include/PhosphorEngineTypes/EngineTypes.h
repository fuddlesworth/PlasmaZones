// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QHashFunctions>
#include <QLatin1StringView>
#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>

namespace PhosphorEngineApi {

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

} // namespace PhosphorEngineApi
