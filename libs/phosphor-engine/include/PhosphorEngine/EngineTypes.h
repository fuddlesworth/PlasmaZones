// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QHashFunctions>
#include <QLatin1StringView>
#include <functional>
#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>

namespace PhosphorEngine {

/// Identity of a per-screen placement state: a window's placement is scoped to
/// the (screen, virtual desktop, activity) triple it was created in. Both the
/// snap engine and the autotile engine key their per-screen state on this so a
/// window keeps distinct placement per context (e.g. per desktop) and migrates
/// between contexts as the window crosses monitors / desktops.
struct PlacementStateKey
{
    QString screenId;
    int desktop = 1;
    QString activity;

    bool operator==(const PlacementStateKey& other) const
    {
        return screenId == other.screenId && desktop == other.desktop && activity == other.activity;
    }
};

inline size_t qHash(const PlacementStateKey& key, size_t seed = 0)
{
    return qHashMulti(seed, key.screenId, key.desktop, key.activity);
}

} // namespace PhosphorEngine

/// std::hash alongside qHash, so the key works in a std:: container too. Needed
/// where a value is move-only: Qt's containers are implicitly shared and require
/// copyable values, so anything owning a unique_ptr cannot live in a QHash.
template<>
struct std::hash<PhosphorEngine::PlacementStateKey>
{
    size_t operator()(const PhosphorEngine::PlacementStateKey& key) const noexcept
    {
        return qHash(key);
    }
};

namespace PhosphorEngine {

/// Backwards-compatible spelling for autotile's existing sources. The autotile
/// engine predates the shared base primitives and refers to this triple as
/// `TilingStateKey`; keep the alias so that source keeps compiling unchanged.
using TilingStateKey = PlacementStateKey;

enum class SnapIntent {
    UserInitiated,
    AutoRestored,
};

/// Coarse structural classification for the snap-restore consume gate.
/// Wire/JSON encoding is `int`; `Unknown` is the permissive default.
enum class WindowKind : int {
    Unknown = 0,
    Normal = 1,
    Transient = 2,
};

/// Clamp an integer wire value to a valid WindowKind. Unknown wire values
/// (out-of-range, future enum values from an older daemon) collapse to
/// `Unknown` rather than producing an undefined enum — the consume gate
/// treats `Unknown` permissively, which is the safe-by-default policy.
/// Centralised here so the three persistence-layer call sites
/// (`WindowTrackingAdaptor::windowClosed`, `SnapAdaptor::resolveWindowRestore`,
/// `WindowPlacement::fromJson`) stay in lockstep when a new kind is added.
inline WindowKind clampWindowKindFromWire(int wire)
{
    switch (wire) {
    case static_cast<int>(WindowKind::Normal):
        return WindowKind::Normal;
    case static_cast<int>(WindowKind::Transient):
        return WindowKind::Transient;
    default:
        return WindowKind::Unknown;
    }
}

struct ResnapEntry
{
    QString windowId;
    int zonePosition = 0;
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
    /// Closing window's kind; the consume gate refuses when both sides are concrete and disagree.
    WindowKind windowKind = WindowKind::Unknown;
};

struct SnapResult
{
    bool shouldSnap = false;
    QRect geometry;
    QString zoneId;
    QStringList zoneIds;
    QString screenId;
    /// Target virtual desktop the snap should be committed in (1-based). 0 means
    /// "the window's current desktop" — the historical behaviour. Set non-zero only
    /// by a placement rule that also routes the window to a desktop (RouteToDesktop),
    /// so the zone assignment is recorded on the desktop the window ends up on, not
    /// the one it momentarily opened on.
    int virtualDesktop = 0;

    bool isValid() const
    {
        return shouldSnap && geometry.isValid() && !zoneId.isEmpty();
    }

    static SnapResult noSnap()
    {
        // Value-initialize so every field takes its in-class default; a
        // positional initializer here would silently stop covering fields
        // added to the struct later.
        return SnapResult{};
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
    QString windowId;
    QString sourceZoneId;
    QString targetZoneId;
    QStringList targetZoneIds;
    QRect targetGeometry;
    QString targetScreenId;
    /// Virtual desktop to record the assignment on (1-based). 0 means "the
    /// window's current desktop" — the historical behaviour. Resnap producers
    /// stamp the window's recorded desktop here so a batch commit preserves it
    /// instead of re-stamping whatever desktop is currently active (which
    /// corrupts off-desktop windows caught in a cross-desktop batch).
    int virtualDesktop = 0;
};

enum class StickyWindowHandling {
    TreatAsNormal = 0,
    RestoreOnly = 1,
    IgnoreAll = 2
};

inline constexpr QLatin1StringView RestoreSentinel("__restore__");

} // namespace PhosphorEngine
