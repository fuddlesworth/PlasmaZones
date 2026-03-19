// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "constants.h"
#include <QString>

namespace PlasmaZones {

/**
 * @brief Key for layout assignment (screen + desktop + activity)
 */
struct LayoutAssignmentKey
{
    QString screenId; // Stable EDID-based identifier (or connector name fallback)
    int virtualDesktop = 0; // 0 = all desktops
    QString activity; // Empty = all activities

    bool operator==(const LayoutAssignmentKey& other) const
    {
        return screenId == other.screenId && virtualDesktop == other.virtualDesktop && activity == other.activity;
    }
};

inline size_t qHash(const LayoutAssignmentKey& key, size_t seed = 0)
{
    seed = ::qHash(key.screenId, seed);
    seed = ::qHash(key.virtualDesktop, seed);
    seed = ::qHash(key.activity, seed);
    return seed;
}

/**
 * @brief Explicit per-context assignment entry storing both mode fields
 *
 * Each screen/desktop/activity context stores an explicit Mode, SnappingLayout (UUID),
 * and TilingAlgorithm. Toggling between modes only flips the mode field —
 * the other field is preserved, eliminating the need for shadow assignments.
 */
struct AssignmentEntry
{
    enum Mode {
        Snapping = 0,
        Autotile = 1
    };
    Mode mode = Snapping;
    QString snappingLayout; // UUID string of manual layout
    QString tilingAlgorithm; // e.g. "dwindle", "wide", "tall"

    QString activeLayoutId() const
    {
        if (mode == Autotile) {
            return !tilingAlgorithm.isEmpty() ? LayoutId::makeAutotileId(tilingAlgorithm) : QString();
        }
        return snappingLayout;
    }
    bool isValid() const
    {
        return !snappingLayout.isEmpty() || !tilingAlgorithm.isEmpty();
    }
    bool operator==(const AssignmentEntry& other) const
    {
        return mode == other.mode && snappingLayout == other.snappingLayout && tilingAlgorithm == other.tilingAlgorithm;
    }

    /** @brief Update an existing AssignmentEntry from a layoutId, preserving the "other" field.
     *  Mode IS set from the layoutId type — batch operations from the KCM
     *  send a single layout ID per context, and that ID determines the active mode.
     *  The non-matching field is preserved for easy mode toggling.
     */
    static AssignmentEntry fromLayoutId(const QString& layoutId, const AssignmentEntry& existing)
    {
        AssignmentEntry entry = existing;
        if (LayoutId::isAutotile(layoutId)) {
            entry.mode = Autotile;
            entry.tilingAlgorithm = LayoutId::extractAlgorithmId(layoutId);
        } else {
            entry.mode = Snapping;
            entry.snappingLayout = layoutId;
        }
        return entry;
    }
    /** @brief Create a fresh AssignmentEntry from a layoutId string */
    static AssignmentEntry fromLayoutId(const QString& layoutId)
    {
        AssignmentEntry entry;
        if (LayoutId::isAutotile(layoutId)) {
            entry.mode = Autotile;
            entry.tilingAlgorithm = LayoutId::extractAlgorithmId(layoutId);
        } else {
            entry.mode = Snapping;
            entry.snappingLayout = layoutId;
        }
        return entry;
    }
};

} // namespace PlasmaZones
