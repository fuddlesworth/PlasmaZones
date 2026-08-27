// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorworkspaces_export.h>

#include <functional>

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace PhosphorWorkspaces {

/// One owned desktop in a screen's workspace slice.
struct PHOSPHORWORKSPACES_EXPORT WorkspaceEntry
{
    QString desktopId; ///< KWin UUID string (braced, as KWin reports it)
    QString name; ///< empty = dynamic; non-empty = named (destroy-exempt)
    QString homeScreenId; ///< set only while displaced by hotplug

    bool operator==(const WorkspaceEntry& other) const
    {
        return desktopId == other.desktopId && name == other.name && homeScreenId == other.homeScreenId;
    }
};

/// The per-monitor workspace ownership model: screenId → ordered slice of KWin
/// desktop UUIDs, with the inverse owner index. Pure data — no D-Bus, no
/// signals; the reconciler mutates it and the controller streams/persists it.
/// Every KWin desktop id appears in exactly one slice; concatenating the slices
/// in screen order yields the intended KWin global order (contiguity is
/// insert-correct with opportunistic repair; see the plan §4.3).
class PHOSPHORWORKSPACES_EXPORT WorkspaceMap
{
public:
    // ── Screen order (slice concatenation order) ────────────────────────────
    QStringList screenOrder() const;
    /// Replace the screen order (left-to-right geometry, ties by id — computed
    /// by the caller, which owns screen geometry knowledge). Screens absent
    /// from `order` but holding slices are appended in their previous relative
    /// order so no slice is ever orphaned by a reorder.
    void setScreenOrder(const QStringList& order);

    // ── Slice access ────────────────────────────────────────────────────────
    bool hasScreen(const QString& screenId) const;
    QList<WorkspaceEntry> slice(const QString& screenId) const;
    int sliceSize(const QString& screenId) const;
    /// Owner screen of a desktop id, or empty when unowned.
    QString ownerOf(const QString& desktopId) const;
    /// Slice position (0-based) of a desktop within its owner, or -1.
    int sliceIndexOf(const QString& desktopId) const;
    /// All owned desktop ids, slice-concatenated in screen order.
    QStringList allDesktopIds() const;
    /// The entry for a desktop id (owner looked up via the index); nullptr-like
    /// default entry when unowned.
    WorkspaceEntry entryFor(const QString& desktopId) const;

    // ── Mutation (reconciler only) ──────────────────────────────────────────
    /// Insert an entry into a screen's slice at sliceIndex (clamped). The id
    /// must not be owned elsewhere; a duplicate insert is repaired by removal
    /// from the previous owner first (logged by the caller).
    void insert(const QString& screenId, int sliceIndex, const WorkspaceEntry& entry);
    /// Remove a desktop from whatever slice owns it. Returns false if unowned.
    bool remove(const QString& desktopId);
    /// Move a desktop within its owner's slice to newSliceIndex (clamped).
    bool reorderWithinSlice(const QString& desktopId, int newSliceIndex);
    /// Re-own a desktop to another screen at sliceIndex (clamped); keeps the
    /// entry's metadata. Returns false if unowned.
    bool transfer(const QString& desktopId, const QString& toScreenId, int sliceIndex);
    void setName(const QString& desktopId, const QString& name);
    void setHomeScreen(const QString& desktopId, const QString& homeScreenId);
    /// Drop a screen's slice entirely, returning its entries in order (used by
    /// hotplug migration, which re-inserts them elsewhere).
    QList<WorkspaceEntry> takeSlice(const QString& screenId);
    void clear();

    // ── Fork-5 position arithmetic ──────────────────────────────────────────
    /// Global 0-based insert position for createDesktop(): the sum of slice
    /// sizes of screens preceding `screenId` in screen order, plus sliceIndex
    /// (clamped to the slice size). KWin's D-Bus position base is 0 per its
    /// `desktops` property (position starts at 0); this is the single place
    /// that encodes it.
    uint globalPositionForInsert(const QString& screenId, int sliceIndex) const;

    // ── Consistency ─────────────────────────────────────────────────────────
    /// True when every invariant holds against `kwinIds` (each KWin id owned
    /// exactly once, no entry for a vanished id). Does not check contiguity
    /// (deliberately weakened, plan §4.3).
    bool consistentWith(const QStringList& kwinIds) const;
    /// Repair against the authoritative KWin id list: drop entries whose id
    /// vanished, report ids KWin has that no slice owns (for adoption by the
    /// caller). Returns the unowned ids in KWin order.
    QStringList repairAgainst(const QStringList& kwinIds);

    // ── Serialization (wire + state file share this) ────────────────────────
    /// Wire/state JSON per plan §3.2/§3.3. `currentByScreen` (screenId → 1-based
    /// global int) and `indexOf` (uuid → 1-based global int) are supplied by the
    /// caller so the model stays free of VirtualDesktopManager. `includeState`
    /// adds homeScreen fields (state file); the stream omits them.
    QString toJson(quint64 generation, const QHash<QString, int>& currentByScreen,
                   const std::function<int(const QString&)>& indexOf, bool includeState) const;
    /// Parse a state-file/wire payload into this map. Returns false (and leaves
    /// the map cleared) on version mismatch or malformed JSON.
    bool fromJson(const QString& json);

    bool operator==(const WorkspaceMap& other) const
    {
        return m_screenOrder == other.m_screenOrder && m_slices == other.m_slices;
    }

private:
    QStringList m_screenOrder;
    QHash<QString, QList<WorkspaceEntry>> m_slices;
    QHash<QString, QString> m_ownerOf; ///< desktopId → screenId (inverse index)
};

} // namespace PhosphorWorkspaces
